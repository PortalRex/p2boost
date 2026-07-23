#pragma once

#include "hook.h"

namespace p2boost {

	class Metrics;

	// Research-only timing for main-thread BSP surface construction.
	class WorldTrace {
	public:
		~WorldTrace( );

		bool install( Metrics* metrics );
		void remove( );

		bool installed( ) const noexcept {
			return m_call.installed( );
		}

	private:
		using BuildSurfaces_t = void( __cdecl* )( int*, int, unsigned int );

		static void __cdecl BuildSurfaces( int* render_context, int world_list, unsigned int flags );

		static WorldTrace* m_instance;
		CallSitePatch	   m_call;
		BuildSurfaces_t	   m_original{ };
		Metrics*		   m_metrics{ };
	};

}
