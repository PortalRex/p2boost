#pragma once

#include "hook.h"

#include <atomic>

namespace p2boost {
	class Metrics;

	void __cdecl FrameCompleteHook( );
	void __cdecl FrameCompleteDispatch( void* host_state, void* caller_frame );
	float carry_frame_overshoot( float elapsed, float target, bool enabled ) noexcept;

	// preserves normal fps limiter phase without carrying real hitches.
	class FramePacer {
	public:
		// default ctor / dtor.
		FramePacer( ) = default;
		~FramePacer( );

		// install or remove the verified limiter hook.
		bool install( Metrics* metrics );
		void remove( );

		// control the reversible pacing fix.
		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_frame_complete.installed( );
		}

	private:
		friend void __cdecl FrameCompleteDispatch( void*, void* );

		static FramePacer*	m_instance;
		CallSitePatch		m_frame_complete;
		Metrics*			m_metrics{ };
		std::atomic< bool > m_enabled{ false };
	};

}
