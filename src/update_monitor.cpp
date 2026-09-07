/**
 * @file     update_monitor.cpp
 * @author   Kai Hendrik Behrends (kai.behrends@gmail.com)
 * @date     2013-02-07
 * @version  1.0
 * 
 * Monitor program that checks for updates and notifies via system LED.
 *
 *
 * Changelog:
 *
 * 2013-02-07 - Kai Hendrik Behrends
 *  - Initialy created file.
 *
 * 2013-02-16 - Kai Hendrik Behrends
 *  - Fixed reading of update string returned by apt-check.
 *
 *
 * Copyright (c) 2013 Kai Hendrik Behrends
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 */

// Altered version, 2026: no worker threads or blocking package checks.
#include "update_monitor.h"
#include <iostream>
#include <sys/inotify.h>
#include <sys/stat.h>

bool parseUpdateCounts(const std::string& output, UpdateCounts& counts) {
    const auto text = trim(output);
    const auto delimiter = text.find(';');
    UpdateCounts parsed;
    if (delimiter == std::string::npos ||
        !parseUnsigned(text.substr(0, delimiter), parsed.total) ||
        !parseUnsigned(text.substr(delimiter + 1), parsed.security) || parsed.security > parsed.total) return false;
    counts = parsed;
    return true;
}
UpdateMonitor::UpdateMonitor(LedControlPtr leds, UpdatePaths paths, bool revoke_io, Milliseconds timeout)
    : leds_(std::move(leds)), paths_(std::move(paths)), helper_(revoke_io, timeout),
      watch_(inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
    if (!watch_) std::cerr << "Filesystem notifications unavailable; using periodic reconciliation\n";
    watchDirectories(); rebootStatus(); display();
}
UpdateMonitor::~UpdateMonitor() {
    try { leds_->SetSystemLed(LED_BLUE | LED_RED, false); } catch (...) {}
}
void UpdateMonitor::watchDirectories() {
    if (!watch_) return;
    const std::array<std::string, 3> paths{{paths_.runtime, paths_.dpkg, paths_.apt}};
    for (size_t i = 0; i < paths.size(); ++i) if (watches_[i] < 0)
        watches_[i] = inotify_add_watch(watch_.get(), paths[i].c_str(),
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
}
void UpdateMonitor::rebootStatus() {
    struct stat st{};
    const auto path = paths_.runtime + "/reboot-required";
    if (stat(path.c_str(), &st) == 0) reboot_ = true;
    else if (errno == ENOENT) reboot_ = false;
    else std::cerr << "Cannot check reboot-required: " << strerror(errno) << '\n';
}
void UpdateMonitor::display() {
    const int color = reboot_ ? LED_RED : known_ && counts_.security ? LED_BLUE | LED_RED :
        known_ && counts_.total ? LED_BLUE : 0;
    if (color == color_) return;
    for (int channel : {LED_BLUE, LED_RED})
        if (color_ < 0 || ((color ^ color_) & channel)) leds_->SetSystemLed(channel, (color & channel) != 0);
    color_ = color;
}
void UpdateMonitor::filesystemEvents(Time now) {
    if (!watch_) return;
    bool reboot_changed = false, packages_changed = false;
    for (unsigned batch = 0; batch < 4; ++batch) {
        alignas(inotify_event) char buffer[4096];
        const auto n = read(watch_.get(), buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) break;
        if (n <= 0) { watch_.reset(); break; }
        for (size_t pos = 0; pos < static_cast<size_t>(n);) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + pos);
            const std::string name = event->len ? event->name : "";
            if (event->mask & IN_Q_OVERFLOW) { reboot_changed = packages_changed = true; }
            if (event->wd == watches_[0] && name == "reboot-required") reboot_changed = true;
            if ((event->wd == watches_[1] && name == "status") || event->wd == watches_[2]) packages_changed = true;
            if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                for (auto& wd : watches_) if (wd == event->wd) {
                    if (!(event->mask & IN_IGNORED)) inotify_rm_watch(watch_.get(), wd);
                    wd = -1;
                }
                reboot_changed = packages_changed = true;
            }
            pos += sizeof(inotify_event) + event->len;
        }
    }
    if (reboot_changed) { rebootStatus(); display(); }
    // Leading-edge deadline bounds debounce even under a continuous event stream.
    if (packages_changed && !pending_) { pending_ = true; debounce_ = now + std::chrono::seconds(2); }
}
void UpdateMonitor::service(Time now) {
    const bool was_running = helper_.running();
    helper_.service(now);
    if (was_running && helper_.finished()) {
        UpdateCounts result;
        if (helper_.success() && parseUpdateCounts(helper_.output(), result)) {
            counts_ = result; known_ = true; next_ = now + std::chrono::minutes(15);
        } else {
            std::cerr << "Update check failed; retaining last-known status and retrying in 60 seconds\n";
            next_ = now + std::chrono::seconds(60); not_before_ = next_;
        }
        if (!stopping_) display();
    }
    if (stopping_) return;
    if (now >= fallback_) {
        watchDirectories(); rebootStatus(); display();
        fallback_ = now + std::chrono::seconds(30);
    }
    if (!helper_.running() && now >= not_before_ && (now >= next_ || (pending_ && now >= debounce_))) {
        try { helper_.start(paths_.command, now); pending_ = false; }
        catch (const std::exception& error) {
            std::cerr << "Update helper: " << error.what() << '\n';
            next_ = now + std::chrono::seconds(60); not_before_ = next_; pending_ = false;
        }
    }
}
void UpdateMonitor::stop(Time now) {
    stopping_ = true; helper_.stop(now);
    if (color_ != 0) { leds_->SetSystemLed(LED_BLUE | LED_RED, false); color_ = 0; }
}

int UpdateMonitor::waitMs(Time now) const {
    if (helper_.running()) return 50;
    if (stopping_) return 0;
    const auto check = std::max(not_before_, pending_ ? std::min(next_, debounce_) : next_);
    return millisecondsUntil(std::min(check, fallback_), now);
}
