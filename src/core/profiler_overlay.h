#pragma once

#include "frame_metrics.h"
#include "hook.h"
#include "interfaces.h"
#include "qpc.h"
#include "sdk.h"

namespace p2boost {

	// draw a low-frequency summary through Valve's public engine ABI.
	class ProfilerOverlay {
	public:
		bool connect( sdk::CreateInterfaceFn interface_factory, FrameMetrics* metrics, sdk::EngineClient* engine_client ) noexcept;
		void disconnect( ) noexcept;
		void set_enabled( bool enabled, FrameMetrics& metrics ) noexcept;
		void set_fixes_enabled( bool enabled ) noexcept;

		bool available( ) const noexcept {
			return m_debug_overlay && m_vgui_paint.installed( );
		}

		bool enabled( ) const noexcept {
			return m_enabled;
		}

	private:
		using VGuiPaintFn = void( __thiscall* )( void*, int );

		static void __fastcall VGuiPaint( void* self, void*, int mode );
		void draw( ) noexcept;
		void draw_panel( const overlay_metrics_t& sample, bool fixes_enabled, bool sar_loaded ) noexcept;

		static ProfilerOverlay* m_instance;

		sdk::DebugOverlay* m_debug_overlay{ };
		sdk::EngineClient* m_engine_client{ };
		FrameMetrics*	   m_metrics{ };
		VmtHook			   m_vgui_paint;
		Qpc::tick_t		   m_next_update{ };
		bool			   m_enabled{ };
		bool			   m_fixes_enabled{ };
	};

}
