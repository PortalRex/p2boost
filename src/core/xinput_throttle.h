#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace p2boost {

	// slots that last reported ERROR_DEVICE_NOT_CONNECTED are re-probed at most
	// this often; a connected controller is still read on every poll.
	inline constexpr ULONGLONG xinput_absent_probe_interval_ms{ 1000 };

	// decide whether one XInputGetState call may reuse the slot's last
	// not-connected result instead of reaching the driver stack.
	bool xinput_probe_skippable( bool absent, ULONGLONG now, ULONGLONG next_probe ) noexcept;

	// Portal 2's inputsystem calls XInputGetState for all four slots on every
	// input poll. With no controller attached each call fails through the
	// Steam overlay's XInput hooks, which enumerate devices through SetupAPI on
	// the main thread. Valve's later inputsystem re-probes absent controllers
	// only every 0.5-20 seconds; this applies the same policy to the stored
	// function pointer without changing results for connected controllers.
	class XInputThrottle {
	public:
		~XInputThrottle( );

		bool install( const portal2_modules_t& modules );
		void remove( );

		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_get_state.installed( );
		}

		std::uint64_t skipped_count( ) const noexcept {
			return m_skipped.load( std::memory_order_relaxed );
		}

	private:
		using get_state_fn_t = DWORD( WINAPI* )( DWORD, void* );

		static DWORD WINAPI GetState( DWORD user_index, void* state );

		static XInputThrottle* m_instance;

		static constexpr DWORD slot_count{ 4 };

		VmtHook                      m_get_state;
		bool                         m_absent[ slot_count ]{ };
		ULONGLONG                    m_next_probe[ slot_count ]{ };
		std::atomic_bool             m_enabled{ };
		std::atomic< std::uint64_t > m_skipped{ };
	};

}
