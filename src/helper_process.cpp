// Bounded, event-loop-owned child process. Altered version, 2026.
#include "helper_process.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/io.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

HelperProcess::HelperProcess(bool revoke_io, Milliseconds timeout, Milliseconds grace)
    : revoke_io_(revoke_io), timeout_(timeout), grace_(grace) {
    // Reap descendants that keep the helper pipe open after their parent exits.
    if (prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) < 0) throw ErrnoException("child subreaper");
}
HelperProcess::~HelperProcess() {
    if (!running()) return;
    kill(-pid_, SIGKILL);
    const auto until = Clock::now() + std::chrono::seconds(1);
    try {
        while (running() && Clock::now() < until) {
            service(Clock::now());
            if (running()) poll(nullptr, 0, 5);
        }
    } catch (...) {}
    // Kernel-uninterruptible processes cannot be given a userspace time bound.
}
void HelperProcess::start(const std::vector<std::string>& command, Time now) {
    if (running()) throw std::logic_error("Helper already running");
    if (command.empty() || command[0].empty() || command[0][0] != '/')
        throw std::invalid_argument("Helper needs an absolute executable path");
    std::vector<char*> argv;
    for (const auto& word : command) argv.push_back(const_cast<char*>(word.c_str()));
    argv.push_back(nullptr);
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC) < 0) throw ErrnoException("pipe2");
    Fd read_end(pipes[0]), write_end(pipes[1]);
    if (fcntl(read_end.get(), F_SETFL, O_NONBLOCK) < 0) throw ErrnoException("nonblocking helper pipe");
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) < 0) throw ErrnoException("getrlimit");
    const auto child = fork();
    if (child < 0) throw ErrnoException("fork helper");
    if (child == 0) {
        if (setpgid(0, 0) < 0) _exit(126);
        sigset_t empty; sigemptyset(&empty);
        if (sigprocmask(SIG_SETMASK, &empty, nullptr) < 0) _exit(126);
        // ioperm permissions survive fork AND exec on supported Linux kernels.
        if (revoke_io_ && ioperm(0, 65536, 0) < 0) _exit(126);
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) _exit(126);
        const int output_fd = fcntl(write_end.get(), F_DUPFD_CLOEXEC, 3);
        if (output_fd < 0) _exit(126);
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0 ||
            dup2(output_fd, STDOUT_FILENO) < 0 || dup2(output_fd, STDERR_FILENO) < 0) _exit(126);
#ifdef SYS_close_range
        if (syscall(SYS_close_range, 3U, ~0U, 0) < 0)
#endif
            for (rlim_t fd = 3; fd < limit.rlim_cur; ++fd) close(static_cast<int>(fd));
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
        char locale[] = "LC_ALL=C";
        char home[] = "HOME=/nonexistent";
        char* env[] = {path, locale, home, nullptr};
        execve(argv[0], argv.data(), env);
        _exit(127);
    }
    // Child also sets its group, closing the scheduling race with cancellation.
    if (setpgid(child, child) < 0 && errno != EACCES && errno != ESRCH) {
        kill(child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        throw std::runtime_error("Cannot establish helper process group");
    }
    pid_ = child; output_ = std::move(read_end);
    deadline_ = now + timeout_;
    stopping_ = killed_ = failed_ = eof_ = finished_ = success_ = false;
    text_.clear(); exit_status_ = -1;
}
void HelperProcess::terminate(Time now) {
    if (!running() || stopping_) return;
    stopping_ = true; failed_ = true;
    kill(-pid_, SIGTERM);
    deadline_ = now + grace_;
}
void HelperProcess::stop(Time now) { terminate(now); }
void HelperProcess::service(Time now) {
    if (!running()) return;
    // Bound work per dispatch even if a malicious/broken helper writes forever.
    for (unsigned budget = 0; output_ && budget < 16384;) {
        char buffer[1024];
        const auto n = read(output_.get(), buffer, sizeof(buffer));
        if (n > 0) {
            budget += static_cast<unsigned>(n);
            if (text_.size() + static_cast<size_t>(n) > 4096) terminate(now);
            else text_.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) { output_.reset(); eof_ = true; }
        else if (errno == EINTR) continue;
        else if (errno == EAGAIN) break;
        else { output_.reset(); terminate(now); }
    }
    if (now >= deadline_) {
        if (!stopping_) terminate(now);
        else if (!killed_) { kill(-pid_, SIGKILL); killed_ = true; }
    }
    if (exit_status_ < 0) {
        siginfo_t info{};
        if (waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
            if (errno == EINTR) return;
            throw ErrnoException("waitid helper");
        }
        if (!info.si_pid) return;
        exit_status_ = info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
        if (!eof_) {
            // Drain the final bytes next dispatch. Descendants must not hold the
            // pipe indefinitely; the deadline continues to apply.
            service(now);
            return;
        }
    }
    if (!eof_ && !killed_) return;
    // Keep the leader unreaped until signaling the group to avoid PID reuse.
    kill(-pid_, SIGKILL);
    killed_ = true;
    int status;
    for (;;) {
        const auto child = waitpid(-pid_, &status, WNOHANG);
        if (child > 0) continue;
        if (child < 0 && errno == EINTR) continue;
        if (child == 0) return;
        if (errno != ECHILD) throw ErrnoException("waitpid helper group");
        break;
    }
    success_ = !failed_ && eof_ && exit_status_ == 0;
    finished_ = true; pid_ = -1; output_.reset();
}
