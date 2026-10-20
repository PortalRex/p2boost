#include "xinput_throttle.h"

#include "address.h"
#include "game_profile.h"

namespace p2boost {

	XInputThrottle* XInputThrottle::m_instance{ };

	bool xinput_probe_skippable( bool absent, ULONGLONG now, ULONGLONG next_probe ) noexcept {
		return absent && now < next_probe;
	}

	XInputThrottle::~XInputThrottle( ) {
		remove( );
	}

	bool XInputThrottle::install( const portal2_modules_t& modules ) {
		if( m_instance || m_get_state.installed( ) || !modules.m_input_system )
			return false;

		// the operand of PollXDevices' indirect call is inputsystem's stored
		// XInputGetState pointer, filled once from XInput1_3.dll at startup.
		const Address call = modules.m_input_system.find( game::active( )->xinput_get_state_call );
		auto*         slot = call.at< void** >( game::active( )->xref_xinput_get_state_pointer );
		if( !slot || !*slot )
			return false;

		for( DWORD index = 0; index < slot_count; ++index ) {
			m_absent[ index ]     = false;
			m_next_probe[ index ] = 0;
		}

		m_instance = this;
		if( !m_get_state.install( slot, reinterpret_cast< void* >( &GetState ) ) ) {
			m_instance = nullptr;
			return false;
		}

		return true;
	}

	void XInputThrottle::remove( ) {
		m_get_state.remove( );
		m_enabled.store( false, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	DWORD WINAPI XInputThrottle::GetState( DWORD user_index, void* state ) {
		XInputThrottle* instance = m_instance;
		auto            original = instance ? instance->m_get_state.previous< get_state_fn_t >( ) : nullptr;
		if( !original )
			return ERROR_DEVICE_NOT_CONNECTED;

		// PollXDevices acts on a not-connected result only for a slot it still
		// considers connected, so repeating it for an absent slot changes nothing.
		const ULONGLONG now = GetTickCount64( );
		if( user_index < slot_count && instance->enabled( ) &&
		    xinput_probe_skippable( instance->m_absent[ user_index ], now, instance->m_next_probe[ user_index ] ) ) {
			instance->m_skipped.fetch_add( 1, std::memory_order_relaxed );
			return ERROR_DEVICE_NOT_CONNECTED;
		}

		const DWORD result = original( user_index, state );
		if( user_index < slot_count ) {
			instance->m_absent[ user_index ]     = result == ERROR_DEVICE_NOT_CONNECTED;
			instance->m_next_probe[ user_index ] = now + xinput_absent_probe_interval_ms;
		}
		return result;
	}

}
