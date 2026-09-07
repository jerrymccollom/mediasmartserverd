// Explicit opt-in credential test; no LED or raw port access. Altered version, 2026.
#include "helper_process.h"
#include "runtime.h"
#include <iostream>
#include <poll.h>
int main(int argc, char** argv) try {
    if (argc != 2 || geteuid() != 0) {
        std::cerr << "Run explicitly as root with the absolute helper-fixture path\n";
        return 77;
    }
    Signals signals;
    Privileges privileges;
    dropPrivileges(privileges, "nobody");
    HelperProcess helper(true); // Revokes inherited I/O permissions, never enables any.
    helper.start({argv[1], "identity"}, Clock::now());
    const auto until = Clock::now() + std::chrono::seconds(2);
    while (helper.running() && Clock::now() < until) {
        helper.service(Clock::now()); poll(nullptr, 0, 5);
    }
    if (!helper.success()) throw std::runtime_error("Unprivileged helper failed");
    const auto expected = std::to_string(getuid()) + " " + std::to_string(geteuid()) + " " +
        std::to_string(getgid()) + " " + std::to_string(getegid()) + " 0 0 1\n";
    if (helper.output() != expected) throw std::runtime_error("Unexpected helper identity/mask/groups");
    std::cout << "PASS actual UID/GID transition, empty groups, child identity, I/O permission revocation and no_new_privs\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
