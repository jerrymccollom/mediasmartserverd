/**
 * @file     update_monitor.h
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

// Altered version, 2026: asynchronous helper and independent reboot notifications.
#pragma once
#include "helper_process.h"
#include "led_control_base.h"
#include <array>
struct UpdateCounts { uint64_t total = 0, security = 0; };
bool parseUpdateCounts(const std::string& output, UpdateCounts& counts);
struct UpdatePaths {
    std::vector<std::string> command{"/usr/lib/update-notifier/apt-check"};
    std::string runtime = "/run";
    std::string dpkg = "/var/lib/dpkg";
    std::string apt = "/var/lib/apt/lists";
};
class UpdateMonitor {
    LedControlPtr leds_;
    UpdatePaths paths_;
    HelperProcess helper_;
    Fd watch_;
    std::array<int, 3> watches_{{-1, -1, -1}};
    UpdateCounts counts_;
    bool known_ = false, reboot_ = false, stopping_ = false;
    bool pending_ = false;
    int color_ = -1;
    Time next_{}, fallback_{}, debounce_{}, not_before_{};
    void watchDirectories();
    void rebootStatus();
    void display();
public:
    explicit UpdateMonitor(LedControlPtr leds, UpdatePaths paths = {}, bool revoke_io = true,
                           Milliseconds timeout = Milliseconds(30000));
    ~UpdateMonitor();
    int watchFd() const { return watch_.get(); }
    int helperFd() const { return helper_.fd(); }
    void filesystemEvents(Time now);
    void service(Time now);
    void stop(Time now);
    bool running() const { return helper_.running(); }
    bool known() const { return known_; }
    int waitMs(Time now) const;
};
