// Explicit model selection. Altered version, 2026.
#pragma once
#include "led_control_base.h"
#include "port_io.h"
#include <string>
std::string detectModel(const std::string& vendor, const std::string& product, const std::string& override_model = {});
std::string dmiAttribute(const char* attribute);
LedControlPtr createHardware(const std::string& model, PortIo& io = NativePortIo::instance());
