// Single owner for disk events, timers, signals and update status. Altered version, 2026.
#pragma once
#include "runtime.h"
class UpdateMonitor;
class IpcServer;
struct DaemonState;
class DeviceEvents {
public:
    virtual ~DeviceEvents() = default;
    virtual int fd() const = 0;
    virtual void drain(Time now) = 0;
    virtual void maintain(Time now) = 0;
    virtual void disconnected(Time now) = 0;
    virtual void sample(Time now) = 0;
    virtual size_t count() const = 0;
    virtual void clear() = 0;
    virtual int waitMs(Time now) const = 0;
};
void runEventLoop(Signals& signals, DeviceEvents& devices, UpdateMonitor* updates, bool& activity,
    IpcServer& ipc, DaemonState& state);
