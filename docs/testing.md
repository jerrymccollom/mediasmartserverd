# Testing and validation

## Hardware-free checks

The regular suite uses fake port I/O and LEDs. It never acquires raw-I/O
permissions or writes to hardware. It tests production parsers, bay state,
hardware initialization against simulated registers, descriptor ownership,
privilege-operation failure handling, process groups, signals, timers, and the
same event loop used by the daemon.

```sh
make -j2
make test
make test-cli
make test-sanitize
```

Linux, a C++17 compiler, make, and libudev development headers are required.
ASan/UBSan builds are isolated in `build/sanitize`. LeakSanitizer needs process
inspection permissions; a restrictive execution sandbox can block it. No
ThreadSanitizer target is needed because the daemon no longer creates threads.
Tests create their own temporary directories and child processes. All helper
fixtures run with raw-I/O permission changes disabled and fake LEDs.

The helper tests include nonzero exit with valid-looking output, malformed text,
empty output, excessive output, a missing executable, a hung process, ignored
SIGTERM, closed output with a running process, and a descendant holding the pipe.
They verify descriptor closure, signal masks, `no_new_privs`, output limits, and
reaping. A separate event-loop test replays 100 events while a helper is stalled
and verifies activity sampling and shutdown continue.

`make` generates header dependencies and supports incremental/parallel builds.
Do not run `make clean` concurrently with a build or test invocation.

## Read-only inventory and benchmark

These commands read the actual machine's udev/sysfs inventory but use no physical
LED access. They need permission to create a udev netlink socket. A machine
without the supported on-board ATA topology has no benchmark candidates.

```sh
make test
./build/tests --inventory
make benchmark
```

The benchmark runs 1,000 sampling iterations over currently eligible disks.
`before` reproduces the former open/getline/tokenize loop and counts its LED
requests. `after` runs the new registry and descriptor-based reader against fake
LEDs. It does not sleep between samples, install the daemon, touch data blocks,
or generate disk workload. It measures statistics-loop overhead, not physical
LED latency or total real-time daemon CPU usage. Live disk activity and competing
processes affect the results.

For syscall counts after building:

```sh
strace -c -e openat,close,read,pread64 ./build/benchmark before
strace -c -e openat,close,read,pread64 ./build/benchmark after
```

The expected difference is approximately four opens/closes per sample in the
old loop versus one initial open per disk in the new loop. Initialization and
udev enumeration add calls to both measurements.

## Optional real credential check

The ordinary tests inject every privilege-operation failure. This separate check
executes real credential changes in an isolated process, then validates a helper's
identity, supplementary groups, signal mask, descriptor policy, and revocation of
inherited port permissions. It does not acquire port permissions or access LEDs.
It intentionally requires an explicit root invocation:

```sh
make build/privilege-check build/helper-fixture
sudo ./build/privilege-check "$PWD/build/helper-fixture"
```

The runtime account must be able to traverse the build directory and execute the
fixture. The process drops to `nobody`; this command does not change the invoking
shell's identity or modify system accounts.

## Explicit physical validation

Physical tests are not part of `make test` or CI. They require a supported server,
permission to stop its existing LED controller, an operator able to observe LEDs,
and an agreed watchdog policy. Do not hot-remove a disk containing a mounted
filesystem or active storage-pool member merely to test LEDs. Use a spare bay and
a disposable test disk prepared for safe removal.

1. Record model/DMI data, kernel version, runtime account, GPIO wiring assumptions,
   current service arguments, brightness, and watchdog policy. Record the current
   source revision and preserve the existing installed binary for rollback.
2. Stop the existing service before running the new executable. Old versions do
   not honor the new instance lock. Do not install or restart a service merely by
   invoking the regular test suite.
3. Start the new binary in the foreground with the intended options and an explicit
   brightness. Confirm model validation succeeds and process real/effective/saved
   IDs and supplementary groups match the selected unprivileged account.
4. Check each bay's blue/red channels, system colors, brightness, and the USB GPIO
   only if its effect is understood. The default must preserve watchdog state.
   Test `--disable-watchdog` only where deliberately disabling it is acceptable.
5. With a safely removable test disk, verify insertion, short read/write bursts,
   removal while activity is indicated, and replacement. Both channels must clear
   on removal. Verify bay mapping stays stable with another controller installed.
6. Verify reboot/update notifications without modifying package state solely to
   exercise LEDs; filesystem and helper failure behavior is already covered by
   isolated tests. Confirm the installed `apt-check` works as the runtime account.
7. Send SIGINT/SIGTERM during monitoring and shows, then verify clean exit and LEDs
   off. Check `--xmas` intentionally leaves the static display on. Check foreground
   and detached operation separately.
8. Measure event delivery and LED changes under a defined workload. Targets are
   next-sample activity indication at 100 ms, no sampling delay for hotplug or
   reboot events, and under two seconds for shutdown with a noncooperative helper.
   Separate kernel/udev delivery time from daemon dispatch and physical LED time.
9. Restore the chosen monitoring mode and brightness. Record results per model;
   a shared driver or passing fake-register test does not validate another board.

The shutdown target is a userspace scheduling target. A process stuck in a
kernel-uninterruptible state cannot be forced to exit within a hard deadline.
The helper process-group policy covers descendants remaining in its group; the
configured helper is a trusted fixed executable, not an arbitrary job runner.

## Hardware references

- [SCH5127 datasheet DS00002081A](https://ww1.microchip.com/downloads/en/DeviceDoc/00002081A.pdf):
  global configuration ID `0x86` (register `0x20`), runtime block alignment and
  address bounds, and byte-wide GP1–GP6 registers (offsets `0x4b`–`0x50`).
  The H341's historical masks above bit 7 are translated to the corresponding
  adjacent byte and bit, preserving their physical target without wide writes.
- [Linux ATA transport source](https://github.com/torvalds/linux/blob/master/drivers/ata/libata-transport.c):
  sysfs `port_no` is the controller-local port index plus one.
- [Linux block statistics](https://docs.kernel.org/block/stat.html): cumulative
  completion fields and instantaneous in-flight I/O.
- [ioperm(2)](https://man7.org/linux/man-pages/man2/ioperm.2.html): permissions
  survive fork and exec on supported kernels, so the helper explicitly revokes
  them before exec. Disabling permissions does not require enabling privileges.

See [validation-results.md](validation-results.md) for this change's recorded
results and the hardware checks still requiring an operator.
