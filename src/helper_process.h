// Bounded, event-loop-owned child process. Altered version, 2026.
#pragma once
#include "runtime.h"
#include <vector>
class HelperProcess {
    pid_t pid_ = -1;
    Fd output_;
    Time deadline_{};
    bool stopping_ = false, killed_ = false, failed_ = false, eof_ = false;
    bool finished_ = false, success_ = false;
    bool revoke_io_;
    Milliseconds timeout_, grace_;
    std::string text_;
    int exit_status_ = -1;
    void terminate(Time now);
public:
    explicit HelperProcess(bool revoke_io = true, Milliseconds timeout = Milliseconds(30000),
                           Milliseconds grace = Milliseconds(500));
    ~HelperProcess();
    HelperProcess(const HelperProcess&) = delete;
    HelperProcess& operator=(const HelperProcess&) = delete;
    void start(const std::vector<std::string>& command, Time now);
    void service(Time now);
    void stop(Time now);
    bool running() const { return pid_ > 0; }
    bool finished() const { return finished_; }
    bool success() const { return finished_ && success_; }
    int fd() const { return output_.get(); }
    const std::string& output() const { return text_; }
};
