// Single owner for disk events, timers, signals and update status. Altered version, 2026.
#include "event_loop.h"
#include "command_dispatch.h"
#include "daemon_state.h"
#include "ipc_server.h"
#include "update_monitor.h"
#include <algorithm>
#include <poll.h>

void runEventLoop(Signals& signals, DeviceEvents& devices, UpdateMonitor* updates, bool& activity,
    IpcServer& ipc, DaemonState& state) {
    Timer sample;
    bool sampling = false, stopping = false;
    for (;;) {
        const auto now = Clock::now();
        if (signals.consume() && !stopping) {
            stopping = true;
            devices.clear();
            sample.arm(Milliseconds(0));
            if (updates) updates->stop(now);
        }
        if (!stopping) {
            devices.maintain(now);
            const bool needed = activity && devices.count();
            if (needed != sampling) {
                sample.arm(Milliseconds(needed ? 100 : 0));
                sampling = needed;
                if (needed) devices.sample(now);
            }
        }
        if (updates) updates->service(now);
        if (stopping && (!updates || !updates->running())) break;
        pollfd fds[] = {
            {signals.fd(), POLLIN, 0},
            {stopping ? -1 : devices.fd(), POLLIN, 0},
            {sample.fd(), POLLIN, 0},
            {updates && !stopping ? updates->watchFd() : -1, POLLIN, 0},
            {updates ? updates->helperFd() : -1, POLLIN, 0},
            {stopping ? -1 : ipc.fd(), POLLIN, 0},
        };
        const auto before_wait = Clock::now();
        int timeout = stopping ? 50 : devices.waitMs(before_wait);
        if (updates) timeout = std::min(timeout, updates->waitMs(before_wait));
        const int result = poll(fds, 6, timeout);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw ErrnoException("poll");
        }
        // Prioritize termination before any other ready work.
        if (fds[0].revents) continue;
        if (!stopping && (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) devices.disconnected(Clock::now());
        else if (!stopping && fds[1].revents) devices.drain(Clock::now());
        if (!stopping && fds[2].revents && sample.consume()) devices.sample(Clock::now());
        if (!stopping && updates && fds[3].revents) updates->filesystemEvents(Clock::now());
        if (!stopping && fds[5].revents) {
            ipc.acceptAndHandle([&](const Command& cmd, const ResponseCallback& ack) {
                return dispatch(cmd, state, signals, &ipc, ack);
            });
        }
    }
}

