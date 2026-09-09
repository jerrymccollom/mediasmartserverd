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

// Altered version, 2026: signal-aware timer-based shows, isolated for regression tests.
#include "light_show.h"
#include "command_dispatch.h"
#include "device_monitor.h"
#include "ipc_server.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <poll.h>
using std::cout;

int run_light_show( const LedControlPtr& leds, int light_show, Signals& signals,
    IpcServer* ipc, DaemonState* daemon_state ) {

	int light_leds = 0;
	size_t show_mode = 0;
	if ( 1 == light_show ) {
		// holiday lights
		srand( time(0) );
	} else {
		show_mode = (light_show - 2) % 4 + 1;
		switch ( (light_show - 2) / 4 ) {
		default:
		case 0: light_leds = LED_BLUE; break;
		case 1: light_leds = LED_RED;  break;
		case 2: light_leds = LED_BLUE | LED_RED; break;
		}
	}


	size_t phase = 0;
    bool stop_requested = false;
    int restart_mode = 0;

	Timer timer;
    timer.arm(Milliseconds(200));
	while ( !signals.consume() && !stop_requested && !restart_mode ) {

		switch ( show_mode ) {
		case 0: // holiday lights
		{
			for ( size_t i = 0; i < 4; ++i ) {
				switch ( rand() % 4 ) {
				default:
				case 0: light_leds = 0; break;
				case 1: light_leds = LED_BLUE; break;
				case 2: light_leds = LED_RED;  break;
				case 3: light_leds = LED_BLUE | LED_RED; break;
				}

				leds->Set(  light_leds, i, true );
				leds->Set( ~light_leds, i, false );
			}
			break;
		}
		case 1: // descending chasers
		{
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == (3 - phase)) );
			if ( ++phase >= 4 ) phase = 0;
			break;
		}
		case 2: // ascending chasers
		{
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == phase) );
			if ( ++phase >= 4 ) phase = 0;
			break;
		}
		case 3: // knight rider
		{
			const size_t sel = ( phase < 3 ) ? phase : 6 - phase;
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == sel) );
			if ( ++phase >= 6 ) phase = 0;
			break;
		}
		case 4: // pulsing
		{
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, true );
			const size_t sel = 1 + ( ( phase < 9 ) ? phase : 16 - phase );
			leds->SetBrightness( sel );
			if ( ++phase >= 16 ) phase = 0;
			break;
		}
		default:
			cout << "Unsupported light show\n";
			return 1;
		}


        pollfd waiters[] = {
            {signals.fd(), POLLIN, 0},
            {timer.fd(), POLLIN, 0},
            {ipc ? ipc->fd() : -1, POLLIN, 0},
        };
        for (;;) {
            const int result = poll(waiters, 3, -1);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) throw ErrnoException("light-show poll");
            if (signals.consume()) break;
            if (waiters[2].revents) {
                ipc->acceptAndHandle([&](const Command& cmd, const ResponseCallback& ack) -> Response {
                    if (cmd.type == CommandType::LightShowStop) {
                        stop_requested = true;
                        return {true, "light show stopped"};
                    }
                    if (cmd.type == CommandType::LightShow) {
                        restart_mode = cmd.intValue;
                        return {true, "switching light show"};
                    }
                    return dispatch(cmd, *daemon_state, signals, ipc, ack);
                });
                if (stop_requested || restart_mode) break;
            }
            if (waiters[1].revents && timer.consume()) break;
        }

	}

    if (restart_mode) return run_light_show(leds, restart_mode, signals, ipc, daemon_state);
    if (stop_requested) {
        // Undo the show's overrides: bay LEDs go back to reflecting real disk state,
        // brightness goes back to whatever was last explicitly configured.
        if (daemon_state && daemon_state->last_brightness >= 0) leds->SetBrightness(daemon_state->last_brightness);
        if (daemon_state && daemon_state->devices) {
            // reconcile() picks up any real disk changes since the show started;
            // resync() then force-rewrites every bay's LEDs, since the show wrote
            // them directly and reconcile() alone would skip bays it thinks are
            // already correct (including bays with no disk, which it never touches).
            daemon_state->devices->reconcile();
            daemon_state->devices->resync();
        }
    }

	return 0;
}

