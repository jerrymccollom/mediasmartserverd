/////////////////////////////////////////////////////////////////////////////
/// @file mediasmartserverd.cpp
///
/// Daemon for controlling the LEDs on the HP MediaSmart Server EX48X
///
/// -------------------------------------------------------------------------
///
/// Copyright (c) 2009-2010 Chris Byrne
///
/// This software is provided 'as-is', without any express or implied
/// warranty. In no event will the authors be held liable for any damages
/// arising from the use of this software.
///
/// Permission is granted to anyone to use this software for any purpose,
/// including commercial applications, and to alter it and redistribute it
/// freely, subject to the following restrictions:
///
/// 1. The origin of this software must not be misrepresented; you must not
/// claim that you wrote the original software. If you use this software
/// in a product, an acknowledgment in the product documentation would be
/// appreciated but is not required.
///
/// 2. Altered source versions must be plainly marked as such, and must not
/// be misrepresented as being the original software.
///
/// 3. This notice may not be removed or altered from any source
/// distribution.
///
/////////////////////////////////////////////////////////////////////////////

/// Changelog
///
/// 2012-02-04 - Kai Hendrik Behrends
///  - Added system vendor and product name detection to get_led_interface().
///    Neccessary for adding H341 without breaking HPEX485.
///  - Disabled SystemLed.
///
/// 2012-02-07 - Kai Hendrik Behrends
///  - Added update monitor
///
/// 2014-08-16 - Kai Hendrik Behrends
///  - Added support for the Lenovo IdeaCenter D400 as suggested by Gordon seen here:
///    http://kaibehrends.org/ubuntu-enable-acer-aspire-easystore-h341-leds/#comment-1510585114

//- includes
#include "errno_exception.h"
#include "device_monitor.h"
#include "hardware.h"
#include "event_loop.h"
#include "light_show.h"
#include "runtime.h"
#include <poll.h>
#include "update_monitor.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <string>
#include <stdio.h>

#include <getopt.h>
#include <libudev.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

using std::cout;

//- globals
int debug = 0;		///< show debug messages
int verbose = 0;	///< how much debugging we spew out
bool activity = 0;	///< do we make the lights blink?



// Altered version, 2026: event-loop ownership, strict options and privilege dropping.

/////////////////////////////////////////////////////////////////////////////
/// show command line help
int show_help( ) {
	cout << "Usage: mediasmartserverd [OPTION]...\n"
		<< "     --brightness=X    Set LED brightness (0 to 9)\n"
		<< " -D, --daemon          Detach and run in the background\n"
		<< " -a, --activity        Use the bay lights as disk activity lights\n"
		<< "     --debug           Print debug messages\n"
		<< "     --help            Print help text\n"
		<< " -u  --update-monitor  Use system LED as update notification light\n"
		<< " -v, --verbose         verbose (use twice to be more verbose)\n"
		<< "     --model=NAME      Explicit supported model override\n"
        << "     --user=NAME       Runtime account (default nobody)\n"
        << "     --disable-watchdog Explicitly disable the SCH5127 watchdog\n"
        << "     --light-show=N    Animation mode (1 to 13)\n"
        << "     --xmas            Set all bay channels and exit\n"
        << "     --usb=N           Set USB GPIO (0 or 1)\n"
        << " -V, --version         Show version number\n"
	;

	return 0;
}

/////////////////////////////////////////////////////////////////////////////
/// show version
int show_version( ) {
	cout << "mediasmartserverd 0.0.1 compiled on " __DATE__ " " __TIME__ "\n";
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
/// run a light show


/////////////////////////////////////////////////////////////////////////////
/// main entry point
int main( int argc, char* argv[] ) try {
	int brightness = -1;
	int light_show = 0;
	int mount_usb = -1;
	bool run_as_daemon = false;
	bool xmas = false;
	bool run_update_monitor = false;
    bool disable_watchdog = false;
    std::string model_override, runtime_user = "nobody";

	// long command line arguments
	const struct option long_opts[] = {
		{ "brightness",     required_argument, 0, 'b' },
		{ "daemon",         no_argument,       0, 'D' },
		{ "activity",       no_argument,       0, 'a' },
		{ "debug",          no_argument,       0, 'd' },
		{ "help",           no_argument,       0, 'h' },
		{ "light-show",     required_argument, 0, 'S' },
		{ "update-monitor", no_argument,       0, 'u' },
		{ "usb",            required_argument, 0, 'U' },
		{ "verbose",        no_argument,       0, 'v' },
		{ "version",        no_argument,       0, 'V' },
		{ "model", required_argument, 0, 1000 },
        { "user", required_argument, 0, 1001 },
        { "disable-watchdog", no_argument, 0, 1002 },
        { "xmas",           no_argument,       0, 'X' },
		{ 0, 0, 0, 0 },
	};

    auto number = [](const char* value, uint64_t maximum) {
        uint64_t parsed;
        if (!value || !parseUnsigned(value, parsed) || parsed > maximum)
            throw std::invalid_argument("Invalid numeric argument");
        return static_cast<int>(parsed);
    };
	// pass command line arguments
	while ( true ) {
		const int c = getopt_long( argc, argv, "aDuvV", long_opts, 0 );
		if ( -1 == c ) break;

		switch ( c ) {
        case 1000: model_override = optarg; break;
        case 1001: runtime_user = optarg; break;
        case 1002: disable_watchdog = true; break;
		case 'b': // brightness
			brightness = number(optarg, 9);
			break;
		case 'd': // debug
			++debug;
			break;
		case 'D': // run as a daemon (background)
			run_as_daemon = true;
			break;
		case 'a': // run as a daemon (background)
			activity = true;
			break;
		case 'h': // help!
			return show_help( );
		case 'S': // light-show
			light_show = number(optarg, 13);
			break;
		case 'u': //Use system LED as update notification light.
			run_update_monitor = true;
			break;
		case 'U': // mount/unmount USB device
			mount_usb = number(optarg, 1);
			break;
		case 'v': // verbose, more verbose, even more verbose
			++verbose;
			break;
		case 'V': // our version
			return show_version( );
		case 'X': // light all the LEDs up like a xmas tree
			xmas = true;
			break;
		case '?': // no idea
			cout << "Try `" << argv[0] << " --help' for more information.\n";
			return 1;
		default:
			cout << "+++ '" << (char)c << "'\n";
		}
	}


    if (optind != argc) throw std::invalid_argument("Unexpected positional argument");
    if (!model_override.empty()) detectModel("", "", model_override);
    if (runtime_user.empty()) throw std::invalid_argument("Empty runtime account");
    Signals signals; // Block termination before initialization or daemonization.
    InstanceLock lock("/run/mediasmartserverd.lock");
    // Detach before constructing thread-affine hardware access or changing identity.
    if (run_as_daemon && daemon(0, 0)) throw ErrnoException("daemon");
    if (signals.consume()) return 0;
    std::cout.setf(std::ios::unitbuf);
    const auto model = detectModel(dmiAttribute("sys_vendor"), dmiAttribute("product_name"), model_override);
    auto leds = createHardware(model);
    struct ClearOnExit {
        LedControlPtr leds; bool enabled = true;
        ~ClearOnExit() {
            if (!enabled) return;
            try {
                for (size_t bay = 0; bay < 4; ++bay) leds->Set(LED_BLUE | LED_RED, bay, false);
                leds->SetSystemLed(LED_BLUE | LED_RED, false);
            } catch (...) {}
        }
    } cleanup{leds};
    if (signals.consume()) return 0;
    if (disable_watchdog) leds->DisableWatchdog();
    Privileges privileges;
    dropPrivileges(privileges, runtime_user);
    if (signals.consume()) return 0;
    if (mount_usb >= 0) leds->MountUsb(mount_usb != 0);
    std::cout << "Found: " << leds->Desc() << "; runtime user: " << runtime_user << '\n';
    leds->SetSystemLed(LED_BLUE | LED_RED, false);
    if (brightness >= 0) leds->SetBrightness(brightness);
    for (size_t bay = 0; bay < 4; ++bay) leds->Set(LED_BLUE | LED_RED, bay, xmas);
    if (xmas) { cleanup.enabled = false; return 0; }
    if (light_show) return run_light_show(leds, light_show, signals);
    DeviceMonitor devices(leds);
    std::unique_ptr<UpdateMonitor> updates;
    if (run_update_monitor && !signals.consume()) updates = std::make_unique<UpdateMonitor>(leds);
    runEventLoop(signals, devices, updates.get(), activity);

	return 0;

} catch ( std::exception& e ) {
	std::cerr << e.what() << '\n';


	return 1;
}
