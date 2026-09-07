// Regression suite: all hardware accesses are fake. Altered version, 2026.
#include "device_monitor.h"
#include "event_loop.h"
#include "light_show.h"
#include "hardware.h"
#include "helper_process.h"
#include "led_hpex485.h"
#include "led_acerh341.h"
#include "update_monitor.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <deque>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

int debug = 0, verbose = 0;
bool activity = false;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #x); } while (false)
template<typename F> void throws(F fn) { bool caught = false; try { fn(); } catch (const std::exception&) { caught = true; } CHECK(caught); }
struct TempDir {
    std::string path;
    TempDir() { char pattern[] = "/tmp/mediasmartserverd-test-XXXXXX"; const char* p = mkdtemp(pattern); CHECK(p); path = p; }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    void write(const std::string& name, const std::string& content) { std::ofstream file(path + "/" + name); file << content; CHECK(file.good()); }
};
struct FakeLeds : LedControlBase {
    std::array<int, 4> bays{};
    int system = 0;
    size_t calls = 0;
    const char* Desc() const override { return "fake"; }
    bool Init() override { return true; }
    void MountUsb(bool) override {}
    void SetBrightness(int) override {}
    void Set(int channel, size_t bay, bool on) override {
        CHECK(bay < 4); ++calls;
        if (on) bays[bay] |= channel; else bays[bay] &= ~channel;
    }
    using LedControlBase::SetSystemLed;
    void SetSystemLed(int channel, LedState state) override {
        ++calls;
        if (state == LED_ON) system |= channel; else system &= ~channel;
    }
};
struct FakeIo : PortIo {
    struct Write { unsigned port; unsigned width; uint32_t value; };
    std::vector<Write> writes;
    std::bitset<65536> allowed;
    std::map<unsigned, uint8_t> memory;
    uint32_t pci_address = 0, lpc = 0x29168086, gpio_base = 0x501;
    unsigned sch_base = 0x600, sio = 0x2e;
    uint8_t id = 0x86;
    std::map<unsigned, uint8_t> index;
    int grants = 0, fail_grant = -1;
    size_t reads = 0;
    int permission(unsigned port, unsigned count, bool enable) override {
        if (enable && ++grants == fail_grant) { errno = EPERM; return -1; }
        for (unsigned i = port; i < port + count; ++i) allowed[i] = enable;
        return 0;
    }
    void check(unsigned port, unsigned width) { for (unsigned i = port; i < port + width; ++i) CHECK(allowed[i]); }
    uint8_t read8(unsigned port) override {
        check(port, 1); ++reads;
        if (port == 0x2f || port == 0x4f) {
            if (port != sio + 1) return 0xff;
            switch (index[port - 1]) {
            case 0x20: return id;
            case 0x60: return sch_base >> 8;
            case 0x61: return sch_base & 0xff;
            default: return 0;
            }
        }
        return memory[port];
    }
    uint32_t read32(unsigned port) override {
        check(port, 4); ++reads;
        if (port == 0xcfc) return pci_address == 0x8000f800 ? lpc : gpio_base;
        uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result |= uint32_t(memory[port + i]) << (8 * i);
        return result;
    }
    void write8(uint8_t value, unsigned port) override {
        check(port, 1); writes.push_back({port, 1, value});
        if (port == 0x2e || port == 0x4e) index[port] = value;
        else memory[port] = value;
    }
    void write32(uint32_t value, unsigned port) override {
        check(port, 4); writes.push_back({port, 4, value});
        if (port == 0xcf8) pci_address = value;
        else for (unsigned i = 0; i < 4; ++i) memory[port + i] = (value >> (8 * i)) & 0xff;
    }
    bool configured() const {
        return std::any_of(writes.begin(), writes.end(), [](const Write& w) { return w.port >= 0x100 && w.port != 0xcf8; });
    }
};
struct Probe : LedHpEx48X { using LedHpEx48X::LedHpEx48X; using LedHpEx48X::setBit32_; };
void testHardware() {
    for (int bit : {0, 31, 32, 38, 39, 57, 60}) {
        uint32_t first = 0, second = 0;
        Probe::setBit32_(bit, first, second);
        CHECK((bit < 32 ? first : second) == (uint32_t{1} << (bit % 32)));
        CHECK((bit < 32 ? second : first) == 0);
    }
    uint32_t first = 0, second = 0;
    throws([&] { Probe::setBit32_(-1, first, second); });
    throws([&] { Probe::setBit32_(61, first, second); });
    for (const std::string model : {"hp-ex48x", "acer-h340", "acer-h341", "acer-altos-m2"}) {
        FakeIo io;
        if (model == "acer-h340" || model == "acer-altos-m2") io.lpc = 0x27b88086;
        io.memory[0x500] = 1; io.memory[0x504] = 0xff;
        {
            auto hardware = createHardware(model, io);
            CHECK(io.memory[0x500] & 1); CHECK(io.memory[0x504] & 1);
            CHECK(std::none_of(io.writes.begin(), io.writes.end(), [](const FakeIo::Write& w) { return w.port >= 0x665 && w.port <= 0x668; }));
            hardware->SetSystemLed(LED_BLUE, true);
            hardware->Set(LED_RED, 0, true);
            if (model == "hp-ex48x") {
                CHECK(!(io.read32(0x50c) & (uint32_t{1} << 28)));
                CHECK(!(io.read32(0x50c) & (uint32_t{1} << 4)));
                CHECK(io.memory[0x533] & 2); // GPIO57 in second bank.
            }
            hardware->SetBrightness(4); hardware->DisableWatchdog();
            for (unsigned i = 0x665; i <= 0x668; ++i) CHECK(io.memory[i] == 0);
        }
        CHECK(io.allowed.none());
    }
}
void testRejectedHardware() {
    for (int scenario = 0; scenario < 6; ++scenario) {
        FakeIo io;
        if (scenario == 0) io.lpc = 0;
        if (scenario == 1) io.id = 0;
        if (scenario == 2) io.gpio_base = 0;
        if (scenario == 3) io.sch_base = 0;
        if (scenario == 4) io.sch_base = 0x601;
        if (scenario == 5) io.sch_base = 0x500;
        throws([&] { createHardware("hp-ex48x", io); });
        CHECK(!io.configured()); CHECK(io.allowed.none());
    }
    for (int failure = 1; failure <= 15; ++failure) {
        FakeIo io; io.fail_grant = failure;
        try { auto leds = createHardware("hp-ex48x", io); } catch (const std::exception&) {}
        CHECK(io.allowed.none());
    }
    FakeIo io; io.sio = 0x4e;
    { auto hardware = createHardware("hp-ex48x", io); }
    CHECK(io.allowed.none());
    CHECK(detectModel("HP", "MediaSmart Server") == "hp-ex48x");
    CHECK(detectModel("Acer", "Aspire easyStore H342") == "acer-h341");
    CHECK(detectModel("LENOVO", "IdeaCentre D400 10023") == "acer-h340");
    CHECK(detectModel("", "", "acer-altos-m2") == "acer-altos-m2");
    throws([] { detectModel("", ""); }); throws([] { detectModel("HP", "Laptop"); });
    throws([] { detectModel("Acer", "Unknown"); }); throws([] { detectModel("", "", "bad"); });
}
void testSchWidths() {
    FakeIo io;
    {
        LedAcerH341 leds(io); CHECK(leds.Init()); io.writes.clear();
        leds.Set(LED_BLUE, 0, true);
        CHECK(io.writes.size() == 1);
        CHECK(io.writes[0].port == 0x64f && io.writes[0].width == 1 && io.writes[0].value == 8);
        leds.Set(LED_RED, 0, true);
        CHECK(io.writes.back().port == 0x650 && io.writes.back().value == 2);
    }
    CHECK(io.allowed.none());
}
struct FakeStats : StatsReader {
    std::map<std::string, DiskStats> values;
    size_t reads = 0;
    bool read(Fd&, const std::string& path, DiskStats& stats) override {
        ++reads;
        if (!values.count(path)) return false;
        stats = values.at(path); return true;
    }
};
Disk disk(int bay, const std::string& identity = "disk", uint64_t sequence = 1) {
    return {"/fake/bay" + std::to_string(bay), identity, "stats" + std::to_string(bay), bay, sequence};
}
void testDiskLifecycle() {
    auto leds = std::make_shared<FakeLeds>(); FakeStats reader;
    DiskRegistry registry(leds, reader); const auto now = Clock::now();
    CHECK(registry.count() == 0);
    CHECK(registry.add(disk(0))); CHECK(leds->bays[0] == LED_BLUE);
    reader.values["stats0"] = {}; registry.sample(now);
    const auto calls = leds->calls;
    registry.sample(now + Milliseconds(100)); CHECK(leds->calls == calls);
    reader.values["stats0"].completed[0] = 1;
    registry.sample(now + Milliseconds(200)); CHECK(leds->bays[0] == (LED_BLUE | LED_RED));
    registry.remove("/fake/bay0", 2); CHECK(leds->bays[0] == 0); CHECK(registry.count() == 0);
    CHECK(registry.add(disk(0, "replacement", 4)));
    registry.remove("/fake/bay0", 3); CHECK(registry.count() == 1);
    CHECK(!registry.add(disk(0, "stale", 2)));
    registry.sample(now + Milliseconds(300)); CHECK(leds->bays[0] == LED_BLUE);
    reader.values["stats0"].inflight = 1;
    registry.sample(now + Milliseconds(400)); CHECK(leds->bays[0] & LED_RED);
    reader.values["stats0"].inflight = 0; reader.values["stats0"].completed[0] = 0;
    registry.sample(now + Milliseconds(500)); CHECK(leds->bays[0] == LED_BLUE);
    CHECK(!registry.add(disk(-1))); CHECK(!registry.add(disk(4))); CHECK(!registry.add(disk(100)));
    registry.clear(); const auto clear_calls = leds->calls; registry.clear(); CHECK(leds->calls == clear_calls);
    CHECK(registry.add(disk(1)));
    registry.sample(now); const auto reads = reader.reads;
    registry.sample(now + Milliseconds(100)); CHECK(reader.reads == reads);
    reader.values["stats1"].inflight = 1;
    registry.sample(now + Milliseconds(1000)); CHECK(leds->bays[1] == (LED_BLUE | LED_RED));
}
void testReconciliation() {
    auto leds = std::make_shared<FakeLeds>(); FakeStats reader; DiskRegistry registry(leds, reader);
    std::vector<Disk> inventory;
    for (int i = 0; i < 20; ++i) inventory.push_back(disk(i, "disk" + std::to_string(i)));
    for (int i = 0; i < 500; ++i) {
        std::reverse(inventory.begin(), inventory.end()); registry.reconcile(inventory); CHECK(registry.count() == 4);
        registry.remove("/fake/bay0", 2); registry.add(disk(0, "replacement", 3));
        registry.remove("/fake/bay0", 1); CHECK(registry.count() == 4);
    }
    auto conflict = disk(0, "other"); conflict.path = "/fake/conflict";
    inventory.push_back(conflict); registry.reconcile(inventory);
    CHECK(registry.count() == 3 && leds->bays[0] == 0);
    registry.reconcile({}); CHECK(registry.count() == 0);
    for (unsigned p = 1; p <= 4; ++p) CHECK(bayForPort("0000:00:1f.2", p) == static_cast<int>(p - 1));
    CHECK(bayForPort("0000:02:00.0", 1) == -1); CHECK(bayForPort("0000:00:1f.2", 0) == -1);
    CHECK(bayForPort("0000:00:1f.2", 12) == -1);
}
struct ReplaySource : DiskSource {
    bool connected = false, fail = false;
    unsigned connections = 0;
    std::vector<Disk> snapshot;
    std::deque<DiskEvent> queue;
    void connect() override { connected = true; ++connections; }
    int fd() const override { return -1; }
    std::vector<Disk> inventory() override { CHECK(connected); return snapshot; }
    bool receive(DiskEvent& event) override {
        if (fail) { fail = false; throw std::runtime_error("simulated ENOBUFS"); }
        if (queue.empty()) return false;
        event = queue.front(); queue.pop_front(); return true;
    }
};
void testStartupEvents() {
    auto leds = std::make_shared<FakeLeds>();
    auto source = std::make_unique<ReplaySource>(); auto* replay = source.get();
    replay->snapshot = {disk(0)};
    // Disk 0 was removed and disk 1 inserted while enumeration was underway.
    replay->queue.push_back({"/fake/bay0", {}, 2, false, false});
    replay->queue.push_back({"/fake/bay1", disk(1, "new", 3), 3, true, false});
    DeviceMonitor monitor(leds, std::move(source));
    CHECK(monitor.count() == 1 && leds->bays[0] == 0 && leds->bays[1] == LED_BLUE);
    replay->snapshot = {disk(2, "recovered", 4)}; replay->fail = true;
    const auto now = Clock::now(); monitor.drain(now); CHECK(monitor.fd() == -1);
    monitor.maintain(now); CHECK(replay->connections == 2);
    CHECK(monitor.count() == 1 && leds->bays[1] == 0 && leds->bays[2] == LED_BLUE);
    for (unsigned i = 0; i < 1000; ++i) replay->queue.push_back({"/fake/bay2", disk(2, "recovered", 5 + i), 5 + i, true, false});
    monitor.drain(now); CHECK(replay->queue.size() == 936); // Bounded dispatch.
    while (!replay->queue.empty()) monitor.drain(now);
    CHECK(monitor.count() == 1);
    replay->queue.push_back({"/fake/bay2", {}, 4, false, false});
    monitor.drain(now); CHECK(monitor.count() == 1); // Stale removal is ignored.
}
void testStatistics() {
    DiskStats stats;
    auto parse = [&](const std::string& s) { return parseDiskStats(s.data(), s.size(), stats); };
    CHECK(parse("1 2 3 4 5 6 7 8 9 10 11")); CHECK(stats.completed[0] == 1 && stats.completed[1] == 5 && stats.inflight == 9);
    CHECK(parse("\t18446744073709551615 0 0 0 0 0 0 0 0 0 0 6 0 0 0 7 0 99\n")); CHECK(stats.completed[2] == 6 && stats.completed[3] == 7);
    for (const std::string value : {"", "1 2", "-1 0 0 0 0 0 0 0 0 0 0", "18446744073709551616 0 0 0 0 0 0 0 0 0 0",
                                  "0 0 0 0 0 0 0 0 0 0 0x", "0 0 0 0 0 0 0 0 0 0 0 1"}) CHECK(!parse(value));
    TempDir dir; dir.write("stat", "0 0 0 0 0 0 0 0 1 0 0\n"); StatsReader reader; Fd fd;
    CHECK(reader.read(fd, dir.path + "/stat", stats)); const int first_fd = fd.get();
    dir.write("stat", "1 0 0 0 0 0 0 0 0 0 0\n"); CHECK(reader.read(fd, dir.path + "/stat", stats)); CHECK(fd.get() == first_fd && stats.completed[0] == 1);
    dir.write("stat", "broken"); CHECK(!reader.read(fd, dir.path + "/stat", stats)); CHECK(!fd);
}
void testUpdateParsing() {
    UpdateCounts counts; CHECK(parseUpdateCounts("0;0", counts));
    CHECK(parseUpdateCounts(" \t12;3\n", counts)); CHECK(counts.total == 12 && counts.security == 3);
    for (const std::string value : {"", "apt-check: not found", "1", "-1;0", "1;-1", "1;2", "1;0;0", "1;0 junk", "1;", "1; 0", "18446744073709551616;0"}) CHECK(!parseUpdateCounts(value, counts));
}
struct FakePrivileges : Privileges {
    std::vector<std::string> calls; std::string fail;
    void call(const std::string& name) { calls.push_back(name); if (name == fail) throw std::runtime_error(name); }
    Identity lookup(const std::string&) override { call("lookup"); return {123, 456}; }
    void clearGroups() override { call("groups"); }
    void group(gid_t gid) override { CHECK(gid == 456); call("gid"); }
    void user(uid_t uid) override { CHECK(uid == 123); call("uid"); }
    bool verify(Identity id) override { CHECK(id.uid == 123 && id.gid == 456); call("verify"); return true; }
    void noNewPrivileges() override { call("no-new-privileges"); }
};
void testPrivileges() {
    const std::vector<std::string> expected{"lookup", "groups", "gid", "uid", "verify", "no-new-privileges"};
    FakePrivileges ok; dropPrivileges(ok, "test"); CHECK(ok.calls == expected);
    for (size_t i = 0; i < expected.size(); ++i) {
        FakePrivileges ops; ops.fail = expected[i]; throws([&] { dropPrivileges(ops, "test"); }); CHECK(ops.calls.size() == i + 1);
    }
}
void testLock() {
    TempDir dir;
    { InstanceLock first(dir.path + "/lock"); throws([&] { InstanceLock second(dir.path + "/lock"); }); }
    { InstanceLock second(dir.path + "/lock"); }
    CHECK(symlink((dir.path + "/lock").c_str(), (dir.path + "/symlink").c_str()) == 0);
    throws([&] { InstanceLock unsafe(dir.path + "/symlink"); });
}
void driveHelper(HelperProcess& helper, Milliseconds bound = Milliseconds(2000)) {
    const auto until = Clock::now() + bound;
    while (helper.running() && Clock::now() < until) { pollfd fd{helper.fd(), POLLIN, 0}; poll(&fd, 1, 5); helper.service(Clock::now()); }
    CHECK(!helper.running()); CHECK(helper.finished());
}
void testHelpers(const std::string& fixture) {
    Signals signals; HelperProcess helper(false, Milliseconds(1000), Milliseconds(100)); helper.stop(Clock::now());
    for (const std::string mode : {"success", "none", "failure", "empty", "malformed", "oversize", "hang", "ignore", "descendant", "close-hang"}) {
        helper.start({fixture, mode}, Clock::now()); driveHelper(helper);
        if (helper.success() != (mode == "success" || mode == "none" || mode == "empty" || mode == "malformed"))
            throw std::runtime_error("Unexpected helper result for " + mode + ": " + helper.output());
        CHECK(helper.output().size() <= 4096); CHECK(helper.fd() == -1);
    }
    helper.start({"/nonexistent/helper"}, Clock::now()); driveHelper(helper); CHECK(!helper.success());
    helper.start({fixture, "ignore"}, Clock::now()); poll(nullptr, 0, 30);
    helper.stop(Clock::now()); helper.stop(Clock::now()); driveHelper(helper); CHECK(!helper.success());
    TempDir dir; Fd inherited(open((dir.path + "/fd").c_str(), O_CREAT | O_RDWR, 0600)); CHECK(inherited);
    helper.start({fixture, "identity", std::to_string(inherited.get())}, Clock::now()); driveHelper(helper); CHECK(helper.success());
    CHECK(helper.output().find(" 0 1\n1\n") != std::string::npos);
    int status; CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}
void testSignalsAndTimer() {
    Signals signals; CHECK(kill(getpid(), SIGTERM) == 0); CHECK(signals.consume()); CHECK(signals.consume());
    Timer timer; timer.arm(Milliseconds(5)); pollfd fd{timer.fd(), POLLIN, 0};
    CHECK(poll(&fd, 1, 100) == 1); CHECK(timer.consume()); timer.arm(Milliseconds(0)); CHECK(poll(&fd, 1, 10) == 0);
}
void testUpdates(const std::string& fixture) {
    Signals signals; TempDir dir; auto leds = std::make_shared<FakeLeds>();
    UpdatePaths paths{{fixture, "success"}, dir.path, dir.path, dir.path};
    {
        UpdateMonitor updates(leds, paths, false, Milliseconds(500));
        const auto until = Clock::now() + std::chrono::seconds(2);
        while (!updates.known() && Clock::now() < until) { updates.service(Clock::now()); poll(nullptr, 0, 5); }
        CHECK(updates.known()); CHECK(leds->system == (LED_RED | LED_BLUE));
        dir.write("reboot-required", ""); updates.filesystemEvents(Clock::now()); CHECK(leds->system == LED_RED);
        CHECK(unlink((dir.path + "/reboot-required").c_str()) == 0); updates.filesystemEvents(Clock::now()); CHECK(leds->system == (LED_RED | LED_BLUE));
        const auto calls = leds->calls; updates.service(Clock::now()); CHECK(leds->calls == calls);
        updates.stop(Clock::now()); CHECK(leds->system == 0);
    }
    paths.command = {fixture, "ignore"}; UpdateMonitor updates(leds, paths, false, Milliseconds(100)); updates.service(Clock::now());
    dir.write("reboot-required", ""); updates.filesystemEvents(Clock::now()); CHECK(leds->system == LED_RED);
    updates.stop(Clock::now()); const auto until = Clock::now() + std::chrono::seconds(2);
    while (updates.running() && Clock::now() < until) { updates.service(Clock::now()); poll(nullptr, 0, 5); }
    CHECK(!updates.running()); CHECK(!updates.known()); CHECK(leds->system == 0);
}
void testUpdateRecovery(const std::string& fixture) {
    Signals signals; TempDir dir;
    for (const std::string name : {"run", "dpkg", "apt"}) std::filesystem::create_directory(dir.path + "/" + name);
    dir.write("mode", "success");
    UpdatePaths paths{{fixture, "controlled", dir.path + "/mode"}, dir.path + "/run", dir.path + "/dpkg", dir.path + "/apt"};
    auto leds = std::make_shared<FakeLeds>(); UpdateMonitor updates(leds, paths, false);
    auto now = Clock::now();
    auto complete = [&] {
        const auto until = Clock::now() + std::chrono::seconds(2);
        while (updates.running() && Clock::now() < until) { updates.service(now); poll(nullptr, 0, 5); }
        CHECK(!updates.running());
    };
    updates.service(now); complete(); CHECK(updates.known() && leds->system == (LED_BLUE | LED_RED));
    dir.write("mode", "failure"); dir.write("dpkg/status", "changed"); updates.filesystemEvents(now);
    updates.service(now + std::chrono::seconds(1)); CHECK(!updates.running());
    now += std::chrono::seconds(2); updates.service(now); CHECK(updates.running()); complete();
    CHECK(leds->system == (LED_BLUE | LED_RED)); // Preserve last-known valid result.
    dir.write("mode", "none"); dir.write("dpkg/status", "changed again"); updates.filesystemEvents(now);
    updates.service(now + std::chrono::seconds(3)); CHECK(!updates.running()); // Failure backoff wins over events.
    now += std::chrono::seconds(61); updates.service(now); CHECK(updates.running()); complete();
    CHECK(updates.known() && leds->system == 0);
    std::filesystem::rename(dir.path + "/run", dir.path + "/old-run");
    updates.filesystemEvents(now);
    std::filesystem::create_directory(dir.path + "/run"); dir.write("run/reboot-required", "");
    now += std::chrono::seconds(31); updates.service(now); CHECK(leds->system == LED_RED);
    CHECK(unlink((dir.path + "/run/reboot-required").c_str()) == 0);
    updates.filesystemEvents(now); CHECK(leds->system == 0); // Replacement directory is watched again.
}
struct EventDevices : DeviceEvents {
    Fd input, output; size_t disks = 0, samples = 0, events = 0;
    Time begin = Clock::now(), queued; bool sent = false, cleared = false;
    std::vector<double> latency;
    EventDevices() { int fds[2]; CHECK(pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0); input.reset(fds[0]); output.reset(fds[1]); }
    int fd() const override { return input.get(); }
    void drain(Time now) override {
        char c;
        if (read(input.get(), &c, 1) == 1) {
            disks = 1; ++events; sent = false;
            latency.push_back(std::chrono::duration<double, std::milli>(now - queued).count());
        }
    }
    void maintain(Time now) override {
        if (!sent && events < 100) { queued = now; CHECK(write(output.get(), "x", 1) == 1); sent = true; }
        if (now - begin > Milliseconds(350)) CHECK(kill(getpid(), SIGTERM) == 0);
    }
    void disconnected(Time) override { input.reset(); }
    void sample(Time) override { ++samples; }
    size_t count() const override { return disks; }
    void clear() override { disks = 0; cleared = true; }
    int waitMs(Time) const override { return 50; }
};
void testEventLoop(const std::string& fixture) {
    Signals signals; EventDevices devices; TempDir dir; auto leds = std::make_shared<FakeLeds>();
    UpdatePaths paths{{fixture, "ignore"}, dir.path, dir.path, dir.path}; UpdateMonitor updates(leds, paths, false);
    const auto start = Clock::now(); runEventLoop(signals, devices, &updates, true);
    const auto elapsed = std::chrono::duration_cast<Milliseconds>(Clock::now() - start).count();
    CHECK(elapsed < 2000); CHECK(devices.events == 100); CHECK(devices.samples >= 3); CHECK(devices.cleared && !updates.running());
    std::sort(devices.latency.begin(), devices.latency.end());
    CHECK(devices.latency[94] < 50);
    std::cout << "  hotplug p95=" << devices.latency[94] << " ms; sampling during hung helper; shutdown scenario=" << elapsed << " ms\n";
}
void testIdleAndProcessingSignals() {
    struct IdleDevices : DeviceEvents {
        size_t disks = 0, samples = 0; bool cleared = false, stop_in_sample = false;
        Time stop_at = Clock::now() + Milliseconds(150);
        int fd() const override { return -1; }
        void drain(Time) override {}
        void disconnected(Time) override {}
        void maintain(Time now) override { if (now >= stop_at) CHECK(kill(getpid(), SIGINT) == 0); }
        void sample(Time) override { ++samples; if (stop_in_sample) CHECK(kill(getpid(), SIGTERM) == 0); }
        size_t count() const override { return disks; }
        void clear() override { cleared = true; }
        int waitMs(Time now) const override { return millisecondsUntil(stop_at, now); }
    };
    for (int scenario = 0; scenario < 3; ++scenario) {
        Signals signals; IdleDevices devices;
        devices.disks = scenario ? 4 : 0; devices.stop_in_sample = scenario == 2;
        const auto start = Clock::now(); runEventLoop(signals, devices, nullptr, scenario != 1);
        CHECK(devices.cleared);
        CHECK(devices.samples == (scenario == 2 ? 1U : 0U));
        CHECK(Clock::now() - start < Milliseconds(500));
    }
}
void testShowSignals() {
    struct InterruptLeds : FakeLeds {
        bool sent = false;
        void Set(int channel, size_t bay, bool on) override {
            FakeLeds::Set(channel, bay, on);
            if (!sent) { sent = true; CHECK(kill(getpid(), SIGTERM) == 0); }
        }
    };
    for (int mode = 1; mode <= 13; ++mode) {
        Signals signals; auto leds = std::make_shared<InterruptLeds>();
        const auto before = Clock::now();
        CHECK(run_light_show(leds, mode, signals) == 0);
        CHECK(leds->sent && signals.consume());
        CHECK(Clock::now() - before < Milliseconds(500));
    }
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string fixture = argv[1];
    if (fixture == "--inventory") {
        UdevSource source; source.connect();
        for (const auto& disk : source.inventory()) std::cout << "bay=" << disk.bay << " " << disk.path << '\n';
        return 0;
    }
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"GPIO masks and shared registers", testHardware}, {"hardware rejection and cleanup", testRejectedHardware},
        {"SCH byte access", testSchWidths}, {"hotplug lifecycle and activity", testDiskLifecycle},
        {"inventory reconciliation stress", testReconciliation}, {"startup snapshot/event recovery", testStartupEvents}, {"statistics parsing and descriptor reuse", testStatistics},
        {"strict update parsing", testUpdateParsing}, {"privilege transition failures", testPrivileges},
        {"exclusive instance lock", testLock}, {"helper lifecycle", [&] { testHelpers(fixture); }},
        {"signals and timer", testSignalsAndTimer}, {"update notifications", [&] { testUpdates(fixture); }},
        {"update failure recovery and watch replacement", [&] { testUpdateRecovery(fixture); }},
        {"idle polling and signals during processing", testIdleAndProcessingSignals},
        {"termination during all 13 light shows", testShowSignals},
        {"event-loop responsiveness", [&] { testEventLoop(fixture); }}
    };
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << tests.size() << " test groups passed\n";
}
