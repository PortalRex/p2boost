#pragma once

#include "hook.h"
#include "game_profile.h"
#include "portal2_context.h"
#include "sdk.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace p2boost {

	class FrameMetrics;
	// reuse a byte-identical projected-light rebuild. lights projected through
	// the client leaf system stay cached until the light, map, or device changes:
	// the leaf system itself moves receivers between lit leaves. lights bound
	// to a target entity read its live child hierarchy, so their reuse stays
	// limited to one client-update generation. Dirty-list cleanup still runs on
	// every cache hit.
	class ProjectedShadowCache {
	public:
		~ProjectedShadowCache( );

		bool install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces, FrameMetrics* metrics );
		void remove( );

		// end the current client-update generation. only target-entity lights
		// depend on per-update entity state, so leaf-projected lights survive.
		void invalidate( ) noexcept;

		// drop every cached light: map, device, model-transfer, and A/B changes.
		void invalidate_all( ) noexcept;

		void set_enabled( bool enabled ) noexcept;

		// exact depth culling tests only the main camera, but stencil portal,
		// monitor, and reflection views reuse the main view's depth maps without
		// a selection of their own. it therefore stays opt-in.
		void set_exact_cull( bool enabled ) noexcept {
			m_exact_cull.store( enabled, std::memory_order_relaxed );
		}

		bool exact_cull( ) const noexcept {
			return m_exact_cull.load( std::memory_order_relaxed );
		}

		// hand selected lights to ComputeShadowDepthTextures in handle order
		// when that cannot change which lights receive depth slots.
		void set_stable_depth_order( bool enabled ) noexcept {
			m_stable_depth_order.store( enabled, std::memory_order_relaxed );
		}

		bool stable_depth_order_available( ) const noexcept {
			return m_depth_capacity_offset > 0;
		}

		bool installed( ) const noexcept {
			return m_update_projected.installed( ) && m_build_active_depth.installed( ) && m_frame_stage.installed( ) &&
			       m_change_instance.installed( ) && m_destroy_shadow.installed( );
		}

		unsigned int hit_count( ) const noexcept {
			return m_hits.load( std::memory_order_relaxed );
		}

		unsigned int culled_count( ) const noexcept {
			return m_depth_culls.load( std::memory_order_relaxed );
		}

	private:
		struct cached_state_t {
			std::uint16_t                                       m_handle{ };
			std::array< std::uint8_t, game::shadow_entry_size > m_entry{ };
			int                                                 m_flashlight_models_value{ };
			std::uint64_t                                       m_generation{ };
			bool                                                m_persistent{ };
		};

		using state_iterator_t = std::vector< cached_state_t >::iterator;

		using update_projected_fn_t   = void( __thiscall* )( void*, unsigned int, bool );
		using remove_dirty_fn_t       = void( __thiscall* )( void*, unsigned int );
		using build_active_depth_fn_t = int( __thiscall* )( void*, const void*, int, std::uint16_t*, int* );
		using frame_stage_fn_t        = void( __thiscall* )( void*, int );
		using change_instance_fn_t    = bool( __thiscall* )( void*, unsigned short, void* );
		using destroy_shadow_fn_t     = void( __thiscall* )( void*, unsigned int );

		static void __fastcall UpdateProjected( void* self, void*, unsigned int handle, bool force );
		static void __fastcall DestroyShadow( void* self, void*, unsigned int handle );
		static int __fastcall BuildActiveDepthList(
		    void* self, void*, const void* view, int maximum, std::uint16_t* output, int* high_priority_count );
		static void __fastcall FrameStageNotify( void* self, void*, int stage );
		static bool __fastcall ChangeInstance( void* self, void*, unsigned short handle, void* renderable );
		state_iterator_t find_state( std::uint16_t handle ) noexcept;
		int cull_depth_list( void* self, const void* view, int selected, std::uint16_t* output, int* high_priority_count );

		static ProjectedShadowCache* m_instance;

		VmtHook                       m_update_projected;
		CallSitePatch                 m_build_active_depth;
		VmtHook                       m_frame_stage;
		VmtHook                       m_change_instance;
		VmtHook                       m_destroy_shadow;
		FrameMetrics*                 m_metrics{ };
		std::vector< cached_state_t > m_states;
		std::mutex                    m_state_mutex;
		std::uint64_t                 m_generation{ };
		std::uint64_t                 m_epoch{ };
		int**                         m_flashlight_models_cvar_parent{ };
		remove_dirty_fn_t             m_remove_dirty{ };
		build_active_depth_fn_t       m_original_build_active_depth{ };
		sdk::IShadowMgr**             m_shadow_manager_parent{ };
		std::atomic_bool              m_enabled{ };
		std::atomic_bool              m_exact_cull{ };
		std::atomic_bool              m_stable_depth_order{ };
		std::ptrdiff_t                m_depth_capacity_offset{ };
		std::atomic_uint              m_hits{ };
		std::atomic_uint              m_depth_culls{ };
	};

}
