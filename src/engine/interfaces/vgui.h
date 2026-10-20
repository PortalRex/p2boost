#pragma once

#include "interface.h"

namespace p2boost::sdk {

	struct glyph_metrics_t {
		float advance{ };
		float before{ };
		float after{ };
	};

	// IClientPanel: the game-side object behind a VGUI panel.
	class IClientPanel final : public Interface {
	public:
		static constexpr std::size_t paint_traverse_slot{ 3 };
		static constexpr std::size_t repaint_slot{ 4 };

		void PaintTraverse( bool repaint, bool allow_force ) {
			using function_t = void( __thiscall* )( void*, bool, bool );
			invoke< paint_traverse_slot, function_t >( repaint, allow_force );
		}

		void Repaint( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< repaint_slot, function_t >( );
		}
	};

	// VPANEL: vgui2's panel handle (VPanel), which owns its client panel.
	class VPanel final : public Interface {
	public:
		static constexpr std::size_t client_slot{ 39 };

		IClientPanel* Client( ) {
			using function_t = IClientPanel*(__thiscall*)( void* );
			return invoke< client_slot, function_t >( );
		}
	};

	// VGUI_Surface031 owns the final text layout and material-backed paint pass.
	class ISurface final : public Interface {
	public:
		static constexpr const char* version{ "VGUI_Surface031" };
		static constexpr std::size_t draw_set_color_slot{ 14 };
		static constexpr std::size_t draw_set_texture_slot{ 37 };
		static constexpr std::size_t create_new_texture_id_slot{ 41 };
		static constexpr std::size_t solve_traverse_slot{ 89 };
		static constexpr std::size_t draw_textured_sub_rect_slot{ 103 };
		static constexpr std::size_t draw_set_texture_material_slot{ 165 };
		static constexpr std::size_t get_font_tall_slot{ 72 };
		static constexpr std::size_t paint_traverse_ex_slot{ 117 };
		static constexpr std::size_t get_kerned_char_width_slot{ 147 };
		static constexpr std::size_t draw_colored_text_slot{ 160 };
		// IMatSystemSurface::DrawPanelIn3DSpace: how C_VGuiScreen::DrawModel paints.
		static constexpr std::size_t draw_panel_in_3d_space_slot{ 164 };

		void DrawSetColor( int red, int green, int blue, int alpha ) {
			using function_t = void( __thiscall* )( void*, int, int, int, int );
			invoke< draw_set_color_slot, function_t >( red, green, blue, alpha );
		}

		void DrawSetTexture( int id ) {
			using function_t = void( __thiscall* )( void*, int );
			invoke< draw_set_texture_slot, function_t >( id );
		}

		int CreateNewTextureID( bool procedural ) {
			using function_t = int( __thiscall* )( void*, bool );
			return invoke< create_new_texture_id_slot, function_t >( procedural );
		}

		// lay out a panel tree before it paints.
		void SolveTraverse( VPanel* panel, bool force_apply_scheme ) {
			using function_t = void( __thiscall* )( void*, void*, bool );
			invoke< solve_traverse_slot, function_t >( panel, force_apply_scheme );
		}

		void DrawTexturedSubRect( int x0, int y0, int x1, int y1, float u0, float v0, float u1, float v1 ) {
			using function_t = void( __thiscall* )( void*, int, int, int, int, float, float, float, float );
			invoke< draw_textured_sub_rect_slot, function_t >( x0, y0, x1, y1, u0, v0, u1, v1 );
		}

		// IMatSystemSurface::DrawSetTextureMaterial.
		void DrawSetTextureMaterial( int id, void* material ) {
			using function_t = void( __thiscall* )( void*, int, void* );
			invoke< draw_set_texture_material_slot, function_t >( id, material );
		}

		int GetFontTall( unsigned long font ) {
			using function_t = int( __thiscall* )( void*, unsigned long );
			return invoke< get_font_tall_slot, function_t >( font );
		}

		glyph_metrics_t GetKernedCharWidth( unsigned long font, wchar_t character, wchar_t previous, wchar_t next ) {
			using function_t = void( __thiscall* )( void*, unsigned long, wchar_t, wchar_t, wchar_t, float&, float&, float& );

			glyph_metrics_t result;
			invoke< get_kerned_char_width_slot, function_t >( font, character, previous, next, result.advance, result.before, result.after );
			return result;
		}

		int DrawColoredText( unsigned long font, int x, int y, int red, int green, int blue, int alpha, const char* text ) {
			using function_t = int( __cdecl* )( void*, unsigned long, int, int, int, int, int, int, const char*, ... );
			auto target      = DrawColoredTextAddress( ).as< function_t >( );
			return target( this, font, x, y, red, green, blue, alpha, "%s", text );
		}

		Address PaintTraverseExAddress( ) const noexcept {
			return method( paint_traverse_ex_slot );
		}

		Address DrawPanelIn3DSpaceAddress( ) const noexcept {
			return method( draw_panel_in_3d_space_slot );
		}

		void** DrawPanelIn3DSpaceSlot( ) noexcept {
			return method_slot( draw_panel_in_3d_space_slot );
		}

		Address DrawColoredTextAddress( ) const noexcept {
			return method( draw_colored_text_slot );
		}

		Address GetKernedCharWidthAddress( ) const noexcept {
			return method( get_kerned_char_width_slot );
		}

		Address GetFontTallAddress( ) const noexcept {
			return method( get_font_tall_slot );
		}
	};

	// IScheme resolves the existing engine font selected by the profiler.
	class IScheme final : public Interface {
	public:
		static constexpr std::size_t get_font_slot{ 3 };

		unsigned long GetFont( const char* name, bool proportional ) {
			using function_t = unsigned long( __thiscall* )( void*, const char*, bool );
			return invoke< get_font_slot, function_t >( name, proportional );
		}

		Address GetFontAddress( ) const noexcept {
			return method( get_font_slot );
		}
	};

	// VGUI_Scheme010 returns the live scheme without duplicating font resources.
	class ISchemeManager final : public Interface {
	public:
		static constexpr const char* version{ "VGUI_Scheme010" };
		static constexpr std::size_t get_scheme_slot{ 8 };

		IScheme* GetIScheme( unsigned long handle ) {
			using function_t = IScheme*(__thiscall*)( void*, unsigned long );
			return invoke< get_scheme_slot, function_t >( handle );
		}

		Address GetISchemeAddress( ) const noexcept {
			return method( get_scheme_slot );
		}
	};

}
