// Hardware-free before/after polling benchmark. Altered version, 2026.
// Baseline reproduces the original statistics open/getline/tokenize loop;
// GPIO access is represented by counters, never physical port instructions.
#include "device_monitor.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
int debug = 0, verbose = 0;
bool activity = false;
struct CountLeds : LedControlBase {
    uint64_t calls = 0;
    const char* Desc() const override { return "benchmark"; }
    bool Init() override { return true; }
    void MountUsb(bool) override {}
    void SetBrightness(int) override {}
    void SetSystemLed(int, LedState) override {}
    void Set(int, size_t, bool) override { ++calls; }
};
double cpu() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}
int main(int argc, char** argv) {
    if (argc != 2 || (std::string(argv[1]) != "before" && std::string(argv[1]) != "after")) return 2;
    UdevSource source; source.connect(); const auto inventory = source.inventory();
    if (inventory.empty()) { std::cerr << "No eligible ATA disks for read-only benchmark\n"; return 77; }
    constexpr unsigned iterations = 1000;
    uint64_t active = 0, led_calls = 0;
    const auto start = Clock::now(); const double start_cpu = cpu();
    if (std::string(argv[1]) == "before") {
        for (unsigned sample = 0; sample < iterations; ++sample) for (const auto& disk : inventory) {
            std::ifstream file(disk.stats); char buffer[256]{}; file.getline(buffer, 255); std::string s(buffer);
            auto last = s.find_first_not_of(" ", 0), pos = s.find_first_of(" ", last);
            int token = 0, queue = 0;
            while (pos != std::string::npos || last != std::string::npos) {
                last = s.find_first_not_of(" ", pos); pos = s.find_first_of(" ", last);
                if (++token == 8) { if (last != std::string::npos) queue = atoi(s.substr(last, pos - last).c_str()); break; }
            }
            active += queue > 0; led_calls += 2;
        }
    } else {
        StatsReader reader; auto leds = std::make_shared<CountLeds>(); DiskRegistry registry(leds, reader);
        registry.reconcile(inventory); leds->calls = 0;
        for (unsigned sample = 0; sample < iterations; ++sample) registry.sample(Clock::now());
        led_calls = leds->calls;
    }
    std::cout << argv[1] << ": disks=" << inventory.size() << " samples=" << iterations
        << " wall_ms=" << std::chrono::duration_cast<Milliseconds>(Clock::now() - start).count()
        << " cpu_ms=" << (cpu() - start_cpu) * 1000 << " led_requests=" << led_calls
        << " baseline_busy_observations=" << active << '\n';
}
