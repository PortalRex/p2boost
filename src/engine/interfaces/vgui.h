#pragma once

#include "interface.h"

namespace p2boost::sdk {

	struct glyph_metrics_t {
		float advance{ };
		float before{ };
		float after{ };
	};

	// VGUI_Surface031 owns the final text layout and material-backed paint pass.
	class ISurface final : public Interface {
	public:
		static constexpr const char* version{ "VGUI_Surface031" };
		static constexpr std::size_t get_font_tall_slot{ 72 };
		static constexpr std::size_t paint_traverse_ex_slot{ 117 };
		static constexpr std::size_t get_kerned_char_width_slot{ 147 };
		static constexpr std::size_t draw_colored_text_slot{ 160 };

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
			auto target = DrawColoredTextAddress( ).as< function_t >( );
			return target( this, font, x, y, red, green, blue, alpha, "%s", text );
		}

		Address PaintTraverseExAddress( ) const noexcept {
			return method( paint_traverse_ex_slot );
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
