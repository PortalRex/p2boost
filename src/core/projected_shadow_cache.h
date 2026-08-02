#pragma once

#include "hook.h"
#include "portal2.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace p2boost {

	class FrameMetrics;

	// reuse a projected-shadow receiver list only when every renderer-visible
	// input is byte-identical. Dirty-list cleanup still runs on every cache hit.
	class ProjectedShadowCache {
	public:
		~ProjectedShadowCache( );

		bool install( FrameMetrics* metrics );
		void remove( );
		void invalidate( ) noexcept;
		void set_enabled( bool enabled ) noexcept;

		bool installed( ) const noexcept {
			return m_update_projected.installed( );
		}

		unsigned int hit_count( ) const noexcept {
			return m_hits.load( std::memory_order_relaxed );
		}

	private:
		struct cached_state_t {
			std::uint16_t					  m_handle{ };
			std::array< std::uint8_t, 0x114 > m_entry{ };
			int								  m_flashlight_models{ };
		};

		using UpdateProjectedFn = void( __thiscall* )( void*, unsigned int, bool );
		using RemoveDirtyFn		= void( __thiscall* )( void*, unsigned int );

		static void __fastcall UpdateProjected( void* self, void*, unsigned int handle, bool force );

		static ProjectedShadowCache* m_instance;

		VmtHook						  m_update_projected;
		FrameMetrics*				  m_metrics{ };
		std::vector< cached_state_t > m_states;
		int**						  m_flashlight_models_parent{ };
		RemoveDirtyFn				  m_remove_dirty{ };
		std::atomic_bool			  m_enabled{ };
		std::atomic_uint			  m_hits{ };
	};

}
