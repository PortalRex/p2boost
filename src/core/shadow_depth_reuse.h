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
	// per-split-slot renderable visibility could in principle make a local
	// player's model differ between the two depth passes; an in-game check
	// with each split-screen player standing in a spotlight showed identical
	// shadows in both views (lobby 430 -> 470 FPS).
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

		// also reuse a light's map across frames, refreshing it at most this
		// many times per second (0 = same frame only). a light whose view
		// changes re-renders at once; a moving caster's shadow lags by under
		// 1/fps s.
		void set_max_fps( int fps ) noexcept {
			m_max_fps.store( fps < 0 ? 0 : fps, std::memory_order_relaxed );
		}

		int max_fps( ) const noexcept {
			return m_max_fps.load( std::memory_order_relaxed );
		}

		// forget every recorded map: level loads and device restores can leave
		// a depth texture's contents undefined.
		void invalidate( ) noexcept {
			m_invalidate.store( true, std::memory_order_release );
		}

		// the view-setup fields ComputeShadowDepthTextures writes for a light.
		static constexpr std::size_t key_size{ 0x80 };
		using key_t = std::array< std::uint8_t, key_size >;
		static key_t make_key( const void* view ) noexcept;

		// a recorded flashlight depth view, for diagnostics and test harnesses.
		struct light_view_t {
			float m_origin[ 3 ]{ };
			float m_angles[ 3 ]{ };
			float m_fov{ };
			int   m_frame{ -1 };
		};

		// copy of the recorded views; call on the main thread.
		std::array< light_view_t, 32 > views( ) const noexcept;

	private:
		struct entry_t {
			const void*  m_texture{ };
			int          m_frame{ -1 };
			long long    m_time{ };
			key_t        m_key{ };
			light_view_t m_view{ };
		};

		using update_depth_fn_t = bool( __thiscall* )( void*, void*, void*, const void* );

		static bool __fastcall UpdateShadowDepthTexture( void* self, void*, void* color, void* depth, const void* view );

		static ShadowDepthReuse* m_instance;

		VmtHook                      m_update_depth;
		void* const*                 m_globals{ };
		std::array< entry_t, 32 >    m_entries{ };
		std::atomic_bool             m_enabled{ };
		std::atomic_bool             m_invalidate{ };
		std::atomic_int              m_max_fps{ };
		long long                    m_frequency{ };
		std::atomic< std::uint64_t > m_rendered{ };
		std::atomic< std::uint64_t > m_reused{ };
	};

}
