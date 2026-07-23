#pragma once

#include "hook.h"

namespace p2boost {

	class Metrics;

	// Research-only timer for CPortalGameMovement's spatial query.
	class ServerTrace {
	public:
		~ServerTrace( );

		bool install( Metrics* metrics );
		void remove( );

		bool installed( ) const noexcept {
			return m_spatial_query.installed( );
		}

	private:
		using SpatialQuery_t = void( __thiscall* )( void*, void* );

		static void __fastcall SpatialQuery( void* self, void*, void* move_data );

		static ServerTrace* m_instance;
		VtableSlotHook		m_spatial_query;
		SpatialQuery_t		m_original{ };
		Metrics*			m_metrics{ };
	};

}
