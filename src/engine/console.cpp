#include "console.h"

#include "module.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace p2boost {

	namespace {

		struct color_t {
			std::uint8_t r, g, b, a;
		};

		// Portal 2's console panel colors the whole message when printed through
		// tier0's ConColorMsg export (the same path SAR uses). the inline
		// \x01-\x05 codes used by other Source consoles are NOT parsed here.
		constexpr color_t console_palette[]{
		    { 255, 255, 255, 255 },  // normal
		    { 255, 80, 80, 255 },    // red
		    { 120, 220, 120, 255 },  // green
		    { 120, 160, 255, 255 },  // blue
		    { 255, 220, 120, 255 },  // yellow
		};

		using color_msg_fn_t = void( __cdecl* )( const color_t&, const char*, ... );
		using msg_fn_t       = void( __cdecl* )( const char*, ... );

		color_msg_fn_t color_msg( ) {
			static color_msg_fn_t fn = Module{ L"tier0.dll" }.export_function< color_msg_fn_t >(
			    "?ConColorMsg@@YAXABVColor@@PBDZZ" );
			return fn;
		}

		msg_fn_t msg( ) {
			static msg_fn_t fn = Module{ L"tier0.dll" }.export_function< msg_fn_t >( "Msg" );
			return fn;
		}

		void emit( console_color color, const char* text ) {
			if( color == console_color::normal ) {
				if( msg( ) )
					msg( )( "%s", text );
			} else if( color_msg( ) ) {
				color_msg( )( console_palette[ static_cast< std::size_t >( color ) - 1 ], "%s", text );
			} else if( msg( ) ) {
				msg( )( "%s", text );
			}
			OutputDebugStringA( text );
		}

	}

	void console_print( const char* format, ... ) {
		char    buffer[ 4096 ];
		va_list args;
		va_start( args, format );
		vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, format, args );
		va_end( args );
		emit( console_color::normal, buffer );
	}

	void console_print_color( console_color color, const char* format, ... ) {
		char    buffer[ 4096 ];
		va_list args;
		va_start( args, format );
		vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, format, args );
		va_end( args );
		emit( color, buffer );
	}

}