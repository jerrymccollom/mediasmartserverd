// Unix-domain command socket for mediasmartctl. Bound before privilege drop so it
// can be owned by a dedicated group; the accepted-connection handling itself has
// no privilege requirements.
#pragma once
#include "ipc_protocol.h"
#include "runtime.h"
#include <functional>
#include <string>

class IpcServer {
    Fd fd_;
    std::string path_;
public:
    // Binds and listens on `path`; chowns/chmods it to root:`group` mode 0660 when
    // `group` exists, otherwise falls back to root-only (mode 0600).
    IpcServer(const std::string& path, const std::string& group);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    int fd() const { return fd_.get(); }

    // `handler` receives the parsed command plus a callback it may invoke to send an
    // early reply (e.g. acknowledging that a light show started) before doing any
    // further, potentially long-running work. Its return value is only sent if the
    // callback was never invoked.
    using Handler = std::function<Response(const Command&, const ResponseCallback&)>;
    // Accepts one pending connection, reads one bounded command line, invokes
    // `handler`, and writes back the formatted response (unless already sent via the
    // early-reply callback). Never throws on per-connection errors (malformed input,
    // timeouts); those are reported to the client as an ERROR response instead.
    void acceptAndHandle(const Handler& handler);
};
