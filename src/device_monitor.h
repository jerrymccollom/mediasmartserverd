/////////////////////////////////////////////////////////////////////////////
/// @file device_monitor.h
///
/// Device monitoring (disk add/removal etc)
///
/// -------------------------------------------------------------------------
///
/// Copyright (c) 2009-2010 Chris Byrne
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
// Altered version, 2026: live four-bay state and consistent block-device events.
#pragma once
#include "led_control_base.h"
#include "runtime.h"
#include "event_loop.h"
#include <array>
#include <cstdint>
#include <vector>
#include <libudev.h>

struct DiskStats {
    std::array<uint64_t, 4> completed{}; // reads, writes, discards, flushes
    uint64_t inflight = 0;
};
bool parseDiskStats(const char* data, size_t size, DiskStats& stats);
int bayForPort(const std::string& controller, unsigned port);
struct Disk {
    std::string path, identity, stats;
    int bay = -1;
    uint64_t sequence = 0;
};
class StatsReader {
public:
    virtual ~StatsReader() = default;
    virtual bool read(Fd& fd, const std::string& path, DiskStats& stats);
};
class DiskRegistry {
    struct Bay {
        Disk disk;
        Fd fd;
        DiskStats previous;
        bool baseline = false;
        int color = -1;
        Time retry{};
    };
    std::array<Bay, 4> bays_;
    LedControlPtr leds_;
    StatsReader& reader_;
    void color(size_t bay, int color);
public:
    DiskRegistry(LedControlPtr leds, StatsReader& reader);
    ~DiskRegistry();
    bool add(const Disk& disk);
    void remove(std::string path, uint64_t sequence = 0);
    void reconcile(const std::vector<Disk>& disks);
    void resync();
    void sample(Time now);
    size_t count() const;
    void clear();
};
struct DiskEvent {
    std::string path;
    Disk current;
    uint64_t sequence = 0;
    bool exists = false, retry = false;
};
class DiskSource {
public:
    virtual ~DiskSource() = default;
    virtual void connect() = 0;
    virtual int fd() const = 0;
    virtual std::vector<Disk> inventory() = 0;
    virtual bool receive(DiskEvent& event) = 0;
};
class UdevSource : public DiskSource {
    using Context = std::unique_ptr<udev, decltype(&udev_unref)>;
    using Monitor = std::unique_ptr<udev_monitor, decltype(&udev_monitor_unref)>;
    Context context_{nullptr, udev_unref};
    Monitor monitor_{nullptr, udev_monitor_unref};
    bool normalize(udev_device* device, Disk& disk);
public:
    UdevSource();
    void connect() override;
    int fd() const override;
    std::vector<Disk> inventory() override;
    bool receive(DiskEvent& event) override;
};
class DeviceMonitor : public DeviceEvents {
    std::unique_ptr<DiskSource> source_;
    StatsReader reader_;
    DiskRegistry registry_;
    Time reconcile_at_{};
    bool connected_ = false;
public:
    explicit DeviceMonitor(const LedControlPtr& leds, std::unique_ptr<DiskSource> source = std::make_unique<UdevSource>());
    int fd() const override { return connected_ ? source_->fd() : -1; }
    void drain(Time now) override;
    void reconcile();
    void resync() { registry_.resync(); }
    void maintain(Time now) override;
    void disconnected(Time now) override { connected_ = false; reconcile_at_ = now; }
    void sample(Time now) override { registry_.sample(now); }
    size_t count() const override { return registry_.count(); }
    void clear() override { registry_.clear(); }
    int waitMs(Time now) const override { return millisecondsUntil(reconcile_at_, now); }
};
