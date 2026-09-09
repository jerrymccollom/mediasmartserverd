/////////////////////////////////////////////////////////////////////////////
/// @file device_monitor.cpp
///
/// Device monitoring (disk add/removal etc)
///
/// -------------------------------------------------------------------------
///
/// Copyright (c) 2009-2010 Chris Byrne, Brian Teague
/// 
/// This software is provided 'as-is', without any express or implied
/// warranty. In no event will the authors be held liable for any damages
/// arising from the use of this software.
/// 
/// Permission is granted to anyone to use this software for any purpose,
/// including commercial applications, and to alter it and redistribute it
/// freely, subject to the following restrictions:
/// 
/// 1. The origin of this software must not be misrepresented; you must not
/// claim that you wrote the original software. If you use this software
/// in a product, an acknowledgment in the product documentation would be
/// appreciated but is not required.
/// 
/// 2. Altered source versions must be plainly marked as such, and must not
/// be misrepresented as being the original software.
/// 
/// 3. This notice may not be removed or altered from any source
/// distribution.
///
/////////////////////////////////////////////////////////////////////////////

// Altered version, 2026: live device state, bounded events and cumulative I/O.
#include "device_monitor.h"
#include "mediasmartserverd.h"
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string_view>

bool parseDiskStats(const char* data, size_t size, DiskStats& result) {
    std::array<uint64_t, 17> fields{};
    size_t count = 0;
    const char* end = data + size;
    while (data < end) {
        while (data < end && (*data == ' ' || *data == '\t' || *data == '\n' || *data == '\r')) ++data;
        if (data == end) break;
        uint64_t value;
        const auto parsed = std::from_chars(data, end, value);
        if (parsed.ec != std::errc{} || parsed.ptr == data) return false;
        data = parsed.ptr;
        if (data < end && *data != ' ' && *data != '\t' && *data != '\n' && *data != '\r') return false;
        if (count < fields.size()) fields[count] = value;
        ++count;
    }
    if (count < 11 || (count > 11 && count < 15) || count == 16) return false;
    result.completed = {fields[0], fields[4], count >= 15 ? fields[11] : 0, count >= 17 ? fields[15] : 0};
    result.inflight = fields[8];
    return true;
}
int bayForPort(const std::string& controller, unsigned port) {
    // Existing four-bay models wire the first four on-board Intel SATA ports.
    // port_no is controller-local and 1-based, independent of global ata/host IDs.
    if (controller != "0000:00:1f.2" || port < 1 || port > 4) return -1;
    return static_cast<int>(port - 1);
}
bool StatsReader::read(Fd& fd, const std::string& path, DiskStats& stats) {
    if (!fd) fd.reset(open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd) return false;
    char buffer[1024];
    const auto n = pread(fd.get(), buffer, sizeof(buffer), 0);
    if (n <= 0 || n == sizeof(buffer) || !parseDiskStats(buffer, static_cast<size_t>(n), stats)) {
        fd.reset(); return false;
    }
    return true;
}
DiskRegistry::DiskRegistry(LedControlPtr leds, StatsReader& reader) : leds_(std::move(leds)), reader_(reader) {
    for (size_t i = 0; i < bays_.size(); ++i) color(i, 0);
}
DiskRegistry::~DiskRegistry() { try { clear(); } catch (...) {} }
void DiskRegistry::color(size_t bay, int value) {
    auto& old = bays_.at(bay).color;
    if (debug && old != value) std::cout << "Bay " << bay << " color=" << value << '\n';
    for (int channel : {LED_BLUE, LED_RED})
        if (old < 0 || ((old ^ value) & channel)) leds_->Set(channel, bay, (value & channel) != 0);
    old = value;
}
bool DiskRegistry::add(const Disk& disk) {
    if (disk.bay < 0 || disk.bay >= 4 || disk.path.empty() || disk.identity.empty()) return false;
    auto& bay = bays_[disk.bay];
    if (!bay.disk.path.empty() && disk.sequence && bay.disk.sequence > disk.sequence) return false;
    if (!bay.disk.path.empty() && bay.disk.path != disk.path) {
        std::cerr << "Conflicting devices for bay " << disk.bay << ": " << bay.disk.path << " and " << disk.path << '\n';
        return false;
    }
    if (bay.disk.identity == disk.identity && bay.disk.path == disk.path) {
        bay.disk.sequence = std::max(bay.disk.sequence, disk.sequence);
        return true;
    }
    bay.fd.reset(); bay.baseline = false; bay.retry = Time{};
    bay.disk = disk;
    color(disk.bay, LED_BLUE);
    return true;
}
void DiskRegistry::remove(std::string path, uint64_t sequence) {
    for (size_t i = 0; i < bays_.size(); ++i) {
        auto& bay = bays_[i];
        if (bay.disk.path != path || (sequence && sequence < bay.disk.sequence)) continue;
        bay.fd.reset(); bay.disk = {}; bay.baseline = false; bay.retry = Time{};
        color(i, 0);
    }
}
void DiskRegistry::reconcile(const std::vector<Disk>& disks) {
    std::array<const Disk*, 4> desired{};
    std::array<bool, 4> ambiguous{};
    for (const auto& disk : disks) {
        if (disk.bay < 0 || disk.bay >= 4) continue;
        if (desired[disk.bay] && desired[disk.bay]->identity != disk.identity) ambiguous[disk.bay] = true;
        desired[disk.bay] = &disk;
    }
    for (size_t i = 0; i < bays_.size(); ++i) {
        if (ambiguous[i]) {
            std::cerr << "Ambiguous device mapping for bay " << i << '\n';
            desired[i] = nullptr;
        }
        if (!desired[i] || desired[i]->identity != bays_[i].disk.identity || desired[i]->path != bays_[i].disk.path)
            if (!bays_[i].disk.path.empty()) remove(bays_[i].disk.path);
        if (desired[i]) add(*desired[i]);
    }
}
void DiskRegistry::resync() {
    // Something outside the registry (a light show) may have driven these LEDs
    // directly, so `color()`'s cached-value skip would otherwise leave stale hardware
    // state in place. Force every bay to be rewritten regardless of the cache; any
    // busy/red indication is restored separately by the next sample().
    for (size_t i = 0; i < bays_.size(); ++i) {
        bays_[i].color = -1;
        color(i, bays_[i].disk.path.empty() ? 0 : LED_BLUE);
    }
}
void DiskRegistry::sample(Time now) {
    for (size_t i = 0; i < bays_.size(); ++i) {
        auto& bay = bays_[i];
        if (bay.disk.path.empty() || now < bay.retry) continue;
        DiskStats stats;
        if (!reader_.read(bay.fd, bay.disk.stats, stats)) {
            if (bay.retry == Time{}) std::cerr << "Cannot read disk statistics: " << bay.disk.stats << '\n';
            bay.retry = now + std::chrono::seconds(1); bay.baseline = false;
            color(i, LED_BLUE); continue;
        }
        bay.retry = Time{};
        bool changed = false, reset = false;
        for (size_t j = 0; j < stats.completed.size(); ++j) {
            changed |= stats.completed[j] > bay.previous.completed[j];
            reset |= stats.completed[j] < bay.previous.completed[j];
        }
        const bool busy = stats.inflight || (bay.baseline && !reset && changed);
        bay.previous = stats; bay.baseline = true;
        color(i, LED_BLUE | (busy ? LED_RED : 0));
    }
}
size_t DiskRegistry::count() const {
    size_t n = 0;
    for (const auto& bay : bays_) n += !bay.disk.path.empty();
    return n;
}
void DiskRegistry::clear() {
    for (size_t i = 0; i < bays_.size(); ++i) {
        bays_[i].fd.reset(); bays_[i].disk = {}; bays_[i].baseline = false;
        color(i, 0);
    }
}
namespace {
using Device = std::unique_ptr<udev_device, decltype(&udev_device_unref)>;
std::string property(udev_device* d, const char* key) {
    const char* value = udev_device_get_property_value(d, key);
    return value ? value : "";
}
}
UdevSource::UdevSource() : context_(udev_new(), udev_unref) {
    if (!context_) throw ErrnoException("udev_new");
}
void UdevSource::connect() {
    Monitor next(udev_monitor_new_from_netlink(context_.get(), "udev"), udev_monitor_unref);
    if (!next) throw ErrnoException("udev monitor");
    const int filter = udev_monitor_filter_add_match_subsystem_devtype(next.get(), "block", "disk");
    if (filter < 0) throw ErrnoException("udev filter", -filter);
    const int enabled = udev_monitor_enable_receiving(next.get());
    if (enabled < 0) throw ErrnoException("udev receiving", -enabled);
    if (udev_monitor_get_fd(next.get()) < 0) throw std::runtime_error("Invalid udev descriptor");
    monitor_ = std::move(next);
}
int UdevSource::fd() const { return monitor_ ? udev_monitor_get_fd(monitor_.get()) : -1; }
bool UdevSource::normalize(udev_device* device, Disk& disk) {
    if (!device || property(device, "DEVTYPE") != "disk" || property(device, "ID_BUS") != "ata") return false;
    auto* pci = udev_device_get_parent_with_subsystem_devtype(device, "pci", nullptr);
    if (!pci) return false;
    const char* controller = udev_device_get_sysname(pci);
    if (!controller || std::string(controller) != "0000:00:1f.2") return false;
    for (auto* parent = udev_device_get_parent(device); parent && parent != pci; parent = udev_device_get_parent(parent)) {
        const char* name = udev_device_get_sysname(parent);
        uint64_t ata;
        if (!name || std::strncmp(name, "ata", 3) || !parseUnsigned(name + 3, ata)) continue;
        const char* parent_path = udev_device_get_syspath(parent);
        if (!parent_path) continue;
        const auto port_path = std::string(parent_path) + "/ata_port/" + name;
        Device port(udev_device_new_from_syspath(context_.get(), port_path.c_str()), udev_device_unref);
        if (!port) continue;
        const char* value = udev_device_get_sysattr_value(port.get(), "port_no");
        uint64_t number;
        if (!value || !parseUnsigned(trim(value), number) || number > 4) return false;
        disk.bay = bayForPort(controller, static_cast<unsigned>(number));
        if (disk.bay < 0) return false;
        const char* path = udev_device_get_syspath(device);
        if (!path) return false;
        disk.path = path; disk.stats = disk.path + "/stat";
        disk.identity = disk.path + ":" + std::to_string(udev_device_get_devnum(device)) + ":" +
            property(device, "USEC_INITIALIZED") + ":" + property(device, "ID_SERIAL");
        disk.sequence = udev_device_get_seqnum(device);
        return true;
    }
    return false;
}
std::vector<Disk> UdevSource::inventory() {
    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> list(udev_enumerate_new(context_.get()), udev_enumerate_unref);
    if (!list) throw ErrnoException("udev enumerate");
    int result = udev_enumerate_add_match_subsystem(list.get(), "block");
    if (result < 0) throw ErrnoException("udev enumeration filter", -result);
    result = udev_enumerate_scan_devices(list.get());
    if (result < 0) throw ErrnoException("udev scan", -result);
    std::vector<Disk> disks;
    for (auto* entry = udev_enumerate_get_list_entry(list.get()); entry; entry = udev_list_entry_get_next(entry)) {
        Device device(udev_device_new_from_syspath(context_.get(), udev_list_entry_get_name(entry)), udev_device_unref);
        Disk disk;
        if (normalize(device.get(), disk)) {
            if (verbose > 1) std::cout << "Bay " << disk.bay << ": " << disk.path << '\n';
            disks.push_back(std::move(disk));
        }
    }
    return disks;
}
bool UdevSource::receive(DiskEvent& result) {
    errno = 0;
    Device event(udev_monitor_receive_device(monitor_.get()), udev_device_unref);
    if (!event) {
        if (errno && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            throw ErrnoException("udev receive");
        return false;
    }
    const char* path = udev_device_get_syspath(event.get());
    if (!path) { result.retry = true; return true; }
    result.path = path;
    result.sequence = udev_device_get_seqnum(event.get());
    // Resolve current sysfs state rather than trusting an old queued event.
    Device current(udev_device_new_from_syspath(context_.get(), path), udev_device_unref);
    result.exists = normalize(current.get(), result.current);
    if (result.exists) result.current.sequence = result.sequence;
    else if (current) result.retry = true;
    return true;
}
DeviceMonitor::DeviceMonitor(const LedControlPtr& leds, std::unique_ptr<DiskSource> source)
    : source_(std::move(source)), registry_(leds, reader_) {
    source_->connect(); connected_ = true;
    reconcile(); // Reception is already enabled when the snapshot is taken.
    drain(Clock::now());
}
void DeviceMonitor::reconcile() {
    registry_.reconcile(source_->inventory());
    reconcile_at_ = Clock::now() + std::chrono::seconds(30);
}
void DeviceMonitor::drain(Time now) {
    if (!connected_) return;
    try {
        for (unsigned i = 0; i < 64; ++i) {
            DiskEvent event;
            if (!source_->receive(event)) break;
            if (event.retry) reconcile_at_ = now;
            else if (event.exists) {
                if (!registry_.add(event.current)) reconcile_at_ = now;
            } else registry_.remove(event.path, event.sequence);
        }
    } catch (const std::exception& error) {
        std::cerr << "Device events: " << error.what() << "; reconnecting\n";
        connected_ = false; reconcile_at_ = now;
    }
}
void DeviceMonitor::maintain(Time now) {
    if (now < reconcile_at_) return;
    try {
        if (!connected_) { source_->connect(); connected_ = true; }
        reconcile();
    } catch (const std::exception& error) {
        std::cerr << "Device reconciliation: " << error.what() << '\n';
        reconcile_at_ = now + std::chrono::seconds(1);
    }
}
