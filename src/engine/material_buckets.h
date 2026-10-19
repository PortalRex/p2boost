#pragma once

#include "tier1.h"

#include <cstddef>
#include <cstdint>

namespace p2boost {

	// engine CMaterialsBuckets< SurfaceHandle_t >::MaterialSortInfo_t.
	struct material_sort_info_t {
		int           m_flush_count;
		std::uint16_t m_head;
		std::uint16_t m_padding;
	};

	// engine CMaterialsBuckets< SurfaceHandle_t >, the per-flashlight list of
	// world surfaces grouped by material sort id (MaterialBuckets.h).
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

	static_assert( sizeof( sort_id_node_t ) == 6, "CUtlLinkedList< unsigned short > node ABI changed" );
	static_assert( sizeof( surface_node_t ) == 8, "CUtlLinkedList< SurfaceHandle_t > node ABI changed" );
	static_assert( offsetof( material_buckets_t, m_elements ) == 0x30, "CMaterialsBuckets ABI changed" );
	static_assert( offsetof( material_buckets_t, m_flush_count ) == 0x4c, "CMaterialsBuckets ABI changed" );

}
