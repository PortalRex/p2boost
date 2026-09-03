#include "profiler_overlay.h"

#include "address.h"
#include "game_profile.h"
#include "game_window.h"
#include "module.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace p2boost {
	namespace {

		constexpr int           panel_margin{ 12 };
		constexpr unsigned long fps_font{ 20 };

		// encode graph samples without depending on a custom font or texture.
		char graph_character( double milliseconds ) noexcept {
			if( milliseconds >= 8.3 )
				return '!';
			if( milliseconds >= 4.0 )
				return '^';
			if( milliseconds >= 2.0 )
				return '=';
			if( milliseconds >= 1.0 )
				return '-';
			return '.';
		}

	}

	ProfilerOverlay* ProfilerOverlay::m_instance{ };

	bool ProfilerOverlay::connect(
	    const portal2_modules_t& modules, const Portal2Interfaces& interfaces, FrameMetrics* metrics ) noexcept {
		auto* engine_client  = interfaces.engine_client( );
		auto* engine_vgui    = interfaces.engine_vgui( );
		auto* scheme_manager = interfaces.scheme_manager( );

		if( m_instance || !metrics || !engine_client || !engine_vgui || !scheme_manager )
			return false;

		if( !modules.m_engine || !modules.m_surface || !modules.m_vgui )
			return false;

		if( !modules.m_engine.contains( engine_client->GetScreenSizeAddress( ) ) ||
		    !modules.m_engine.matches( engine_client->GetMaxClientsAddress( ), game::active( )->overlay_engine_get_max_clients ) )
			return false;

		m_surface = interfaces.surface( );
		if( !m_surface ) {
			m_surface = nullptr;
			return false;
		}

		// SAR renders its HUD after CEngineVGui::Paint, using these exact surface
		// helpers to open and close a direct 2D draw section.
		const Address paint_traverse{ m_surface->PaintTraverseExAddress( ) };
		const Address draw_colored_text{ m_surface->DrawColoredTextAddress( ) };
		const Address get_kerned_char_width{ m_surface->GetKernedCharWidthAddress( ) };
		const Address get_font_tall{ m_surface->GetFontTallAddress( ) };
		const Address start_operand  = paint_traverse.add( game::active( )->surface_start_drawing_operand );
		const Address finish_operand = paint_traverse.add( game::active( )->surface_finish_drawing_operand );
		if( !modules.m_surface.matches( paint_traverse, game::active( )->overlay_surface_paint_traverse ) ||
		    !modules.m_surface.matches( draw_colored_text, game::active( )->overlay_surface_draw_colored_text ) ||
		    !modules.m_surface.contains( get_kerned_char_width ) ||
		    !modules.m_surface.matches( get_font_tall, game::active( )->overlay_surface_get_font_tall ) ||
		    start_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 ||
		    finish_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 ) {
			m_surface = nullptr;
			return false;
		}

		const Address start_drawing  = start_operand.rel32( );
		const Address finish_drawing = finish_operand.rel32( );
		if( !modules.m_surface.matches( start_drawing, game::active( )->overlay_surface_start_drawing ) ||
		    !modules.m_surface.matches( finish_drawing, game::active( )->overlay_surface_finish_drawing ) ) {
			m_surface = nullptr;
			return false;
		}

		if( !modules.m_vgui.contains( scheme_manager->GetISchemeAddress( ) ) ) {
			m_surface = nullptr;
			return false;
		}
		sdk::IScheme* scheme = scheme_manager->GetIScheme( 1 );
		if( !scheme || !modules.m_vgui.contains( scheme->GetFontAddress( ) ) ) {
			m_surface = nullptr;
			return false;
		}

		m_panel_font = scheme->GetFont( "DefaultFixedOutline", false );
		if( !m_panel_font ) {
			m_surface = nullptr;
			return false;
		}

		const Module  sar{ L"sar.dll" };
		const Address engine_paint = engine_vgui->PaintAddress( );
		const bool    engine_owned = modules.m_engine.contains( engine_paint );
		if( ( !engine_owned && !sar.contains( engine_paint ) ) ||
		    ( engine_owned && !modules.m_engine.matches( engine_paint, game::active( )->overlay_engine_vgui_paint ) ) ) {
			m_surface    = nullptr;
			m_panel_font = 0;
			return false;
		}

		m_start_drawing  = start_drawing.as< start_drawing_fn_t >( );
		m_finish_drawing = finish_drawing.as< finish_drawing_fn_t >( );
		m_line_height    = std::max( m_surface->GetFontTall( m_panel_font ) + 2, 12 );
		m_metrics        = metrics;
		m_engine_client  = engine_client;
		m_instance       = this;

		if( m_engine_vgui_paint.install( engine_vgui->PaintSlot( ), reinterpret_cast< void* >( &EngineVGuiPaint ) ) )
			return true;

		m_instance       = nullptr;
		m_metrics        = nullptr;
		m_engine_client  = nullptr;
		m_surface        = nullptr;
		m_start_drawing  = nullptr;
		m_finish_drawing = nullptr;
		m_panel_font     = 0;
		return false;
	}

	void ProfilerOverlay::disconnect( ) noexcept {
		m_engine_vgui_paint.remove( );
		if( m_instance == this )
			m_instance = nullptr;

		m_surface        = nullptr;
		m_engine_client  = nullptr;
		m_metrics        = nullptr;
		m_start_drawing  = nullptr;
		m_finish_drawing = nullptr;
		m_sample         = { };
		m_next_update    = 0;
		m_panel_font     = 0;
		m_line_height    = 16;
		m_have_sample    = false;
		m_fixes_enabled  = false;
		m_mode           = profiler_mode_t::off;
	}

	void ProfilerOverlay::set_mode( profiler_mode_t mode, FrameMetrics& metrics ) noexcept {
		m_mode = available( ) ? mode : profiler_mode_t::off;
		metrics.set_overlay_collection( enabled( ) );
		m_next_update = 0;
		m_have_sample = false;
	}

	void ProfilerOverlay::set_fixes_enabled( bool enabled ) noexcept {
		m_fixes_enabled = enabled;
	}

	int __fastcall ProfilerOverlay::EngineVGuiPaint( void* self, void*, int mode ) {
		ProfilerOverlay* overlay = m_instance;
		if( !overlay )
			return 0;

		auto original = overlay->m_engine_vgui_paint.previous< engine_vgui_paint_fn_t >( );
		if( !original )
			return 0;

		// match SAR's ordering: let the engine paint its panels, then draw the
		// plugin HUD directly on the material-system surface.
		const int result = original( self, mode );
		if( mode & game::paint_mode_ui_panels )
			overlay->draw( );
		return result;
	}

	void ProfilerOverlay::draw( ) noexcept {
		if( !enabled( ) || !m_surface || !m_metrics || !m_engine_client || !m_start_drawing || !m_finish_drawing )
			return;

		// refresh the formatted data at 4 Hz, but paint the retained sample every
		// frame. Debug-overlay lifetimes are no longer involved.
		const Qpc::tick_t now = Qpc::now( );
		if( !m_have_sample || now >= m_next_update ) {
			m_next_update = now + Qpc::ticks( 250.0 );
			m_metrics->set_coop( m_engine_client && m_engine_client->GetMaxClients( ) > 1 );
			m_metrics->set_focused( game_is_foreground( ) );
			m_sample      = m_metrics->overlay_metrics( );
			m_have_sample = true;
		}

		int screen_width{ };
		int screen_height{ };
		m_engine_client->GetScreenSize( screen_width, screen_height );
		if( screen_width <= 0 || screen_height <= 0 )
			return;

		m_start_drawing( m_surface );
		if( m_mode == profiler_mode_t::fps )
			draw_fps( screen_width, m_sample );
		else
			draw_panel( screen_width, m_sample, m_fixes_enabled, static_cast< bool >( Module{ L"sar.dll" } ) );
		m_finish_drawing( );
	}

	void ProfilerOverlay::draw_line(
	    int screen_width, unsigned long font, int line, const color_t& color, const char* text ) noexcept {
		// measure every line independently so all modes share one exact right edge
		// after any resolution, fullscreen, or split-screen layout change.
		const int width = text_width( font, text );
		const int x     = std::max( panel_margin, screen_width - panel_margin - width );
		m_surface->DrawColoredText(
		    font, x, panel_margin + line * m_line_height, color.red, color.green, color.blue, color.alpha, text );
	}

	int ProfilerOverlay::text_width( unsigned long font, const char* text ) const noexcept {
		int width{ };
		for( size_t index = 0; text && text[ index ]; ++index ) {
			const auto                 character = static_cast< wchar_t >( static_cast< unsigned char >( text[ index ] ) );
			const wchar_t              previous  = index ? static_cast< wchar_t >( static_cast< unsigned char >( text[ index - 1 ] ) ) : wchar_t{ };
			const auto                 next      = static_cast< wchar_t >( static_cast< unsigned char >( text[ index + 1 ] ) );
			const sdk::glyph_metrics_t glyph     = m_surface->GetKernedCharWidth( font, character, previous, next );

			// match SAR's pixel rounding so measurement and final VGUI drawing
			// advance through every glyph by the same amount.
			width += std::max( static_cast< int >( std::floor( glyph.advance + 0.6f ) ), 0 );
		}
		return width;
	}

	void ProfilerOverlay::draw_panel(
	    int screen_width, const overlay_metrics_t& sample, bool fixes_enabled, bool sar_loaded ) noexcept {
		static constexpr color_t neutral{ 200, 220, 242, 255 };
		static constexpr color_t good{ 115, 242, 158, 255 };
		static constexpr color_t warning{ 255, 191, 64, 255 };
		static constexpr color_t bad{ 255, 77, 64, 255 };

		char         line[ 192 ]{ };
		const double fps = sample.m_average_ms > 0.0 ? 1000.0 / sample.m_average_ms : 0.0;
		std::snprintf( line,
		               sizeof( line ),
		               "P2BOOST / VULKAN  %s  |  %s  |  SAR %s",
		               sample.m_coop ? "CO-OP" : "SINGLE PLAYER",
		               fixes_enabled ? "FIXES ON" : "A/B OFF",
		               sar_loaded ? "ON" : "OFF" );
		draw_line( screen_width, m_panel_font, 0, neutral, line );

		std::snprintf( line,
		               sizeof( line ),
		               "%5.0f FPS  |  FRAME %5.2f ms  |  PRESENT %5.2f  P95 %5.2f  P99 %5.2f",
		               fps,
		               sample.m_frame_ms,
		               sample.m_present_ms,
		               sample.m_p95_ms,
		               sample.m_p99_ms );
		const auto& timing_color = sample.m_hitch_count > 0 ? bad : sample.m_p99_ms >= 4.0 ? warning
		                                                                                   : good;
		draw_line( screen_width, m_panel_font, 1, timing_color, line );

		std::array< char, overlay_metrics_t::graph_capacity + 3 > graph{ };
		graph[ 0 ] = '[';
		for( size_t index = 0; index < sample.m_graph_count; ++index )
			graph[ index + 1 ] = graph_character( sample.m_graph_ms[ index ] );
		graph[ sample.m_graph_count + 1 ] = ']';
		draw_line( screen_width, m_panel_font, 2, timing_color, graph.data( ) );

		std::snprintf( line,
		               sizeof( line ),
		               "LAST 1S  |  HITCHES >=8.3 ms: %u  |  SHADOW CACHE %u/%u  |  %s",
		               sample.m_hitch_count,
		               sample.m_shadow_hits,
		               sample.m_shadow_updates,
		               sample.m_focused ? "FOCUSED" : "UNFOCUSED" );
		draw_line( screen_width, m_panel_font, 3, sample.m_hitch_count > 0 ? bad : neutral, line );
	}

	void ProfilerOverlay::draw_fps( int screen_width, const overlay_metrics_t& sample ) noexcept {
		static constexpr color_t normal{ 255, 255, 255, 255 };
		static constexpr color_t hitch{ 255, 77, 64, 255 };

		const double fps = sample.m_average_ms > 0.0 ? 1000.0 / sample.m_average_ms : 0.0;
		char         text[ 32 ]{ };
		std::snprintf( text, sizeof( text ), "%.0ffps", fps );

		// red means exactly one thing: the rolling one-second window contains a
		// frame at or above the profiler's 8.3 ms hitch threshold.
		const color_t color = sample.m_hitch_count > 0 ? hitch : normal;
		const int     width = text_width( fps_font, text );
		const int     x     = std::max( panel_margin, screen_width - panel_margin - width );
		m_surface->DrawColoredText(
		    fps_font, x, 0, color.red, color.green, color.blue, color.alpha, text );
	}

}
