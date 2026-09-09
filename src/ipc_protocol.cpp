// Text wire protocol shared by the daemon's IPC listener and mediasmartctl.
#include "ipc_protocol.h"
#include "runtime.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
    return value;
}
}

bool parseCommandLine(const std::string& line, Command& out, std::string& error) {
    std::istringstream stream(trim(line));
    std::string verb;
    if (!(stream >> verb)) { error = "Empty command"; return false; }
    verb = upper(verb);

    if (verb == "BRIGHTNESS") {
        std::string arg;
        uint64_t value;
        if (!(stream >> arg) || !parseUnsigned(arg, value) || value > 9) {
            error = "Usage: BRIGHTNESS 0-9";
            return false;
        }
        out = Command{CommandType::Brightness, static_cast<int>(value), false};
    } else if (verb == "LIGHT-SHOW") {
        std::string arg;
        if (!(stream >> arg)) { error = "Usage: LIGHT-SHOW 1-13|STOP"; return false; }
        if (upper(arg) == "STOP") {
            out = Command{CommandType::LightShowStop, 0, false};
        } else {
            uint64_t value;
            if (!parseUnsigned(arg, value) || value < 1 || value > 13) {
                error = "Usage: LIGHT-SHOW 1-13|STOP";
                return false;
            }
            out = Command{CommandType::LightShow, static_cast<int>(value), false};
        }
    } else if (verb == "ACTIVITY") {
        std::string arg;
        if (!(stream >> arg)) { error = "Usage: ACTIVITY ON|OFF"; return false; }
        const auto normalized = upper(arg);
        if (normalized != "ON" && normalized != "OFF") { error = "Usage: ACTIVITY ON|OFF"; return false; }
        out = Command{CommandType::Activity, 0, normalized == "ON"};
    } else if (verb == "DISABLE-WATCHDOG") {
        out = Command{CommandType::DisableWatchdog, 0, false};
    } else if (verb == "STATUS") {
        out = Command{CommandType::Status, 0, false};
    } else {
        error = "Unknown command: " + verb;
        return false;
    }

    std::string extra;
    if (stream >> extra) { error = "Unexpected extra argument: " + extra; return false; }
    return true;
}

std::string formatCommand(const Command& cmd) {
    switch (cmd.type) {
    case CommandType::Brightness: return "BRIGHTNESS " + std::to_string(cmd.intValue);
    case CommandType::LightShow: return "LIGHT-SHOW " + std::to_string(cmd.intValue);
    case CommandType::LightShowStop: return "LIGHT-SHOW STOP";
    case CommandType::Activity: return std::string("ACTIVITY ") + (cmd.boolValue ? "ON" : "OFF");
    case CommandType::DisableWatchdog: return "DISABLE-WATCHDOG";
    case CommandType::Status: return "STATUS";
    }
    return {};
}

std::string formatResponse(const Response& response) {
    return (response.ok ? "OK" : "ERROR") + (response.text.empty() ? std::string() : " " + response.text);
}

void parseResponseLine(const std::string& line, Response& out) {
    const auto trimmed = trim(line);
    const auto space = trimmed.find(' ');
    const auto verb = trimmed.substr(0, space);
    out.ok = (verb == "OK");
    out.text = (space == std::string::npos) ? std::string() : trimmed.substr(space + 1);
}
