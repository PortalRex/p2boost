#pragma once

#include "game_profile.h"
#include "math_types.h"
#include "vmt.h"

#include <cstdint>
#include <cstring>

namespace p2boost {

	// read-only view over one private ClientShadow_t record from build 23973718.
	class client_shadow_entry_view_t {
	public:
		client_shadow_entry_view_t( ) noexcept = default;

		explicit client_shadow_entry_view_t( const std::uint8_t* bytes ) noexcept
		    : m_bytes{ bytes } {
		}

		explicit operator bool( ) const noexcept {
			return m_bytes != nullptr;
		}

		const std::uint8_t* bytes( ) const noexcept {
			return m_bytes;
		}

		matrix4x4_t world_to_shadow( ) const noexcept {
			return read< matrix4x4_t >( game::active( )->shadow_world_to_shadow );
		}

		std::uint16_t engine_handle( ) const noexcept {
			return read< std::uint16_t >( game::active( )->shadow_engine_handle );
		}

	private:
		template < typename value_t >
		value_t read( std::ptrdiff_t offset ) const noexcept {
			value_t value{ };
			if( m_bytes )
				std::memcpy( &value, m_bytes + offset, sizeof( value ) );
			return value;
		}

		const std::uint8_t* m_bytes{ };
	};

	// localizes the only reversed field used from CClientShadowMgr itself.
	class client_shadow_manager_view_t {
	public:
		explicit client_shadow_manager_view_t( void* manager ) noexcept
		    : m_manager{ manager } {
		}

		explicit operator bool( ) const noexcept {
			return m_manager != nullptr;
		}

		void** update_projected_slot( ) const noexcept {
			return vmt_slot( m_manager, game::active( )->vmt_update_projected );
		}

		client_shadow_entry_view_t entry( std::uint16_t handle ) const noexcept {
			std::uint8_t* entries{ };
			if( m_manager )
				std::memcpy( &entries, static_cast< const std::uint8_t* >( m_manager ) + game::active( )->shadow_entries, sizeof( entries ) );
			if( !entries )
				return { };

			return client_shadow_entry_view_t{
			    entries + static_cast< std::size_t >( handle ) * game::shadow_entry_size };
		}

	private:
		void* m_manager{ };
	};

	// read the private ConVar value consulted by UpdateProjectedTexture.
	inline int flashlight_models_value( int** owner_parent ) noexcept {
		int* owner = owner_parent ? *owner_parent : nullptr;
		if( !owner )
			return 0;

		int         value{ };
		const auto* bytes = reinterpret_cast< const std::uint8_t* >( owner );
		std::memcpy( &value, bytes + game::active( )->shadow_flashlight_models, sizeof( value ) );
		return value;
	}

}
