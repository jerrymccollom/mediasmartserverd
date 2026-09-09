// Mutable daemon state that IPC commands are allowed to observe or change.
#pragma once
#include "led_control_base.h"

class DeviceMonitor;

struct DaemonState {
    LedControlPtr leds;
    bool& activity;
    int last_brightness = -1;
    bool watchdog_disabled = false;
    DeviceMonitor* devices = nullptr; // Set once constructed; used to restore bay LEDs after a light show.
};
