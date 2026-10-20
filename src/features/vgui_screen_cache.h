#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace p2boost {

	// in-world VGUI screens (C_VGuiScreen) lay out and paint their whole panel
	// tree straight into the 3D view every frame, in every view. This replaces
	// the surface's DrawPanelIn3DSpace for them: each screen is painted into
	// its own rectangle of a 2048x2048 render target at most N times per
	// second, and every frame the rectangle is drawn as one textured quad with
	// the same 3D transform and depth state the painted panel used. Only
	// panels drawn by C_VGuiScreen::DrawModel qualify: a movie display paints
	// its video through the surface's own full-screen texture and keeps the
	// stock path. Split-screen lobby: 690 -> 760 FPS.
	class VguiScreenCache {
	public:
		~VguiScreenCache( );

		VguiScreenCache( )                                   = default;
		VguiScreenCache( const VguiScreenCache& )            = delete;
		VguiScreenCache& operator=( const VguiScreenCache& ) = delete;

		// draw_model_slot is C_VGuiScreen::DrawModel's vtable entry, already
		// hooked by the shadow skip; this hook chains after it.
		bool install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces, void** draw_model_slot, const std::string& game_root );
		void remove( );

		// repaint each screen at most this many times per second; 0 paints
		// every screen every frame through the stock path.
		void set_max_fps( int fps ) noexcept {
			m_max_fps.store( fps < 0 ? 0 : fps, std::memory_order_relaxed );
		}

		int max_fps( ) const noexcept {
			return m_max_fps.load( std::memory_order_relaxed );
		}

		// forget every painted rectangle: level loads replace the panels and a
		// device reset can leave the texture's contents undefined.
		void invalidate( ) noexcept {
			m_invalidate.store( true, std::memory_order_release );
		}

		bool installed( ) const noexcept {
			return m_draw_panel.installed( ) && m_draw_model.installed( );
		}

		std::uint64_t painted_count( ) const noexcept {
			return m_painted.load( std::memory_order_relaxed );
		}

		std::uint64_t reused_count( ) const noexcept {
			return m_reused.load( std::memory_order_relaxed );
		}

	private:
		using draw_panel_fn_t     = void( __thiscall* )( void*, void*, const float*, int, int, float, float );
		using start_3d_fn_t       = void( __thiscall* )( void*, const float*, int, int, float, float );
		using start_drawing_fn_t  = int( __thiscall* )( void* );
		using finish_drawing_fn_t = int( __cdecl* )( );

		struct entry_t {
			const void* m_panel{ };
			int         m_x{ };
			int         m_y{ };
			int         m_width{ };
			int         m_height{ };
			long long   m_time{ };
			long long   m_used{ };
			bool        m_painted{ };
		};

		using draw_model_fn_t = int( __thiscall* )( void*, int, const void* );

		static void __fastcall DrawPanelIn3DSpace( void* self, void*, void* panel, const float* matrix, int pixel_width, int pixel_height, float width, float height );
		static int __fastcall DrawModel( void* self, void*, int flags, const void* instance );

		entry_t* find_or_allocate( const void* panel, int width, int height, int atlas_width, int atlas_height );
		void     reset_atlas( ) noexcept;
		bool     create_atlas( sdk::IMaterialSystem* materials, const std::string& game_root );

		static VguiScreenCache* m_instance;

		VmtHook               m_draw_panel;
		VmtHook               m_draw_model;
		bool                  m_in_draw_model{ };
		sdk::IMaterialSystem* m_materials{ };
		start_3d_fn_t         m_start_3d{ };
		start_drawing_fn_t    m_start_drawing{ };
		finish_drawing_fn_t   m_finish_drawing{ };
		void*                 m_texture{ };
		void*                 m_material{ };
		int                   m_texture_id{ -1 };
		long long             m_frequency{ };

		std::array< entry_t, 64 > m_entries{ };
		int                       m_shelf_x{ };
		int                       m_shelf_y{ };
		int                       m_shelf_height{ };
		int                       m_atlas_width{ };
		int                       m_atlas_height{ };

		std::atomic_int              m_max_fps{ };
		std::atomic_bool             m_invalidate{ };
		std::atomic< std::uint64_t > m_painted{ };
		std::atomic< std::uint64_t > m_reused{ };
	};

}
