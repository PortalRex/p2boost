#include "address.h"
#include "display_adjustments.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "hook.h"
#include "nvidia_profile.h"
#include "pattern.h"
#include "portal2.h"
#include "runtime_verification.h"
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
		void*			 table[ 1 ] = { reinterpret_cast< void* >( &original ) };
		p2boost::VmtHook hook;
		if( !hook.install( &table[ 0 ], reinterpret_cast< void* >( &replacement ) ) )
			return false;
		if( table[ 0 ] != reinterpret_cast< void* >( &replacement ) || hook.previous( ) != reinterpret_cast< void* >( &original ) )
			return false;
		table[ 0 ] = reinterpret_cast< void* >( &original );  // simulate a later owner replacing our method.
		if( hook.remove( ) )
			return false;  // must not overwrite the later owner.
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
			   stack.arg( 0 ).to< std::uintptr_t >( ) == frame[ 2 ] && stack.next( ).as< std::uintptr_t >( ) == frame[ 0 ];
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
		return displacement == 0x100 && call.rel32( 1 ).as< std::uintptr_t >( ) == call.add( 0x105 ).as< std::uintptr_t >( );
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
		auto* memory = static_cast< unsigned char* >( VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		const std::vector< std::uint8_t > expected{ 0xff, 0x15, 1, 2, 3, 4 };
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::CallSitePatch patch;
		const bool			   installed = patch.install( memory, expected, reinterpret_cast< void* >( &replacement ) );
		const bool			   encoded	 = installed && memory[ 0 ] == 0xe8 && memory[ 5 ] == 0x90;
		const bool			   removed	 = patch.remove( ) && std::memcmp( memory, expected.data( ), expected.size( ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && encoded && removed;
	}

	bool test_call_site_chain( ) {
		auto* memory = static_cast< unsigned char* >( VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		const std::vector< std::uint8_t > expected{ 0xff, 0x15, 1, 2, 3, 4 };
		const std::uint8_t				  later_owner[]{ 0xe9, 5, 6, 7, 8, 0x90 };
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::CallSitePatch patch;
		const bool			   installed = patch.install( memory, expected, reinterpret_cast< void* >( &replacement ) );

		// a later owner must survive p2boost removal byte-for-byte.
		std::memcpy( memory, later_owner, sizeof( later_owner ) );
		const bool preserved = !patch.remove( ) && std::memcmp( memory, later_owner, sizeof( later_owner ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && preserved;
	}

	bool test_known_hash( ) {
		wchar_t path[ MAX_PATH ]{ };
		GetModuleFileNameW( nullptr, path, MAX_PATH );
		const std::string first	 = p2boost::sha256_file( path );
		const std::string second = p2boost::sha256_file( path );
		return first.size( ) == 64 && first == second;
	}

	bool test_overlay_abi_contract( ) {
		static_assert( p2boost::portal2::vmt::engine_get_max_clients == 20 );
		static_assert( p2boost::portal2::vmt::engine_vgui_paint == 14 );

		bool surface_fingerprinted{ };
		bool scheme_fingerprinted{ };
		bool material_system_fingerprinted{ };
		for( const auto& module : p2boost::supported_build_23973718( ) ) {
			if( std::strcmp( module.m_name, "materialsystem.dll" ) == 0 )
				material_system_fingerprinted = true;
			if( std::strcmp( module.m_name, "vguimatsurface.dll" ) == 0 )
				surface_fingerprinted = true;
			if( std::strcmp( module.m_name, "vgui2.dll" ) == 0 )
				scheme_fingerprinted = true;
		}

		return p2boost::supported_build_23973718( ).size( ) == 7 && material_system_fingerprinted && surface_fingerprinted &&
			   scheme_fingerprinted;
	}

	bool test_metrics( ) {
		auto metrics = std::make_unique< p2boost::FrameMetrics >( );
		metrics->set_tracing( true );
		metrics->set_simulating( true );
		metrics->set_focused( true );
		metrics->record_frame( );
		Sleep( 1 );
		metrics->record_shadow_update( 0.25, true );
		metrics->record_frame( );
		metrics->set_simulating( false );
		Sleep( 1 );
		metrics->record_frame( );
		const std::string path = "p2boost-test-metrics.csv";
		if( !metrics->dump( path ) )
			return false;
		std::ifstream input( path );
		std::string	  line;
		int			  lines = 0;
		while( std::getline( input, line ) )
			++lines;
		input.close( );
		DeleteFileA( path.c_str( ) );
		return lines == 3;
	}

	bool test_overlay_metrics( ) {
		auto metrics = std::make_unique< p2boost::FrameMetrics >( );
		metrics->set_overlay_collection( true );
		metrics->set_simulating( true );
		metrics->record_frame( );
		Sleep( 1 );
		metrics->record_shadow_update( 0.1, true );
		metrics->record_frame( );

		const p2boost::overlay_metrics_t result = metrics->overlay_metrics( );
		return result.m_average_ms > 0.0 && result.m_shadow_updates == 1 && result.m_shadow_hits == 1 && result.m_graph_count == 1 &&
			   std::isfinite( result.m_p99_ms );
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

	bool test_flat_texture_alpha( ) {
		// A8R8G8B8 stores BGRA bytes. flattening must replace only BGR.
		const std::uint8_t			argb_source[]{ 245, 246, 247, 0x19, 238, 239, 240, 0xc7 };
		std::uint8_t				argb_flat[ sizeof( argb_source ) ]{ };
		const p2boost::flat_color_t argb_color = p2boost::classify_flat_texture( D3DFMT_A8R8G8B8, argb_source, sizeof( argb_source ), 2, 1 );
		if( argb_color.m_red != 240 || argb_color.m_green != 239 || argb_color.m_blue != 238 ||
			!p2boost::write_flat_texture( D3DFMT_A8R8G8B8,
										  argb_flat,
										  sizeof( argb_flat ),
										  argb_source,
										  sizeof( argb_source ),
										  2,
										  1,
										  argb_color ) ||
			argb_flat[ 3 ] != argb_source[ 3 ] || argb_flat[ 7 ] != argb_source[ 7 ] )
			return false;

		// DXT1 index 3 is transparent only in the three-color mode. preserve
		// that exact mask while collapsing every opaque index to one endpoint.
		std::uint8_t		dxt1_source[ 8 ]{ };
		const std::uint16_t black		   = 0;
		const std::uint16_t white		   = 0xffffu;
		const std::uint32_t source_indices = 3u | ( 2u << 2u ) | ( 1u << 4u );
		std::memcpy( dxt1_source, &black, sizeof( black ) );
		std::memcpy( dxt1_source + 2, &white, sizeof( white ) );
		std::memcpy( dxt1_source + 4, &source_indices, sizeof( source_indices ) );
		std::uint8_t dxt1_flat[ sizeof( dxt1_source ) ]{ };
		if( !p2boost::write_flat_texture( D3DFMT_DXT1,
										  dxt1_flat,
										  sizeof( dxt1_flat ),
										  dxt1_source,
										  sizeof( dxt1_source ),
										  4,
										  4,
										  { 255, 255, 255 } ) )
			return false;
		std::uint32_t flat_indices{ };
		std::memcpy( &flat_indices, dxt1_flat + 4, sizeof( flat_indices ) );
		if( ( flat_indices & 3u ) != 3u || ( ( flat_indices >> 2u ) & 3u ) != 0u || ( ( flat_indices >> 4u ) & 3u ) != 0u )
			return false;

		// DXT5's first eight bytes contain the complete alpha block. they must
		// survive byte-for-byte even though its color block is rewritten.
		std::uint8_t dxt5_source[ 16 ]{ 0x20, 0xe0, 1, 2, 3, 4, 5, 6, 0xff, 0xff, 0, 0, 0x1b, 0xe4, 0x39, 0x4e };
		std::uint8_t dxt5_flat[ sizeof( dxt5_source ) ]{ };
		return p2boost::write_flat_texture( D3DFMT_DXT5,
											dxt5_flat,
											sizeof( dxt5_flat ),
											dxt5_source,
											sizeof( dxt5_source ),
											4,
											4,
											{ 0, 0, 0 } ) &&
			   std::memcmp( dxt5_flat, dxt5_source, 8 ) == 0 && std::memcmp( dxt5_flat + 8, dxt5_source + 8, 8 ) != 0;
	}

	bool test_flat_texture_dominant_color( ) {
		// a small saturated accent must not tint an otherwise neutral surface.
		std::array< std::uint8_t, 10u * 4u > source{ };
		for( size_t pixel = 0; pixel < 8; ++pixel ) {
			source[ pixel * 4u ]	  = 48;
			source[ pixel * 4u + 1u ] = 48;
			source[ pixel * 4u + 2u ] = 48;
			source[ pixel * 4u + 3u ] = 255;
		}
		for( size_t pixel = 8; pixel < 10; ++pixel ) {
			source[ pixel * 4u ]	  = 255;
			source[ pixel * 4u + 1u ] = 255;
			source[ pixel * 4u + 2u ] = 0;
			source[ pixel * 4u + 3u ] = 255;
		}

		const auto color = p2boost::classify_flat_texture( D3DFMT_A8R8G8B8, source.data( ), 40, 10, 1 );
		return color.m_red == 48 && color.m_green == 48 && color.m_blue == 48;
	}

	bool test_monitor_gamma_curve( ) {
		// 2.2 is a zero-cost identity; lower values brighten and upper values darken.
		const float neutral = p2boost::monitor_gamma_curve( 2.2f );
		const float bright	= p2boost::monitor_gamma_curve( 1.6f );
		const float dark	= p2boost::monitor_gamma_curve( 2.6f );
		return std::fabs( neutral - 1.0f ) < 0.0001f && bright < neutral && dark > neutral;
	}

	bool test_viewport_scale_rects( ) {
		const D3DSURFACE_DESC	  target{ D3DFMT_A8R8G8B8, D3DRTYPE_SURFACE, 0, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0, 2560, 1440 };
		p2boost::viewport_scale_t result{ };

		// Portal 2 keeps player one's origin at zero and scales its slot size.
		const D3DVIEWPORT9 player_one{ 0, 0, 640, 720, 0.0f, 1.0f };
		if( !p2boost::scaled_viewport_rect( player_one, target, 0.5f, result ) ||
			result.m_source.left != 0 || result.m_source.right != 640 || result.m_destination.left != 0 || result.m_destination.right != 1280 ||
			result.m_destination.bottom != 1440 )
			return false;

		// Player two retains the native half-screen origin instead of moving into
		// a globally scaled coordinate space.
		const D3DVIEWPORT9 player_two{ 1280, 0, 640, 720, 0.0f, 1.0f };
		if( !p2boost::scaled_viewport_rect( player_two, target, 0.5f, result ) || result.m_source.left != 1280 ||
			result.m_source.right != 1920 || result.m_destination.left != 1280 || result.m_destination.right != 2560 )
			return false;

		// Full-screen views use the same calculation and still fill the target.
		const D3DVIEWPORT9 full_view{ 0, 0, 1280, 720, 0.0f, 1.0f };
		if( !p2boost::scaled_viewport_rect( full_view, target, 0.5f, result ) || result.m_destination.right != 2560 ||
			result.m_destination.bottom != 1440 )
			return false;

		// Tiny engine borders and rectangles that would leave the render target
		// must never be mistaken for scaled gameplay views.
		const D3DVIEWPORT9 border{ 0, 0, 1, 1440, 0.0f, 1.0f };
		const D3DVIEWPORT9 outside{ 1920, 0, 640, 720, 0.0f, 1.0f };
		return !p2boost::scaled_viewport_rect( border, target, 0.5f, result ) &&
			   !p2boost::scaled_viewport_rect( outside, target, 0.5f, result );
	}

	bool test_nvidia_vendor_filter( ) {
		// PCI vendor 10DE is NVIDIA; AMD, Intel, and unknown vendors are skipped.
		return p2boost::needs_nvidia_profile( 0x10de ) && !p2boost::needs_nvidia_profile( 0x1002 ) &&
			   !p2boost::needs_nvidia_profile( 0x8086 ) && !p2boost::needs_nvidia_profile( 0 );
	}

}

int main( ) {
	const bool address		  = test_address_and_stack_frame( );
	const bool relative_call  = test_relative_call_encoding( );
	const bool pattern		  = test_pattern_scanner( );
	const bool hook			  = test_hook_chain( );
	const bool callsite		  = test_call_site_patch( );
	const bool callsite_chain = test_call_site_chain( );
	const bool hash			  = test_known_hash( );
	const bool overlay_abi	  = test_overlay_abi_contract( );
	const bool metrics		  = test_metrics( );
	const bool overlay		  = test_overlay_metrics( );
	const bool phase		  = test_frame_phase_carry( );
	const bool qpc			  = test_qpc( );
	const bool flat_alpha	  = test_flat_texture_alpha( );
	const bool flat_color	  = test_flat_texture_dominant_color( );
	const bool gamma_curve	  = test_monitor_gamma_curve( );
	const bool viewport_scale = test_viewport_scale_rects( );
	const bool nvidia_vendor  = test_nvidia_vendor_filter( );
	std::printf( "address=%s rel32=%s pattern=%s hook_chain=%s callsite=%s callsite_chain=%s "
				 "hash=%s overlay_abi=%s metrics=%s overlay=%s "
				 "phase=%s qpc=%s flat_alpha=%s flat_color=%s gamma_curve=%s viewport_scale=%s nvidia_vendor=%s\n",
				 address ? "ok" : "FAIL",
				 relative_call ? "ok" : "FAIL",
				 pattern ? "ok" : "FAIL",
				 hook ? "ok" : "FAIL",
				 callsite ? "ok" : "FAIL",
				 callsite_chain ? "ok" : "FAIL",
				 hash ? "ok" : "FAIL",
				 overlay_abi ? "ok" : "FAIL",
				 metrics ? "ok" : "FAIL",
				 overlay ? "ok" : "FAIL",
				 phase ? "ok" : "FAIL",
				 qpc ? "ok" : "FAIL",
				 flat_alpha ? "ok" : "FAIL",
				 flat_color ? "ok" : "FAIL",
				 gamma_curve ? "ok" : "FAIL",
				 viewport_scale ? "ok" : "FAIL",
				 nvidia_vendor ? "ok" : "FAIL" );
	return address && relative_call && pattern && hook && callsite && callsite_chain && hash && overlay_abi && metrics && overlay && phase && qpc && flat_alpha && flat_color && gamma_curve &&
				   viewport_scale && nvidia_vendor
			   ? 0
			   : 1;
}
