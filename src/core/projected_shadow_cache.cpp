#include "projected_shadow_cache.h"

#include "address.h"
#include "frame_metrics.h"
#include "pattern.h"
#include "qpc.h"

#include <Windows.h>

#include <cstring>

namespace p2boost {

	ProjectedShadowCache* ProjectedShadowCache::m_instance{ };

	ProjectedShadowCache::~ProjectedShadowCache( ) {
		remove( );
	}

	bool ProjectedShadowCache::install( FrameMetrics* metrics ) {
		if( m_instance || !metrics )
			return false;

		const Address client{ GetModuleHandleW( L"client.dll" ) };
		if( !client )
			return false;

		// this xref loads Portal 2's private CClientShadowMgr singleton.
		const Address manager_xref = pattern::find_unique( client, portal2::signature::shadow_manager );
		const Address manager	   = manager_xref.add( portal2::xref::shadow_manager_object ).at< Address >( );
		auto**		  vmt		   = manager.to< void** >( );
		if( !manager || !vmt )
			return false;

		// method 24 is CClientShadowMgr::UpdateProjectedTexture. its second direct
		// call removes the handle from both dirty trees and must never be skipped.
		const Address update_projected{ vmt[ portal2::vmt::update_projected ] };
		const Address remove_dirty_call = update_projected.add( portal2::shadow::remove_dirty_call );
		if( remove_dirty_call.at< std::uint8_t >( ) != 0xe8 )
			return false;
		m_remove_dirty = remove_dirty_call.rel32( 1 ).as< RemoveDirtyFn >( );

		// flashlight-model membership changes generated receiver geometry even if
		// the shadow entry itself is unchanged, so it is part of the cache key.
		const Address models_xref  = pattern::find_unique( client, portal2::signature::flashlight_models );
		m_flashlight_models_parent = models_xref.add( portal2::xref::flashlight_models_global ).at< Address >( ).as< int** >( );
		if( !m_remove_dirty || !m_flashlight_models_parent || !*m_flashlight_models_parent )
			return false;

		m_metrics = metrics;
		m_states.reserve( 16 );
		m_instance = this;

		if( !m_update_projected.install( vmt + portal2::vmt::update_projected, reinterpret_cast< void* >( &UpdateProjected ) ) ) {
			m_instance = nullptr;
			m_metrics  = nullptr;
			return false;
		}

		return true;
	}

	void ProjectedShadowCache::remove( ) {
		m_update_projected.remove( );
		invalidate( );
		m_metrics				   = nullptr;
		m_flashlight_models_parent = nullptr;
		m_remove_dirty			   = nullptr;
		m_enabled.store( false, std::memory_order_relaxed );
		m_hits.store( 0, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	void ProjectedShadowCache::invalidate( ) noexcept {
		m_states.clear( );
	}

	void ProjectedShadowCache::set_enabled( bool enabled ) noexcept {
		// cached state must not cross a patched/unpatched A/B transition.
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) != enabled )
			invalidate( );
	}

	void __fastcall ProjectedShadowCache::UpdateProjected( void* self, void*, unsigned int handle, bool force ) {
		// recover the chain target before inspecting any reversed object fields.
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return;

		auto original = reinterpret_cast< UpdateProjectedFn >( cache->m_update_projected.previous( ) );
		if( !original )
			return;

		// 0xffff is Source's invalid projected-texture handle.
		if( ( handle & 0xffffu ) == 0xffffu ) {
			original( self, handle, force );
			return;
		}

		// resolve the shadow record from the verified manager layout.
		auto* entries = *reinterpret_cast< std::uint8_t** >( static_cast< std::uint8_t* >( self ) + portal2::shadow::entries );
		if( !entries ) {
			original( self, handle, force );
			return;
		}

		const auto	index = static_cast< std::uint16_t >( handle & 0xffffu );
		const auto* entry = entries + static_cast< size_t >( index ) * portal2::shadow::entry_size;
		auto		prior = cache->m_states.end( );
		for( auto state = cache->m_states.begin( ); state != cache->m_states.end( ); ++state ) {
			if( state->m_handle == index ) {
				prior = state;
				break;
			}
		}

		// include flashlight receiver membership in the cache key.
		int* models_owner = *cache->m_flashlight_models_parent;
		int	 model_count  = 0;
		if( models_owner ) {
			auto* model_bytes = reinterpret_cast< std::uint8_t* >( models_owner );
			model_count		  = *reinterpret_cast< int* >( model_bytes + portal2::shadow::flashlight_models );
		}
		const bool known_state = prior != cache->m_states.end( );
		const bool same_models = known_state && prior->m_flashlight_models == model_count;
		const bool same_entry  = known_state && std::memcmp( prior->m_entry.data( ), entry, prior->m_entry.size( ) ) == 0;
		const bool identical   = same_models && same_entry;

		const bool		collecting = cache->m_metrics && cache->m_metrics->collecting( );
		const Stopwatch timer{ collecting };

		// skip geometry rebuild only when every renderer-visible input is identical.
		if( cache->m_enabled.load( std::memory_order_relaxed ) && identical ) {
			cache->m_remove_dirty( self, handle );
			cache->m_hits.fetch_add( 1, std::memory_order_relaxed );
			if( collecting )
				cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), true );
			return;
		}

		// changed state follows the untouched engine path before becoming cacheable.
		original( self, handle, force );
		if( collecting )
			cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), false );

		if( prior == cache->m_states.end( ) ) {
			cache->m_states.push_back( { } );
			prior			= cache->m_states.end( ) - 1;
			prior->m_handle = index;
		}

		std::memcpy( prior->m_entry.data( ), entry, prior->m_entry.size( ) );
		prior->m_flashlight_models = model_count;
	}

}
