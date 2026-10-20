#include "vgui_screen_cache.h"

#include "address.h"
#include "game_profile.h"
#include "interfaces.h"
#include "vmt.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace p2boost {

	VguiScreenCache* VguiScreenCache::m_instance{ };

	namespace {

		using void_fn_t             = void( __thiscall* )( void* );
		using texture_download_fn_t = void( __thiscall* )( void*, void*, int );
		using clear_color_fn_t      = void( __thiscall* )( void*, unsigned char, unsigned char, unsigned char, unsigned char );
		using clear_buffers_fn_t    = void( __thiscall* )( void*, bool, bool, bool );
		using int_fn_t              = int( __thiscall* )( void* );
		using panel_client_fn_t     = void*(__thiscall*)( void* );
		using paint_traverse_fn_t   = void( __thiscall* )( void*, bool, bool );
		using solve_fn_t            = void( __thiscall* )( void*, void*, bool );
		using depth_fn_t            = void( __thiscall* )( void*, bool, bool, bool );
		using push_target_fn_t      = void( __thiscall* )( void*, void*, int, int, int, int );
		using set_color_fn_t        = void( __thiscall* )( void*, int, int, int, int );
		using set_texture_fn_t      = void( __thiscall* )( void*, int );
		using new_texture_fn_t      = int( __thiscall* )( void*, bool );
		using set_material_fn_t     = void( __thiscall* )( void*, int, void* );
		using sub_rect_fn_t         = void( __thiscall* )( void*, int, int, int, int, float, float, float, float );

		constexpr int atlas_size{ 2048 };
		// IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_SEPARATE, and the texture
		// flags clamp S/T, no mipmaps, no LOD.
		constexpr int          atlas_format{ 12 };
		constexpr int          atlas_depth{ 2 };
		constexpr unsigned int atlas_texture_flags{ 0x4 | 0x8 | 0x100 | 0x200 };
		constexpr const char*  atlas_texture_name{ "_rt_p2boost_vgui" };
		constexpr const char*  atlas_material_name{ "p2boost/vgui_atlas" };
		constexpr const char*  atlas_material_file{ "\\portal2\\materials\\p2boost\\vgui_atlas.vmt" };
		constexpr const char*  atlas_material_text{
            "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"_rt_p2boost_vgui\"\n\t\"$nocull\" 1\n\t\"$nofog\" 1\n\t\"$ignorez\" 1\n}\n" };

	}

	VguiScreenCache::~VguiScreenCache( ) {
		remove( );
	}

	bool VguiScreenCache::install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces, void** draw_model_slot, const std::string& game_root ) {
		sdk::ISurface*        surface   = interfaces.surface( );
		sdk::IMaterialSystem* materials = interfaces.material_system( );
		if( m_instance || m_draw_panel.installed( ) || !modules.m_surface || !modules.m_material_system || !surface || !materials ||
		    !modules.m_client.contains( Address{ draw_model_slot }, sizeof( void* ) ) )
			return false;

		// IMatSystemSurface::DrawPanelIn3DSpace, its 3D StartDrawing, and the 2D
		// StartDrawing/FinishDrawing pair PaintTraverseEx calls around a panel.
		const game::profile_t* game       = game::active( );
		const Address          draw_panel = surface->DrawPanelIn3DSpaceAddress( );
		const Address          start_3d   = modules.m_surface.find( game->surface_start_drawing_3d );
		const Address          traverse   = surface->PaintTraverseExAddress( );
		if( !modules.m_surface.matches( draw_panel, game->surface_draw_panel_in_3d ) || !start_3d ||
		    !modules.m_surface.matches( traverse, game->overlay_surface_paint_traverse ) )
			return false;

		const Address start_operand  = traverse.add( game->surface_start_drawing_operand );
		const Address finish_operand = traverse.add( game->surface_finish_drawing_operand );
		if( start_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 || finish_operand.sub( 1 ).at< std::uint8_t >( ) != 0xe8 )
			return false;

		const Address start_drawing  = start_operand.rel32( );
		const Address finish_drawing = finish_operand.rel32( );
		if( !modules.m_surface.matches( start_drawing, game->overlay_surface_start_drawing ) ||
		    !modules.m_surface.matches( finish_drawing, game->overlay_surface_finish_drawing ) ||
		    !modules.m_material_system.contains( materials->FindMaterialAddress( ) ) ||
		    !modules.m_material_system.contains( materials->CreateNamedRenderTargetAddress( ) ) )
			return false;
		if( !create_atlas( materials, game_root ) )
			return false;

		// C_VGuiScreen::DrawModel marks the DrawPanelIn3DSpace calls this cache
		// may take; every other caller (movie displays) keeps the stock path.
		void** model_slot = draw_model_slot;

		m_materials      = materials;
		m_start_3d       = start_3d.as< start_3d_fn_t >( );
		m_start_drawing  = start_drawing.as< start_drawing_fn_t >( );
		m_finish_drawing = finish_drawing.as< finish_drawing_fn_t >( );
		reset_atlas( );
		m_instance = this;
		if( !m_draw_panel.install( surface->DrawPanelIn3DSpaceSlot( ), reinterpret_cast< void* >( &DrawPanelIn3DSpace ) ) ||
		    !m_draw_model.install( model_slot, reinterpret_cast< void* >( &DrawModel ) ) ) {
			m_draw_panel.remove( );
			m_instance       = nullptr;
			m_materials      = nullptr;
			m_start_3d       = nullptr;
			m_start_drawing  = nullptr;
			m_finish_drawing = nullptr;
			return false;
		}
		return true;
	}

	int __fastcall VguiScreenCache::DrawModel( void* self, void*, int flags, const void* instance ) {
		VguiScreenCache* cache    = m_instance;
		const auto       original = cache ? cache->m_draw_model.previous< draw_model_fn_t >( ) : nullptr;
		if( !original )
			return 0;

		cache->m_in_draw_model = true;
		const int result       = original( self, flags, instance );
		cache->m_in_draw_model = false;
		return result;
	}

	void VguiScreenCache::remove( ) {
		m_draw_model.remove( );
		m_draw_panel.remove( );
		m_in_draw_model = false;
		m_max_fps.store( 0, std::memory_order_relaxed );
		m_materials      = nullptr;
		m_start_3d       = nullptr;
		m_start_drawing  = nullptr;
		m_finish_drawing = nullptr;
		m_texture        = nullptr;
		m_material       = nullptr;
		m_texture_id     = -1;
		reset_atlas( );

		if( m_instance == this )
			m_instance = nullptr;
	}

	// the atlas is a named render target created with the material system's
	// allocation flag raised, and a material file next to the game's that
	// samples it; FindMaterial loads the file once.
	bool VguiScreenCache::create_atlas( sdk::IMaterialSystem* materials, const std::string& game_root ) {
		const game::profile_t* game = game::active( );
		const std::string      path = game_root + atlas_material_file;
		{
			const std::string directory = path.substr( 0, path.find_last_of( '\\' ) );
			CreateDirectoryA( directory.c_str( ), nullptr );
			HANDLE file = CreateFileA( path.c_str( ), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
			if( file == INVALID_HANDLE_VALUE )
				return false;
			DWORD      written{ };
			const bool wrote = WriteFile( file, atlas_material_text, static_cast< DWORD >( std::strlen( atlas_material_text ) ), &written, nullptr ) != 0;
			CloseHandle( file );
			if( !wrote )
				return false;
		}

		// the engine only lets render targets be created inside its own
		// allocation block at start-up; the flag it checks is raised for this
		// one call, and the texture is downloaded (its GPU surface created)
		// right away, as the engine's restore path would.
		int*      allocating = Address{ static_cast< void* >( materials ) }.add( game->materials_allocating_render_targets ).as< int* >( );
		const int saved      = *allocating;
		*allocating          = 1;
		m_texture            = materials->CreateNamedRenderTargetTextureEx2( atlas_texture_name, atlas_size, atlas_size, 0, atlas_format, atlas_depth,
		                                                                     atlas_texture_flags, 0 );
		*allocating          = saved;
		if( !m_texture )
			return false;
		vfunc< void_fn_t >( m_texture, game->vmt_texture_add_ref )( m_texture );
		vfunc< texture_download_fn_t >( m_texture, game->vmt_texture_download )( m_texture, nullptr, 0 );

		m_material = materials->FindMaterial( atlas_material_name, "Other textures", true, nullptr );
		if( !m_material ) {
			m_texture = nullptr;
			return false;
		}
		vfunc< void_fn_t >( m_material, game->vmt_material_add_ref )( m_material );
		return true;
	}

	void VguiScreenCache::reset_atlas( ) noexcept {
		m_entries      = { };
		m_shelf_x      = 0;
		m_shelf_y      = 0;
		m_shelf_height = 0;
	}

	// first-fit shelves, left to right, top to bottom. a screen keeps its
	// rectangle while its panel and pixel size stay the same.
	VguiScreenCache::entry_t* VguiScreenCache::find_or_allocate( const void* panel, int width, int height, int atlas_width, int atlas_height ) {
		entry_t* free_slot = nullptr;
		for( entry_t& entry : m_entries ) {
			if( entry.m_panel == panel && entry.m_width == width && entry.m_height == height )
				return &entry;
			if( !entry.m_panel && !free_slot )
				free_slot = &entry;
		}
		if( width > atlas_width || height > atlas_height )
			return nullptr;

		// a full atlas holds rectangles of panels that were replaced; once the
		// stalest has gone a second without a draw, start over.
		if( !free_slot || ( m_shelf_x + width > atlas_width && m_shelf_y + m_shelf_height + height > atlas_height ) ) {
			const Qpc::tick_t now    = Qpc::now( );
			Qpc::tick_t       oldest = now;
			for( const entry_t& entry : m_entries )
				if( entry.m_panel && entry.m_used < oldest )
					oldest = entry.m_used;
			if( now - oldest < Qpc::frequency( ) )
				return nullptr;
			reset_atlas( );
			free_slot = &m_entries[ 0 ];
		}

		if( m_shelf_x + width > atlas_width ) {
			m_shelf_y += m_shelf_height;
			m_shelf_x      = 0;
			m_shelf_height = 0;
		}
		if( m_shelf_y + height > atlas_height )
			return nullptr;

		free_slot->m_panel   = panel;
		free_slot->m_x       = m_shelf_x;
		free_slot->m_y       = m_shelf_y;
		free_slot->m_width   = width;
		free_slot->m_height  = height;
		free_slot->m_time    = 0;
		free_slot->m_painted = false;
		m_shelf_x += width;
		if( height > m_shelf_height )
			m_shelf_height = height;
		return free_slot;
	}

	void __fastcall VguiScreenCache::DrawPanelIn3DSpace( void* self, void*, void* panel, const float* matrix, int pixel_width, int pixel_height, float width, float height ) {
		VguiScreenCache* cache    = m_instance;
		const auto       original = cache ? cache->m_draw_panel.previous< draw_panel_fn_t >( ) : nullptr;
		if( !original )
			return;

		const int fps = cache->m_max_fps.load( std::memory_order_relaxed );
		if( fps <= 0 || !cache->m_in_draw_model || !panel || !matrix || pixel_width <= 0 || pixel_height <= 0 ) {
			original( self, panel, matrix, pixel_width, pixel_height, width, height );
			return;
		}

		const game::profile_t* game         = game::active( );
		void*                  texture      = cache->m_texture;
		const int              atlas_width  = vfunc< int_fn_t >( texture, game->vmt_texture_actual_width )( texture );
		const int              atlas_height = vfunc< int_fn_t >( texture, game->vmt_texture_actual_height )( texture );
		if( cache->m_invalidate.exchange( false, std::memory_order_acq_rel ) || atlas_width != cache->m_atlas_width || atlas_height != cache->m_atlas_height ) {
			cache->reset_atlas( );
			cache->m_atlas_width  = atlas_width;
			cache->m_atlas_height = atlas_height;
		}

		entry_t* entry = cache->find_or_allocate( panel, pixel_width, pixel_height, atlas_width, atlas_height );
		if( !entry ) {
			original( self, panel, matrix, pixel_width, pixel_height, width, height );
			return;
		}

		const Qpc::tick_t now = Qpc::now( );
		entry->m_used         = now;
		const bool due        = !entry->m_painted || now - entry->m_time >= Qpc::frequency( ) / fps;

		// the stock method: lay the panel out, take the render context, set the
		// panel's depth mode, and load the panel-to-world transform.
		const Address surface{ self };
		auto*         flags = surface.add( game->surface_paint_flags ).as< std::uint8_t* >( );
		vfunc< solve_fn_t >( self, game->vmt_surface_solve_traverse )( self, panel, false );
		void* context = cache->m_materials->GetRenderContext( );
		if( !context ) {
			original( self, panel, matrix, pixel_width, pixel_height, width, height );
			return;
		}
		vfunc< void_fn_t >( context, game->vmt_context_begin_render )( context );
		vfunc< depth_fn_t >( context, game->vmt_context_override_depth )( context, true, false, true );
		*flags |= 2;
		cache->m_start_3d( self, matrix, pixel_width, pixel_height, width, height );

		if( due ) {
			// paint the panel in 2D into its rectangle of the full-screen
			// texture, keeping the panel's depth override: the texture's depth
			// buffer holds nothing meaningful, and image materials without
			// $ignorez would test against it. the surface clips against its
			// extents, which StartDrawing takes from the viewport origin; the
			// rectangle's paint runs from 0,0.
			vfunc< push_target_fn_t >( context, game->vmt_context_push_render_target )( context, texture, entry->m_x, entry->m_y, pixel_width, pixel_height );
			// the rectangle starts black: a panel's paint blends over whatever
			// is there, and the atlas is drawn opaque (the paint leaves no
			// usable alpha behind, so translucent or additive screen art ends
			// up composited over black rather than over the wall).
			vfunc< clear_color_fn_t >( context, game->vmt_context_clear_color )( context, 0, 0, 0, 0 );
			vfunc< clear_buffers_fn_t >( context, game->vmt_context_clear_buffers )( context, true, false, false );
			cache->m_start_drawing( self );
			auto* extents = surface.add( game->surface_extents ).as< int* >( );
			extents[ 0 ]  = 0;
			extents[ 1 ]  = 0;
			extents[ 2 ]  = pixel_width;
			extents[ 3 ]  = pixel_height;
			void* client  = vfunc< panel_client_fn_t >( panel, game->vmt_vpanel_client )( panel );
			if( client ) {
				vfunc< void_fn_t >( client, game->vmt_client_panel_repaint )( client );
				vfunc< paint_traverse_fn_t >( client, game->vmt_client_panel_paint_traverse )( client, true, false );
			}
			cache->m_finish_drawing( );
			vfunc< void_fn_t >( context, game->vmt_context_pop_render_target )( context );
			entry->m_time    = now;
			entry->m_painted = true;
			cache->m_painted.fetch_add( 1, std::memory_order_relaxed );
		} else {
			cache->m_reused.fetch_add( 1, std::memory_order_relaxed );
		}

		// draw the rectangle in the panel's place, as End3DPaint's
		// DrawFullScreenBuffer does: the atlas material on a lazily created
		// texture id, white, half-texel inset.
		if( cache->m_texture_id == -1 ) {
			cache->m_texture_id = vfunc< new_texture_fn_t >( self, game->vmt_surface_create_texture_id )( self, false );
			vfunc< set_material_fn_t >( self, game->vmt_surface_draw_set_texture_material )( self, cache->m_texture_id, cache->m_material );
		}
		std::uint8_t color[ 4 ];
		std::memcpy( color, surface.add( game->surface_draw_color ).as< const void* >( ), sizeof( color ) );
		vfunc< set_color_fn_t >( self, game->vmt_surface_draw_set_color )( self, 255, 255, 255, 255 );
		vfunc< set_texture_fn_t >( self, game->vmt_surface_draw_set_texture )( self, cache->m_texture_id );
		const float u0 = ( static_cast< float >( entry->m_x ) + 0.5f ) / static_cast< float >( atlas_width );
		const float v0 = ( static_cast< float >( entry->m_y ) + 0.5f ) / static_cast< float >( atlas_height );
		const float u1 = ( static_cast< float >( entry->m_x + pixel_width ) - 0.5f ) / static_cast< float >( atlas_width );
		const float v1 = ( static_cast< float >( entry->m_y + pixel_height ) - 0.5f ) / static_cast< float >( atlas_height );
		vfunc< sub_rect_fn_t >( self, game->vmt_surface_draw_textured_sub_rect )( self, 0, 0, pixel_width, pixel_height, u0, v0, u1, v1 );
		std::memcpy( surface.add( game->surface_draw_color ).as< void* >( ), color, sizeof( color ) );

		cache->m_finish_drawing( );
		vfunc< depth_fn_t >( context, game->vmt_context_override_depth )( context, false, true, true );
		*flags &= static_cast< std::uint8_t >( ~2 );
		vfunc< void_fn_t >( context, game->vmt_context_end_render )( context );
		vfunc< void_fn_t >( context, game->vmt_context_release )( context );
	}

}
