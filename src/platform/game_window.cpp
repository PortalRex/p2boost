#include "game_window.h"

namespace p2boost {
	namespace {

		struct window_search_t {
			DWORD m_process{ };
			HWND  m_window{ };
		};

		BOOL CALLBACK find_process_window( HWND window, LPARAM parameter ) {
			auto* search = reinterpret_cast< window_search_t* >( parameter );
			DWORD process{ };
			GetWindowThreadProcessId( window, &process );
			if( process != search->m_process || !IsWindowVisible( window ) || GetWindow( window, GW_OWNER ) )
				return TRUE;

			search->m_window = window;
			return FALSE;
		}

	}

	HWND game_window( ) noexcept {
		window_search_t search{ GetCurrentProcessId( ) };
		EnumWindows( &find_process_window, reinterpret_cast< LPARAM >( &search ) );
		return search.m_window;
	}

	bool game_is_foreground( ) noexcept {
		DWORD foreground_process{ };
		GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
		return foreground_process == GetCurrentProcessId( );
	}

	[[noreturn]] void restart_after_nvidia_profile_update( ) noexcept {
		MessageBoxW( game_window( ),
		             L"p2boost applied the NVIDIA settings required for reliable Vulkan alt-tabbing.\n\n"
		             L"Portal 2 must restart and will now close.",
		             L"p2boost - restart required",
		             MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND );

		// Source ignores WM_CLOSE while its startup command buffer is active.
		ExitProcess( 0 );
	}

}
