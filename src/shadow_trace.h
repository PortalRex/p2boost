#pragma once

#include "hook.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace p2boost {

	class Metrics;

	// skips byte-identical projected-shadow rebuilds without changing visuals.
	class ShadowTrace {
	public:
		~ShadowTrace( );

		bool install( Metrics* metrics );
		void remove( );
		void invalidate( ) noexcept;
		void set_enabled( bool enabled ) noexcept;

		bool installed( ) const noexcept {
			return m_update_projected.installed( );
		}

		unsigned int skips( ) const noexcept {
			return m_skips.load( std::memory_order_relaxed );
		}

	private:
		// caches all renderer-visible bytes used by one shadow entry.
		struct StateHash {
			std::uint16_t					  m_handle{ };
			std::array< std::uint8_t, 0x114 > m_bytes{ };
			int								  m_flashlight_models{ };
		};

		using UpdateProjected_t = void( __thiscall* )( void*, unsigned int, bool );
		using RemoveDirty_t		= void( __thiscall* )( void*, unsigned int );

		static void __fastcall UpdateProjected( void* self, void*, unsigned int handle, bool force );

		static ShadowTrace*		 m_instance;
		VtableSlotHook			 m_update_projected;
		Metrics*				 m_metrics{ };
		std::vector< StateHash > m_states;
		int**					 m_flashlight_models_parent{ };
		RemoveDirty_t			 m_remove_dirty{ };
		std::atomic_bool		 m_enabled{ };
		std::atomic_uint		 m_skips{ };
	};

}
