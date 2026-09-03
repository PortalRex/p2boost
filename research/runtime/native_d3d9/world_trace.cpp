#include "world_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2_research.h"
#include "qpc.h"
#include "x86.h"

#include <Windows.h>

#include <cstdint>
#include <vector>

namespace p2boost {

	namespace {

		// FNV-1a provides a compact equality check for the decompressed bitset.
		std::uint64_t hash_bytes( const std::uint8_t* bytes, size_t size ) noexcept {
			std::uint64_t hash = 14695981039346656037ull;
			for( size_t index = 0; index < size; ++index ) {
				hash ^= bytes[ index ];
				hash *= 1099511628211ull;
			}
			return hash;
		}

	}

	WorldTrace* WorldTrace::m_instance{ };

	WorldTrace::~WorldTrace( ) {
		remove( );
	}

	bool WorldTrace::install( Metrics* metrics ) {
#ifndef P2BOOST_RESEARCH_VISIBILITY_ANY
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

#if defined( P2BOOST_RESEARCH_WORLD_TRACE ) || defined( P2BOOST_RESEARCH_PVS_TRACE )
		const Address pvs = pattern::find_unique( engine, portal2::signature::pvs_decompress );
		if( !pvs ) {
			remove( );
			return false;
		}

		// Six displaced bytes end on an instruction boundary:
		// push ebp; mov ebp,esp; mov edx,[ebp+8].
		const std::vector< std::uint8_t > prologue{ 0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08 };
		if( !m_pvs_decompress.install( pvs.as< void* >( ), prologue, reinterpret_cast< void* >( &DecompressPvs ) ) ) {
			remove( );
			return false;
		}
		m_original_pvs = reinterpret_cast< DecompressPvs_t >( m_pvs_decompress.previous( ) );
#endif

#if defined( P2BOOST_RESEARCH_WORLD_TRACE ) || defined( P2BOOST_RESEARCH_RENDERABLE_TRACE )
		const Address client{ GetModuleHandleW( L"client.dll" ) };
		const Address renderable_list  = pattern::find_unique( client, portal2::signature::renderable_list_build );
		const Address view_filter	   = pattern::find_unique( client, portal2::signature::renderable_view_filter );
		const Address occlusion_filter = pattern::find_unique( client, portal2::signature::renderable_occlusion_filter );
		if( !client || !renderable_list || !view_filter || !occlusion_filter ) {
			remove( );
			return false;
		}

		// The outer builder's first eight bytes stop before its relative
		// __alloca_probe call, so its trampoline requires no relocation.
		const std::vector< std::uint8_t > renderable_list_prologue{ 0x55, 0x8b, 0xec, 0xb8, 0x44, 0xa0, 0x00, 0x00 };
		const auto*						  view_filter_bytes = view_filter.as< const std::uint8_t* >( );
		const std::vector< std::uint8_t > view_filter_prologue{ view_filter_bytes, view_filter_bytes + 8 };
		const std::vector< std::uint8_t > occlusion_filter_prologue{ 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08 };
		if( !m_renderable_list.install( renderable_list.as< void* >( ),
				renderable_list_prologue,
				reinterpret_cast< void* >( &BuildRenderableList ) ) ||
			!m_renderable_view_filter.install( view_filter.as< void* >( ),
				view_filter_prologue,
				reinterpret_cast< void* >( &FilterRenderablesByView ) ) ||
			!m_renderable_occlusion_filter.install( occlusion_filter.as< void* >( ),
				occlusion_filter_prologue,
				reinterpret_cast< void* >( &FilterRenderablesByOcclusion ) ) ) {
			remove( );
			return false;
		}
		m_original_renderable_list			   = reinterpret_cast< BuildRenderableList_t >( m_renderable_list.previous( ) );
		m_original_renderable_view_filter	   = reinterpret_cast< FilterRenderables_t >( m_renderable_view_filter.previous( ) );
		m_original_renderable_occlusion_filter = reinterpret_cast< FilterRenderables_t >( m_renderable_occlusion_filter.previous( ) );
#endif
		return true;
#endif
	}

	void WorldTrace::remove( ) {
		m_renderable_occlusion_filter.remove( );
		m_renderable_view_filter.remove( );
		m_renderable_list.remove( );
		m_pvs_decompress.remove( );
		m_call.remove( );
		m_original_renderable_occlusion_filter = nullptr;
		m_original_renderable_view_filter	   = nullptr;
		m_original_renderable_list			   = nullptr;
		m_original_pvs						   = nullptr;
		m_original							   = nullptr;
		m_metrics							   = nullptr;
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

	void __cdecl WorldTrace::DecompressPvs( void* model, int cluster, int type, std::uint8_t* output ) {
		WorldTrace* instance = m_instance;
		if( !instance || !instance->m_original_pvs )
			return;

		if( !instance->m_metrics || !instance->m_metrics->tracing( ) || !model || !output ) {
			instance->m_original_pvs( model, cluster, type, output );
			return;
		}

		Stopwatch timer;
		instance->m_original_pvs( model, cluster, type, output );

		// CM_Vis emits one bit per BSP cluster. Reject corrupt research input
		// before reading beyond the engine's fixed 0x2000-byte PVS workspace.
		const auto* bytes		  = static_cast< const std::uint8_t* >( model );
		const int	cluster_count = *reinterpret_cast< const int* >( bytes + 0x234 );
		if( cluster_count < 0 )
			return;
		const size_t output_size = ( static_cast< size_t >( cluster_count ) + 7 ) / 8;
		if( output_size > 0x2000 )
			return;

		instance->m_metrics->record_pvs_decompress( timer.elapsed_ms( ),
			reinterpret_cast< std::uintptr_t >( model ),
			cluster,
			type,
			output_size,
			hash_bytes( output, output_size ) );
	}

	void __fastcall WorldTrace::BuildRenderableList( void* self, void*, int* view ) {
		WorldTrace* instance = m_instance;
		if( !instance || !instance->m_original_renderable_list )
			return;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			instance->m_original_renderable_list( self, view );
			return;
		}

		Stopwatch timer;
		instance->m_original_renderable_list( self, view );
		instance->m_metrics->record_renderable_list( timer.elapsed_ms( ) );
	}

	int __fastcall WorldTrace::FilterRenderablesByView(
		void* self, void*, int count, std::uintptr_t renderables, std::uintptr_t metadata ) {
		WorldTrace* instance = m_instance;
		if( !instance || !instance->m_original_renderable_view_filter )
			return count;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return instance->m_original_renderable_view_filter( self, count, renderables, metadata );

		Stopwatch timer;
		const int result = instance->m_original_renderable_view_filter( self, count, renderables, metadata );
		instance->m_metrics->record_renderable_filter( false, timer.elapsed_ms( ), count, result );
		return result;
	}

	int __fastcall WorldTrace::FilterRenderablesByOcclusion(
		void* self, void*, int count, std::uintptr_t renderables, std::uintptr_t metadata ) {
		WorldTrace* instance = m_instance;
		if( !instance || !instance->m_original_renderable_occlusion_filter )
			return count;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return instance->m_original_renderable_occlusion_filter( self, count, renderables, metadata );

		// Metadata records are 28 bytes; bit zero of the final byte gates the
		// engine's expensive occlusion test for that candidate.
		unsigned int eligible = 0;
		if( count > 0 && metadata ) {
			const auto* entries = reinterpret_cast< const std::uint8_t* >( metadata );
			for( int index = 0; index < count; ++index ) {
				if( ( entries[ static_cast< size_t >( index ) * 28 + 27 ] & 1 ) != 0 )
					++eligible;
			}
		}

		Stopwatch timer;
		const int result = instance->m_original_renderable_occlusion_filter( self, count, renderables, metadata );
		instance->m_metrics->record_renderable_filter( true, timer.elapsed_ms( ), count, result, eligible );
		return result;
	}

}
