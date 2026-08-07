#include "profiler_overlay.h"

#include "address.h"
#include "pattern.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace p2boost {
	namespace {

		constexpr int panel_x{ 20 };
		constexpr int panel_y{ 20 };

		// Read focus from the local process instead of a listen-server callback.
		bool process_is_foreground( ) noexcept {
			DWORD foreground_process{ };
			GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
			return foreground_process == GetCurrentProcessId( );
		}

		bool address_in_module( Address address, Address module ) noexcept {
			if( !address || !module )
				return false;
			const Address end = module.add( static_cast< std::ptrdiff_t >( pattern::image_size( module ) ) );
			return address.as< std::uintptr_t >( ) >= module.as< std::uintptr_t >( ) &&
				   address.as< std::uintptr_t >( ) < end.as< std::uintptr_t >( );
		}

		// Match a recovered function entry without scanning for a nearby lookalike.
		bool signature_at( Address address, Address module, const char* text ) {
			if( !address_in_module( address, module ) || !text )
				return false;

			const auto signature = pattern::parse( text );
			const auto base      = module.as< std::uintptr_t >( );
			const auto current   = address.as< std::uintptr_t >( );
			const auto size      = pattern::image_size( module );
			const auto offset    = static_cast< size_t >( current - base );
			if( signature.empty( ) || offset > size || signature.size( ) > size - offset )
				return false;

			return pattern::find( address, signature.size( ), signature ) == address;
		}

		// Encode graph samples without depending on a custom font or texture.
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
		sdk::CreateInterfaceFn interface_factory, FrameMetrics* metrics, sdk::EngineClient* engine_client ) noexcept {
		if( m_instance || !interface_factory || !metrics || !engine_client || !engine_client->methods( ) )
			return false;

		auto* engine_vgui = static_cast< sdk::EngineVGui* >( interface_factory( "VEngineVGui001", nullptr ) );
		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		const Address surface_module{ GetModuleHandleW( L"vguimatsurface.dll" ) };
		const Address vgui_module{ GetModuleHandleW( L"vgui2.dll" ) };
		if( !engine_vgui || !engine_vgui->methods( ) || !engine || !surface_module || !vgui_module )
			return false;

		const Address get_max_clients{ engine_client->method( portal2::vmt::engine_get_max_clients ) };
		if( !signature_at( get_max_clients, engine, portal2::signature::overlay_engine_get_max_clients ) )
			return false;

		auto surface_factory = reinterpret_cast< sdk::CreateInterfaceFn >(
			GetProcAddress( surface_module.as< HMODULE >( ), "CreateInterface" ) );
		auto scheme_factory = reinterpret_cast< sdk::CreateInterfaceFn >(
			GetProcAddress( vgui_module.as< HMODULE >( ), "CreateInterface" ) );
		m_surface = surface_factory ? static_cast< sdk::Surface* >( surface_factory( "VGUI_Surface031", nullptr ) ) : nullptr;
		auto* scheme_manager = scheme_factory ? static_cast< sdk::SchemeManager* >( scheme_factory( "VGUI_Scheme010", nullptr ) ) : nullptr;
		if( !m_surface || !m_surface->methods( ) || !scheme_manager || !scheme_manager->methods( ) ) {
			m_surface = nullptr;
			return false;
		}

		// SAR renders its HUD after CEngineVGui::Paint, using these exact surface
		// helpers to open and close a direct 2D draw section.
		const Address paint_traverse{ m_surface->method( portal2::vmt::surface_paint_traverse_ex ) };
		const Address draw_colored_text{ m_surface->method( portal2::vmt::surface_draw_colored_text ) };
		const Address get_font_tall{ m_surface->method( portal2::vmt::surface_get_font_tall ) };
		const Address start_operand = paint_traverse.add( portal2::surface::start_drawing_operand );
		const Address finish_operand = paint_traverse.add( portal2::surface::finish_drawing_operand );
		if( !signature_at( paint_traverse, surface_module, portal2::signature::overlay_surface_paint_traverse ) ||
			!signature_at( draw_colored_text, surface_module, portal2::signature::overlay_surface_draw_colored_text ) ||
			!signature_at( get_font_tall, surface_module, portal2::signature::overlay_surface_get_font_tall ) ||
			start_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 ||
			finish_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 ) {
			m_surface = nullptr;
			return false;
		}

		const Address start_drawing = start_operand.rel32( );
		const Address finish_drawing = finish_operand.rel32( );
		if( !signature_at( start_drawing, surface_module, portal2::signature::overlay_surface_start_drawing ) ||
			!signature_at( finish_drawing, surface_module, portal2::signature::overlay_surface_finish_drawing ) ) {
			m_surface = nullptr;
			return false;
		}

		const Address get_scheme{ scheme_manager->method( portal2::vmt::scheme_manager_get_scheme ) };
		if( !address_in_module( get_scheme, vgui_module ) ) {
			m_surface = nullptr;
			return false;
		}
		sdk::Scheme* scheme = scheme_manager->GetIScheme( 1 );
		if( !scheme || !scheme->methods( ) || !address_in_module( scheme->method( portal2::vmt::scheme_get_font ), vgui_module ) ) {
			m_surface = nullptr;
			return false;
		}

		m_font = scheme->GetFont( "DefaultFixedOutline", false );
		if( !m_font ) {
			m_surface = nullptr;
			return false;
		}

		const Address sar{ GetModuleHandleW( L"sar.dll" ) };
		const Address engine_paint{ engine_vgui->method( portal2::vmt::engine_vgui_paint ) };
		const bool engine_owned = address_in_module( engine_paint, engine );
		if( ( !engine_owned && !address_in_module( engine_paint, sar ) ) ||
			( engine_owned && !signature_at( engine_paint, engine, portal2::signature::overlay_engine_vgui_paint ) ) ) {
			m_surface = nullptr;
			m_font	  = 0;
			return false;
		}

		m_start_drawing	 = start_drawing.as< StartDrawingFn >( );
		m_finish_drawing	 = finish_drawing.as< FinishDrawingFn >( );
		m_draw_colored_text = draw_colored_text.as< DrawColoredTextFn >( );
		m_line_height		 = std::max( m_surface->GetFontTall( m_font ) + 2, 12 );
		m_metrics			 = metrics;
		m_engine_client	 = engine_client;
		m_instance			 = this;

		if( m_engine_vgui_paint.install(
				engine_vgui->methods( ) + portal2::vmt::engine_vgui_paint, reinterpret_cast< void* >( &EngineVGuiPaint ) ) )
			return true;

		m_instance			 = nullptr;
		m_metrics			 = nullptr;
		m_engine_client	 = nullptr;
		m_surface			 = nullptr;
		m_start_drawing	 = nullptr;
		m_finish_drawing	 = nullptr;
		m_draw_colored_text = nullptr;
		m_font				 = 0;
		return false;
	}

	void ProfilerOverlay::disconnect( ) noexcept {
		m_engine_vgui_paint.remove( );
		if( m_instance == this )
			m_instance = nullptr;

		m_surface			 = nullptr;
		m_engine_client	 = nullptr;
		m_metrics			 = nullptr;
		m_start_drawing	 = nullptr;
		m_finish_drawing	 = nullptr;
		m_draw_colored_text = nullptr;
		m_sample			 = { };
		m_next_update		 = 0;
		m_font				 = 0;
		m_line_height		 = 16;
		m_have_sample		 = false;
		m_enabled			 = false;
		m_fixes_enabled	 = false;
	}

	void ProfilerOverlay::set_enabled( bool enabled, FrameMetrics& metrics ) noexcept {
		m_enabled = enabled && available( );
		metrics.set_overlay_collection( m_enabled );
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

		auto original = reinterpret_cast< EngineVGuiPaintFn >( overlay->m_engine_vgui_paint.previous( ) );
		if( !original )
			return 0;

		// Match SAR's ordering: let the engine paint its panels, then draw the
		// plugin HUD directly on the material-system surface.
		const int result = original( self, mode );
		if( mode & portal2::paint_mode::ui_panels )
			overlay->draw( );
		return result;
	}

	void ProfilerOverlay::draw( ) noexcept {
		if( !m_enabled || !m_surface || !m_metrics || !m_start_drawing || !m_finish_drawing || !m_draw_colored_text )
			return;

		// Refresh the formatted data at 4 Hz, but paint the retained sample every
		// frame. Debug-overlay lifetimes are no longer involved.
		const Qpc::tick_t now = Qpc::now( );
		if( !m_have_sample || now >= m_next_update ) {
			m_next_update = now + Qpc::ticks( 250.0 );
			m_metrics->set_coop( m_engine_client && m_engine_client->GetMaxClients( ) > 1 );
			m_metrics->set_focused( process_is_foreground( ) );
			m_sample		 = m_metrics->overlay_metrics( );
			m_have_sample = true;
		}

		m_start_drawing( m_surface );
		draw_panel( m_sample, m_fixes_enabled, GetModuleHandleW( L"sar.dll" ) != nullptr );
		m_finish_drawing( );
	}

	void ProfilerOverlay::draw_line( int line, const std::array< int, 4 >& color, const char* text ) noexcept {
		m_draw_colored_text(
			m_surface,
			m_font,
			panel_x,
			panel_y + line * m_line_height,
			color[ 0 ],
			color[ 1 ],
			color[ 2 ],
			color[ 3 ],
			"%s",
			text );
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
		draw_line( 0, neutral, line );

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
		draw_line( 1, timing_color, line );

		std::array< char, overlay_metrics_t::graph_capacity + 3 > graph{ };
		graph[ 0 ] = '[';
		for( size_t index = 0; index < sample.m_graph_count; ++index )
			graph[ index + 1 ] = graph_character( sample.m_graph_ms[ index ] );
		graph[ sample.m_graph_count + 1 ] = ']';
		draw_line( 2, timing_color, graph.data( ) );

		std::snprintf( line,
					   sizeof( line ),
					   "LAST 1S  |  HITCHES >=8.3 ms: %u  |  SHADOW CACHE %u/%u  |  %s",
					   sample.m_hitch_count,
					   sample.m_shadow_hits,
					   sample.m_shadow_updates,
					   sample.m_focused ? "FOCUSED" : "UNFOCUSED" );
		draw_line( 3, sample.m_hitch_count > 0 ? bad : neutral, line );
	}

}
