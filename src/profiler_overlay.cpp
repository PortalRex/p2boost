#include "profiler_overlay.h"

#include "metrics.h"

#include <Windows.h>
#include <array>
#include <cstdio>

namespace p2boost {
	namespace {
		// Portal 2 follows Valve's public Alien Swarm-era VDebugOverlay004
		// layout. AddScreenTextOverlay is vtable entry seven in that SDK.
		constexpr size_t add_screen_text_slot = 7;
		constexpr float	 panel_x			  = 0.018f;
		constexpr float	 panel_y			  = 0.025f;
		constexpr float	 line_height		  = 0.022f;
		constexpr float	 text_lifetime		  = 0.35f;

		using AddScreenTextFn = void( __thiscall* )( void*, float, float, float, int, int, int, int, const char* );

		AddScreenTextFn add_screen_text( void* overlay ) noexcept {
			auto*** object = reinterpret_cast< void*** >( overlay );
			return reinterpret_cast< AddScreenTextFn >( ( *object )[ add_screen_text_slot ] );
		}

		void draw_line( void* overlay, int line, const std::array< int, 4 >& color, const char* text ) noexcept {
			// Normalized coordinates keep the panel stable at every resolution.
			add_screen_text( overlay )( overlay,
				panel_x,
				panel_y + static_cast< float >( line ) * line_height,
				text_lifetime,
				color[ 0 ],
				color[ 1 ],
				color[ 2 ],
				color[ 3 ],
				text );
		}

		// Quantize frame time relative to the 8.3 ms hitch threshold. The graph
		// remains readable with Source's fixed-width ASCII console font.
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

	bool ProfilerOverlay::connect( sdk::CreateInterfaceFn interface_factory ) noexcept {
		// The plugin factory can expose the overlay directly on some branches.
		if( interface_factory )
			m_debug_overlay = interface_factory( "VDebugOverlay004", nullptr );
		if( m_debug_overlay )
			return true;

		// Portal 2 supplies a restricted plugin factory. Resolve the same public
		// interface through engine.dll's exported factory when necessary.
		const HMODULE engine_module = GetModuleHandleW( L"engine.dll" );
		if( !engine_module )
			return false;
		auto engine_factory = reinterpret_cast< sdk::CreateInterfaceFn >( GetProcAddress( engine_module, "CreateInterface" ) );
		if( !engine_factory )
			return false;
		m_debug_overlay = engine_factory( "VDebugOverlay004", nullptr );
		return m_debug_overlay != nullptr;
	}

	void ProfilerOverlay::disconnect( ) noexcept {
		// Existing lines naturally expire after text_lifetime. Clearing every
		// debug overlay would incorrectly remove overlays owned by other tools.
		m_debug_overlay = nullptr;
		m_next_update	= 0;
		m_enabled		= false;
	}

	void ProfilerOverlay::set_enabled( bool enabled, Metrics& metrics ) noexcept {
		m_enabled = enabled && available( );
		metrics.set_overlay_collection( m_enabled );
		m_next_update = 0;
	}

	void ProfilerOverlay::draw( const Metrics& metrics, bool fixes_enabled, bool sar_loaded ) noexcept {
		if( !m_enabled || !m_debug_overlay )
			return;
		const auto now = Qpc::now( );
		if( now < m_next_update )
			return;
		m_next_update = now + Qpc::ticks( 250.0 );

		const OverlayMetrics sample = metrics.overlay_metrics( );
		draw_panel( sample, fixes_enabled, sar_loaded );
	}

	void ProfilerOverlay::draw_panel( const OverlayMetrics& sample, bool fixes_enabled, bool sar_loaded ) noexcept {
		static constexpr std::array< int, 4 > neutral{ 200, 220, 242, 255 };
		static constexpr std::array< int, 4 > good{ 115, 242, 158, 255 };
		static constexpr std::array< int, 4 > warning{ 255, 191, 64, 255 };
		static constexpr std::array< int, 4 > bad{ 255, 77, 64, 255 };

		char		 line[ 160 ]{ };
		const double fps = sample.m_average_ms > 0.0 ? 1000.0 / sample.m_average_ms : 0.0;
		std::snprintf( line,
			sizeof( line ),
			"P2BOOST  %s  |  %4.0f FPS  |  %s  |  SAR %s",
			sample.m_coop ? "CO-OP" : "SINGLE PLAYER",
			fps,
			fixes_enabled ? "FIXES ON" : "A/B OFF",
			sar_loaded ? "ON" : "OFF" );
		draw_line( m_debug_overlay, 0, neutral, line );

		std::snprintf( line,
			sizeof( line ),
			"FRAME %5.2f ms   P99 %5.2f ms   MAX %6.2f ms",
			sample.m_frame_ms,
			sample.m_p99_ms,
			sample.m_max_ms );
		const auto& timing_color = sample.m_max_ms >= 8.3 ? bad : sample.m_p99_ms >= 4.0 ? warning : good;
		draw_line( m_debug_overlay, 1, timing_color, line );

		std::array< char, OverlayMetrics::graph_capacity + 3 > graph{ };
		graph[ 0 ] = '[';
		for( size_t index = 0; index < sample.m_graph_count; ++index )
			graph[ 1 + index ] = graph_character( sample.m_graph_ms[ sample.m_graph_count - index - 1 ] );
		graph[ 1 + sample.m_graph_count ] = ']';
		graph[ 2 + sample.m_graph_count ] = '\0';
		draw_line( m_debug_overlay, 2, timing_color, graph.data( ) );

		const double portal_average = sample.m_portal_count > 0 ? sample.m_portal_ms / static_cast< double >( sample.m_portal_count ) : 0.0;
		std::snprintf( line,
			sizeof( line ),
			"CPU  PORTALS %u x %.2f ms   RENDER THREAD %.1f%% / %u calls",
			sample.m_portal_count,
			portal_average,
			sample.m_render_worker_ms / 10.0,
			sample.m_render_worker_count );
		draw_line( m_debug_overlay, 3, neutral, line );
		draw_line( m_debug_overlay, 4, neutral, "FRAME GRAPH  . <1ms   - 1ms   = 2ms   ^ 4ms   ! hitch (8.3ms+)" );
	}

}
