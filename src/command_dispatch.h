// Applies a parsed Command to the running daemon's LED state. Altered version, 2026.
#pragma once
#include "daemon_state.h"
#include "ipc_protocol.h"
#include "runtime.h"
class IpcServer;
Response dispatch(const Command& cmd, DaemonState& state, Signals& signals, IpcServer* ipc,
    const ResponseCallback& acknowledge = {});
