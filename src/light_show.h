// Signal-aware animations. Altered version, 2026.
#pragma once
#include "led_control_base.h"
#include "runtime.h"
int run_light_show(const LedControlPtr& leds, int light_show, Signals& signals);
