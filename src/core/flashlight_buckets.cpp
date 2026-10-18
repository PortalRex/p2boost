#include "flashlight_buckets.h"

#include "address.h"
#include "game_profile.h"

#include <vector>

namespace p2boost {

	FlashlightBuckets* FlashlightBuckets::m_instance{ };

	bool release_used_surface_elements( material_buckets_t& buckets ) noexcept {
		// the buckets and every sort id start with a flush count of -1, so before
		// the first flush AddElement links surfaces without recording their sort
		// id. only the stock full scan can find those.
		if( buckets.m_flush_count == 0 )
			return false;

		utl_linked_list_t& used     = buckets.m_used_sort_ids;
		utl_linked_list_t& elements = buckets.m_elements;
		auto*              sort_ids = reinterpret_cast< sort_id_node_t* >( used.m_memory );
		auto*              surfaces = reinterpret_cast< surface_node_t* >( elements.m_memory );

		for( std::uint16_t used_index = used.m_head; used_index != invalid_list_index; used_index = sort_ids[ used_index ].m_next ) {
			const std::uint16_t sort_id = sort_ids[ used_index ].m_element;
			for( std::uint16_t element = buckets.m_sort_info[ sort_id ].m_head; element != invalid_list_index; ) {
				// a free node points at itself and chains through m_next, exactly as
				// the stock RemoveAll leaves every released multilist element.
				surface_node_t&     node = surfaces[ element ];
				const std::uint16_t next = node.m_next;
				node.m_previous          = element;
				node.m_next              = elements.m_first_free;
				elements.m_first_free    = element;
				element                  = next;
			}
		}

		elements.m_head          = invalid_list_index;
		elements.m_tail          = invalid_list_index;
		elements.m_element_count = 0;
		return true;
	}

	FlashlightBuckets::~FlashlightBuckets( ) {
		remove( );
	}

	bool FlashlightBuckets::install( const portal2_modules_t& modules ) {
		if( m_instance || m_entry.installed( ) || !modules.m_engine )
			return false;

		// the signature spans the whole function, so every shadow-manager and
		// bucket offset used below is verified by its own instruction bytes.
		const game::profile_t* game  = game::active( );
		const Address          clear = modules.m_engine.find( game->flashlight_bucket_flush );
		if( !clear )
			return false;

		m_remove_all_elements = clear.rel32( game->xref_flashlight_elements_remove_all ).as< remove_all_fn_t >( );
		m_remove_all_sort_ids = clear.rel32( game->xref_flashlight_sort_ids_remove_all ).as< remove_all_fn_t >( );
		if( !m_remove_all_elements || !m_remove_all_sort_ids )
			return false;

		// replace the function at its entry with a jump to ClearAll.
		const std::uintptr_t              target = reinterpret_cast< std::uintptr_t >( &ClearAll );
		const std::int32_t                jump   = static_cast< std::int32_t >( target - ( clear.as< std::uintptr_t >( ) + 5 ) );
		const std::vector< std::uint8_t > expected{ 0x53, 0x8b, 0xd9, 0x80, 0xbb };
		const std::vector< std::uint8_t > replacement{ 0xe9,
		                                               static_cast< std::uint8_t >( jump ),
		                                               static_cast< std::uint8_t >( jump >> 8 ),
		                                               static_cast< std::uint8_t >( jump >> 16 ),
		                                               static_cast< std::uint8_t >( jump >> 24 ) };

		m_instance = this;
		if( !m_entry.install( clear.as< void* >( ), expected, replacement ) ) {
			m_instance = nullptr;
			return false;
		}

		return true;
	}

	void FlashlightBuckets::remove( ) {
		m_entry.remove( );
		m_enabled.store( false, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	void __fastcall FlashlightBuckets::ClearAll( void* shadow_manager, void* ) {
		FlashlightBuckets* instance = m_instance;
		if( !instance )
			return;

		const game::profile_t* game = game::active( );
		const Address          manager{ shadow_manager };

		// single-pass flashlight mode never fills the per-flashlight buckets.
		if( manager.at< bool >( game->shadow_single_pass_flashlight ) )
			return;

		const bool used_only = instance->enabled( );
		for( std::uint16_t flashlight = manager.at< std::uint16_t >( game->shadow_flashlight_head ); flashlight != invalid_list_index; ) {
			const Address state   = manager.at< Address >( game->shadow_flashlight_states ).add( flashlight * game->flashlight_state_size );
			auto*         buckets = state.add( game->flashlight_material_buckets ).as< material_buckets_t* >( );

			// CMaterialsBuckets::Flush.
			++buckets->m_flush_count;
			if( !used_only || !release_used_surface_elements( *buckets ) )
				instance->m_remove_all_elements( &buckets->m_elements );
			instance->m_remove_all_sort_ids( &buckets->m_used_sort_ids );

			flashlight = state.at< std::uint16_t >( game->flashlight_state_next );
		}
	}

}
