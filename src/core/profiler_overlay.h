#pragma once

#include "frame_metrics.h"
#include "hook.h"
#include "interfaces.h"
#include "qpc.h"
#include "sdk.h"

#include <array>

namespace p2boost {

	// Draw a low-frequency summary directly in the final VGUI surface pass.
	class ProfilerOverlay {
	public:
		bool connect( sdk::CreateInterfaceFn interface_factory, FrameMetrics* metrics, sdk::EngineClient* engine_client ) noexcept;
		void disconnect( ) noexcept;
		void set_enabled( bool enabled, FrameMetrics& metrics ) noexcept;
		void set_fixes_enabled( bool enabled ) noexcept;

		bool available( ) const noexcept {
			return m_surface && m_engine_vgui_paint.installed( ) && m_start_drawing && m_finish_drawing && m_draw_colored_text && m_font;
		}

		bool enabled( ) const noexcept {
			return m_enabled;
		}

	private:
		using EngineVGuiPaintFn = void( __thiscall* )( void*, int );
		using StartDrawingFn	 = int( __thiscall* )( void* );
		using FinishDrawingFn	 = int( __cdecl* )( );
		using DrawColoredTextFn = int( __cdecl* )(
			void*, unsigned long, int, int, int, int, int, int, const char*, ... );

		static void __fastcall EngineVGuiPaint( void* self, void*, int mode );
		void draw( ) noexcept;
		void draw_panel( const overlay_metrics_t& sample, bool fixes_enabled, bool sar_loaded ) noexcept;
		void draw_line( int line, const std::array< int, 4 >& color, const char* text ) noexcept;

		static ProfilerOverlay* m_instance;

		sdk::Surface*	   m_surface{ };
		sdk::EngineClient* m_engine_client{ };
		FrameMetrics*	   m_metrics{ };
		VmtHook			   m_engine_vgui_paint;
		StartDrawingFn	   m_start_drawing{ };
		FinishDrawingFn   m_finish_drawing{ };
		DrawColoredTextFn m_draw_colored_text{ };
		overlay_metrics_t m_sample{ };
		Qpc::tick_t		   m_next_update{ };
		unsigned long	   m_font{ };
		int				   m_line_height{ 16 };
		bool				   m_have_sample{ };
		bool				   m_enabled{ };
		bool				   m_fixes_enabled{ };
	};

}
