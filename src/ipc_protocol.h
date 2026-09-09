// Text wire protocol shared by the daemon's IPC listener and mediasmartctl.
#pragma once
#include <cstddef>
#include <functional>
#include <string>

constexpr size_t kMaxCommandLine = 256;

enum class CommandType { Brightness, LightShow, LightShowStop, Activity, DisableWatchdog, Status };

struct Command {
    CommandType type = CommandType::Status;
    int intValue = 0;    // brightness (0-9) or light-show mode (1-13)
    bool boolValue = false; // activity on/off
};

// Result of executing a Command, rendered as "OK <text>" or "ERROR <text>".
struct Response {
    bool ok = false;
    std::string text;
};

// Lets a command handler send its reply before finishing work that may block for a
// long time (e.g. starting a light show that only returns once stopped), so the
// requesting client is not left waiting. Calling it more than once has no extra effect.
using ResponseCallback = std::function<void(const Response&)>;

// Parses one line of the protocol (no trailing newline). Returns false and fills `error` on failure.
bool parseCommandLine(const std::string& line, Command& out, std::string& error);

// Renders a Command back into a protocol line (used by the client to build requests).
std::string formatCommand(const Command& cmd);

// Renders/parses the server's "OK <text>" or "ERROR <text>" reply line.
std::string formatResponse(const Response& response);
void parseResponseLine(const std::string& line, Response& out);
