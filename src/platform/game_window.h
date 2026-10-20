#pragma once

#include <Windows.h>

namespace p2boost {

	// resolve Portal 2's visible top-level window without retaining a stale HWND.
	HWND game_window( ) noexcept;

	// the refresh rate of the monitor the game window is on, or 0 when Windows
	// reports only the hardware default.
	int display_refresh_rate( ) noexcept;

	// compare the foreground owner with the current Portal 2 process.
	bool game_is_foreground( ) noexcept;

	// explain a one-time driver change before closing the startup-only process.
	[[noreturn]] void restart_after_nvidia_profile_update( ) noexcept;

}
