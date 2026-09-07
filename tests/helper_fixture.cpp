// Hardware-free process fixtures. Altered version, 2026.
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "success";
    std::string selected;
    if (!strcmp(mode, "controlled") && argc > 2) {
        std::ifstream file(argv[2]); file >> selected; mode = selected.c_str();
    }
    if (!strcmp(mode, "success")) { std::cerr << "12;3\n"; return 0; }
    if (!strcmp(mode, "none")) { std::cerr << "0;0\n"; return 0; }
    if (!strcmp(mode, "failure")) { std::cerr << "0;0\n"; return 42; }
    if (!strcmp(mode, "malformed")) { std::cerr << "apt-check: not found\n"; return 0; }
    if (!strcmp(mode, "empty")) return 0;
    if (!strcmp(mode, "oversize")) {
        char data[1024]; memset(data, 'x', sizeof(data));
        for (;;) if (write(STDERR_FILENO, data, sizeof(data)) < 0) return 1;
    }
    if (!strcmp(mode, "identity")) {
        sigset_t mask; sigprocmask(SIG_SETMASK, nullptr, &mask);
        std::cout << getuid() << ' ' << geteuid() << ' ' << getgid() << ' ' << getegid() << ' '
                  << getgroups(0, nullptr) << ' ' << sigismember(&mask, SIGTERM) << ' ' << prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) << '\n';
        if (argc > 2) std::cout << (fcntl(atoi(argv[2]), F_GETFD) == -1) << '\n';
        return 0;
    }
    if (!strcmp(mode, "descendant")) {
        const auto child = fork();
        if (child < 0) return 1;
        if (child > 0) { std::cerr << "12;3\n"; return 0; }
    }
    if (!strcmp(mode, "ignore") || !strcmp(mode, "descendant")) signal(SIGTERM, SIG_IGN);
    if (!strcmp(mode, "close-hang")) { close(STDOUT_FILENO); close(STDERR_FILENO); }
    for (;;) pause();
}
