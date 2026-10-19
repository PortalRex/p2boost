#pragma once

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

	// one list node: the element followed by the previous/next indices. a free
	// node points at itself through m_previous and chains through m_next.
	template < typename element_t >
	struct utl_list_node_t {
		element_t     m_element;
		std::uint16_t m_previous;
		std::uint16_t m_next;
	};

	static_assert( sizeof( utl_linked_list_t ) == 0x1c, "CUtlLinkedList ABI changed" );

	inline constexpr std::uint16_t invalid_list_index{ 0xffff };

}
