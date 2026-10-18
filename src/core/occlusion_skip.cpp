#include "occlusion_skip.h"

#include "address.h"
#include "game_profile.h"

#include <vector>

namespace p2boost {

	OcclusionSkip* OcclusionSkip::m_instance{ };

	bool occlusion_pass_redundant( const void* world_brush, std::ptrdiff_t occluder_count_offset ) noexcept {
		// without a loaded world, defer to the stock pass.
		return world_brush && Address{ world_brush }.at< int >( occluder_count_offset ) <= 0;
	}

	OcclusionSkip::~OcclusionSkip( ) {
		remove( );
	}

	bool OcclusionSkip::install( const portal2_modules_t& modules ) {
		if( m_instance || m_call.installed( ) || !modules.m_engine || !modules.m_client )
			return false;

		// RecomputeOccluderEdgeList reads host_state.worldbrush and walks its
		// occluders; the signature also pins the numoccluders offset.
		const game::profile_t* game  = game::active( );
		const Address          world = modules.m_engine.find( game->world_occluder_count );
		m_world_brush                = world.at< void* const* >( game->xref_world_brush_pointer );
		if( !m_world_brush )
			return false;

		// the only call to ExtractOccludedRenderables, inside BuildRenderablesList.
		const Address site   = modules.m_client.find( game->occluded_renderables_call );
		const Address call   = site.add( game->xref_occluded_renderables_call );
		const Address target = call.rel32( 1 );
		if( !site || !modules.m_client.matches( target, game->occluded_renderables ) )
			return false;

		const std::uint8_t*               bytes = call.as< const std::uint8_t* >( );
		const std::vector< std::uint8_t > expected( bytes, bytes + 5 );
		m_original = target.as< extract_fn_t >( );
		m_instance = this;
		if( !m_call.install( call.as< void* >( ), expected, reinterpret_cast< void* >( &ExtractOccluded ) ) ) {
			m_instance = nullptr;
			m_original = nullptr;
			return false;
		}

		return true;
	}

	void OcclusionSkip::remove( ) {
		m_call.remove( );
		m_enabled.store( false, std::memory_order_relaxed );
		m_original    = nullptr;
		m_world_brush = nullptr;

		if( m_instance == this )
			m_instance = nullptr;
	}

	int __fastcall OcclusionSkip::ExtractOccluded( void* leaf_system, void*, int count, void* renderables, void* infos ) {
		OcclusionSkip* instance = m_instance;
		if( !instance || !instance->m_original )
			return count;

		// with no occluders every candidate survives in place, so returning the
		// input count reproduces the stock compaction exactly.
		if( instance->enabled( ) && occlusion_pass_redundant( *instance->m_world_brush, game::active( )->world_occluder_count_offset ) ) {
			instance->m_skipped.fetch_add( 1, std::memory_order_relaxed );
			return count;
		}

		return instance->m_original( leaf_system, nullptr, count, renderables, infos );
	}

}
