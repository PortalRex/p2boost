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
#ifdef P2BOOST_RESEARCH_WORLD_TRACE
			return m_call.installed( ) && m_pvs_decompress.installed( );
#else
			return m_call.installed( );
#endif
		}

	private:
		using BuildSurfaces_t = void( __cdecl* )( int*, int, unsigned int );
		using DecompressPvs_t = void( __cdecl* )( void*, int, int, std::uint8_t* );

		static void __cdecl BuildSurfaces( int* render_context, int world_list, unsigned int flags );
		static void __cdecl DecompressPvs( void* model, int cluster, int type, std::uint8_t* output );

		static WorldTrace* m_instance;
		CallSitePatch	   m_call;
		InlineDetour	   m_pvs_decompress;
		BuildSurfaces_t	   m_original{ };
		DecompressPvs_t	   m_original_pvs{ };
		Metrics*		   m_metrics{ };
	};

}
