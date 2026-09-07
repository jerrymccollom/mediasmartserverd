// Runtime ownership and Linux event sources. Altered version, 2026.
#include "runtime.h"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>

Signals::Signals() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, &previous_) < 0) throw ErrnoException("sigprocmask");
    fd_.reset(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
    if (!fd_) {
        const int error = errno;
        sigprocmask(SIG_SETMASK, &previous_, nullptr);
        throw ErrnoException("signalfd", error);
    }
}
Signals::~Signals() {
    // Consume repeated termination signals before restoring the caller's mask.
    signalfd_siginfo info;
    while (read(fd_.get(), &info, sizeof(info)) == sizeof(info)) {}
    sigprocmask(SIG_SETMASK, &previous_, nullptr);
}
bool Signals::consume() {
    signalfd_siginfo info;
    for (;;) {
        const auto n = read(fd_.get(), &info, sizeof(info));
        if (n == sizeof(info)) {
            if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) stopped_ = true;
        } else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && errno == EAGAIN) break;
        else throw ErrnoException("read signalfd");
    }
    return stopped_;
}
Timer::Timer() : fd_(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)) {
    if (!fd_) throw ErrnoException("timerfd_create");
}
void Timer::arm(Milliseconds interval) {
    itimerspec value{};
    value.it_value.tv_sec = interval.count() / 1000;
    value.it_value.tv_nsec = (interval.count() % 1000) * 1000000;
    value.it_interval = value.it_value;
    if (timerfd_settime(fd_.get(), 0, &value, nullptr) < 0) throw ErrnoException("timerfd_settime");
}
bool Timer::consume() {
    uint64_t count;
    if (read(fd_.get(), &count, sizeof(count)) == sizeof(count)) return true;
    if (errno == EAGAIN || errno == EINTR) return false;
    throw ErrnoException("read timerfd");
}
InstanceLock::InstanceLock(const std::string& path)
    : fd_(open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)) {
    if (!fd_) throw ErrnoException("open instance lock");
    struct stat st{};
    if (fstat(fd_.get(), &st) < 0) throw ErrnoException("stat instance lock");
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1)
        throw std::runtime_error("Unsafe instance lock file");
    if (flock(fd_.get(), LOCK_EX | LOCK_NB) < 0)
        throw ErrnoException("Another mediasmartserverd instance owns the hardware lock");
    // Keep the inode: unlinking would let competing processes lock different files.
}
Identity Privileges::lookup(const std::string& name) {
    const auto* pw = getpwnam(name.c_str());
    if (!pw) throw std::runtime_error("Runtime account not found: " + name);
    return {pw->pw_uid, pw->pw_gid};
}
void Privileges::clearGroups() { if (setgroups(0, nullptr) < 0) throw ErrnoException("setgroups"); }
void Privileges::group(gid_t gid) { if (setresgid(gid, gid, gid) < 0) throw ErrnoException("setresgid"); }
void Privileges::user(uid_t uid) { if (setresuid(uid, uid, uid) < 0) throw ErrnoException("setresuid"); }
bool Privileges::verify(Identity id) {
    uid_t r, e, s; gid_t gr, ge, gs;
    return getresuid(&r, &e, &s) == 0 && getresgid(&gr, &ge, &gs) == 0 &&
        r == id.uid && e == id.uid && s == id.uid && gr == id.gid && ge == id.gid &&
        gs == id.gid && getgroups(0, nullptr) == 0;
}
void Privileges::noNewPrivileges() {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) throw ErrnoException("no_new_privs");
}
void dropPrivileges(Privileges& ops, const std::string& name) {
    const auto id = ops.lookup(name);
    if (id.uid == 0 || id.gid == 0) throw std::runtime_error("Runtime identity must be unprivileged");
    ops.clearGroups();
    ops.group(id.gid);
    ops.user(id.uid);
    if (!ops.verify(id)) throw std::runtime_error("Runtime identity verification failed");
    ops.noNewPrivileges();
}
bool parseUnsigned(const std::string& text, uint64_t& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

int millisecondsUntil(Time deadline, Time now) {
    if (deadline <= now) return 0;
    const auto ms = std::chrono::ceil<Milliseconds>(deadline - now).count();
    return static_cast<int>(std::min<int64_t>(ms, 30000));
}
