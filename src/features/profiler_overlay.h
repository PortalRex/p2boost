#pragma once

#include "color.h"
#include "console_commands.h"
#include "frame_metrics.h"
#include "frame_target.h"
#include "hook.h"
#include "interfaces.h"
#include "portal2_context.h"
#include "qpc.h"
#include "sdk.h"

namespace p2boost {

	// select how much frame data the in-game profiler paints.
	enum class profiler_mode_t : int {
		off,
		full,
		fps,
	};

	// draw a low-frequency summary directly in the final VGUI surface pass.
	class ProfilerOverlay {
	public:
		bool connect( const portal2_modules_t& modules, const Portal2Interfaces& interfaces, const ConsoleCommandRegistry& commands,
		              FrameMetrics* metrics ) noexcept;
		void disconnect( ) noexcept;
		void set_mode( profiler_mode_t mode, FrameMetrics& metrics ) noexcept;
		void set_fixes_enabled( bool enabled ) noexcept;

		bool available( ) const noexcept {
			return m_surface && m_engine_vgui_paint.installed( ) && m_start_drawing && m_finish_drawing && m_panel_font;
		}

		bool enabled( ) const noexcept {
			return m_mode != profiler_mode_t::off;
		}

		int mode( ) const noexcept {
			return static_cast< int >( m_mode );
		}

	private:
		using engine_vgui_paint_fn_t = int( __thiscall* )( void*, int );
		using start_drawing_fn_t     = int( __thiscall* )( void* );
		using finish_drawing_fn_t    = int( __cdecl* )( );
		static int __fastcall EngineVGuiPaint( void* self, void*, int mode );
		void draw( ) noexcept;
		void draw_panel( int screen_width, bool sar_loaded ) noexcept;
		void draw_fps( int screen_width ) noexcept;
		void draw_line( int screen_width, unsigned long font, int line, const color_t& color, const char* text ) noexcept;
		int  text_width( unsigned long font, const char* text ) const noexcept;

		static ProfilerOverlay* m_instance;

		sdk::ISurface*       m_surface{ };
		sdk::IVEngineClient* m_engine_client{ };
		FrameMetrics*        m_metrics{ };
		VmtHook              m_engine_vgui_paint;
		start_drawing_fn_t   m_start_drawing{ };
		finish_drawing_fn_t  m_finish_drawing{ };
		const con_var_t*     m_fps_max{ };
		const con_var_t*     m_vsync{ };
		frame_target_t       m_target{ fallback_refresh_rate, "display" };
		overlay_metrics_t    m_sample{ };
		Qpc::tick_t          m_next_update{ };
		unsigned long        m_panel_font{ };
		int                  m_line_height{ 16 };
		bool                 m_have_sample{ };
		bool                 m_fixes_enabled{ };
		profiler_mode_t      m_mode{ profiler_mode_t::off };
	};

}
