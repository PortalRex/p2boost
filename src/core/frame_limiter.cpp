#include "frame_limiter.h"

#include "address.h"
#include "frame_metrics.h"
#include "pattern.h"
#include "portal2.h"
#include "stack.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace p2boost {

	FrameLimiter* FrameLimiter::m_instance{ };

	FrameLimiter::~FrameLimiter( ) {
		remove( );
	}

	bool FrameLimiter::install( FrameMetrics* metrics ) {
		if( m_instance || m_frame_complete.installed( ) || !metrics )
			return false;

		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		if( !engine )
			return false;

		// This is the unique CEngine::Frame tail that clears the limiter's
		// accumulated interval in build 23973718.
		const Address frame_tail = pattern::find_unique( engine, portal2::signature::frame_complete );
		const Address frame_complete = frame_tail.add( portal2::xref::frame_complete );
		if( !frame_complete )
			return false;

		const std::vector< std::uint8_t > expected{ 0x0f, 0x57, 0xc0, 0xf3, 0x0f, 0x11, 0x46, 0x18 };
		m_metrics = metrics;
		m_instance = this;

		if( !m_frame_complete.install( frame_complete.as< void* >( ), expected, reinterpret_cast< void* >( &FrameCompleteHook ) ) ) {
			m_instance = nullptr;
			m_metrics = nullptr;
			return false;
		}

		return true;
	}

	void FrameLimiter::remove( ) {
		m_frame_complete.remove( );
		m_enabled.store( false, std::memory_order_relaxed );
		m_metrics = nullptr;

		if( m_instance == this )
			m_instance = nullptr;
	}

	// Preserve the displaced x86 frame and pass its two required pointers to a
	// normal C++ function. The patched site has no ordinary callable ABI.
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
		FrameLimiter* limiter = FrameLimiter::m_instance;
		const Address host{ host_state };
		const Stack caller{ caller_frame };

		float* accumulated = host.add( portal2::frame::accumulator ).as< float* >( );
		const float target = caller.local( portal2::frame::target_local ).to< float >( );
		*accumulated = carry_frame_overshoot( *accumulated, target, limiter && limiter->enabled( ) );

		// GameFrame follows the 60 Hz simulation tick. This engine tail runs once
		// per rendered frame and therefore drives the real FPS/frametime metrics.
		if( limiter && limiter->m_metrics )
			limiter->m_metrics->record_frame( );
	}

	float carry_frame_overshoot( float elapsed, float target, bool enabled ) noexcept {
		if( enabled && std::isfinite( target ) && std::isfinite( elapsed ) && target > 0.0f && elapsed >= target &&
			elapsed < target * 1.10f )
			return elapsed - target;

		return 0.0f;
	}

}
