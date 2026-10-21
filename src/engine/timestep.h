#pragma once

#include "console_commands.h"

#include <atomic>

namespace p2boost {

	// host_framerate gives every frame a fixed simulated time step (demo
	// renders such as SAR's, timedemo), so frames stop tracking the wall clock:
	// a render runs many frames per real second. features that reuse a picture
	// until a wall-clock interval elapses would then repeat stale shadows or
	// screens across several rendered frames, so they refresh every frame.
	inline std::atomic< const con_var_t* > g_host_framerate{ };

	inline bool fixed_timestep( ) noexcept {
		const con_var_t* host_framerate = g_host_framerate.load( std::memory_order_relaxed );
		return host_framerate && host_framerate->value_float( ) != 0.0f;
	}

}
