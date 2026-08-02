#pragma once

#include "hook.h"

#include <atomic>

namespace p2boost {

	class FrameMetrics;

	void __cdecl FrameCompleteHook( );
	void __cdecl FrameCompleteDispatch( void* host_state, void* caller_frame );

	// Carry only ordinary deadline overshoot. Real work and hitches reset phase.
	float carry_frame_overshoot( float elapsed, float target, bool enabled ) noexcept;

	class FrameLimiter {
	public:
		~FrameLimiter( );

		bool install( FrameMetrics* metrics );
		void remove( );

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

		static FrameLimiter* m_instance;

		CallSitePatch m_frame_complete;
		FrameMetrics* m_metrics{ };
		std::atomic_bool m_enabled{ };
	};

}
