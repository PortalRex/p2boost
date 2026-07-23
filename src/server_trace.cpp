#include "server_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2.h"
#include "qpc.h"

#include <Windows.h>

namespace p2boost {

	ServerTrace* ServerTrace::m_instance{ };

	ServerTrace::~ServerTrace( ) {
		remove( );
	}

	bool ServerTrace::install( Metrics* metrics ) {
#ifndef P2BOOST_RESEARCH_WORLD_TRACE
		(void)metrics;
		return true;
#else
		if( m_instance || !metrics )
			return false;

		const Address server{ GetModuleHandleW( L"server.dll" ) };
		const Address target = pattern::find_unique( server, portal2::signature::portal_movement_spatial );
		const Address slot	 = pattern::find_pointer_unique( server, target );
		if( !server || !target || !slot )
			return false;

		m_metrics  = metrics;
		m_instance = this;
		if( !m_spatial_query.install( slot.as< void** >( ), reinterpret_cast< void* >( &SpatialQuery ) ) ) {
			remove( );
			return false;
		}

		m_original = reinterpret_cast< SpatialQuery_t >( m_spatial_query.previous( ) );
		return m_original != nullptr;
#endif
	}

	void ServerTrace::remove( ) {
		m_spatial_query.remove( );
		m_original = nullptr;
		m_metrics  = nullptr;
		if( m_instance == this )
			m_instance = nullptr;
	}

	void __fastcall ServerTrace::SpatialQuery( void* self, void*, void* move_data ) {
		ServerTrace* instance = m_instance;
		if( !instance || !instance->m_original )
			return;

		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			instance->m_original( self, move_data );
			return;
		}

		Stopwatch timer;
		instance->m_original( self, move_data );
		instance->m_metrics->record_portal_movement_spatial( timer.elapsed_ms( ) );
	}

}
