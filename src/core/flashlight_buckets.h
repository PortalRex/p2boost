#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace p2boost {

	// tier1 CUtlLinkedList< T, unsigned short > header in build 23973718.
	struct utl_linked_list_t {
		std::uint8_t* m_memory;
		int           m_allocation_count;
		int           m_grow_size;
		std::uint16_t m_head;
		std::uint16_t m_tail;
		std::uint16_t m_first_free;
		std::uint16_t m_element_count;
		std::uint16_t m_num_allocated;
		std::uint16_t m_last_alloc;
		void*         m_debug_elements;
	};

	// one list node: the element followed by the previous/next indices.
	template < typename element_t >
	struct utl_list_node_t {
		element_t     m_element;
		std::uint16_t m_previous;
		std::uint16_t m_next;
	};

	// engine CMaterialsBuckets< SurfaceHandle_t >::MaterialSortInfo_t.
	struct material_sort_info_t {
		int           m_flush_count;
		std::uint16_t m_head;
		std::uint16_t m_padding;
	};

	// engine CMaterialsBuckets< SurfaceHandle_t >, the per-flashlight list of
	// world surfaces grouped by material sort id.
	struct material_buckets_t {
		utl_linked_list_t     m_used_sort_ids;
		material_sort_info_t* m_sort_info;
		int                   m_sort_info_allocation_count;
		int                   m_sort_info_grow_size;
		int                   m_sort_info_size;
		void*                 m_sort_info_debug_elements;
		utl_linked_list_t     m_elements;
		int                   m_flush_count;
	};

	using sort_id_node_t = utl_list_node_t< std::uint16_t >;
	using surface_node_t = utl_list_node_t< void* >;

	static_assert( sizeof( utl_linked_list_t ) == 0x1c, "CUtlLinkedList ABI changed" );
	static_assert( sizeof( sort_id_node_t ) == 6, "CUtlLinkedList< unsigned short > node ABI changed" );
	static_assert( sizeof( surface_node_t ) == 8, "CUtlLinkedList< SurfaceHandle_t > node ABI changed" );
	static_assert( offsetof( material_buckets_t, m_elements ) == 0x30, "CMaterialsBuckets ABI changed" );
	static_assert( offsetof( material_buckets_t, m_flush_count ) == 0x4c, "CMaterialsBuckets ABI changed" );

	inline constexpr std::uint16_t invalid_list_index{ 0xffff };

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
