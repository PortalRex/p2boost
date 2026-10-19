#include "projected_shadow_cache.h"

#include "address.h"
#include "client_shadow.h"
#include "client_view.h"
#include "frame_metrics.h"
#include "projected_frustum.h"
#include "qpc.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace p2boost {

	ProjectedShadowCache* ProjectedShadowCache::m_instance{ };

	ProjectedShadowCache::~ProjectedShadowCache( ) {
		remove( );
	}

	bool ProjectedShadowCache::install(
	    const portal2_modules_t& modules, const Portal2Interfaces& interfaces, FrameMetrics* metrics ) {
		if( m_instance || !metrics || !modules.m_client || !modules.m_engine )
			return false;

		// VClient016 is the client lifecycle authority on every machine. Unlike
		// server-plugin GameFrame, this interface also exists for remote Orange.
		auto* base_client = interfaces.base_client( );
		if( !base_client )
			return false;

		// a client ragdoll moves its source entity's model instance through this
		// public engine ABI. the supported engine already runs Valve's
		// RemoveAllShadowsFromModel fix inside ChangeInstance; this hook exists only
		// to invalidate p2boost's separate client receiver cache at that ownership
		// boundary.
		auto* model_render = interfaces.model_render( );
		if( !model_render )
			return false;

		// validate every recovered slot against its fingerprinted owner image.
		if( !modules.m_client.contains( base_client->FrameStageNotifyAddress( ) ) ||
		    !modules.m_engine.contains( model_render->ChangeInstanceAddress( ) ) ) {
			return false;
		}

		// this xref loads Portal 2's private CClientShadowMgr singleton.
		const Address                      manager_xref = modules.m_client.find( game::active( )->shadow_manager );
		const Address                      manager      = manager_xref.at< Address >( game::active( )->xref_shadow_manager_object );
		const client_shadow_manager_view_t manager_view{ manager.as< void* >( ) };
		void**                             update_projected_slot = manager_view.update_projected_slot( );
		void**                             destroy_shadow_slot   = manager_view.destroy_shadow_slot( );
		if( !manager_view || !update_projected_slot || !destroy_shadow_slot ||
		    !modules.m_client.contains( *destroy_shadow_slot ) )
			return false;

		// method 24 is CClientShadowMgr::UpdateProjectedTexture. its second direct
		// call removes the handle from both dirty trees and must never be skipped.
		const Address update_projected{ *update_projected_slot };
		const Address remove_dirty_call = update_projected.add( game::active( )->shadow_remove_dirty_call );
		if( remove_dirty_call.at< std::uint8_t >( ) != 0xe8 )
			return false;
		m_remove_dirty = remove_dirty_call.rel32( 1 ).as< remove_dirty_fn_t >( );

		// the private ConVar value controls model receiver projection even when
		// the shadow entry is unchanged, so it remains part of the cache key.
		const Address models_xref       = modules.m_client.find( game::active( )->flashlight_models );
		m_flashlight_models_cvar_parent = models_xref.at< int** >( game::active( )->xref_flashlight_models_global );

		// hook only the verified call from ComputeShadowDepthTextures. calling the
		// engine selector first preserves every stock visibility, split-screen,
		// priority, and maximum-shadow rule before applying exact frustum culling.
		const Address active_call_xref = modules.m_client.find( game::active( )->active_shadow_depth_call );
		const Address active_call      = active_call_xref.add( game::active( )->xref_active_shadow_depth_call );
		const Address active_target    = active_call.rel32( 1 );
		const Address expected_target  = modules.m_client.find( game::active( )->active_shadow_depth_list );
		if( active_call.at< std::uint8_t >( ) != 0xe8 || active_target != expected_target )
			return false;

		const Address shadow_manager_xref = modules.m_client.find( game::active( )->active_shadow_manager );
		m_shadow_manager_parent           = shadow_manager_xref.at< sdk::IShadowMgr** >( game::active( )->xref_active_shadow_manager_global );
		if( !m_remove_dirty || !m_flashlight_models_cvar_parent || !*m_flashlight_models_cvar_parent ||
		    !m_shadow_manager_parent || !*m_shadow_manager_parent )
			return false;

		// optional: the depth-slot count ComputeShadowDepthTextures searches. a
		// mismatch only disables the stable depth order.
		const Address capacity  = modules.m_client.find( game::active( )->depth_slot_capacity );
		m_depth_capacity_offset = capacity ? capacity.at< std::int32_t >( game::active( )->xref_depth_slot_capacity ) : 0;
		if( m_depth_capacity_offset <= 0 || m_depth_capacity_offset > 0x1000 )
			m_depth_capacity_offset = 0;

		m_original_build_active_depth = active_target.as< build_active_depth_fn_t >( );
		std::vector< std::uint8_t > active_call_bytes( 5 );
		std::memcpy( active_call_bytes.data( ), active_call.as< const void* >( ), active_call_bytes.size( ) );

		m_metrics = metrics;
		m_states.reserve( 16 );
		m_instance = this;

		if( !m_change_instance.install( model_render->ChangeInstanceSlot( ), reinterpret_cast< void* >( &ChangeInstance ) ) ) {
			remove( );
			return false;
		}
		// slot 17 is the only path that frees a client shadow: the deferred
		// destroy queue drains back through this same vtable entry.
		if( !m_destroy_shadow.install( destroy_shadow_slot, reinterpret_cast< void* >( &DestroyShadow ) ) ) {
			remove( );
			return false;
		}
		if( !m_update_projected.install( update_projected_slot, reinterpret_cast< void* >( &UpdateProjected ) ) ) {
			remove( );
			return false;
		}
		if( !m_build_active_depth.install( active_call.as< void* >( ), active_call_bytes, reinterpret_cast< void* >( &BuildActiveDepthList ) ) ) {
			remove( );
			return false;
		}
		if( !m_frame_stage.install( base_client->FrameStageNotifySlot( ), reinterpret_cast< void* >( &FrameStageNotify ) ) ) {
			remove( );
			return false;
		}

		return true;
	}

	void ProjectedShadowCache::remove( ) {
		m_frame_stage.remove( );
		m_build_active_depth.remove( );
		m_update_projected.remove( );
		m_destroy_shadow.remove( );
		m_change_instance.remove( );
		invalidate_all( );
		m_metrics                       = nullptr;
		m_flashlight_models_cvar_parent = nullptr;
		m_remove_dirty                  = nullptr;
		m_original_build_active_depth   = nullptr;
		m_shadow_manager_parent         = nullptr;
		m_enabled.store( false, std::memory_order_relaxed );
		m_hits.store( 0, std::memory_order_relaxed );
		m_depth_culls.store( 0, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	bool __fastcall ProjectedShadowCache::ChangeInstance( void* self, void*, unsigned short handle, void* renderable ) {
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return false;

		auto original = cache->m_change_instance.previous< change_instance_fn_t >( );
		if( !original )
			return false;

		const bool changed = original( self, handle, renderable );
		if( !changed )
			return false;

		// Ghidra confirms the supported engine's original has already removed all
		// engine shadows before it returns success. The model move also changes
		// hidden client receiver ownership that is not part of ClientShadow_t, so
		// no p2boost cache entry may survive it.
		cache->invalidate_all( );
		return true;
	}

	void __fastcall ProjectedShadowCache::DestroyShadow( void* self, void*, unsigned int handle ) {
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return;

		auto original = cache->m_destroy_shadow.previous< destroy_shadow_fn_t >( );

		// forget the handle before the slot can be recycled. a recreated light
		// starts with no leaf projection even if its record bytes match.
		{
			const std::lock_guard lock{ cache->m_state_mutex };
			const auto            index = static_cast< std::uint16_t >( handle & 0xffffu );
			const auto            state = cache->find_state( index );
			if( state != cache->m_states.end( ) )
				cache->m_states.erase( state );
			++cache->m_epoch;
		}

		if( original )
			original( self, handle );
	}

	void __fastcall ProjectedShadowCache::FrameStageNotify( void* self, void*, int stage ) {
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return;

		auto original = cache->m_frame_stage.previous< frame_stage_fn_t >( );
		if( !original )
			return;

		// drop the previous receiver generation before any OnDataChanged handler
		// can replace a player with its goo-death ragdoll.
		if( stage == game::frame_stage_net_update_start )
			cache->invalidate( );

		original( self, stage );

		// drop any intermediate receiver list built while the packet was being
		// applied. Rendering begins with the final client entity topology only.
		if( stage == game::frame_stage_net_update_end )
			cache->invalidate( );
	}

	int __fastcall ProjectedShadowCache::BuildActiveDepthList(
	    void* self, void*, const void* view, int maximum, std::uint16_t* output, int* high_priority_count ) {
		ProjectedShadowCache* cache = m_instance;
		if( !cache || !cache->m_original_build_active_depth )
			return 0;

		// preserve Portal 2's complete stock selection before removing only
		// projected volumes proven disjoint from this exact rendered view.
		int selected = cache->m_original_build_active_depth( self, view, maximum, output, high_priority_count );
		if( !cache->m_enabled.load( std::memory_order_relaxed ) || selected <= 0 || !output || !high_priority_count )
			return selected;

		if( cache->m_exact_cull.load( std::memory_order_relaxed ) )
			selected = cache->cull_depth_list( self, view, selected, output, high_priority_count );

		// ComputeShadowDepthTextures hands each light the first free depth slot
		// in list order, and the stock list is sorted by facing, which differs
		// per view. When every light gets a slot anyway and none needs the
		// high-resolution range, a handle order is equivalent and gives each
		// light the same slot in every view of the frame, so a later view can
		// reuse the earlier depth map (see ShadowDepthReuse).
		if( cache->m_stable_depth_order.load( std::memory_order_relaxed ) && selected > 1 && *high_priority_count == 0 &&
		    cache->m_depth_capacity_offset > 0 &&
		    selected <= Address{ self }.at< int >( cache->m_depth_capacity_offset ) )
			std::sort( output, output + selected );

		return selected;
	}

	int ProjectedShadowCache::cull_depth_list(
	    void* self, const void* view, int selected, std::uint16_t* output, int* high_priority_count ) {
		ProjectedShadowCache* cache = this;
		const client_view_t   client_view{ view };
		if( !client_view )
			return selected;

		const frustum::camera_view_t camera_view{
		    client_view.origin( ), client_view.angles( ), client_view.near_z( ), client_view.far_z( ), client_view.fov( ), client_view.aspect_ratio( ) };
		frustum::frustum_t camera{ };
		if( !frustum::camera_frustum( camera_view, camera ) )
			return selected;

		const client_shadow_manager_view_t manager{ self };
		sdk::IShadowMgr*                   shadow_manager = cache->m_shadow_manager_parent ? *cache->m_shadow_manager_parent : nullptr;
		if( !shadow_manager )
			return selected;

		int retained               = 0;
		int retained_high_priority = *high_priority_count;
		for( int index = 0; index < selected; ++index ) {
			const std::uint16_t              handle = output[ index ];
			const client_shadow_entry_view_t entry  = manager.entry( handle );

			// the list may already be partly compacted, so an unreadable entry is
			// retained in place rather than abandoning the pass.
			frustum::frustum_t projected{ };
			if( entry && frustum::projected_frustum( entry.world_to_shadow( ), projected ) &&
			    !frustum::frusta_intersect( camera, projected ) ) {
				// removed high-priority lights must also release the cache slot that
				// Portal 2 reserved for them before this exact visibility test.
				const auto* state = static_cast< const std::uint8_t* >(
				    shadow_manager->GetFlashlightState( entry.engine_handle( ) ) );
				if( state && state[ game::active( )->shadow_high_priority ] != 0 && retained_high_priority > 0 )
					--retained_high_priority;

				// match the stock selector's own cull: without this the light keeps
				// last frame's depth slot, which this frame may hand to another light.
				shadow_manager->SetFlashlightDepthTexture( entry.engine_handle( ), nullptr, 0 );
				cache->m_depth_culls.fetch_add( 1, std::memory_order_relaxed );
				continue;
			}

			output[ retained++ ] = handle;
		}

		// Portal 2 reserves depth-cache slots for high-priority lights. Recounting
		// after compaction prevents a culled light from consuming that capacity.
		*high_priority_count = retained_high_priority;
		return retained;
	}

	ProjectedShadowCache::state_iterator_t ProjectedShadowCache::find_state( std::uint16_t handle ) noexcept {
		return std::find_if( m_states.begin( ), m_states.end( ), [ handle ]( const cached_state_t& state ) {
			return state.m_handle == handle;
		} );
	}

	void ProjectedShadowCache::invalidate( ) noexcept {
		// A generation prevents a rebuild finishing during invalidation from
		// publishing receiver state that belongs to the previous scene lifetime.
		// generation-scoped entries now mismatch; leaf-projected ones survive.
		const std::lock_guard lock{ m_state_mutex };
		++m_generation;
	}

	void ProjectedShadowCache::invalidate_all( ) noexcept {
		const std::lock_guard lock{ m_state_mutex };
		m_states.clear( );
		++m_generation;
		++m_epoch;
	}

	void ProjectedShadowCache::set_enabled( bool enabled ) noexcept {
		// cached state must not cross a patched/unpatched A/B transition.
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) != enabled )
			invalidate_all( );
	}

	void __fastcall ProjectedShadowCache::UpdateProjected( void* self, void*, unsigned int handle, bool force ) {
		// recover the chain target before inspecting any reversed object fields.
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return;

		auto original = cache->m_update_projected.previous< update_projected_fn_t >( );
		if( !original )
			return;

		// 0xffff is Source's invalid projected-texture handle.
		if( ( handle & 0xffffu ) == 0xffffu ) {
			original( self, handle, force );
			return;
		}

		// resolve the shadow record through the one build-specific layout view.
		const auto                         index = static_cast< std::uint16_t >( handle & 0xffffu );
		const client_shadow_manager_view_t manager{ self };
		client_shadow_entry_view_t         entry = manager.entry( index );
		if( !entry ) {
			original( self, handle, force );
			return;
		}

		// a projection built in the engine's leafless flashlight mode carries no
		// receivers, so neither reuse it nor record it.
		sdk::IShadowMgr* shadow_manager = cache->m_shadow_manager_parent ? *cache->m_shadow_manager_parent : nullptr;
		if( !shadow_manager || shadow_manager->LeaflessFlashlightMode( ) ) {
			original( self, handle, force );
			return;
		}

		// read the reversed `r_flashlightmodels` ConVar value used by this path.
		const int       models_value = flashlight_models_value( cache->m_flashlight_models_cvar_parent );
		const bool      collecting   = cache->m_metrics && cache->m_metrics->collecting( );
		const Stopwatch timer{ collecting };
		std::uint64_t   generation{ };
		std::uint64_t   epoch{ };

		{
			// invalidation and a cache hit must be one transaction. otherwise an
			// entity-lifecycle callback could clear the cache after comparison but
			// before the engine receiver rebuild is skipped.
			const std::lock_guard lock{ cache->m_state_mutex };
			generation = cache->m_generation;
			epoch      = cache->m_epoch;

			auto prior = cache->find_state( index );

			const bool known_state = prior != cache->m_states.end( );
			const bool same_models = known_state && prior->m_flashlight_models_value == models_value;
			const bool same_entry  = known_state && std::memcmp( prior->m_entry.data( ), entry.bytes( ), prior->m_entry.size( ) ) == 0;

			// a leaf-projected light hands its leaves to the client leaf system,
			// which re-adds the light to every renderable whose bounds move into
			// them, so its result outlives the update. a target-entity light
			// walks that entity's live children instead and stays update-scoped.
			const bool current = known_state && ( prior->m_persistent || prior->m_generation == generation );
			if( cache->m_enabled.load( std::memory_order_relaxed ) && current && same_models && same_entry ) {
				cache->m_remove_dirty( self, handle );
				cache->m_hits.fetch_add( 1, std::memory_order_relaxed );
				if( collecting )
					cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), true );
				return;
			}
		}

		// the first update in each generation always follows the vanilla path.
		original( self, handle, force );
		if( collecting )
			cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), false );

		// the original call may run engine callbacks. re-resolve the entry and
		// reject the result if any callback ended the lifetime it was built in.
		const std::lock_guard lock{ cache->m_state_mutex };
		entry = manager.entry( index );
		if( !entry )
			return;

		const bool persistent = !entry.has_target_entity( );
		if( cache->m_epoch != epoch || ( !persistent && cache->m_generation != generation ) )
			return;

		auto prior = cache->find_state( index );

		if( prior == cache->m_states.end( ) ) {
			cache->m_states.push_back( { } );
			prior           = cache->m_states.end( ) - 1;
			prior->m_handle = index;
		}

		std::memcpy( prior->m_entry.data( ), entry.bytes( ), prior->m_entry.size( ) );
		prior->m_flashlight_models_value = models_value;
		prior->m_generation              = generation;
		prior->m_persistent              = persistent;
	}

}
