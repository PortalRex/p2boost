#pragma once

#include "hook.h"
#include "material_buckets.h"
#include "portal2_context.h"

#include <atomic>

namespace p2boost {

	// return every surface element linked since the last flush to the free list.
	// AddElement makes each new element the head of its sort id's chain and
	// records the sort id on its first use, so the chains of the used sort ids
	// are exactly the elements the stock multilist RemoveAll would release
	// after scanning every slot ever allocated. call after incrementing the
	// flush count; returns false when only the stock scan is exact.
	bool release_used_surface_elements( material_buckets_t& buckets ) noexcept;

	// replaces CShadowMgr::ClearAllFlashlightMaterialBuckets. the engine flushes
	// every flashlight's buckets twice per world-list draw for every view, and
	// the stock flush costs the bucket's allocation high-water mark each time.
	class FlashlightBuckets {
	public:
		~FlashlightBuckets( );

		bool install( const portal2_modules_t& modules );
		void remove( );

		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_entry.installed( );
		}

	private:
		using remove_all_fn_t = void( __thiscall* )( utl_linked_list_t* );

		static void __fastcall ClearAll( void* shadow_manager, void* );

		static FlashlightBuckets* m_instance;

		BytePatch        m_entry;
		remove_all_fn_t  m_remove_all_elements{ };
		remove_all_fn_t  m_remove_all_sort_ids{ };
		std::atomic_bool m_enabled{ };
	};

}
