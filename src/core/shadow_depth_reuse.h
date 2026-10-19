#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace p2boost {

	// each split-screen view (and a held +remote_view slot) runs
	// ComputeShadowDepthTextures, re-rendering every selected flashlight's depth
	// map: 19.5% of the main thread in the split-screen co-op lobby. A depth map
	// is drawn from the light, not the camera, so the same light rendered into
	// the same depth texture later in the same frame reproduces the texture it
	// already holds. CViewRender::UpdateShadowDepthTexture is skipped in exactly
	// that case. With the stable depth order (ProjectedShadowCache) every view
	// gives a light the same slot when all lights fit.
	//
	// experimental and off by default: per-split-slot renderable visibility
	// could make a local player's model differ between the two depth passes.
	class ShadowDepthReuse {
	public:
		~ShadowDepthReuse( );

		bool install( const portal2_modules_t& modules );
		void remove( );

		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_update_depth.installed( );
		}

		std::uint64_t rendered_count( ) const noexcept {
			return m_rendered.load( std::memory_order_relaxed );
		}

		std::uint64_t reused_count( ) const noexcept {
			return m_reused.load( std::memory_order_relaxed );
		}

		// the view-setup fields ComputeShadowDepthTextures writes for a light.
		static constexpr std::size_t key_size{ 0x80 };
		using key_t = std::array< std::uint8_t, key_size >;
		static key_t make_key( const void* view ) noexcept;

	private:
		struct entry_t {
			const void* m_texture{ };
			int         m_frame{ -1 };
			key_t       m_key{ };
		};

		using update_depth_fn_t = bool( __thiscall* )( void*, void*, void*, const void* );

		static bool __fastcall UpdateShadowDepthTexture( void* self, void*, void* color, void* depth, const void* view );

		static ShadowDepthReuse* m_instance;

		VmtHook                      m_update_depth;
		void* const*                 m_globals{ };
		std::array< entry_t, 32 >    m_entries{ };
		std::atomic_bool             m_enabled{ };
		std::atomic< std::uint64_t > m_rendered{ };
		std::atomic< std::uint64_t > m_reused{ };
	};

}
