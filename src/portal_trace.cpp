#include "portal_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2.h"
#include "qpc.h"
#include "x86.h"

#include <Windows.h>

namespace p2boost {

	PortalTrace* PortalTrace::m_instance = nullptr;

	PortalTrace::~PortalTrace( ) {
		remove( );
	}

	bool PortalTrace::install( Metrics* metrics ) {
		if( m_instance || !metrics )
			return false;

		Address client{ GetModuleHandleW( L"client.dll" ) };
		if( !client )
			return false;

		// resolve both independent CViewRender xrefs.
		Address primary_call = pattern::find_unique( client, portal2::signature::primary_portal_render )
								   .add( portal2::xref::primary_portal_call );
		Address secondary_call = pattern::find_unique( client, portal2::signature::secondary_portal_render )
									 .add( portal2::xref::secondary_portal_call );

		// both calls must still reach one internal portal renderer.
		Address portal_render{ primary_call.rel32( 1 ) };
		if( primary_call.at< std::uint8_t >( ) != 0xe8 || secondary_call.at< std::uint8_t >( ) != 0xe8 ||
			!portal_render || secondary_call.rel32( 1 ) != portal_render )
			return false;

		m_original = portal_render.as< PortalRender_t >( );
		m_metrics  = metrics;
		m_instance = this;

		if( !m_primary_view.install( primary_call.as< void* >( ),
				x86::relative_call( primary_call, portal_render ),
				reinterpret_cast< void* >( &PortalRender ) ) ||
			!m_secondary_view.install( secondary_call.as< void* >( ),
				x86::relative_call( secondary_call, portal_render ),
				reinterpret_cast< void* >( &PortalRender ) ) ) {
			remove( );
			return false;
		}

		return true;
	}

	void PortalTrace::remove( ) {
		m_secondary_view.remove( );
		m_primary_view.remove( );
		m_metrics  = nullptr;
		m_original = nullptr;

		if( m_instance == this )
			m_instance = nullptr;
	}

	void __fastcall PortalTrace::PortalRender( void* self, void*, void* view, unsigned int clear_flags ) {
		PortalTrace* instance = m_instance;
		if( !instance || !instance->m_original )
			return;

		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			instance->m_original( self, view, clear_flags );
			return;
		}

		// time only the original portal renderer.
		Stopwatch timer;
		instance->m_original( self, view, clear_flags );
		instance->m_metrics->record_portal_render( timer.elapsed_ms( ) );
	}

}
