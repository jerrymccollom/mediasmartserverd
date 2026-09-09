// Applies a parsed Command to the running daemon's LED state. Altered version, 2026.
#include "command_dispatch.h"
#include "light_show.h"
#include <sstream>

Response dispatch(const Command& cmd, DaemonState& state, Signals& signals, IpcServer* ipc,
    const ResponseCallback& acknowledge) {
    try {
        switch (cmd.type) {
        case CommandType::Brightness:
            state.leds->SetBrightness(cmd.intValue);
            state.last_brightness = cmd.intValue;
            return {true, "brightness set to " + std::to_string(cmd.intValue)};
        case CommandType::LightShow:
            // Starting a show only returns once it stops (it runs its own poll loop
            // to watch for STOP/switch commands), so acknowledge the request that
            // started it right away instead of leaving that client hanging.
            if (acknowledge) acknowledge({true, "light show started"});
            run_light_show(state.leds, cmd.intValue, signals, ipc, &state);
            return {true, "light show finished"};
        case CommandType::LightShowStop:
            // Only meaningful while a show's own loop is watching for it; see light_show.cpp.
            return {false, "No light show is running"};
        case CommandType::Activity:
            state.activity = cmd.boolValue;
            return {true, std::string("activity ") + (cmd.boolValue ? "enabled" : "disabled")};
        case CommandType::DisableWatchdog:
            state.leds->DisableWatchdog();
            state.watchdog_disabled = true;
            return {true, "watchdog disabled"};
        case CommandType::Status: {
            std::ostringstream out;
            out << "hardware=" << state.leds->Desc()
                << " activity=" << (state.activity ? "on" : "off")
                << " brightness=" << (state.last_brightness >= 0 ? std::to_string(state.last_brightness) : "unknown")
                << " watchdog_disabled=" << (state.watchdog_disabled ? "yes" : "no");
            return {true, out.str()};
        }
        }
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
    return {false, "Unhandled command"};
}
