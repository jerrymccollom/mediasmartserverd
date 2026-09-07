// Runtime ownership and Linux event sources. Altered version, 2026.
#pragma once
#include "errno_exception.h"
#include <chrono>
#include <string>
#include <utility>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Milliseconds = std::chrono::milliseconds;

class Fd {
    int fd_ = -1;
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& rhs) noexcept : fd_(rhs.release()) {}
    Fd& operator=(Fd&& rhs) noexcept { if (this != &rhs) reset(rhs.release()); return *this; }
    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
    int release() { return std::exchange(fd_, -1); }
    void reset(int fd = -1) { if (fd_ >= 0) close(fd_); fd_ = fd; }
};

class Signals {
    sigset_t previous_{};
    Fd fd_;
    bool stopped_ = false;
public:
    Signals();
    ~Signals();
    Signals(const Signals&) = delete;
    Signals& operator=(const Signals&) = delete;
    int fd() const { return fd_.get(); }
    bool consume();
};

class Timer {
    Fd fd_;
public:
    Timer();
    int fd() const { return fd_.get(); }
    void arm(Milliseconds interval);
    bool consume();
};

class InstanceLock {
    Fd fd_;
public:
    explicit InstanceLock(const std::string& path);
};

struct Identity { uid_t uid; gid_t gid; };
class Privileges {
public:
    virtual ~Privileges() = default;
    virtual Identity lookup(const std::string& user);
    virtual void clearGroups();
    virtual void group(gid_t gid);
    virtual void user(uid_t uid);
    virtual bool verify(Identity identity);
    virtual void noNewPrivileges();
};
void dropPrivileges(Privileges& ops, const std::string& user);
bool parseUnsigned(const std::string& text, uint64_t& value);
std::string trim(const std::string& value);
int millisecondsUntil(Time deadline, Time now);
