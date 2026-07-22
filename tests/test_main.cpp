#include "address.h"
#include "build_fingerprint.h"
#include "frame_pacer.h"
#include "hook.h"
#include "metrics.h"
#include "pattern.h"
#include "stack.h"
#include "x86.h"

#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
	volatile int				hook_marker = 0;
	__declspec( noinline ) void original( ) {
		hook_marker = 1;
	}
	__declspec( noinline ) void replacement( ) {
		hook_marker = 2;
	}

	bool test_hook_chain( ) {
		void*					table[ 1 ] = { reinterpret_cast< void* >( &original ) };
		p2boost::VtableSlotHook hook;
		if( !hook.install( &table[ 0 ], reinterpret_cast< void* >( &replacement ) ) )
			return false;
		if( table[ 0 ] != reinterpret_cast< void* >( &replacement ) ||
			hook.previous( ) != reinterpret_cast< void* >( &original ) )
			return false;
		table[ 0 ] = reinterpret_cast< void* >( &original ); // Simulate a later owner replacing our slot.
		if( hook.remove( ) )
			return false; // Must not overwrite the later owner.
		return table[ 0 ] == reinterpret_cast< void* >( &original );
	}

	bool test_address_and_stack_frame( ) {
		std::uintptr_t frame[ 5 ]{ };
		frame[ 0 ] = reinterpret_cast< std::uintptr_t >( &frame[ 3 ] );
		frame[ 1 ] = reinterpret_cast< std::uintptr_t >( &original );
		frame[ 2 ] = 0x12345678u;

		const p2boost::Address address( frame );
		const p2boost::Stack   stack( address );
		return address.add( sizeof( std::uintptr_t ) ).to< std::uintptr_t >( ) == frame[ 1 ] &&
			   stack.ReturnAddress( ).as< void* >( ) == reinterpret_cast< void* >( &original ) &&
			   stack.arg( 0 ).to< std::uintptr_t >( ) == frame[ 2 ] &&
			   stack.next( ).as< std::uintptr_t >( ) == frame[ 0 ];
	}

	bool test_relative_call_encoding( ) {
		std::uint8_t		   instruction[ 5 ]{ };
		const p2boost::Address call( instruction );
		const auto			   encoded = p2boost::x86::relative_call( call, call.add( 0x105 ) );
		if( encoded.size( ) != 5 || encoded[ 0 ] != 0xe8 )
			return false;
		std::int32_t displacement = 0;
		std::memcpy( &displacement, encoded.data( ) + 1, sizeof( displacement ) );
		std::memcpy( instruction + 1, &displacement, sizeof( displacement ) );
		return displacement == 0x100 &&
			   call.rel32( 1 ).as< std::uintptr_t >( ) == call.add( 0x105 ).as< std::uintptr_t >( );
	}

	bool test_pattern_scanner( ) {
		const std::uint8_t bytes[]{ 0x10, 0x48, 0x8b, 0x42, 0x20, 0x90 };
		const auto		   signature = p2boost::pattern::parse( "48 8B ? 20" );
		const auto		   match	 = p2boost::pattern::find( p2boost::Address( bytes ), sizeof( bytes ), signature );

		// malformed tokens must fail closed instead of weakening a signature.
		if( match.as< const std::uint8_t* >( ) != bytes + 1 || !p2boost::pattern::parse( "48 nope 20" ).empty( ) )
			return false;

		// construct a minimal mapped PE image to exercise uniqueness checks.
		std::vector< std::uint8_t > image( 512 );
		auto*						dos = reinterpret_cast< IMAGE_DOS_HEADER* >( image.data( ) );
		dos->e_magic					= IMAGE_DOS_SIGNATURE;
		dos->e_lfanew					= sizeof( IMAGE_DOS_HEADER );
		auto* nt						= reinterpret_cast< IMAGE_NT_HEADERS32* >( image.data( ) + dos->e_lfanew );
		nt->Signature					= IMAGE_NT_SIGNATURE;
		nt->OptionalHeader.Magic		= IMAGE_NT_OPTIONAL_HDR32_MAGIC;
		nt->OptionalHeader.SizeOfImage	= static_cast< DWORD >( image.size( ) );

		const std::uint8_t unique_bytes[]{ 0xde, 0xad, 0x11, 0xef };
		std::memcpy( image.data( ) + 256, unique_bytes, sizeof( unique_bytes ) );
		const p2boost::Address base{ image.data( ) };
		if( p2boost::pattern::find_unique( base, "DE AD ? EF" ) != base.add( 256 ) )
			return false;

		// the same signature appearing twice must be rejected as ambiguous.
		std::memcpy( image.data( ) + 384, unique_bytes, sizeof( unique_bytes ) );
		return !p2boost::pattern::find_unique( base, "DE AD ? EF" );
	}

	bool test_call_site_patch( ) {
		auto* memory = static_cast< unsigned char* >(
			VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		const std::vector< std::uint8_t > expected{ 0xff, 0x15, 1, 2, 3, 4 };
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::CallSitePatch patch;
		const bool			   installed = patch.install( memory, expected, reinterpret_cast< void* >( &replacement ) );
		const bool			   encoded	 = installed && memory[ 0 ] == 0xe8 && memory[ 5 ] == 0x90;
		const bool removed = patch.remove( ) && std::memcmp( memory, expected.data( ), expected.size( ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && encoded && removed;
	}

	bool test_pointer_immediate_patch( ) {
		auto* memory = static_cast< unsigned char* >(
			VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		std::vector< std::uint8_t > expected{ 0xb8, 0, 0, 0, 0 };
		const auto original_pointer = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( &original ) );
		std::memcpy( expected.data( ) + 1, &original_pointer, sizeof( original_pointer ) );
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::PointerImmediatePatch patch;
		const bool	  installed = patch.install( memory, expected, 1, reinterpret_cast< void* >( &replacement ) );
		std::uint32_t encoded	= 0;
		std::memcpy( &encoded, memory + 1, sizeof( encoded ) );
		const bool points_to_replacement =
			installed &&
			encoded == static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( &replacement ) ) &&
			patch.previous( ) == reinterpret_cast< void* >( &original );
		const bool removed = patch.remove( ) && std::memcmp( memory, expected.data( ), expected.size( ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && points_to_replacement && removed;
	}

	bool test_inline_detour( ) {
		auto* memory = static_cast< unsigned char* >(
			VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		const std::vector< std::uint8_t > expected{ 0xa1, 1, 2, 3, 4 };
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::InlineDetour hook;
		const bool			  installed = hook.install( memory, expected, reinterpret_cast< void* >( &replacement ) );
		const bool			  encoded	= installed && memory[ 0 ] == 0xe9 && hook.previous( ) != nullptr;
		const bool removed = hook.remove( ) && std::memcmp( memory, expected.data( ), expected.size( ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && encoded && removed;
	}

	bool test_known_hash( ) {
		wchar_t path[ MAX_PATH ]{ };
		GetModuleFileNameW( nullptr, path, MAX_PATH );
		const std::string first	 = p2boost::sha256_file( path );
		const std::string second = p2boost::sha256_file( path );
		return first.size( ) == 64 && first == second;
	}

	bool test_metrics( ) {
		auto metrics = std::make_unique< p2boost::Metrics >( );
		metrics->set_tracing( true );
		p2boost::RenderWorkerMetrics worker{ };
		worker.m_begin_qpc	  = 100;
		worker.m_end_qpc	  = 200;
		worker.m_cycles		  = 75;
		worker.m_queued_calls = 4;
		metrics->record_render_worker( worker );
		metrics->frame( true );
		metrics->frame( false );
		const std::string path = "p2boost-test-metrics.csv";
		if( !metrics->dump( path ) )
			return false;
		std::ifstream input( path );
		std::string	  line;
		int			  lines = 0;
		while( std::getline( input, line ) )
			++lines;
		input.close( );
		std::ifstream workers( "p2boost-test-metrics_render_workers.csv" );
		int			  worker_lines = 0;
		while( std::getline( workers, line ) )
			++worker_lines;
		workers.close( );
		DeleteFileA( path.c_str( ) );
		DeleteFileA( "p2boost-test-metrics_render_workers.csv" );
		return lines == 3 && worker_lines == 2;
	}

	bool test_frame_phase_carry( ) {
		constexpr float epsilon = 0.000001f;
		return std::abs( p2boost::carry_frame_overshoot( 0.0021f, 0.0020f, true ) - 0.0001f ) < epsilon &&
			   p2boost::carry_frame_overshoot( 0.0026f, 0.0020f, true ) == 0.0f &&
			   p2boost::carry_frame_overshoot( 0.0021f, 0.0020f, false ) == 0.0f &&
			   p2boost::carry_frame_overshoot( 0.0019f, 0.0020f, true ) == 0.0f;
	}

	bool test_qpc( ) {
		const auto				 ticks = p2boost::Qpc::ticks( 2.5 );
		const p2boost::Stopwatch disabled{ false };
		return std::abs( p2boost::Qpc::milliseconds( ticks ) - 2.5 ) < 0.001 && disabled.elapsed( ) == 0;
	}

}

int main( ) {
	const bool address		 = test_address_and_stack_frame( );
	const bool relative_call = test_relative_call_encoding( );
	const bool pattern		 = test_pattern_scanner( );
	const bool hook			 = test_hook_chain( );
	const bool callsite		 = test_call_site_patch( );
	const bool pointer		 = test_pointer_immediate_patch( );
	const bool inline_detour = test_inline_detour( );
	const bool hash			 = test_known_hash( );
	const bool metrics		 = test_metrics( );
	const bool phase		 = test_frame_phase_carry( );
	const bool qpc			 = test_qpc( );
	std::printf( "address=%s rel32=%s pattern=%s hook_chain=%s callsite=%s pointer=%s inline=%s hash=%s "
				 "metrics=%s "
				 "phase=%s qpc=%s\n",
		address ? "ok" : "FAIL",
		relative_call ? "ok" : "FAIL",
		pattern ? "ok" : "FAIL",
		hook ? "ok" : "FAIL",
		callsite ? "ok" : "FAIL",
		pointer ? "ok" : "FAIL",
		inline_detour ? "ok" : "FAIL",
		hash ? "ok" : "FAIL",
		metrics ? "ok" : "FAIL",
		phase ? "ok" : "FAIL",
		qpc ? "ok" : "FAIL" );
	return address && relative_call && pattern && hook && callsite && pointer && inline_detour && hash && metrics &&
				   phase && qpc
			   ? 0
			   : 1;
}
