# Installing and using mediasmartserverd

`mediasmartserverd` controls the four drive-bay LEDs and system/status LED on
supported HP MediaSmart and Acer/Lenovo servers. It can indicate disk presence,
show disk activity, report available Ubuntu updates, or run built-in light shows.

This guide describes the source in this repository. Examples that start the program access real hardware. Run only
one LED-controlling instance at a time.

## Hardware and system requirements

The program uses Linux x86 I/O-port access (`ioperm`, `inb`/`outb`, and related
operations) to control the LPC GPIO and SMSC SCH5127 hardware. It runs in
userspace; this repository does not require or supply a kernel patch, custom
kernel module, module blacklist, sysctl setting, special udev rule, or boot
parameter. It needs a system that permits this direct hardware access.

Start it as root, normally through `sudo` or the startup service. After hardware
initialization it clears supplementary groups and changes real, effective, and
saved user/group IDs to `nobody`, or the account selected by `--user`. It verifies
the transition and refuses to continue on failure. The runtime account must have
nonzero user and group IDs. A dedicated existing service account can be selected
with `--user=mediasmartserver`.

The main process retains only its granted runtime I/O-port permissions. Update
helpers explicitly revoke inherited port permissions, close other descriptors,
restore signal delivery, and execute with a small fixed environment and
`no_new_privs`. The root-owned instance lock is held until process exit.

Hardware initialization validates the LPC identity, the SCH5127 device ID
(`0x86`, plus the HP EX48x OEM value `0xc1` on that model only),
and register bases before configuring GPIOs for LED control. The watchdog is
preserved by default. `--disable-watchdog` explicitly requests the historical
behavior of clearing the four watchdog registers; use it only when that is the
intended watchdog policy for the server. This is hardware-specific software, not a general-purpose LED tool
for arbitrary PCs. It does not provide fan-speed or temperature-monitoring modes.

The source selects hardware using DMI vendor and product strings:

| Vendor | Product | Driver selected |
| --- | --- | --- |
| `Acer` | `Aspire easyStore H340` | H340 |
| `Acer` | `Altos easyStore M2` | Altos M2 |
| `Acer` | `Aspire easyStore H341` or `Aspire easyStore H342` | H341 |
| `LENOVO` | `IdeaCentre D400 10023` | H340-compatible |
| `HP` or `Hewlett-Packard` | `MediaSmart Server`, `HP MediaSmart Server`, `EX485`, or `EX487` | HP ex48X |

Unknown vendor/product combinations are rejected. An explicit `--model` override
accepts `hp-ex48x`, `acer-h340`, `acer-h341`, or `acer-altos-m2`; chipset and register
checks still apply. An override is for a verified compatible board with different
DMI text, not for arbitrary hardware. The original README reports testing on an HP
ex485; code paths for other models are not a guarantee of compatibility.

## Building from source

Download or clone this repository, then open a terminal in its top-level
directory (the directory containing `Makefile`). The commands below assume
you are running them from that directory.

The build needs `g++`, `make`, C++ headers/libraries, and libudev development
headers. On a Debian/Ubuntu installation with these packages available:

```bash
sudo apt install build-essential libudev-dev
make
./mediasmartserverd --help
./mediasmartserverd --version
```

`make -j2` builds incrementally and creates `./mediasmartserverd` and
`./mediasmartctl`; it does not install them or restart a running copy. Run
`sudo make install` to copy both binaries to `/usr/sbin` (or `sudo make install
PREFIX=/path` to install elsewhere); it does not restart a running copy or
install the systemd unit. The Makefile requires a C++17 compiler and links
libudev. Objects and generated header dependencies are kept in `build/`. The
daemon has no update-monitor worker thread.

Run `make test` for hardware-free regression and process-integration tests, and
`make test-sanitize` for AddressSanitizer/UndefinedBehaviorSanitizer checks. These
tests do not perform physical port I/O and do not need root. See
[testing and validation](docs/testing.md) for the read-only benchmark and the
separate, explicitly invoked physical validation procedure.

`--help` and `--version` exit before hardware initialization and do not need
root. The version string is hardcoded to `0.0.1` plus compilation date/time;
it is not a reliable identifier of the source revision.

The `.deb` files in `bin/` and Debian packaging metadata are historical. They
refer to older dependencies such as `libudev0` and `upstart-job`; use care when
treating them as installation instructions for a different distribution.

## Installing and starting with systemd

The repository includes `lib/systemd/system/mediasmartserver.service`, which runs:

```text
/usr/sbin/mediasmartserverd --activity --update-monitor
```

It stays in the foreground under systemd, starts as root and then drops privileges, and uses
`Restart=always`. Do not add `-D` to this unit. An explicit `systemctl stop`
stops it; simply killing its process can cause it to restart.

On a Linux installation using systemd, build the binary as described above.
If upgrading or replacing an existing installation, stop its service or process
first. Disable any competing startup entry (for example, an older Upstart job or
an entry in `/etc/rc.local`) so only one instance starts at boot.

Install the daemon and CLI, and the bundled service, then enable monitoring now and at boot:

```bash
sudo make install
sudo install -m 0644 lib/systemd/system/mediasmartserver.service /etc/systemd/system/mediasmartserver.service
sudo systemctl daemon-reload
sudo systemctl enable --now mediasmartserver.service
```

The bundled service enables update notifications, which require
`/usr/lib/update-notifier/apt-check` (historically supplied by
`update-notifier-common` on Ubuntu). If that helper is unavailable or you only
want disk monitoring, use the override below to omit `--update-monitor`.

Check service status and logs, or manage it with:

```bash
systemctl status mediasmartserver.service
journalctl -u mediasmartserver.service -b
sudo systemctl stop mediasmartserver.service
sudo systemctl start mediasmartserver.service
sudo systemctl restart mediasmartserver.service
```

To customize arguments, use `sudo systemctl edit mediasmartserver.service` and
provide an override such as:

```ini
[Service]
ExecStart=
ExecStart=/usr/sbin/mediasmartserverd --activity --brightness=4
```

This example omits update monitoring. Then run `sudo systemctl daemon-reload`
and `sudo systemctl restart mediasmartserver.service`. For a foreground light
show, stop the service first and start it again afterward. Avoid putting
`--xmas` in the supplied `Restart=always` unit: it exits immediately and would
be repeatedly restarted.

An older Upstart job is also included at `etc/init/mediasmartserver.conf`, with
the same activity/update options and `respawn`. It is an alternative startup
mechanism, not an additional one to enable alongside systemd or `rc.local`.

## Running without systemd

After building, test normal monitoring in the foreground:

```bash
sudo ./mediasmartserverd --activity --brightness=4 --verbose
```

Stop it with Ctrl+C. To install the daemon and CLI and launch the daemon in the
background:

```bash
sudo make install
sudo /usr/sbin/mediasmartserverd -D --activity --brightness=4
```

This manual launch does not configure startup at boot. Use your distribution's
service manager for persistent startup, with the installed executable path and
your chosen options. Use `-D` only when the startup mechanism expects the program
to detach; a service manager supervising a foreground process should omit it.

## Command-line options

```text
mediasmartserverd [OPTION]...
```

Long options with values accept either `--option=value` or `--option value`.
Only the short forms shown below are accepted: the parser's short-option list
is `aDuvV`. In particular, `-b`, `-d`, `-h`, `-S`, `-U`, and `-X` are not
supported, even though matching letters appear internally in the source.

| Option | Behavior |
| --- | --- |
| `-D`, `--daemon` | Detach and run in the background. Without this, stay in the foreground. |
| `-a`, `--activity` | Add drive activity indication to normal disk monitoring. Off by default. |
| `-u`, `--update-monitor` | Use the system LED for update/reboot notifications. Off by default. |
| `--brightness=N` | Set shared LED brightness, **0–9**: 0 off, 9 maximum. Other values are rejected. Omission leaves the current brightness unchanged. |
| `--light-show=N` | Run a continuous drive-bay animation instead of normal monitoring. See the mode table below. Default 0 means no show. |
| `--xmas` | Turn on both red and blue channels of all four drive-bay LEDs, then exit. This is a static display, distinct from animated light-show mode 1. |
| `--usb=N` | Set the hardware `OUT_USB_DEVICE` GPIO: 0 clears it, 1 sets it. Other values are rejected. Continues into the selected operating mode. |
| `-v`, `--verbose` | Print additional detection/status information. Repeat (`-vv`) for more detail. |
| `--debug` | Print low-level hardware and device/activity diagnostics. Repeatable; can produce substantial output. |
| `--help` | Print command-line help and exit before hardware access. |
| `--model=NAME` | Select a supported hardware model explicitly; chipset checks still apply. |
| `--user=NAME` | Existing unprivileged runtime account; default `nobody`. |
| `--disable-watchdog` | Explicitly clear the SCH5127 watchdog registers after validated initialization. |
| `-V`, `--version` | Print the compiled version/date/time and exit. |

Brightness is not independently adjustable per bay or color. Numeric arguments
must be unsigned decimal integers in range. Negative values, overflow, trailing
junk, and unexpected positional arguments are rejected before hardware access.
If migrating from an old `--brightness=10` command, use `--brightness=9`.

Despite its name and source comments, `--usb` does **not** call filesystem
`mount`/`umount`, accept a device path, or select a mount point. Its external
effect depends on the server wiring. It is not a general USB disk command.

## Normal monitoring

With no show/Xmas option, the program initializes the hardware, turns off both
system LED channels, applies brightness if requested, clears the bay LEDs, then
enumerates disks and watches udev events.

```bash
# Foreground disk-presence monitoring; stop with Ctrl+C.
sudo ./mediasmartserverd --verbose

# Add activity indication and an explicit brightness.
sudo ./mediasmartserverd --activity --brightness=4 --verbose

# Background monitoring, including update notifications.
sudo ./mediasmartserverd -D --activity --update-monitor --brightness=4
```

These are alternative invocations, not commands to run together. Stop an
existing instance before starting one of them.

Disk presence turns the corresponding bay's blue channel on. With `--activity`,
a monotonic timer samples sysfs statistics every 100 ms. Changes in completed
read/write/discard/flush counters, or outstanding I/O, turn on red as well as
blue; idle returns to blue. This detects short bursts completed between samples
once a baseline is established. It is still sampled activity, not a flash per
operation. Descriptors are retained while the disk is present, and unchanged
LED state causes no GPIO access. Activity sampling stops when no eligible disks
are present or the option is disabled. Red here does not mean
a disk failure; the daemon does not check SMART health or RAID status.

Initial enumeration and live events select whole ATA block devices through udev.
Bay assignment uses controller-local ATA `port_no` attributes: ports 1–4 on
`0000:00:1f.2` map to indices 0–3. External controllers, partitions, and ports
outside that mapping are excluded. Global `ataN`, `hostN`, and `/dev/sdX` numbering
do not determine bay assignment. The original HP ex485
documentation numbers bays bottom to top, 1–4; internal LED indices are 0–3.
Mapping depends on the hardware and device topology.

Reception starts before enumeration. Queued events are reconciled against current
sysfs state, and device inventory is reconciled every 30 seconds as a fallback.
Inserted disks automatically enter activity monitoring. Removal closes their
statistics descriptor and clears both colors; replacement resets the counter
baseline. Monitor errors trigger reconnection and inventory reconciliation.

## System LED update notifications

`--update-monitor` invokes `/usr/lib/update-notifier/apt-check` asynchronously at
startup and then approximately every 15 minutes. Changes to dpkg status and apt
list files trigger a debounced check, with only one helper running at a time. The historical package dependency is
`update-notifier-common`. It independently watches `/run/reboot-required`, with a 30-second reconciliation fallback.

| Condition, in priority order | System LED |
| --- | --- |
| Reboot required | Red |
| Security updates available | Red and blue together (described as purple in the source) |
| Other updates available | Blue |
| No updates or reboot required | Off |

This reports available updates; it does not install them. The helper has a
30-second execution deadline and a 4 KiB output limit. Failure is logged and
retried after 60 seconds; it is not interpreted as zero updates. The last valid
update indication is retained on failure, or stays off if no valid result has
been received. Reboot-required always takes priority and is independent of
helper success. Shutdown terminates the helper process group, allows 500 ms for
exit, then uses SIGKILL and reaps it. Disk monitoring continues during checks.

## Light shows

`--light-show=N` animates all four drive-bay positions, regardless of disk
presence. Each step lasts approximately 200 ms; there is no speed or duration
option. Stop a foreground show with Ctrl+C.

| Mode | Channels | Pattern |
| --- | --- | --- |
| 1 | Random per bay | Each step independently chooses off, blue, red, or both |
| 2 | Blue | Descending chase: indices 3, 2, 1, 0 |
| 3 | Blue | Ascending chase: indices 0, 1, 2, 3 |
| 4 | Blue | Bouncing chase: indices 0, 1, 2, 3, 2, 1 |
| 5 | Blue | All bays on, pulsing shared brightness |
| 6 | Red | Descending chase |
| 7 | Red | Ascending chase |
| 8 | Red | Bouncing chase |
| 9 | Red | All bays on, pulsing shared brightness |
| 10 | Red and blue | Descending chase |
| 11 | Red and blue | Ascending chase |
| 12 | Red and blue | Bouncing chase |
| 13 | Red and blue | All bays on, pulsing shared brightness |

On the documented HP bay layout, descending goes top to bottom and ascending
goes bottom to top. Both channels together are a hardware color combination,
not arbitrary RGB color selection.

```bash
# Run these only after stopping the regular daemon.
sudo ./mediasmartserverd --light-show=4 --brightness=4
sudo ./mediasmartserverd --light-show=1 --brightness=4
sudo ./mediasmartserverd --xmas --brightness=4
```

Mode 0 selects normal monitoring; modes 1–13 select shows. Other values are rejected.

Pulse modes vary shared brightness from 1 up to 9 and back, overriding the
initial `--brightness` setting. Shows clear their final frame on exit but do not restore the previous brightness. Restart normal monitoring with an explicit
brightness if you want to restore a known setting.

Mode precedence is fixed, independent of argument order:

1. Acquire the instance lock, detach if requested, validate/initialize hardware,
   optionally disable the watchdog, and drop privileges.
2. Apply `--usb` if supplied, switch off the system LED, and apply brightness.
3. If `--xmas` is present, light all bay channels and exit.
4. Otherwise, if `--light-show` is positive, run that show until interrupted.
5. Otherwise, start normal disk monitoring and the optional update monitor.

Consequently, `--xmas` wins over `--light-show`, and neither mode starts disk
activity or update monitoring even if `--activity`/`--update-monitor` is supplied.

## Stopping, switching modes, and restoring monitoring

There is no RPC/socket interface, runtime command client, configuration file,
PID file, or reload option. Options apply to the process being launched; a new
invocation does not send commands to the running daemon. A process holds an
exclusive lock at `/run/mediasmartserverd.lock` before initializing hardware;
a second current-version instance fails before hardware access. Older versions
do not honor this lock, so stop the old service before upgrading or testing.

If installed with systemd, stop the service before running a foreground mode,
then start it again afterward:

```bash
sudo systemctl stop mediasmartserver.service
sudo /usr/sbin/mediasmartserverd --light-show=4 --brightness=4
# After stopping the show with Ctrl+C:
sudo systemctl start mediasmartserver.service
```

For a manually launched instance, inspect the process and stop it by PID:

```bash
ps -C mediasmartserverd -o pid,user,group,args
# Replace 1234 with the PID you just inspected.
sudo kill -TERM 1234
ps -C mediasmartserverd -o pid,user,group,args
```

Wait for the process to exit before launching another instance. If it is managed
by another startup mechanism, stop it through that manager to prevent automatic
restarts. After a show, restart your normal monitoring command with an explicit
`--brightness=N` to restore a known brightness.

SIGINT/SIGTERM are consumed through the event loop, including when delivered
between waits. Shutdown clears the bay/system LEDs and stops/reaps the helper.
Shows also clear LEDs; `--xmas` intentionally leaves the static display on. The old blinking
system LED behavior is not restored. Background mode redirects standard output
and error through `daemon(0, 0)`; use foreground execution for diagnostics.

## Troubleshooting and implementation references

| Symptom | What to check |
| --- | --- |
| `ioperm` error / “Try running as root” | Start as root on supported hardware; the environment must allow direct I/O-port access. |
| Unsupported DMI or failed chipset validation | Verify the exact model and hardware; use a model override only for a known compatible board. |
| LEDs remain dark | Brightness may be zero from a previous run. Try an explicit nonzero brightness when restarting. |
| Bay colors change unexpectedly during a show | Check for another running instance or an automatically restarting service. |
| No update notification | Check the `apt-check` helper and use `-vv`; this feature is optional and distribution-specific. |
| Newly added disk has no activity indication | Inspect `-vv` inventory and `--debug` state transitions; confirm it is on one of the four supported internal ports. Statistics failures retry automatically. |
| No useful logs from a manual background launch | Stop it and reproduce in the foreground without `-D`. |
| Invalid brightness argument after upgrade | Use an integer from 0 to 9; the old maximum value 10 is now rejected. |

Primary implementation references:

- [Options, hardware selection, startup, and shows](src/mediasmartserverd.cpp)
- [Disk presence and activity monitoring](src/device_monitor.cpp)
- [Update notification rules](src/update_monitor.cpp)
- [Shared hardware initialization](src/led_control_sch5127_base.h)
- [HP LED mapping and brightness table](src/led_hpex485.h)
- [Bundled systemd unit](lib/systemd/system/mediasmartserver.service)
- [Build rules](Makefile)
