#include "qpc.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

	struct Handle {
		HANDLE m_value = nullptr;
		~Handle( ) {
			if( m_value && m_value != INVALID_HANDLE_VALUE )
				CloseHandle( m_value );
		}
		explicit operator bool( ) const {
			return m_value && m_value != INVALID_HANDLE_VALUE;
		}
	};

	struct Module {
		std::uintptr_t m_begin;
		std::uintptr_t m_end;
		std::string	   m_name;
	};

	DWORD find_main_thread( DWORD process_id ) {
		Handle snapshot{ CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 ) };
		if( !snapshot )
			return 0;
		THREADENTRY32 entry{ sizeof( entry ) };
		FILETIME	  earliest{ 0xffffffffu, 0xffffffffu };
		DWORD		  result = 0;
		if( !Thread32First( snapshot.m_value, &entry ) )
			return 0;
		do {
			if( entry.th32OwnerProcessID != process_id )
				continue;
			Handle	 thread{ OpenThread( THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID ) };
			FILETIME created{ }, exited{ }, kernel{ }, user{ };
			if( !thread || !GetThreadTimes( thread.m_value, &created, &exited, &kernel, &user ) )
				continue;
			if( CompareFileTime( &created, &earliest ) < 0 ) {
				earliest = created;
				result	 = entry.th32ThreadID;
			}
		} while( Thread32Next( snapshot.m_value, &entry ) );
		return result;
	}

	std::vector< Module > read_modules( DWORD process_id ) {
		std::vector< Module > modules;
		Handle				  snapshot{ CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id ) };
		if( !snapshot )
			return modules;
		MODULEENTRY32 entry{ sizeof( entry ) };
		if( !Module32First( snapshot.m_value, &entry ) )
			return modules;
		do {
			const auto begin = reinterpret_cast< std::uintptr_t >( entry.modBaseAddr );
			modules.push_back( { begin, begin + entry.modBaseSize, entry.szModule } );
		} while( Module32Next( snapshot.m_value, &entry ) );
		std::sort( modules.begin( ), modules.end( ), []( const Module& a, const Module& b ) { return a.m_begin < b.m_begin; } );
		return modules;
	}

	const Module* find_module( const std::vector< Module >& modules, std::uintptr_t address ) {
		const auto found = std::upper_bound( modules.begin( ), modules.end( ), address, []( std::uintptr_t value, const Module& module ) {
			return value < module.m_begin;
		} );
		if( found == modules.begin( ) )
			return nullptr;
		const Module& module = *std::prev( found );
		return address < module.m_end ? &module : nullptr;
	}

	void print_usage( ) {
		std::fputs( "usage: p2profile <pid> <duration-seconds> <output.csv> [--stack] [--thread <tid>]\n"
					"       p2profile <pid> --modules\n"
					"       default EIP sampling is low overhead; --stack is intrusive research mode\n",
			stderr );
	}

}

int main( int argc, char** argv ) {
	if( argc == 3 && std::strcmp( argv[ 2 ], "--modules" ) == 0 ) {
		const DWORD process_id = std::strtoul( argv[ 1 ], nullptr, 10 );
		const auto	modules	   = read_modules( process_id );
		if( !process_id || modules.empty( ) )
			return 1;
		std::fputs( "module,begin,end\n", stdout );
		for( const auto& module : modules ) {
			std::fprintf( stdout,
				"%s,0x%08llx,0x%08llx\n",
				module.m_name.c_str( ),
				static_cast< unsigned long long >( module.m_begin ),
				static_cast< unsigned long long >( module.m_end ) );
		}
		return 0;
	}
	if( argc < 4 ) {
		print_usage( );
		return 2;
	}
	bool  stack_mode		  = false;
	DWORD requested_thread_id = 0;
	for( int index = 4; index < argc; ++index ) {
		if( std::strcmp( argv[ index ], "--stack" ) == 0 ) {
			stack_mode = true;
		} else if( std::strcmp( argv[ index ], "--thread" ) == 0 && index + 1 < argc ) {
			requested_thread_id = std::strtoul( argv[ ++index ], nullptr, 10 );
		} else {
			print_usage( );
			return 2;
		}
	}
	const DWORD	 process_id		  = std::strtoul( argv[ 1 ], nullptr, 10 );
	const double duration_seconds = std::strtod( argv[ 2 ], nullptr );
	if( !process_id || !( duration_seconds > 0.0 ) ) {
		print_usage( );
		return 2;
	}
	const DWORD thread_id = requested_thread_id ? requested_thread_id : find_main_thread( process_id );
	Handle		process{ OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id ) };
	Handle		thread{ OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id ) };
	const auto	modules = read_modules( process_id );
	std::FILE*	output	= nullptr;
	if( !process || !thread || modules.empty( ) || fopen_s( &output, argv[ 3 ], "w" ) != 0 || !output ) {
		std::fprintf( stderr, "p2profile: attach/output failed (pid=%lu tid=%lu error=%lu)\n", process_id, thread_id, GetLastError( ) );
		return 1;
	}
	SymSetOptions( SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME );
	if( !SymInitialize( process.m_value, nullptr, TRUE ) ) {
		std::fprintf( stderr, "p2profile: SymInitialize failed (error=%lu)\n", GetLastError( ) );
		std::fclose( output );
		return 1;
	}

	const p2boost::Qpc::tick_t begin = p2boost::Qpc::now( );
	std::fputs( "qpc,sample_id,depth,thread_id,module,rva,eip,suspend_us\n", output );
	std::uint64_t samples = 0;
	for( ;; ) {
		const p2boost::Qpc::tick_t now	   = p2boost::Qpc::now( );
		const double			   elapsed = p2boost::Qpc::milliseconds( now - begin ) / 1000.0;
		if( elapsed >= duration_seconds )
			break;

		const p2boost::Qpc::tick_t suspend_begin = p2boost::Qpc::now( );
		if( SuspendThread( thread.m_value ) != static_cast< DWORD >( -1 ) ) {
			CONTEXT context{ };
			context.ContextFlags = CONTEXT_CONTROL;
			const BOOL captured	 = GetThreadContext( thread.m_value, &context );
			if( captured ) {
				STACKFRAME64 frame{ };
				frame.AddrPC.Offset	   = context.Eip;
				frame.AddrPC.Mode	   = AddrModeFlat;
				frame.AddrStack.Offset = context.Esp;
				frame.AddrStack.Mode   = AddrModeFlat;
				frame.AddrFrame.Offset = context.Ebp;
				frame.AddrFrame.Mode   = AddrModeFlat;
				struct CapturedFrame {
					std::uintptr_t m_address;
					unsigned int   m_depth;
				};
				std::vector< CapturedFrame > frames;
				const unsigned int			 depth_limit = stack_mode ? 24u : 1u;
				for( unsigned int depth = 0; depth < depth_limit && frame.AddrPC.Offset; ++depth ) {
					const auto address = static_cast< std::uintptr_t >( frame.AddrPC.Offset );
					frames.push_back( { address, depth } );
					if( depth + 1 >= depth_limit )
						break;
					if( !StackWalk64( IMAGE_FILE_MACHINE_I386,
							process.m_value,
							thread.m_value,
							&frame,
							&context,
							nullptr,
							SymFunctionTableAccess64,
							SymGetModuleBase64,
							nullptr ) )
						break;
				}
				const p2boost::Qpc::tick_t suspend_end = p2boost::Qpc::now( );
				ResumeThread( thread.m_value );
				const double suspend_us = p2boost::Qpc::milliseconds( suspend_end - suspend_begin ) * 1000.0;
				for( const auto& captured_frame : frames ) {
					const Module* module = find_module( modules, captured_frame.m_address );
					std::fprintf( output,
						"%lld,%llu,%u,%lu,%s,0x%08llx,0x%08llx,%.3f\n",
						now,
						static_cast< unsigned long long >( samples ),
						captured_frame.m_depth,
						thread_id,
						module ? module->m_name.c_str( ) : "<unknown>",
						static_cast< unsigned long long >( module ? captured_frame.m_address - module->m_begin : 0 ),
						static_cast< unsigned long long >( captured_frame.m_address ),
						suspend_us );
				}
				++samples;
			} else {
				ResumeThread( thread.m_value );
			}
		}
		Sleep( 1 );
	}
	SymCleanup( process.m_value );
	std::fclose( output );
	std::fprintf( stdout,
		"p2profile: %llu samples from pid=%lu tid=%lu\n",
		static_cast< unsigned long long >( samples ),
		process_id,
		thread_id );
	return samples ? 0 : 1;
}
