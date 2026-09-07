# mediasmartserverd usage guide

`mediasmartserverd` controls the four drive-bay LEDs and system/status LED on
supported HP MediaSmart and Acer/Lenovo servers. It can indicate disk presence,
show disk activity, report available Ubuntu updates, or run built-in light shows.

This guide describes the source in this repository, including options omitted
from `--help`. Examples that start the program access real hardware. Run only
one LED-controlling instance at a time.

## How this machine is running it

Read-only inspection on September 7, 2026 found:

| Item | Observed value |
| --- | --- |
| Hardware DMI vendor/product | `HP` / `MediaSmart Server` |
| Running command | `/opt/mediasmartserverd -D` |
| Process user/group | `root` / `nogroup` |
| Startup configuration | `/etc/rc.local`, line 7 |
| Startup line | `test -x /opt/mediasmartserverd && /opt/mediasmartserverd -D` |
| `mediasmartserver.service` | Not installed |

Those arguments select background disk-presence monitoring, without activity
or update monitoring and without an explicit brightness setting. The installed
`/opt` binary was not compared with the repository build, so the option reference
below is specifically for this source version.

## Hardware and system requirements

The program uses Linux x86 I/O-port access (`ioperm`, `inb`/`outb`, and related
operations) to control the LPC GPIO and SMSC SCH5127 hardware. It runs in
userspace; this repository does not require or supply a kernel patch, custom
kernel module, module blacklist, sysctl setting, special udev rule, or boot
parameter. It needs a system that permits this direct hardware access.

Start it as root, normally through `sudo` or the startup service. Although the
source contains a function intended to drop privileges to `nobody`, its
`setgid(...) && setuid(...)` expression skips `setuid` when `setgid` succeeds.
Do not assume the daemon becomes unprivileged; the observed process remains root.

Hardware initialization disables the SCH5127 watchdog and configures GPIOs for
LED control. This is hardware-specific software, not a general-purpose LED tool
for arbitrary PCs. It does not provide fan-speed or temperature-monitoring modes.

The source selects hardware using DMI vendor and product strings:

| Vendor | Product | Driver selected |
| --- | --- | --- |
| `Acer` | `Aspire easyStore H340` | H340 |
| `Acer` | `Altos easyStore M2` | Altos M2 |
| `Acer` | `Aspire easyStore H341` or `Aspire easyStore H342` | H341 |
| `LENOVO` | `IdeaCentre D400 10023` | H340-compatible |
| Other vendors, including `HP` | Any | Attempts HP ex48X hardware detection |

An unrecognized Acer or Lenovo product does not take the HP fallback. Actual
chipset checks must also succeed. The original README reports testing on an HP
ex485; code paths for other models are not a guarantee of compatibility.

## Building

The build needs `g++`, `make`, C++ headers/libraries, and libudev development
headers. On a Debian/Ubuntu installation with these packages available:

```bash
sudo apt install build-essential libudev-dev
make
./mediasmartserverd --help
./mediasmartserverd --version
```

`make` cleans previous build outputs and creates `./mediasmartserverd`; it does
not install it or restart a running copy. There is no `make install` target.
The Makefile links libudev, libdl, and pthreads. The source uses C++11 features,
so an older compiler may need an explicit C++11-or-newer language setting.

`--help` and `--version` exit before hardware initialization and do not need
root. The version string is hardcoded to `0.0.1` plus compilation date/time;
it is not a reliable identifier of the source revision.

The `.deb` files in `bin/` and Debian packaging metadata are historical. They
refer to older dependencies such as `libudev0` and `upstart-job`; use care when
treating them as installation instructions for a different distribution.

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
| `--brightness=N` | Set shared LED brightness. Effective levels are **0–9**: 0 off, 9 maximum. Values above 9 clamp to 9; negative values skip the brightness change. Omission leaves the current brightness unchanged. |
| `--light-show=N` | Run a continuous drive-bay animation instead of normal monitoring. See the mode table below. Default 0 means no show. |
| `--xmas` | Turn on both red and blue channels of all four drive-bay LEDs, then exit. This is a static display, distinct from animated light-show mode 1. |
| `--usb=N` | Set the hardware `OUT_USB_DEVICE` GPIO: 0 clears it, positive values set it, negative values skip the operation. Continues into the selected operating mode. |
| `-v`, `--verbose` | Print additional detection/status information. Repeat (`-vv`) for more detail. |
| `--debug` | Print low-level hardware and device/activity diagnostics. Repeatable; can produce substantial output. |
| `--help` | Print abbreviated help and exit. Omits light shows, Xmas, and USB options. |
| `-V`, `--version` | Print the compiled version/date/time and exit. |

The README/help describe brightness as 0–10 or 1–10, but all four driver
implementations have a ten-entry table indexed **0–9**. Levels 9 and 10 therefore
produce the same maximum setting. Brightness is not independently adjustable
per bay or color.

Numeric arguments use `atoi`, with no strict validation. Use the documented
integer values; malformed text can become zero rather than produce an error.

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
the monitor checks sysfs disk statistics roughly every 100 ms: outstanding I/O
turns on red as well as blue, while idle returns to blue. This is sampled
activity, not a flash for every individual operation. Red here does not mean
a disk failure; the daemon does not check SMART health or RAID status.

Initial enumeration selects ATA disks through udev. Bay assignment is derived
from ATA/SCSI host paths rather than `/dev/sda` ordering. The original HP ex485
documentation numbers bays bottom to top, 1–4; internal LED indices are 0–3.
Mapping depends on the hardware and device topology.

Hotplug events update presence, but the activity-statistics list is built at
startup. A newly introduced disk may need a daemon restart for activity
monitoring to include it.

## System LED update notifications

`--update-monitor` invokes `/usr/lib/update-notifier/apt-check` immediately and
then approximately every 15 minutes. The historical package dependency is
`update-notifier-common`. It also checks `/var/run/reboot-required`.

| Condition, in priority order | System LED |
| --- | --- |
| Reboot required | Red |
| Security updates available | Red and blue together (described as purple in the source) |
| Other updates available | Blue |
| No updates or reboot required | Off |

This reports available updates; it does not install them. If the helper is
unavailable or its output cannot be parsed, the update-monitor thread stops and
clears the system LED. Normal disk monitoring can continue. Use `-vv` to see
update-monitor diagnostics. The reboot indication also depends on successful
execution/parsing of the update check.

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

Modes 0 and negative numbers fall through to normal monitoring. Values above
13 are not rejected: the arithmetic repeats patterns and falls back to blue.
Use 1–13 for predictable selection.

Pulse modes vary shared brightness from 1 up to 9 and back, overriding the
initial `--brightness` setting. Shows do not restore the previous brightness
or clear their final frame on exit. Restart normal monitoring with an explicit
brightness if you want to restore a known setting.

Mode precedence is fixed, independent of argument order:

1. Initialize hardware and apply `--usb` if supplied.
2. Detach if requested, switch off the system LED, and apply brightness.
3. If `--xmas` is present, light all bay channels and exit.
4. Otherwise, if `--light-show` is positive, run that show until interrupted.
5. Otherwise, start normal disk monitoring and the optional update monitor.

Consequently, `--xmas` wins over `--light-show`, and neither mode starts disk
activity or update monitoring even if `--activity`/`--update-monitor` is supplied.

## Stopping, switching modes, and restoring monitoring

There is no RPC/socket interface, runtime command client, configuration file,
PID file, or reload option. Options apply to the process being launched; a new
invocation does not send commands to the running daemon. Separate instances
can overwrite each other's LED state and interfere with register updates.

For this machine's current `/opt` and `rc.local` setup:

```bash
# Inspect the running PID and command first.
ps -C mediasmartserverd -o pid,user,group,args

# Replace 1234 with the PID you just inspected.
sudo kill -TERM 1234

# Confirm it has exited before launching a foreground show.
ps -C mediasmartserverd -o pid,user,group,args
sudo ./mediasmartserverd --light-show=4 --brightness=4

# After Ctrl+C, restore the original background monitoring mode.
sudo /opt/mediasmartserverd -D
```

The final command restores the original arguments, not the previous brightness.
Add `--brightness=N` to choose a known level, using an installed binary that
supports this option. Editing `rc.local` affects future starts, not the running
process.

In normal monitoring, SIGINT/SIGTERM interrupts the wait and the shutdown path
clears the tracked bay LEDs and stops the update monitor. The old blinking
system LED behavior is not restored. Background mode redirects standard output
and error through `daemon(0, 0)`; use foreground execution for diagnostics.

## Using the bundled systemd service

The repository includes `lib/systemd/system/mediasmartserver.service`, which runs:

```text
/usr/sbin/mediasmartserverd --activity --update-monitor
```

It stays in the foreground under systemd, starts as root, and uses
`Restart=always`. Do not add `-D` to this unit. An explicit `systemctl stop`
stops it; simply killing its process can cause it to restart.

For a fresh manual installation, after building and removing/disabling any
competing startup entry and stopping its running instance:

```bash
sudo install -m 0755 mediasmartserverd /usr/sbin/mediasmartserverd
sudo install -m 0644 lib/systemd/system/mediasmartserver.service /etc/systemd/system/mediasmartserver.service
sudo systemctl daemon-reload
sudo systemctl enable --now mediasmartserver.service
```

On this machine, migrating would specifically require removing/commenting the
`/etc/rc.local` startup line as well as stopping the existing `/opt` process.
The commands above are instructions; no migration was performed for this guide.

Once installed:

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

## Troubleshooting and implementation references

| Symptom | What to check |
| --- | --- |
| `ioperm` error / “Try running as root” | Start as root on supported hardware; the environment must allow direct I/O-port access. |
| “Failed to find an LED control interface” | Use `--verbose --debug` in the foreground after stopping the existing instance; inspect DMI selection and chipset detection. |
| LEDs remain dark | Brightness may be zero from a previous run. Try an explicit nonzero brightness when restarting. |
| Bay colors change unexpectedly during a show | Check for another running instance or an automatically restarting service. |
| No update notification | Check the `apt-check` helper and use `-vv`; this feature is optional and distribution-specific. |
| Newly added disk has no activity indication | Restart monitoring to rebuild the startup statistics list. |
| No useful logs from a manual background launch | Stop it and reproduce in the foreground without `-D`. |
| `--help` omits an option or disagrees about brightness | Use this guide and the parser/driver implementations below. |

Primary implementation references:

- [Options, hardware selection, startup, and shows](src/mediasmartserverd.cpp)
- [Disk presence and activity monitoring](src/device_monitor.cpp)
- [Update notification rules](src/update_monitor.cpp)
- [Shared hardware initialization](src/led_control_sch5127_base.h)
- [HP LED mapping and brightness table](src/led_hpex485.h)
- [Bundled systemd unit](lib/systemd/system/mediasmartserver.service)
- [Build rules](Makefile)
