#include "console.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace p2boost {

	void console_print( const char* format, ... ) {
		char buffer[ 4096 ];
		va_list args;
		va_start( args, format );
		vsnprintf_s( buffer, sizeof( buffer ), _TRUNCATE, format, args );
		va_end( args );
		using MsgFn = void( __cdecl* )( const char*, ... );
		static MsgFn msg = [] {
			HMODULE tier0 = GetModuleHandleW( L"tier0.dll" );
			return tier0 ? reinterpret_cast< MsgFn >( GetProcAddress( tier0, "Msg" ) ) : nullptr;
		}( );
		if( msg )
			msg( "%s", buffer );
		OutputDebugStringA( buffer );
	}

}
