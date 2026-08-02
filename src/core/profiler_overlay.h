#pragma once

#include "frame_metrics.h"
#include "interfaces.h"
#include "qpc.h"
#include "sdk.h"

namespace p2boost {

	// draw a low-frequency summary through Valve's public engine ABI.
	class ProfilerOverlay {
	public:
		bool connect( sdk::CreateInterfaceFn interface_factory ) noexcept;
		void disconnect( ) noexcept;
		void set_enabled( bool enabled, FrameMetrics& metrics ) noexcept;
		void draw( const FrameMetrics& metrics, bool fixes_enabled, bool sar_loaded ) noexcept;

		bool available( ) const noexcept {
			return m_debug_overlay != nullptr;
		}

		bool enabled( ) const noexcept {
			return m_enabled;
		}

	private:
		void draw_panel( const overlay_metrics_t& sample, bool fixes_enabled, bool sar_loaded ) noexcept;

		sdk::DebugOverlay* m_debug_overlay{ };
		Qpc::tick_t		   m_next_update{ };
		bool			   m_enabled{ };
	};

}
