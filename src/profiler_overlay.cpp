#include "profiler_overlay.h"

#include "metrics.h"

#include <Windows.h>
#include <algorithm>

namespace p2boost {
	namespace {
		// IVEngineClient013/014 share Con_NPrintf at slot 29. Vararg member
		// functions use the x86 cdecl ABI and receive `this` as argument zero.
		constexpr size_t con_nprintf_slot = 29;
		constexpr int	 line_count		  = 6;

		using ConNPrintfFn = void( __cdecl* )( void*, int, const char*, ... );

		ConNPrintfFn con_nprintf( void* engine ) noexcept {
			return reinterpret_cast< ConNPrintfFn >( ( *reinterpret_cast< void*** >( engine ) )[ con_nprintf_slot ] );
		}
	}

	bool ProfilerOverlay::connect( sdk::CreateInterfaceFn interface_factory ) noexcept {
		// IServerPluginCallbacks receives a restricted factory on Portal 2.
		// Fall back to engine.dll's documented CreateInterface export rather
		// than locating the client interface through an RVA or global xref.
		if( interface_factory )
			m_engine = interface_factory( "VEngineClient015", nullptr );
		if( !m_engine && interface_factory )
			m_engine = interface_factory( "VEngineClient014", nullptr );
		if( !m_engine && interface_factory )
			m_engine = interface_factory( "VEngineClient013", nullptr );
		if( m_engine )
			return true;

		const HMODULE engine_module = GetModuleHandleW( L"engine.dll" );
		if( !engine_module )
			return false;
		auto engine_factory = reinterpret_cast< sdk::CreateInterfaceFn >( GetProcAddress( engine_module, "CreateInterface" ) );
		if( !engine_factory )
			return false;
		m_engine = engine_factory( "VEngineClient015", nullptr );
		if( !m_engine )
			m_engine = engine_factory( "VEngineClient014", nullptr );
		if( !m_engine )
			m_engine = engine_factory( "VEngineClient013", nullptr );
		return m_engine != nullptr;
	}

	void ProfilerOverlay::disconnect( ) noexcept {
		clear( );
		m_engine	  = nullptr;
		m_next_update = 0;
		m_enabled	  = false;
	}

	void ProfilerOverlay::set_enabled( bool enabled, Metrics& metrics ) noexcept {
		m_enabled = enabled && available( );
		metrics.set_overlay_collection( m_enabled );
		m_next_update = 0;
		if( !m_enabled )
			clear( );
	}

	void ProfilerOverlay::draw( const Metrics& metrics, bool fixes_enabled, bool sar_loaded ) noexcept {
		if( !m_enabled || !m_engine )
			return;
		const auto now = Qpc::now( );
		if( now < m_next_update )
			return;
		m_next_update = now + Qpc::ticks( 250.0 );

		const OverlayMetrics sample = metrics.overlay_metrics( );
		const double		 fps	= sample.m_average_ms > 0.0 ? 1000.0 / sample.m_average_ms : 0.0;
		auto				 print	= con_nprintf( m_engine );
		print( m_engine,
			0,
			"p2boost profiler | fixes %s | SAR %s | %s",
			fixes_enabled ? "ON" : "OFF",
			sar_loaded ? "ON" : "OFF",
			sample.m_coop ? "CO-OP" : "SP" );
		print( m_engine, 1, "frame %6.3f ms | avg %6.1f FPS", sample.m_frame_ms, fps );
		print( m_engine, 2, "1s p99 %6.3f ms | max %6.3f ms%s", sample.m_p99_ms, sample.m_max_ms, sample.m_max_ms > 8.3 ? "  HITCH" : "" );
		print( m_engine,
			3,
			"portals %u | %6.3f ms | worker %6.3f ms",
			sample.m_portal_count,
			sample.m_portal_ms,
			sample.m_render_worker_ms );
		print( m_engine, 4, "shadow skips %u | focus events %u", sample.m_shadow_skips, sample.m_focus_events );
		print( m_engine, 5, "focus %s | window 1.0 s", sample.m_focused ? "ACTIVE" : "INACTIVE" );
	}

	void ProfilerOverlay::clear( ) const noexcept {
		if( !m_engine )
			return;
		auto print = con_nprintf( m_engine );
		for( int line = 0; line < line_count; ++line )
			print( m_engine, line, "" );
	}

}
