#include "ipc_server.h"
#include <cstring>
#include <grp.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr int kAcceptTimeoutSeconds = 2;
}

IpcServer::IpcServer(const std::string& path, const std::string& group) : path_(path) {
    fd_.reset(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (!fd_) throw ErrnoException("socket");

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) throw std::runtime_error("Socket path too long: " + path);
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    unlink(path.c_str()); // Best-effort: InstanceLock already guarantees we are the only instance.
    if (bind(fd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) throw ErrnoException("bind " + path);
    if (listen(fd_.get(), 16) < 0) throw ErrnoException("listen " + path);

    const auto* gr = getgrnam(group.c_str());
    if (gr) {
        if (chown(path.c_str(), geteuid(), gr->gr_gid) < 0) throw ErrnoException("chown " + path);
        if (chmod(path.c_str(), 0660) < 0) throw ErrnoException("chmod " + path);
    } else {
        std::cerr << "Group '" << group << "' not found; restricting " << path << " to root\n";
        if (chmod(path.c_str(), 0600) < 0) throw ErrnoException("chmod " + path);
    }
}

IpcServer::~IpcServer() {
    unlink(path_.c_str());
}

void IpcServer::acceptAndHandle(const Handler& handler) {
    Fd client(accept4(fd_.get(), nullptr, nullptr, SOCK_CLOEXEC));
    if (!client) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        throw ErrnoException("accept");
    }

    timeval timeout{kAcceptTimeoutSeconds, 0};
    setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string line;
    char buffer[kMaxCommandLine + 1];
    bool overflow = false;
    for (;;) {
        const auto n = recv(client.get(), buffer, sizeof(buffer), 0);
        if (n <= 0) break; // Timeout, error, or peer closed before sending a newline.
        const auto* end = static_cast<const char*>(memchr(buffer, '\n', n));
        const size_t take = end ? static_cast<size_t>(end - buffer) : static_cast<size_t>(n);
        if (line.size() + take > kMaxCommandLine) { overflow = true; break; }
        line.append(buffer, take);
        if (end) break;
    }

    bool replied = false;
    const ResponseCallback reply = [&](const Response& response) {
        if (replied) return; // Handlers must not send more than one reply per connection.
        replied = true;
        const auto text = formatResponse(response) + "\n";
        send(client.get(), text.data(), text.size(), MSG_NOSIGNAL);
    };

    if (overflow) {
        reply({false, "Command line too long"});
    } else {
        Command cmd;
        std::string error;
        if (!parseCommandLine(line, cmd, error)) reply({false, error});
        else reply(handler(cmd, reply));
    }
}
