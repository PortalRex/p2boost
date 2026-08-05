#pragma once

#include "hook.h"
#include "portal2.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace p2boost {

	class FrameMetrics;
	namespace sdk {
		class ShadowManager;
	}

	// Reuse a projected-shadow receiver list only within one simulation
	// generation. Dirty-list cleanup still runs on every cache hit.
	class ProjectedShadowCache {
	public:
		~ProjectedShadowCache( );

		bool install( FrameMetrics* metrics );
		void remove( );
		void invalidate( ) noexcept;
		void set_enabled( bool enabled ) noexcept;

		bool installed( ) const noexcept {
			return m_update_projected.installed( ) && m_build_active_depth.installed( ) && m_frame_stage.installed( );
		}

		unsigned int hit_count( ) const noexcept {
			return m_hits.load( std::memory_order_relaxed );
		}

		unsigned int culled_count( ) const noexcept {
			return m_depth_culls.load( std::memory_order_relaxed );
		}

	private:
		struct cached_state_t {
			std::uint16_t					  m_handle{ };
			std::array< std::uint8_t, 0x114 > m_entry{ };
			int								  m_flashlight_models_value{ };
		};

		using UpdateProjectedFn	 = void( __thiscall* )( void*, unsigned int, bool );
		using RemoveDirtyFn		 = void( __thiscall* )( void*, unsigned int );
		using BuildActiveDepthFn = int( __thiscall* )( void*, const void*, int, std::uint16_t*, int* );
		using FrameStageFn		 = void( __thiscall* )( void*, int );

		static void __fastcall UpdateProjected( void* self, void*, unsigned int handle, bool force );
		static int __fastcall BuildActiveDepthList(
			void* self, void*, const void* view, int maximum, std::uint16_t* output, int* high_priority_count );
		static void __fastcall FrameStageNotify( void* self, void*, int stage );

		static ProjectedShadowCache* m_instance;

		VmtHook						  m_update_projected;
		CallSitePatch				  m_build_active_depth;
		VmtHook						  m_frame_stage;
		FrameMetrics*				  m_metrics{ };
		std::vector< cached_state_t > m_states;
		std::mutex					  m_state_mutex;
		std::uint64_t				  m_generation{ };
		int**						  m_flashlight_models_cvar_parent{ };
		RemoveDirtyFn				  m_remove_dirty{ };
		BuildActiveDepthFn			  m_original_build_active_depth{ };
		sdk::ShadowManager**		  m_shadow_manager_parent{ };
		std::atomic_bool			  m_enabled{ };
		std::atomic_uint			  m_hits{ };
		std::atomic_uint			  m_depth_culls{ };
	};

}
