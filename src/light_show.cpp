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
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <poll.h>
using std::cout;

int run_light_show( const LedControlPtr& leds, int light_show, Signals& signals ) {

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


	size_t state = 0;

	Timer timer;
    timer.arm(Milliseconds(200));
	while ( !signals.consume() ) {

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
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == (3 - state)) );
			if ( ++state >= 4 ) state = 0;
			break;
		}
		case 2: // ascending chasers
		{
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == state) );
			if ( ++state >= 4 ) state = 0;
			break;
		}
		case 3: // knight rider
		{
			const size_t sel = ( state < 3 ) ? state : 6 - state;
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, (i == sel) );
			if ( ++state >= 6 ) state = 0;
			break;
		}
		case 4: // pulsing
		{
			for ( size_t i = 0; i < 4; ++i ) leds->Set( light_leds, i, true );
			const size_t sel = 1 + ( ( state < 9 ) ? state : 16 - state );
			leds->SetBrightness( sel );
			if ( ++state >= 16 ) state = 0;
			break;
		}
		default:
			cout << "Unsupported light show\n";
			return 1;
		}


        pollfd waiters[] = {{signals.fd(), POLLIN, 0}, {timer.fd(), POLLIN, 0}};
        for (;;) {
            const int result = poll(waiters, 2, -1);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0) throw ErrnoException("light-show poll");
            if (signals.consume() || (waiters[1].revents && timer.consume())) break;
        }

	}

	return 0;
}
