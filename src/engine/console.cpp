#include "console.h"

#include "module.h"

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
		using msg_fn_t = void( __cdecl* )( const char*, ... );
		static msg_fn_t msg = Module{ L"tier0.dll" }.export_function< msg_fn_t >( "Msg" );
		if( msg )
			msg( "%s", buffer );
		OutputDebugStringA( buffer );
	}

}
