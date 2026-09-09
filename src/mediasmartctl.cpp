/////////////////////////////////////////////////////////////////////////////
/// @file mediasmartctl.cpp
///
/// CLI client that sends light-control commands to a running mediasmartserverd
/// over its Unix-domain control socket.
/////////////////////////////////////////////////////////////////////////////
#include "ipc_protocol.h"
#include <cerrno>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using std::cout;
using std::cerr;

namespace {

int show_help() {
    cout << "Usage: mediasmartctl [OPTION]\n"
        << "     --brightness=X       Set LED brightness (0 to 9)\n"
        << "     --light-show=N       Start animation mode (1 to 13)\n"
        << "     --light-show=stop    Stop the running animation\n"
        << "     --activity=on|off    Toggle bay lights as disk activity indicators\n"
        << "     --disable-watchdog   Disable the SCH5127 watchdog\n"
        << "     --status             Report the daemon's current state\n"
        << "     --socket=PATH        Control socket path (default /run/mediasmartserverd.sock)\n"
        << "     --help               Print help text\n"
        << " -V, --version            Show version number\n"
    ;
    return 0;
}

int show_version() {
    cout << "mediasmartctl 0.0.1 compiled on " __DATE__ " " __TIME__ "\n";
    return 0;
}

// Connects, sends one line, reads one line back. Throws on transport failure.
Response send(const std::string& path, const Command& cmd) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw std::runtime_error(std::string("socket: ") + strerror(errno));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) { close(fd); throw std::runtime_error("Socket path too long: " + path); }
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int error = errno;
        close(fd);
        throw std::runtime_error("connect " + path + ": " + strerror(error) + " (is mediasmartserverd running?)");
    }

    // Bound how long we'll wait for a reply so a slow or unresponsive daemon can never
    // hang the CLI forever; the daemon itself always replies within a couple of seconds.
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    const auto request = formatCommand(cmd) + "\n";
    if (::send(fd, request.data(), request.size(), MSG_NOSIGNAL) < 0) {
        const int error = errno;
        close(fd);
        throw std::runtime_error(std::string("send: ") + strerror(error));
    }

    std::string line;
    char buffer[kMaxCommandLine + 1];
    for (;;) {
        const auto n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        const auto* end = static_cast<const char*>(memchr(buffer, '\n', n));
        line.append(buffer, end ? static_cast<size_t>(end - buffer) : static_cast<size_t>(n));
        if (end) break;
    }
    close(fd);

    Response response;
    parseResponseLine(line, response);
    return response;
}

} // namespace

int main(int argc, char* argv[]) try {
    std::string socket_path = "/run/mediasmartserverd.sock";
    bool have_command = false;
    Command cmd;

    const struct option long_opts[] = {
        { "brightness",        required_argument, 0, 'b' },
        { "light-show",        required_argument, 0, 'S' },
        { "activity",          required_argument, 0, 'a' },
        { "disable-watchdog",  no_argument,       0, 'w' },
        { "status",            no_argument,       0, 's' },
        { "socket",            required_argument, 0, 'p' },
        { "help",              no_argument,       0, 'h' },
        { "version",           no_argument,       0, 'V' },
        { 0, 0, 0, 0 },
    };

    while (true) {
        const int c = getopt_long(argc, argv, "V", long_opts, 0);
        if (-1 == c) break;

        switch (c) {
        case 'b': {
            std::string error;
            if (!parseCommandLine(std::string("BRIGHTNESS ") + optarg, cmd, error))
                throw std::invalid_argument(error);
            have_command = true;
            break;
        }
        case 'S': {
            std::string error;
            if (!parseCommandLine(std::string("LIGHT-SHOW ") + optarg, cmd, error))
                throw std::invalid_argument(error);
            have_command = true;
            break;
        }
        case 'a': {
            std::string error;
            if (!parseCommandLine(std::string("ACTIVITY ") + optarg, cmd, error))
                throw std::invalid_argument(error);
            have_command = true;
            break;
        }
        case 'w':
            cmd = Command{CommandType::DisableWatchdog, 0, false};
            have_command = true;
            break;
        case 's':
            cmd = Command{CommandType::Status, 0, false};
            have_command = true;
            break;
        case 'p':
            socket_path = optarg;
            break;
        case 'h':
            return show_help();
        case 'V':
            return show_version();
        case '?':
            cerr << "Try `" << argv[0] << " --help' for more information.\n";
            return 1;
        default:
            break;
        }
    }

    if (optind != argc) throw std::invalid_argument("Unexpected positional argument");
    if (!have_command) { show_help(); return 1; }

    const auto response = send(socket_path, cmd);
    cout << (response.ok ? "OK" : "ERROR") << (response.text.empty() ? "" : ": " + response.text) << '\n';
    return response.ok ? 0 : 1;

} catch (std::exception& e) {
    cerr << e.what() << '\n';
    return 1;
}
