// Signal-aware animations. Altered version, 2026.
#pragma once
#include "led_control_base.h"
#include "runtime.h"
class IpcServer;
struct DaemonState;
// When `ipc` is non-null, the show also watches for incoming commands: LIGHT-SHOW STOP
// ends it, a new LIGHT-SHOW switches modes, and any other command is applied via
// dispatch() (declared in command_dispatch.h) without interrupting the animation.
int run_light_show(const LedControlPtr& leds, int light_show, Signals& signals,
    IpcServer* ipc = nullptr, DaemonState* state = nullptr);
