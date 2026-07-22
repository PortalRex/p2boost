#include "frame_pacer.h"

#include "address.h"
#include "pattern.h"
#include "portal2.h"
#include "stack.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace p2boost {

	FramePacer* FramePacer::m_instance = nullptr;

	FramePacer::~FramePacer( ) {
		remove( );
	}

	bool FramePacer::install( ) {
		if( m_instance || m_frame_complete.installed( ) )
			return false;

		Address engine{ GetModuleHandleW( L"engine.dll" ) };
		if( !engine )
			return false;

		// find the unique CEngine::Frame tail that clears [esi+18h].
		const Address frame_tail	 = pattern::find_unique( engine, portal2::signature::frame_complete );
		const Address frame_complete = frame_tail.add( portal2::xref::frame_complete );
		if( !frame_complete )
			return false;

		// preserve every displaced instruction as the reversible signature.
		std::vector< std::uint8_t > expected{ 0x0f, 0x57, 0xc0, 0xf3, 0x0f, 0x11, 0x46, 0x18 };

		m_instance = this;
		if( !m_frame_complete.install( frame_complete.as< void* >( ), expected, reinterpret_cast< void* >( &FrameCompleteHook ) ) ) {
			m_instance = nullptr;
			return false;
		}

		return true;
	}

	void FramePacer::remove( ) {
		m_frame_complete.remove( );
		m_enabled.store( false, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	// bridge the displaced x86 frame into a normal cdecl helper.
	void __declspec( naked ) __cdecl FrameCompleteHook( ) {
		__asm {
			push ebp
			push esi
			call FrameCompleteDispatch
			add esp, 8
			ret
		}
	}

	void __cdecl FrameCompleteDispatch( void* host_state, void* caller_frame ) {
		FramePacer* instance = FramePacer::m_instance;
		Address		host{ host_state };
		Stack		caller{ caller_frame };
		float*		accumulated = host.add( portal2::frame::accumulator ).as< float* >( );
		float		target		= caller.local( portal2::frame::target_local ).to< float >( );

		*accumulated = carry_frame_overshoot( *accumulated, target, instance && instance->enabled( ) );
	}

	float carry_frame_overshoot( float elapsed, float target, bool enabled ) noexcept {
		// carry only normal deadline polling drift.
		if( enabled && std::isfinite( target ) && std::isfinite( elapsed ) && target > 0.f && elapsed >= target &&
			elapsed < target * 1.10f )
			return elapsed - target;

		// reset phase after real renderer work or a hitch.
		return 0.f;
	}

}
