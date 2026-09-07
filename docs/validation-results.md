# Validation results: responsive monitoring changes

Recorded on 2026-09-07. These results distinguish hardware-free execution,
read-only checks on the host, and physical checks that were not performed.

## Implemented findings

| Review issue | Implementation | Regression coverage |
| --- | --- | --- |
| 1. Invalid GPIO shifts | Unsigned, validated bank/mask construction | Boundary GPIOs, GPIO 57, model initialization, UBSan |
| 2. Privilege dropping | Checked supplementary-group, real/effective/saved identity changes and `no_new_privs`; helper revokes inherited port permissions | Operation ordering and failure injection; separate opt-in real-credential executable |
| 3. Hotplug state | Live four-bay registry, descriptor closure, both channels cleared on removal, replacement baselines | Empty-at-start insertion, active removal, replacement, stale events, read failures |
| 4. Shared GPIO updates | One event-loop owner; instance lock; preserved unrelated register bits | Fake-register state assertions and competing locks |
| 5. Lost termination signals | Blocked SIGINT/SIGTERM consumed through signalfd | Between waits, during sampling, idle, and all 13 shows |
| 6. Blocking helper/shutdown | Nonblocking child output, deadlines, output limit, process-group termination/reaping | Hung, ignoring TERM, oversized, closed-pipe, and descendant fixtures |
| 7. Hardware validation | Explicit DMI selection, LPC/SCH5127 ID/base checks, opt-in watchdog changes, owned temporary permissions | Rejected identities/bases, acquisition failures, both config addresses, byte-width assertions |
| 8. Fragile mapping/bounds/leaks | Controller-local port identity, bounded registry, RAII udev/FD ownership | Extra devices, unrelated controller, invalid ports, repeated reconciliation, real read-only inventory |
| 9. Startup event window | Subscription before snapshot; current-state event resolution; bounded draining and recovery | Snapshot/event replay, simulated receive loss, reconnection, 1,000 queued events |
| 10. Activity detection/overhead | Cumulative 64-bit counters plus outstanding I/O, retained descriptors, cached colors, independent timer | Short bursts, resets, extended/malformed statistics, descriptor reuse, disabled/empty sampling, benchmark |
| 11. Update parsing/recovery | Strict count/exit validation, backoff, retained last valid status, independent reboot watch | Missing/malformed output, nonzero exit, failure/recovery, debounce, moved/replaced watched directory |
| 12. Thread ownership | Worker thread and static lifecycle state removed; owned child state and nonthrowing cleanup | Repeated start/stop, completion/failure cleanup, child reaping, combined event loop |

## Automated results

- Warning-enabled C++17 build: passed (`-Wall -Wextra -Wpedantic`).
- Regression/process suite: 17 test groups passed.
- CLI checks: 11 cases passed, including overflow and out-of-range values exiting
  before lock or hardware access.
- ASan/UBSan with leak detection: all 17 groups passed outside the restrictive
  execution sandbox; no sanitizer findings.
- `git diff --check`: passed.
- Incremental build check (`make -q all`): passed after building.

The event-loop test dispatches 100 synthetic hotplug notifications while a real
fixture helper is hung. A normal-build run measured p95 queue-to-dispatch latency
of 0.005819 ms. Its complete scenario, including approximately 350 ms of monitoring
and termination of a helper ignoring SIGTERM, finished in 858 ms. These are
software-dispatch and process-lifecycle measurements, not physical LED latency.

## Read-only host results

DMI reports `HP` / `MediaSmart Server`. The inventory check mapped:

| Bay index | Disk | Global ATA / SCSI host | Controller-local port |
| --- | --- | --- | --- |
| 0 | sda | ata2 / host1 | 1 |
| 1 | sdb | ata3 / host2 | 2 |
| 2 | sdc | ata4 / host3 | 3 |
| 3 | sdd | ata5 / host4 | 4 |

An additional controller owns `ata1`; it was excluded. No physical LED state was
changed to obtain this inventory, so it establishes software mapping consistency,
not observed bay wiring.

The read-only statistics benchmark used four disks and 1,000 back-to-back samples:

| Measurement | Former loop reproduction | New registry/reader |
| --- | --- | --- |
| CPU time | 89.953 ms | 17.037 ms |
| Wall time | 261 ms | 60 ms |
| LED requests | 8,000 | 0 |

This run observed idle counters and used fake LEDs. CPU time was about 81% lower
for this sampling workload. The benchmark reproduces the old statistics-reading
loop; it does not run the old privileged daemon or measure actual port-I/O cost.
The loops were run sequentially and caching/other load can affect comparisons.
Do not extrapolate these values into a hard real-time guarantee.

## Explicit limitations and remaining operator checks

- The isolated real-credential check was built but could not execute because
  `sudo -n` required a password. Run the documented opt-in command to verify real
  privilege transitions and child I/O permission revocation on this installation.
- No physical port reads/writes, service installation/restart, watchdog changes,
  USB GPIO changes, or disk hot-removal tests were performed.
- Physical HP LED mapping and operation after privilege dropping remain unverified;
  Acer/Lenovo hardware was not available for validation.
- Fake-register tests verify intended transaction behavior, including translating
  the H341's legacy wide masks to byte accesses. They do not establish physical
  chipset behavior or absence of interference from other kernel/hardware owners.
- Foreground event-loop and show signal handling is covered. Detached operation
  still needs the operator's system-level validation.
- Legacy Debian/Upstart packaging and broader driver deduplication remain separate
  follow-ups, as scoped in the improvement plan.

See [testing.md](testing.md) for reproducible commands and the physical validation
procedure. The PR should be hardware-validated before installing it as the live
controller.
