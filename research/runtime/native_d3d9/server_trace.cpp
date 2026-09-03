#include "server_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2_research.h"
#include "qpc.h"

#include <Windows.h>
#include <cstdint>
#include <vector>

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
		const Address laser	 = pattern::find_unique( server, portal2::signature::portal_laser_think );
		if( !server || !target || !slot || !laser )
			return false;

		m_metrics  = metrics;
		m_instance = this;
		const std::vector< std::uint8_t > laser_bytes{ 0x55, 0x8b, 0xec, 0x83, 0xec, 0x2c };
		if( !m_portal_laser_think.install( laser.as< void* >( ), laser_bytes, reinterpret_cast< void* >( &PortalLaserThink ) ) ||
			!m_spatial_query.install( slot.as< void** >( ), reinterpret_cast< void* >( &SpatialQuery ) ) ) {
			remove( );
			return false;
		}

		m_original = reinterpret_cast< SpatialQuery_t >( m_spatial_query.previous( ) );
		return m_original != nullptr;
#endif
	}

	void ServerTrace::remove( ) {
		m_spatial_query.remove( );
		m_portal_laser_think.remove( );
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

	void __fastcall ServerTrace::PortalLaserThink( void* self, void* ) {
		ServerTrace* instance = m_instance;
		auto		 previous = instance ? reinterpret_cast< PortalLaserThink_t >( instance->m_portal_laser_think.previous( ) ) : nullptr;
		if( !previous )
			return;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			previous( self );
			return;
		}

		Stopwatch timer;
		previous( self );
		instance->m_metrics->record_portal_laser( timer.elapsed_ms( ) );
	}

}
