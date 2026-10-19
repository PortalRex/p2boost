#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <atomic>
#include <cstdint>

namespace p2boost {

	// in-world VGUI screens (C_VGuiScreen) are opaque renderables, so every
	// flashlight shadow-depth pass draws them: DrawModel lays out and paints the
	// whole panel tree, text and all, through vguimatsurface into a depth-only
	// target. In the split-screen co-op lobby that was 3.2% of the main thread.
	// Skipping them only in VIEW_SHADOW_DEPTH_TEXTURE views means a screen panel
	// no longer occludes a projected light; the panels sit flush on walls.
	class VguiScreenShadowSkip {
	public:
		~VguiScreenShadowSkip( );

		bool install( const portal2_modules_t& modules );
		void remove( );

		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_draw_model.installed( );
		}

		std::uint64_t skipped_count( ) const noexcept {
			return m_skipped.load( std::memory_order_relaxed );
		}

	private:
		using draw_model_fn_t = int( __thiscall* )( void*, int, const void* );

		static int __fastcall DrawModel( void* self, void*, int flags, const void* instance );

		static VguiScreenShadowSkip* m_instance;

		VmtHook                      m_draw_model;
		const int*                   m_current_view_id{ };
		std::atomic_bool             m_enabled{ };
		std::atomic< std::uint64_t > m_skipped{ };
	};

}
