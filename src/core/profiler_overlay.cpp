#include "profiler_overlay.h"

#include <Windows.h>

#include <array>
#include <cstdio>

namespace p2boost {
	namespace {

		constexpr float panel_x{ 0.018f };
		constexpr float panel_y{ 0.025f };
		constexpr float line_height{ 0.022f };
		constexpr float text_lifetime{ 0.35f };

		// keep engine overlay calls in one small typed helper.
		void draw_line( sdk::DebugOverlay* overlay, int line, const std::array< int, 4 >& color, const char* text ) noexcept {
			overlay->AddScreenText(
				panel_x,
				panel_y + static_cast< float >( line ) * line_height,
				text_lifetime,
				color[ 0 ],
				color[ 1 ],
				color[ 2 ],
				color[ 3 ],
				text );
		}

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

	bool ProfilerOverlay::connect( sdk::CreateInterfaceFn interface_factory ) noexcept {
		// prefer the loader-provided factory so interface ownership stays explicit.
		if( interface_factory )
			m_debug_overlay = static_cast< sdk::DebugOverlay* >( interface_factory( "VDebugOverlay004", nullptr ) );
		if( m_debug_overlay )
			return true;

		const HMODULE engine = GetModuleHandleW( L"engine.dll" );
		if( !engine )
			return false;

		auto factory = reinterpret_cast< sdk::CreateInterfaceFn >( GetProcAddress( engine, "CreateInterface" ) );
		if( factory )
			m_debug_overlay = static_cast< sdk::DebugOverlay* >( factory( "VDebugOverlay004", nullptr ) );
		return m_debug_overlay != nullptr;
	}

	void ProfilerOverlay::disconnect( ) noexcept {
		// p2boost lines expire naturally. clearing the engine overlay would also
		// erase drawings owned by SAR and other plugins.
		m_debug_overlay = nullptr;
		m_next_update	= 0;
		m_enabled		= false;
	}

	void ProfilerOverlay::set_enabled( bool enabled, FrameMetrics& metrics ) noexcept {
		m_enabled = enabled && available( );
		metrics.set_overlay_collection( m_enabled );
		m_next_update = 0;
	}

	void ProfilerOverlay::draw( const FrameMetrics& metrics, bool fixes_enabled, bool sar_loaded ) noexcept {
		if( !m_enabled || !m_debug_overlay )
			return;

		// refresh at 4 Hz while frame samples continue at render frequency.
		const Qpc::tick_t now = Qpc::now( );
		if( now < m_next_update )
			return;
		m_next_update = now + Qpc::ticks( 250.0 );

		draw_panel( metrics.overlay_metrics( ), fixes_enabled, sar_loaded );
	}

	void ProfilerOverlay::draw_panel( const overlay_metrics_t& sample, bool fixes_enabled, bool sar_loaded ) noexcept {
		static constexpr std::array< int, 4 > neutral{ 200, 220, 242, 255 };
		static constexpr std::array< int, 4 > good{ 115, 242, 158, 255 };
		static constexpr std::array< int, 4 > warning{ 255, 191, 64, 255 };
		static constexpr std::array< int, 4 > bad{ 255, 77, 64, 255 };

		char		 line[ 192 ]{ };
		const double fps = sample.m_average_ms > 0.0 ? 1000.0 / sample.m_average_ms : 0.0;
		std::snprintf( line,
					   sizeof( line ),
					   "P2BOOST / VULKAN  %s  |  %s  |  SAR %s",
					   sample.m_coop ? "CO-OP" : "SINGLE PLAYER",
					   fixes_enabled ? "FIXES ON" : "A/B OFF",
					   sar_loaded ? "ON" : "OFF" );
		draw_line( m_debug_overlay, 0, neutral, line );

		std::snprintf( line,
					   sizeof( line ),
					   "%5.0f FPS  |  FRAME %5.2f ms  |  P95 %5.2f  P99 %5.2f  MAX %5.2f",
					   fps,
					   sample.m_frame_ms,
					   sample.m_p95_ms,
					   sample.m_p99_ms,
					   sample.m_max_ms );
		const auto& timing_color = sample.m_hitch_count > 0 ? bad : sample.m_p99_ms >= 4.0 ? warning
																						   : good;
		draw_line( m_debug_overlay, 1, timing_color, line );

		std::array< char, overlay_metrics_t::graph_capacity + 3 > graph{ };
		graph[ 0 ] = '[';
		for( size_t index = 0; index < sample.m_graph_count; ++index )
			graph[ index + 1 ] = graph_character( sample.m_graph_ms[ index ] );
		graph[ sample.m_graph_count + 1 ] = ']';
		draw_line( m_debug_overlay, 2, timing_color, graph.data( ) );

		std::snprintf( line,
					   sizeof( line ),
					   "LAST 1S  |  HITCHES >=8.3 ms: %u  |  SHADOW CACHE %u/%u  |  %s",
					   sample.m_hitch_count,
					   sample.m_shadow_hits,
					   sample.m_shadow_updates,
					   sample.m_focused ? "FOCUSED" : "UNFOCUSED" );
		draw_line( m_debug_overlay, 3, sample.m_hitch_count > 0 ? bad : neutral, line );
	}

}
