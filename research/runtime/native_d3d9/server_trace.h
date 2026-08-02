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
			return m_spatial_query.installed( ) && m_portal_laser_think.installed( );
		}

	private:
		using SpatialQuery_t	 = void( __thiscall* )( void*, void* );
		using PortalLaserThink_t = void( __thiscall* )( void* );

		static void __fastcall SpatialQuery( void* self, void*, void* move_data );
		static void __fastcall PortalLaserThink( void* self, void* );

		static ServerTrace* m_instance;
		VtableSlotHook		m_spatial_query;
		InlineDetour		m_portal_laser_think;
		SpatialQuery_t		m_original{ };
		Metrics*			m_metrics{ };
	};

}
