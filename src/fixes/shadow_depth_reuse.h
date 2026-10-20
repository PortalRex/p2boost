#pragma once

#include "hook.h"
#include "math_types.h"
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
	// a depth map is not purely a function of the light: C_Portal_Player
	// skips drawing the viewpoint player (the active split-screen slot's) in
	// every view, depth views included, so a map rendered for one player's
	// view lacks that player's body and has the other's. A map therefore
	// passes to another view only when neither player's body can be in the
	// light's frustum; ProjectedShadowCache reports each view as its depth
	// maps begin. A skipped render also repeats the one render-context
	// change CShadowDepthView::Draw leaves behind, the white clear color the
	// main view uses to clear when its camera is in solid space.
	class ShadowDepthReuse {
	public:
		~ShadowDepthReuse( );

		bool install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces );
		void remove( );

		// main thread: the view whose depth maps render next (its CViewSetup
		// in CViewRender, one per split-screen slot; its origin is the eye).
		void begin_view( const void* view ) noexcept;

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
			const void*  m_viewer{ };
			vec3_t       m_viewer_eye{ };
		};

		bool shareable( const void* light_view, const entry_t& entry ) const noexcept;
		void repeat_draw_state( ) const noexcept;

		using update_depth_fn_t = bool( __thiscall* )( void*, void*, void*, const void* );

		static bool __fastcall UpdateShadowDepthTexture( void* self, void*, void* color, void* depth, const void* view );

		static ShadowDepthReuse* m_instance;

		VmtHook                      m_update_depth;
		void* const*                 m_globals{ };
		sdk::IMaterialSystem*        m_materials{ };
		const void*                  m_viewer{ };
		vec3_t                       m_viewer_eye{ };
		std::array< entry_t, 32 >    m_entries{ };
		std::atomic_bool             m_enabled{ };
		std::atomic_bool             m_invalidate{ };
		std::atomic_int              m_max_fps{ };
		long long                    m_frequency{ };
		std::atomic< std::uint64_t > m_rendered{ };
		std::atomic< std::uint64_t > m_reused{ };
	};

}
