#pragma once

#include <Windows.h>

namespace p2boost {

	// resolve Portal 2's visible top-level window without retaining a stale HWND.
	HWND game_window( ) noexcept;

	// compare the foreground owner with the current Portal 2 process.
	bool game_is_foreground( ) noexcept;

	// explain a one-time driver change before closing the startup-only process.
	[[noreturn]] void restart_after_nvidia_profile_update( ) noexcept;

}
