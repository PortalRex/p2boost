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
#ifdef P2BOOST_RESEARCH_VISIBILITY_ANY
			bool result = m_call.installed( );
#if defined( P2BOOST_RESEARCH_WORLD_TRACE ) || defined( P2BOOST_RESEARCH_PVS_TRACE )
			result = result && m_pvs_decompress.installed( );
#endif
#if defined( P2BOOST_RESEARCH_WORLD_TRACE ) || defined( P2BOOST_RESEARCH_RENDERABLE_TRACE )
			result = result && m_renderable_list.installed( ) && m_renderable_view_filter.installed( ) &&
					 m_renderable_occlusion_filter.installed( );
#endif
			return result;
#else
			return m_call.installed( );
#endif
		}

	private:
		using BuildSurfaces_t		= void( __cdecl* )( int*, int, unsigned int );
		using DecompressPvs_t		= void( __cdecl* )( void*, int, int, std::uint8_t* );
		using BuildRenderableList_t = void( __thiscall* )( void*, int* );
		using FilterRenderables_t	= int( __thiscall* )( void*, int, std::uintptr_t, std::uintptr_t );

		static void __cdecl BuildSurfaces( int* render_context, int world_list, unsigned int flags );
		static void __cdecl DecompressPvs( void* model, int cluster, int type, std::uint8_t* output );
		static void __fastcall BuildRenderableList( void* self, void*, int* view );
		static int __fastcall FilterRenderablesByView( void* self, void*, int count, std::uintptr_t renderables, std::uintptr_t metadata );
		static int __fastcall FilterRenderablesByOcclusion(
			void* self, void*, int count, std::uintptr_t renderables, std::uintptr_t metadata );

		static WorldTrace*	  m_instance;
		CallSitePatch		  m_call;
		InlineDetour		  m_pvs_decompress;
		InlineDetour		  m_renderable_list;
		InlineDetour		  m_renderable_view_filter;
		InlineDetour		  m_renderable_occlusion_filter;
		BuildSurfaces_t		  m_original{ };
		DecompressPvs_t		  m_original_pvs{ };
		BuildRenderableList_t m_original_renderable_list{ };
		FilterRenderables_t	  m_original_renderable_view_filter{ };
		FilterRenderables_t	  m_original_renderable_occlusion_filter{ };
		Metrics*			  m_metrics{ };
	};

}
