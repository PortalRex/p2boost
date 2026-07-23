#include "world_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2.h"
#include "qpc.h"
#include "x86.h"

#include <Windows.h>

namespace p2boost {

	WorldTrace* WorldTrace::m_instance{ };

	WorldTrace::~WorldTrace( ) {
		remove( );
	}

	bool WorldTrace::install( Metrics* metrics ) {
#ifndef P2BOOST_RESEARCH_WORLD_TRACE
		(void)metrics;
		return true;
#else
		if( m_instance || !metrics )
			return false;

		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		const Address match = pattern::find_unique( engine, portal2::signature::world_surface );
		const Address call	= match.add( portal2::xref::world_surface_call );
		if( !engine || !match || call.at< std::uint8_t >( ) != 0xe8 )
			return false;

		const Address original{ call.rel32( 1 ) };
		m_original = original.as< BuildSurfaces_t >( );
		m_metrics  = metrics;
		m_instance = this;

		if( !m_call.install( call.as< void* >( ), x86::relative_call( call, original ), reinterpret_cast< void* >( &BuildSurfaces ) ) ) {
			remove( );
			return false;
		}

		return true;
#endif
	}

	void WorldTrace::remove( ) {
		m_call.remove( );
		m_original = nullptr;
		m_metrics  = nullptr;
		if( m_instance == this )
			m_instance = nullptr;
	}

	void __cdecl WorldTrace::BuildSurfaces( int* render_context, int world_list, unsigned int flags ) {
		WorldTrace* instance = m_instance;
		if( !instance || !instance->m_original )
			return;

		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			instance->m_original( render_context, world_list, flags );
			return;
		}

		Stopwatch timer;
		instance->m_original( render_context, world_list, flags );
		instance->m_metrics->record_world_surface( timer.elapsed_ms( ), flags );
	}

}
