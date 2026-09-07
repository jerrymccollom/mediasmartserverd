# mediasmartserverd improvement plan

## Objective and scope

Address all 12 findings from the code review, with correctness and hardware
safety first, followed by responsiveness and efficiency. Preserve the supported
server models and documented operating modes. This document plans the changes;
it does not authorize running hardware tests on an arbitrary machine.

The intended design has one event loop owning device state and all hardware
access. It receives udev events, termination signals, timer expirations, filesystem
notifications, and update-helper output. A small `poll()`-based implementation is
sufficient. Update checks must never block disk monitoring or shutdown.

Hardware mappings, register widths, privilege behavior, and physical response
times require validation on supported servers. Do not infer their correctness
from a successful build or mocked tests.

## Implementation order

| Phase | Work | Exit condition |
| --- | --- | --- |
| 1 | Add test seams and build support; fix GPIO masks (#1) and validate hardware detection (#7). | Hardware-free tests prove correct masks and rejection of unsupported identities before configuration writes. |
| 2 | Correct device identity, bounds, ownership (#8), startup reconciliation (#9), and hotplug lifecycle (#3). | Enumeration and event replay converge on the same four-bay state. |
| 3 | Introduce the event loop and reliable termination (#5); serialize hardware access (#4); replace update-thread ownership (#12) and bound helper execution (#6). | No worker writes GPIO; shutdown remains bounded with a hung helper. |
| 4 | Fix update parsing and failure handling (#11), then complete and verify privilege dropping (#2). | Helpers run under the intended identity, failures remain distinguishable from zero updates, and monitoring works after privilege dropping. |
| 5 | Improve activity sampling (#10), add prompt reboot notifications, and measure performance. | Functional, sanitizer, responsiveness, and target-hardware acceptance checks pass. |

Keep changes reviewable in separate commits. Add regression tests alongside each
fix. If intermediate releases retain the update thread, lock complete hardware
transactions and repair join ownership before shipping; remove those temporary
mechanisms when helper execution moves into the event loop.

## Test foundation

- Add a hardware I/O interface with a production port-I/O implementation and a
  fake implementation recording reads, writes, widths, ports, and values. Tests
  must not execute `ioperm`, `inb`, `inl`, `outb`, or `outl` on real hardware.
- Separate normalized device events and bay mapping from libudev acquisition.
  Feed tests synthetic device identities, ancestry, enumeration snapshots, and
  add/remove/change events.
- Introduce narrow seams for the monotonic clock, statistics reads, filesystem
  state, privilege operations, and helper process execution. Prefer testing
  observable behavior over private method structure.
- Provide helper fixtures that succeed, fail, emit malformed or excessive
  output, close output early, block forever, and ignore graceful termination.
- Add `make test` and sanitizer builds. Use AddressSanitizer and
  UndefinedBehaviorSanitizer for hardware-free tests; use ThreadSanitizer while
  any shared-state worker remains.
- Make builds incremental and parallel-safe: remove `clean` from `all`, add
  generated header dependencies and `.PHONY` targets, declare a C++ standard,
  and apply thread flags consistently while threads remain. Enable useful
  warnings and fix warnings in changed code.
- Run tests without root by default. Put privileged and physical-hardware tests
  behind explicit opt-in commands, separate from ordinary CI.

## 1. Fix undefined GPIO shifts

**Source:** `src/led_control_sch5127_base.h`, especially `setBit32_()`.

**Changes**

- Use `uint32_t` masks and `uint32_t{1} << (bit % 32)` after checking that the
  GPIO number is valid for the controller.
- Use an explicit bank selection and reject invalid inputs before any I/O.
- Review the related mask construction paths for signed shifts and narrowing.

**Tests and acceptance**

- Verify bank/mask pairs for GPIOs 0, 31, 32, 38, 39, and 57, plus controller
  boundary and invalid values.
- Verify complete masks for each supported model and preservation of unrelated
  register bits during configuration.
- Run these tests with UBSan; the existing GPIO 57 failure must be eliminated.

## 2. Implement and verify privilege dropping

**Source:** `src/mediasmartserverd.cpp`, `drop_priviledges()` and startup order.

**Changes**

- Specify the runtime identity and the minimal port permissions required after
  initialization. Prefer a dedicated service account when packaging supports it.
- Complete privileged initialization, release temporary port permissions, clear
  supplementary groups, set the group identity, and set the user identity using
  separately checked operations.
- Treat missing account information or any failed transition as a startup
  failure. Verify the resulting identity instead of silently continuing.
- Ensure the update helper does not inherit unnecessary descriptors or raw-I/O
  permissions. Document and verify the applicable exec/permission behavior.
- Verify real hardware operations after the transition; do not assume that a
  source-level fix establishes a working privilege model.

**Tests and acceptance**

- Inject successful and failing account/group/user operations. Assert ordering,
  that every required operation executes, and that failure prevents monitoring
  or helper startup.
- Run an opt-in isolated integration test checking real/effective identities and
  supplementary groups in the daemon and helper.
- On supported hardware, verify LED operations after privileges are dropped.

## 3. Maintain live disk and LED state across hotplug

**Source:** `src/device_monitor.cpp`, add/remove handling and startup-only arrays.

**Changes**

- Maintain initialized bay records containing device identity, statistics
  resource, previous counters, presence, and requested LED state.
- On add, create or replace the matching device entry and open its statistics
  resource once available. Retry transient unavailability without blocking.
- On remove, use cached identity to close the resource, discard counters, and
  turn off both red and blue channels.
- Handle repeated events idempotently and prevent a delayed removal for an old
  device from clearing a replacement in the same bay.
- Use the same state model for normal shutdown cleanup.

**Tests and acceptance**

- Insert into a bay empty at startup and verify activity monitoring begins.
- Remove an active disk and verify both channels clear and the resource closes.
- Replace a disk, including reuse of a device name, and verify counters reset.
- Replay duplicate and stale events, and temporary statistics-open failures.
- No stale statistics resources or illuminated removed bays remain.

## 4. Serialize hardware access

**Source:** `src/led_control_sch5127_base.h`, `doBits_()`, and update-monitor LED calls.

**Changes**

- Give the event-loop thread exclusive ownership of hardware transactions.
  Helper results update logical status; they do not directly write LEDs.
- Apply complete desired-state changes while preserving unrelated register bits.
- Acquire an exclusive process lock before hardware initialization to prevent
  two daemon/show instances from manipulating the same hardware.
- If a transitional implementation uses a mutex, cover entire read–modify–write
  and indexed-register transactions, not individual I/O instructions.

**Tests and acceptance**

- Replay overlapping system-status and disk-activity changes sharing a GPIO
  register; assert that both final states survive and unrelated bits remain.
- Instrument the fake backend to reject access from a non-owner thread.
- Verify a second instance fails before hardware access, and the lock is
  released when the owning process exits.

## 5. Make termination reliable

**Source:** `src/mediasmartserverd.cpp`, signal setup and light shows;
`src/device_monitor.cpp`, wait loop.

**Changes**

- Block SIGINT/SIGTERM before creating workers or other asynchronous activity.
- Consume termination through `signalfd` in the event loop. Preserve pending
  termination during startup and check it before continuing into monitoring.
- Use the same termination mechanism for light shows and normal monitoring.
- Make cleanup explicit and idempotent, with a defined final LED state.
- Restore appropriate signal masks in executed helper processes and account for
  daemonization when creating event-loop descriptors and holding the instance lock.

**Tests and acceptance**

- Send SIGINT/SIGTERM while waiting, processing events, reading statistics,
  initializing, running shows, and waiting for helper output.
- Assert shutdown occurs without requiring a second signal.
- Verify repeated termination requests do not duplicate cleanup or lose process
  ownership. Check both foreground and detached startup paths.

## 6. Bound update-helper execution and shutdown

**Source:** `src/update_monitor.cpp`, blocking `popen`, `getline`, and `pclose` calls.

**Changes**

- Replace blocking shell-based checks with an explicitly owned child process,
  nonblocking output pipes, and event-loop integration.
- Continuously drain output, impose a size limit and monotonic execution
  deadline, and collect the child's exit status. Handle EOF and process exit as
  separate events.
- On timeout or shutdown, request termination, escalate after a bounded grace
  interval, and reap the child. Define descendant/process-group handling.
- Keep sampling and hotplug handling active throughout helper execution.

**Tests and acceptance**

- Exercise helpers that hang, ignore termination, fill a pipe, produce no output,
  exit before being read, and spawn a descendant retaining the output pipe.
- Confirm all owned processes/descriptors are cleaned up and no zombies remain.
- Confirm disk events and sampling continue while the helper is stalled.
- Initial target: shutdown completes within two seconds on the integration-test
  host, including forced termination of a noncooperative helper. Treat this as a
  scheduling-dependent target, not a hard real-time guarantee.

## 7. Validate hardware before configuration writes

**Source:** `src/mediasmartserverd.cpp`, model selection;
`src/led_control_sch5127_base.h`, Super I/O discovery and watchdog initialization.

**Changes**

- Define supported DMI, LPC, and Super I/O identities and validate runtime bases
  before GPIO/watchdog configuration writes.
- Reject unknown models by default. If an override is retained, require explicit
  model selection and continue validating the selected chipset requirements.
- Check missing DMI devices/attributes and use owned strings for their values.
- Separate discovery/configuration-mode transactions from configuration changes;
  always exit configuration mode and release temporary permissions on failure.
- Establish whether watchdog disabling is required for each model. Document and
  limit any required change rather than disabling it unconditionally.
- Verify SCH register widths and H341 mappings against reliable hardware
  information. Do not blindly change the existing wide accesses to byte accesses.

**Tests and acceptance**

- Test every supported identity and mismatched/missing DMI, LPC, Super I/O, and
  invalid-base cases.
- Assert rejected probes perform no GPIO/watchdog configuration writes, while
  any necessary discovery transactions are paired with cleanup.
- Inject failure at each acquisition stage and verify permission/configuration
  cleanup. Validate register behavior separately on supported hardware.

## 8. Replace fragile bay mapping and unchecked arrays

**Source:** `src/device_monitor.cpp`, `scsiHostIndex_()`;
`src/device_monitor.h`, fixed arrays and accessors.

**Changes**

- Derive controller and port identity from udev ancestry and explicit model
  mappings, avoiding character offsets and assumptions about host numbering.
- Distinguish internal supported bays from external or unrelated devices.
- Use four bounded, initialized bay records with a separate device lookup.
  Reject invalid/conflicting mappings before indexing or accessing hardware.
- Wrap owned udev references and descriptors in RAII; respect borrowed-parent
  ownership. Release all host references acquired during discovery.

**Tests and acceptance**

- Cover single- and multi-digit ATA/host identifiers, host-number gaps, missing
  ancestors, unrelated USB/SCSI devices, and malformed identities.
- Replay more than ten candidate devices and verify safe rejection without
  out-of-bounds access.
- Verify enumeration order does not change physical bay assignment and duplicate
  mappings produce a diagnostic rather than silently replacing another device.
- Repeat discovery/hotplug cycles under sanitizers and check resource counts.

## 9. Reconcile startup enumeration with live events

**Source:** `src/device_monitor.cpp`, `Init()`, enumeration and event filters.

**Changes**

- Configure and enable udev reception before enumeration.
- Use one normalization/filtering path for whole-disk block devices during both
  startup and live monitoring, then apply the internal-bay mapping.
- Reconcile the snapshot with queued events through idempotent state updates.
- Drain queued events in bounded batches so bursts do not starve timers or
  termination. On detected event loss or monitor failure, resynchronize state
  through enumeration and recover monitoring where possible.
- Check udev allocation, filter, enumeration, and receive results explicitly.

**Tests and acceptance**

- Inject add/remove/replacement events at each boundary around enumeration.
- Verify the converged state matches the final device inventory, irrespective of
  duplicate events or snapshot ordering.
- Verify partitions and unsupported external disks do not become bay entries.
- Inject monitor errors and event-loss indications; verify reconciliation and
  continued bounded service of termination and sampling.

## 10. Detect short I/O bursts with less overhead

**Source:** `src/device_monitor.cpp`, statistics parsing and activity polling.

**Changes**

- Parse checked, whitespace-separated 64-bit statistics into fixed fields;
  support required base fields and tolerate documented additional fields.
- Indicate activity when supported completion counters change or I/O remains
  outstanding. Define read/write/discard/flush treatment and counter reset logic.
- Start with a 100 ms monotonic sampling timer independent of udev traffic.
  Coalesce missed expirations rather than replaying stale samples in a burst.
- Reuse statistics descriptors with offset-zero reads where supported, handling
  removal/read failure through the live registry.
- Cache requested LED state so unchanged samples require no GPIO accesses.
- Suspend activity sampling when disabled or when no eligible disks exist.

**Tests and acceptance**

- Verify a burst completed entirely between samples is visible on the next sample.
- Cover outstanding I/O, idle transitions, large counters, reset/replacement,
  additional fields, mixed whitespace, truncated input, and failed reads.
- Assert unchanged samples produce no GPIO reads or writes; hotplug bursts do not
  trigger extra full-disk sampling passes.
- Measure allocations, descriptor operations, CPU time, wakeups, and event-to-LED
  latency before and after on the same workload. Reduce overhead without losing
  activity transitions; only shorten the timer after these measurements.

## 11. Parse update results strictly and recover from failures

**Source:** `src/update_monitor.cpp`, `GetUpdateStatus()` and notification policy.

**Changes**

- Require successful helper completion and exactly the expected pair of
  nonnegative integer counts, allowing defined surrounding whitespace only.
- Compare separator results directly with `std::string::npos`, validate numeric
  ranges and trailing data, and distinguish buffer capacity from received length.
- Use owned buffers and bounded output. Keep helper failure/unknown status
  distinct from a valid zero-update result, with diagnostics and bounded retry.
- Check reboot-required independently of helper success. Watch its parent
  directory for creation/removal/replacement and reconcile on watch loss/overflow.
- Define the failure display policy explicitly; preserve last-known update state
  with a diagnostic initially, while reboot-required retains priority.
- Debounce relevant package-state changes before checking updates, with a
  periodic fallback and at most one active helper.

**Tests and acceptance**

- Cover `0;0`, ordinary/security updates, whitespace, absent/extra separators,
  negative/overflowing values, trailing junk, empty and oversized output.
- Reproduce `apt-check: not found` with a failing exit status and assert failure,
  never a successful zero-update result.
- Check failure/retry/recovery, repeated checks without resource growth, and
  system-LED priority across reboot/security/ordinary/none states.
- Verify reboot-file changes are reflected while the helper is missing or hung;
  package-event bursts coalesce into bounded helper invocations.

## 12. Eliminate unsafe update-thread ownership

**Source:** `src/update_monitor.h` and `src/update_monitor.cpp`, static state,
thread creation, cancellation, cleanup, and destruction.

**Changes**

- Replace static LED/thread state with instance-owned update status and the
  asynchronous helper lifecycle described in #6. Remove the update-monitor thread
  once all blocking work has moved out of the event loop.
- Make stop/cleanup idempotent and destructors nonthrowing. Report failures at an
  explicit control boundary without allowing exceptions to escape callbacks.
- If the thread survives an intermediate commit, independently track successful
  creation and join responsibility, synchronize shared state, always join created
  threads, and use returned pthread error codes rather than `errno`.

**Tests and acceptance**

- Cover immediate helper/worker failure, stopping before start, repeated stop,
  stop during completion, and destruction after partial initialization.
- Verify completion does not discard cleanup/reaping responsibility.
- Verify multiple test instances do not share LED or lifecycle state.
- Run ThreadSanitizer on any transitional threaded version. In the final design,
  confirm the update subsystem creates no worker thread and owns/reaps each child.

## Final validation and completion criteria

1. Build normally and with warnings/sanitizers; run all hardware-free regression
   tests, process integration tests, and parallel/incremental build checks.
2. Run deterministic event-replay stress tests combining hotplug, statistics
   changes, helper failures, and termination. Verify state convergence, no resource
   growth, no out-of-bounds access, and no lost LED changes.
3. On each available supported model, explicitly run hardware validation covering
   identity checks, bay/color mapping, brightness, watchdog policy, privilege
   transition, hotplug, activity, and shutdown. Record untested models as such.
4. Measure idle and busy operation on the target server. Initial responsiveness
   targets: activity on the next 100 ms sample; accepted hotplug and reboot events
   applied without waiting for that sample; p95 dispatch-to-LED below 50 ms under
   the defined test load; shutdown within two seconds with a hung helper. Record
   udev delivery latency separately from daemon dispatch latency.
5. Verify no GPIO operations occur for unchanged requested LED state and no
   activity timer runs when activity is disabled or there are no eligible disks.
6. Update operational documentation to match actual hotplug behavior, error
   reporting, account requirements, supported hardware, and shutdown guarantees.
   Include commands to run tests and identify opt-in hardware checks.

Completion requires a regression test or documented hardware validation for every
numbered finding, passing applicable checks, and recorded performance results.
Do not claim a model is hardware-validated solely because it shares driver code.

## Adjacent work kept separate from the 12 findings

The review also identified inconsistent systemd/Upstart maintainer scripts,
historical package dependencies, CLI validation/help discrepancies, and duplicated
driver implementations. Track these as follow-up changes. Build/test support,
single-instance locking, and DMI ownership are included above because they directly
support the fixes. Avoid folding a broad packaging or driver rewrite into the
correctness patches without separate validation.

## Execution record (2026-09-07)

The implementation now addresses findings 1–12 in the source. The update thread
has been removed, the main event loop owns hardware access, and the regression
suite covers the new state and lifecycle behavior. Build/test targets, a
read-only before/after benchmark, an opt-in real-credential check, and CI have
been added. Operating documentation reflects strict numeric validation, explicit
model selection, privilege dropping, and opt-in watchdog disabling.

See [docs/validation-results.md](docs/validation-results.md) for the finding-to-test
mapping and measured results, and [docs/testing.md](docs/testing.md) for commands.
Physical validation and the real-credential check are explicitly pending; the
latter could not run because sudo required a password. This execution record
must not be interpreted as successful validation of untested hardware models or
completion of the operator-only acceptance steps.
