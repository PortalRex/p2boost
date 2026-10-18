#include "address.h"
#include "client_view.h"
#include "display_adjustments.h"
#include "flashlight_buckets.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "hook.h"
#include "nvidia_profile.h"
#include "pattern.h"
#include "game_profile.h"
#include "projected_frustum.h"
#include "runtime_verification.h"
#include "game_profile.h"
#include "stack.h"
#include "x86.h"
#include "xinput_throttle.h"

#include <Windows.h>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
namespace {
	volatile int                hook_marker = 0;
	__declspec( noinline ) void original( ) {
		hook_marker = 1;
	}
	__declspec( noinline ) void replacement( ) {
		hook_marker = 2;
	}

	bool test_hook_chain( ) {
		void*            table[ 1 ] = { reinterpret_cast< void* >( &original ) };
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
		std::uint8_t           instruction[ 5 ]{ };
		const p2boost::Address call( instruction );
		const auto             encoded = p2boost::x86::relative_call( call, call.add( 0x105 ) );
		if( encoded.size( ) != 5 || encoded[ 0 ] != 0xe8 )
			return false;
		std::int32_t displacement = 0;
		std::memcpy( &displacement, encoded.data( ) + 1, sizeof( displacement ) );
		std::memcpy( instruction + 1, &displacement, sizeof( displacement ) );
		return displacement == 0x100 && call.rel32( 1 ).as< std::uintptr_t >( ) == call.add( 0x105 ).as< std::uintptr_t >( );
	}

	bool test_pattern_scanner( ) {
		const std::uint8_t bytes[]{ 0x10, 0x48, 0x8b, 0x42, 0x20, 0x90 };
		const auto         signature = p2boost::pattern::parse( "48 8B ? 20" );
		const auto         match     = p2boost::pattern::find( p2boost::Address( bytes ), sizeof( bytes ), signature );

		// malformed tokens must fail closed instead of weakening a signature.
		if( match.as< const std::uint8_t* >( ) != bytes + 1 || !p2boost::pattern::parse( "48 nope 20" ).empty( ) )
			return false;

		// construct a minimal mapped PE image to exercise uniqueness checks.
		std::vector< std::uint8_t > image( 512 );
		auto*                       dos = reinterpret_cast< IMAGE_DOS_HEADER* >( image.data( ) );
		dos->e_magic                    = IMAGE_DOS_SIGNATURE;
		dos->e_lfanew                   = sizeof( IMAGE_DOS_HEADER );
		auto* nt                        = reinterpret_cast< IMAGE_NT_HEADERS32* >( image.data( ) + dos->e_lfanew );
		nt->Signature                   = IMAGE_NT_SIGNATURE;
		nt->OptionalHeader.Magic        = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
		nt->OptionalHeader.SizeOfImage  = static_cast< DWORD >( image.size( ) );

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
		const bool             installed = patch.install( memory, expected, reinterpret_cast< void* >( &replacement ) );
		const bool             encoded   = installed && memory[ 0 ] == 0xe8 && memory[ 5 ] == 0x90;
		const bool             removed   = patch.remove( ) && std::memcmp( memory, expected.data( ), expected.size( ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && encoded && removed;
	}

	bool test_call_site_chain( ) {
		auto* memory = static_cast< unsigned char* >( VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !memory )
			return false;
		const std::vector< std::uint8_t > expected{ 0xff, 0x15, 1, 2, 3, 4 };
		const std::uint8_t                later_owner[]{ 0xe9, 5, 6, 7, 8, 0x90 };
		std::memcpy( memory, expected.data( ), expected.size( ) );
		p2boost::CallSitePatch patch;
		const bool             installed = patch.install( memory, expected, reinterpret_cast< void* >( &replacement ) );

		// a later owner must survive p2boost removal byte-for-byte.
		std::memcpy( memory, later_owner, sizeof( later_owner ) );
		const bool preserved = !patch.remove( ) && std::memcmp( memory, later_owner, sizeof( later_owner ) ) == 0;
		VirtualFree( memory, 0, MEM_RELEASE );
		return installed && preserved;
	}

	bool test_known_hash( ) {
		wchar_t path[ MAX_PATH ]{ };
		GetModuleFileNameW( nullptr, path, MAX_PATH );
		const std::string first  = p2boost::sha256_file( path );
		const std::string second = p2boost::sha256_file( path );
		return first.size( ) == 64 && first == second;
	}

	bool test_overlay_abi_contract( ) {
		static_assert( p2boost::sdk::IVEngineClient::get_screen_size_slot == 5 );
		static_assert( p2boost::sdk::IVEngineClient::get_max_clients_slot == 20 );
		static_assert( p2boost::sdk::IEngineVGui::paint_slot == 14 );
		static_assert( p2boost::sdk::ISurface::get_font_tall_slot == 72 );
		static_assert( p2boost::sdk::ISurface::get_kerned_char_width_slot == 147 );
		static_assert( p2boost::sdk::ISurface::draw_colored_text_slot == 160 );

		// every production hook signature must remain defined, and the
		// Portal Stories Mel profile must select its own active-shadow list
		// variant instead of the base game's.
		const auto& portal2 = p2boost::game::portal2( );
		const auto& psm     = p2boost::game::portal_stories_mel( );
		return portal2.frame_complete && portal2.frame_complete[ 0 ] && psm.active_shadow_depth_list &&
		       psm.active_shadow_depth_list[ 0 ] && std::strcmp( psm.active_shadow_depth_list, portal2.active_shadow_depth_list ) != 0;
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
		std::string   line;
		int           lines = 0;
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
		const auto               ticks = p2boost::Qpc::ticks( 2.5 );
		const p2boost::Stopwatch disabled{ false };
		return std::abs( p2boost::Qpc::milliseconds( ticks ) - 2.5 ) < 0.001 && disabled.elapsed( ) == 0;
	}

	bool test_flat_texture_alpha( ) {
		// A8R8G8B8 stores BGRA bytes. flattening must replace only BGR.
		const std::uint8_t          argb_source[]{ 245, 246, 247, 0x19, 238, 239, 240, 0xc7 };
		std::uint8_t                argb_flat[ sizeof( argb_source ) ]{ };
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
		std::uint8_t        dxt1_source[ 8 ]{ };
		const std::uint16_t black          = 0;
		const std::uint16_t white          = 0xffffu;
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
			source[ pixel * 4u ]      = 48;
			source[ pixel * 4u + 1u ] = 48;
			source[ pixel * 4u + 2u ] = 48;
			source[ pixel * 4u + 3u ] = 255;
		}
		for( size_t pixel = 8; pixel < 10; ++pixel ) {
			source[ pixel * 4u ]      = 255;
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
		const float bright  = p2boost::monitor_gamma_curve( 1.6f );
		const float dark    = p2boost::monitor_gamma_curve( 2.6f );
		return std::fabs( neutral - 1.0f ) < 0.0001f && bright < neutral && dark > neutral;
	}

	bool test_r_intensity_clamp( ) {
		// r_intensity is clamped to Quake Live's 0.0..2.0 range and 1.0 is neutral.
		p2boost::DisplayAdjustments adjustments;
		adjustments.set_intensity( 1.25f );
		if( std::fabs( adjustments.intensity( ) - 1.25f ) > 0.0001f )
			return false;
		adjustments.set_intensity( 3.0f );
		if( std::fabs( adjustments.intensity( ) - p2boost::max_intensity ) > 0.0001f )
			return false;
		adjustments.set_intensity( -1.0f );
		return std::fabs( adjustments.intensity( ) - p2boost::min_intensity ) < 0.0001f;
	}

	bool test_viewport_scale_rects( ) {
		const D3DSURFACE_DESC     target{ D3DFMT_A8R8G8B8, D3DRTYPE_SURFACE, 0, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE, 0, 2560, 1440 };
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

		// a non-even scale leaves the lround destination a pixel short of the
		// target edge; it must snap to the exact edge so no stale strip shows.
		const D3DVIEWPORT9 odd_view{ 0, 0, 844, 475, 0.0f, 1.0f };
		if( !p2boost::scaled_viewport_rect( odd_view, target, 0.33f, result ) || result.m_destination.right != 2560 ||
		    result.m_destination.bottom != 1440 )
			return false;

		// Tiny engine borders and rectangles that would leave the render target
		// must never be mistaken for scaled gameplay views.
		const D3DVIEWPORT9 border{ 0, 0, 1, 1440, 0.0f, 1.0f };
		const D3DVIEWPORT9 outside{ 1920, 0, 640, 720, 0.0f, 1.0f };
		return !p2boost::scaled_viewport_rect( border, target, 0.5f, result ) &&
		       !p2boost::scaled_viewport_rect( outside, target, 0.5f, result );
	}

	bool test_degenerate_viewport_clamp( ) {
		// ss_force_primary_fullscreen gives the hidden slot a 1x1 corner that
		// scales to 0x0; D3D9 would keep the previous fullscreen viewport and
		// paint the hidden 3D scene stretched across the screen. The clamp must
		// restore a minimal one-pixel rect while leaving real viewports alone.
		const D3DVIEWPORT9 hidden_corner{ 2560, 1440, 0, 0, 0.0f, 1.0f };
		const auto         clamped = p2boost::clamp_degenerate_viewport( hidden_corner );
		if( clamped.Width != 1 || clamped.Height != 1 || clamped.X != 2560 || clamped.Y != 1440 )
			return false;

		const D3DVIEWPORT9 missing_width{ 2560, 1440, 0, 720, 0.0f, 1.0f };
		if( p2boost::clamp_degenerate_viewport( missing_width ).Width != 1 )
			return false;

		const D3DVIEWPORT9 missing_height{ 2560, 1440, 640, 0, 0.0f, 1.0f };
		if( p2boost::clamp_degenerate_viewport( missing_height ).Height != 1 )
			return false;

		const D3DVIEWPORT9 real_split{ 0, 0, 640, 720, 0.0f, 1.0f };
		const auto         unchanged = p2boost::clamp_degenerate_viewport( real_split );
		return unchanged.Width == 640 && unchanged.Height == 720 && unchanged.X == 0 && unchanged.Y == 0;
	}
	bool test_viewport_commit_signature( ) {
		// the exact CommitSetViewports prologue verified in build 23973718's
		// shaderapivk.dll at 0x100127b7. two conditional branches guard the
		// D3D9 SetViewport dispatch and swallow zero-size viewports.
		constexpr std::uint8_t commit_bytes[]{
		    0x83, 0x7e, 0x08, 0x00, 0x74, 0x33, 0x83, 0x7e, 0x0c, 0x00, 0x74, 0x2d, 0xa1, 0xd8, 0xa5, 0x0d,
		    0x10, 0x8b, 0x08, 0x8b, 0x91, 0xbc, 0x00, 0x00, 0x00 };

		const auto signature = p2boost::pattern::parse( p2boost::game::portal2( ).viewport_commit_skip );
		if( signature.empty( ) )
			return false;

		const auto match = p2boost::pattern::find( p2boost::Address( commit_bytes ), sizeof( commit_bytes ), signature );
		if( !match || match.as< std::uint8_t* >( ) != commit_bytes )
			return false;

		// the signature's first and second branches land exactly on the JZ bytes.
		const std::uint8_t* first  = match.as< std::uint8_t* >( ) + 4;
		const std::uint8_t* second = match.as< std::uint8_t* >( ) + 10;
		if( first[ 0 ] != 0x74 || second[ 0 ] != 0x74 )
			return false;

		// neutralising both JZ branches with NOPs falls through to the device
		// dispatch for zero-size viewports; non-zero viewports take the exact
		// same path they did before the patch.
		std::uint8_t patched[ sizeof( commit_bytes ) ];
		std::memcpy( patched, commit_bytes, sizeof( commit_bytes ) );
		patched[ 4 ]  = 0x90;
		patched[ 5 ]  = 0x90;
		patched[ 10 ] = 0x90;
		patched[ 11 ] = 0x90;
		return patched[ 4 ] == 0x90 && patched[ 5 ] == 0x90 && patched[ 10 ] == 0x90 && patched[ 11 ] == 0x90 &&
		       patched[ 0 ] == 0x83 && patched[ 12 ] == 0xa1;
	}

	bool test_nvidia_vendor_filter( ) {
		// PCI vendor 10DE is NVIDIA; AMD, Intel, and unknown vendors are skipped.
		return p2boost::needs_nvidia_profile( 0x10de ) && !p2boost::needs_nvidia_profile( 0x1002 ) &&
		       !p2boost::needs_nvidia_profile( 0x8086 ) && !p2boost::needs_nvidia_profile( 0 );
	}

	bool test_dxvk_fingerprints( ) {
		// the shipped build must stay fingerprinted with its exact hash.
		const auto& shipped = p2boost::shipped_dxvk_sha256( );
		if( shipped.empty( ) )
			return false;
		for( const char* hash : shipped ) {
			if( !hash || std::strlen( hash ) != 64 )
				return false;
		}

		// every approved replacement must be an exact 64-hex hash that differs
		// from the shipped binary, so verification can never approximate.
		const auto& replacements = p2boost::supported_dxvk_replacements( );
		if( replacements.empty( ) )
			return false;
		for( const char* replacement : replacements ) {
			if( !replacement || std::strlen( replacement ) != 64 )
				return false;
			for( size_t index = 0; replacement[ index ]; ++index ) {
				if( !std::isxdigit( static_cast< unsigned char >( replacement[ index ] ) ) )
					return false;
			}
			for( const char* hash : shipped ) {
				if( std::strcmp( replacement, hash ) == 0 )
					return false;
			}
		}
		return true;
	}

	bool test_projected_frustum_views( ) {
		// encode the exact private view fields once, then consume them only through
		// the build-specific wrapper used by the production shadow optimizer.
		std::array< std::uint8_t, 0xa0 > bytes{ };
		auto                             write = [ &bytes ]( std::ptrdiff_t offset, const auto& value ) {
            std::memcpy( bytes.data( ) + offset, &value, sizeof( value ) );
		};

		const p2boost::vec3_t origin{ 0.0f, 0.0f, 0.0f };
		const p2boost::vec3_t angles{ 0.0f, 0.0f, 0.0f };
		const float           near_z = 1.0f;
		const float           far_z  = 10.0f;
		const float           fov    = 90.0f;
		const float           aspect = 1.0f;
		write( p2boost::game::portal2( ).view_origin, origin );
		write( p2boost::game::portal2( ).view_angles, angles );
		write( p2boost::game::portal2( ).view_near_z, near_z );
		write( p2boost::game::portal2( ).view_far_z, far_z );
		write( p2boost::game::portal2( ).view_fov, fov );
		write( p2boost::game::portal2( ).view_aspect_ratio, aspect );

		const p2boost::client_view_t          client_view{ bytes.data( ), &p2boost::game::portal2( ) };
		const p2boost::frustum::camera_view_t camera_view{
		    client_view.origin( ), client_view.angles( ), client_view.near_z( ), client_view.far_z( ), client_view.fov( ), client_view.aspect_ratio( ) };
		p2boost::frustum::frustum_t camera{ };
		if( !p2boost::frustum::camera_frustum( camera_view, camera ) || !p2boost::frustum::frusta_intersect( camera, camera ) )
			return false;

		// translating the same finite volume far away must produce a separating axis.
		p2boost::frustum::frustum_t distant = camera;
		for( p2boost::vec3_t& corner : distant )
			corner = corner + p2boost::vec3_t{ 1000.0f, 0.0f, 0.0f };
		return !p2boost::frustum::frusta_intersect( camera, distant );
	}

	// reference tier1 CUtlLinkedList and CMaterialsBuckets behavior, transcribed
	// from Valve's utllinkedlist.h and MaterialBuckets.h, over fixed storage.
	struct reference_buckets_t {
		static constexpr std::uint16_t invalid = p2boost::invalid_list_index;

		std::vector< p2boost::sort_id_node_t >       sort_ids = std::vector< p2boost::sort_id_node_t >( 64 );
		std::vector< p2boost::surface_node_t >       surfaces = std::vector< p2boost::surface_node_t >( 512 );
		std::vector< p2boost::material_sort_info_t > infos    = std::vector< p2boost::material_sort_info_t >( 16 );
		p2boost::material_buckets_t                  buckets{ };

		reference_buckets_t( ) {
			init( buckets.m_used_sort_ids, sort_ids.data( ), static_cast< int >( sort_ids.size( ) ) );
			init( buckets.m_elements, surfaces.data( ), static_cast< int >( surfaces.size( ) ) );
			for( p2boost::material_sort_info_t& info : infos )
				info = { -1, invalid, 0 };
			buckets.m_sort_info      = infos.data( );
			buckets.m_sort_info_size = static_cast< int >( infos.size( ) );
			buckets.m_flush_count    = -1;
		}

		reference_buckets_t( const reference_buckets_t& other )
		    : sort_ids( other.sort_ids ), surfaces( other.surfaces ), infos( other.infos ), buckets( other.buckets ) {
			buckets.m_used_sort_ids.m_memory = reinterpret_cast< std::uint8_t* >( sort_ids.data( ) );
			buckets.m_elements.m_memory      = reinterpret_cast< std::uint8_t* >( surfaces.data( ) );
			buckets.m_sort_info              = infos.data( );
		}

		template < typename node_t >
		static void init( p2boost::utl_linked_list_t& list, node_t* memory, int count ) {
			list                    = { };
			list.m_memory           = reinterpret_cast< std::uint8_t* >( memory );
			list.m_allocation_count = count;
			list.m_head = list.m_tail = list.m_first_free = list.m_last_alloc = invalid;
		}

		template < typename node_t >
		static node_t& node( p2boost::utl_linked_list_t& list, std::uint16_t index ) {
			return reinterpret_cast< node_t* >( list.m_memory )[ index ];
		}

		template < typename node_t >
		static std::uint16_t alloc( p2boost::utl_linked_list_t& list, bool multilist ) {
			std::uint16_t element;
			if( list.m_first_free == invalid ) {
				element           = list.m_last_alloc == invalid ? 0 : static_cast< std::uint16_t >( list.m_last_alloc + 1 );
				list.m_last_alloc = element;
				++list.m_num_allocated;
			} else {
				element           = list.m_first_free;
				list.m_first_free = node< node_t >( list, element ).m_next;
			}
			node< node_t >( list, element ).m_next     = multilist ? invalid : element;
			node< node_t >( list, element ).m_previous = multilist ? invalid : element;
			return element;
		}

		template < typename node_t >
		static void link_before( p2boost::utl_linked_list_t& list, std::uint16_t before, std::uint16_t element ) {
			node_t& added = node< node_t >( list, element );
			if( added.m_previous != element ) {
				// Unlink: a fresh multilist node counts as linked with invalid neighbors.
				if( added.m_previous != invalid )
					node< node_t >( list, added.m_previous ).m_next = added.m_next;
				else
					list.m_head = added.m_next;
				if( added.m_next != invalid )
					node< node_t >( list, added.m_next ).m_previous = added.m_previous;
				else
					list.m_tail = added.m_previous;
				added.m_previous = added.m_next = element;
				--list.m_element_count;
			}
			added.m_next = before;
			std::uint16_t previous;
			if( before == invalid ) {
				previous    = list.m_tail;
				list.m_tail = element;
			} else {
				previous                                  = node< node_t >( list, before ).m_previous;
				node< node_t >( list, before ).m_previous = element;
			}
			added.m_previous = previous;
			if( previous == invalid )
				list.m_head = element;
			else
				node< node_t >( list, previous ).m_next = element;
			++list.m_element_count;
		}

		void add_element( std::uint16_t sort_id, void* surface ) {
			const std::uint16_t element   = alloc< p2boost::surface_node_t >( buckets.m_elements, true );
			surfaces[ element ].m_element = surface;
			if( infos[ sort_id ].m_flush_count != buckets.m_flush_count ) {
				infos[ sort_id ].m_flush_count = buckets.m_flush_count;
				const std::uint16_t used       = alloc< p2boost::sort_id_node_t >( buckets.m_used_sort_ids, false );
				link_before< p2boost::sort_id_node_t >( buckets.m_used_sort_ids, invalid, used );
				sort_ids[ used ].m_element = sort_id;
			} else {
				link_before< p2boost::surface_node_t >( buckets.m_elements, infos[ sort_id ].m_head, element );
			}
			infos[ sort_id ].m_head = element;
		}

		bool valid_surface( std::uint16_t index ) const {
			const p2boost::utl_linked_list_t& list = buckets.m_elements;
			return list.m_last_alloc != invalid && index <= list.m_last_alloc &&
			       ( surfaces[ index ].m_previous != index || surfaces[ index ].m_next == index );
		}

		// the stock multilist RemoveAll: scan every slot ever allocated.
		void remove_all_surfaces( ) {
			p2boost::utl_linked_list_t& list = buckets.m_elements;
			if( list.m_last_alloc == invalid )
				return;
			for( std::uint16_t index = 0;; ++index ) {
				if( valid_surface( index ) ) {
					surfaces[ index ].m_previous = index;
					surfaces[ index ].m_next     = list.m_first_free;
					list.m_first_free            = index;
				}
				if( index == list.m_last_alloc )
					break;
			}
			list.m_head = list.m_tail = invalid;
			list.m_element_count      = 0;
		}

		void remove_all_sort_ids( ) {
			p2boost::utl_linked_list_t& list = buckets.m_used_sort_ids;
			for( std::uint16_t index = list.m_head; index != invalid; ) {
				const std::uint16_t next = sort_ids[ index ].m_next;
				sort_ids[ index ].m_next = list.m_first_free;
				list.m_first_free        = index;
				index                    = next;
			}
			list.m_head = list.m_tail = invalid;
			list.m_element_count      = 0;
		}

		void flush( bool used_only ) {
			++buckets.m_flush_count;
			if( !used_only || !p2boost::release_used_surface_elements( buckets ) )
				remove_all_surfaces( );
			remove_all_sort_ids( );
		}

		// the observable state: each used sort id with its surfaces in draw order.
		std::vector< std::vector< void* > > draw_lists( ) {
			std::vector< std::vector< void* > > out;
			for( std::uint16_t used = buckets.m_used_sort_ids.m_head; used != invalid; used = sort_ids[ used ].m_next ) {
				std::vector< void* > list{ reinterpret_cast< void* >( static_cast< std::uintptr_t >( sort_ids[ used ].m_element ) ) };
				for( std::uint16_t element = infos[ sort_ids[ used ].m_element ].m_head; element != invalid; element = surfaces[ element ].m_next )
					list.push_back( surfaces[ element ].m_element );
				out.push_back( list );
			}
			return out;
		}
	};

	bool test_flashlight_bucket_signature( ) {
		// CShadowMgr::ClearAllFlashlightMaterialBuckets at engine.dll 0x101222c0
		// in build 23973718; its calls reach RemoveAll at 0x10120bd0 and 0x10094e30.
		constexpr std::uint8_t clear_bytes[]{
		    0x53, 0x8b, 0xd9, 0x80, 0xbb, 0xb8, 0x41, 0x01, 0x00, 0x00, 0x75, 0x59, 0x0f, 0xb7, 0x83, 0x9c, 0x41, 0x01, 0x00, 0xb9, 0xff,
		    0xff, 0x00, 0x00, 0x66, 0x3b, 0xc1, 0x74, 0x48, 0x8b, 0x8b, 0x90, 0x41, 0x01, 0x00, 0x56, 0x57, 0x0f, 0xb7, 0xc0, 0x8d, 0x34,
		    0x40, 0xc1, 0xe6, 0x08, 0xff, 0x84, 0x31, 0x8c, 0x02, 0x00, 0x00, 0x8d, 0xbc, 0x31, 0x40, 0x02, 0x00, 0x00, 0x8d, 0x4f, 0x30,
		    0xe8, 0xcc, 0xe8, 0xff, 0xff, 0x8b, 0xcf, 0xe8, 0x25, 0x2b, 0xf7, 0xff, 0x8b, 0x8b, 0x90, 0x41, 0x01, 0x00, 0x0f, 0xb7, 0x84,
		    0x31, 0xf2, 0x02, 0x00, 0x00, 0xba, 0xff, 0xff, 0x00, 0x00, 0x66, 0x3b, 0xc2, 0x75, 0xc2, 0x5f, 0x5e, 0x5b, 0xc3 };

		const p2boost::game::profile_t& game      = p2boost::game::portal2( );
		const auto                      signature = p2boost::pattern::parse( game.flashlight_bucket_flush );
		if( signature.size( ) != sizeof( clear_bytes ) )
			return false;
		const p2boost::Address match = p2boost::pattern::find( p2boost::Address( clear_bytes ), sizeof( clear_bytes ), signature );
		if( match.as< const std::uint8_t* >( ) != clear_bytes )
			return false;

		// rebase both call operands onto the preferred image address.
		const std::uintptr_t base     = reinterpret_cast< std::uintptr_t >( clear_bytes );
		const std::uintptr_t elements = match.rel32( game.xref_flashlight_elements_remove_all ).as< std::uintptr_t >( ) - base + 0x101222c0u;
		const std::uintptr_t sort_ids = match.rel32( game.xref_flashlight_sort_ids_remove_all ).as< std::uintptr_t >( ) - base + 0x101222c0u;
		return elements == 0x10120bd0u && sort_ids == 0x10094e30u;
	}

	bool test_xinput_throttle( ) {
		// inputsystem.dll PollXDevices at 0x10006eb4 in build 23973718: the
		// per-slot call through the stored XInputGetState pointer 0x100275f8.
		constexpr std::uint8_t poll_bytes[]{ 0x8b, 0x47, 0x2c, 0x03, 0xc0, 0x8d, 0x4c, 0xc7, 0x0c, 0x51, 0x53, 0xff, 0x15,
		                                     0xf8, 0x75, 0x02, 0x10, 0x85, 0xc0, 0x0f, 0x84, 0x8d, 0x00, 0x00, 0x00 };

		const p2boost::game::profile_t& game      = p2boost::game::portal2( );
		const auto                      signature = p2boost::pattern::parse( game.xinput_get_state_call );
		const p2boost::Address          match     = p2boost::pattern::find( p2boost::Address( poll_bytes ), sizeof( poll_bytes ), signature );
		if( match.as< const std::uint8_t* >( ) != poll_bytes || match.at< std::uintptr_t >( game.xref_xinput_get_state_pointer ) != 0x100275f8u )
			return false;

		// only a slot already known absent may skip, and only until its next probe.
		return !p2boost::xinput_probe_skippable( false, 100, 200 ) && p2boost::xinput_probe_skippable( true, 100, 200 ) &&
		       !p2boost::xinput_probe_skippable( true, 200, 200 ) && !p2boost::xinput_probe_skippable( true, 300, 200 );
	}

	bool test_flashlight_bucket_flush( ) {
		if( !test_flashlight_bucket_signature( ) )
			return false;

		reference_buckets_t stock;
		reference_buckets_t fast{ stock };
		std::uint32_t       seed = 0x2545f491u;
		auto                next = [ &seed ]( std::uint32_t range ) {
            seed ^= seed << 13u;
            seed ^= seed >> 17u;
            seed ^= seed << 5u;
            return seed % range;
		};

		// varying fill levels leave a high-water mark far above later frames.
		for( int round = 0; round < 200; ++round ) {
			const std::uint32_t count = round % 7 == 0 ? 400u : next( 40u );
			for( std::uint32_t index = 0; index < count; ++index ) {
				const auto  sort_id = static_cast< std::uint16_t >( next( 16u ) );
				void* const surface = reinterpret_cast< void* >( static_cast< std::uintptr_t >( next( 100000u ) + 1u ) );
				stock.add_element( sort_id, surface );
				fast.add_element( sort_id, surface );
			}
			if( stock.draw_lists( ) != fast.draw_lists( ) )
				return false;

			const int flushes = 1 + static_cast< int >( next( 3u ) );
			for( int flush = 0; flush < flushes; ++flush ) {
				stock.flush( false );
				fast.flush( true );
			}

			// both must release every element and leave identical list headers.
			const p2boost::utl_linked_list_t& a = stock.buckets.m_elements;
			const p2boost::utl_linked_list_t& b = fast.buckets.m_elements;
			if( a.m_head != b.m_head || a.m_tail != b.m_tail || a.m_element_count != b.m_element_count || a.m_last_alloc != b.m_last_alloc ||
			    stock.buckets.m_flush_count != fast.buckets.m_flush_count )
				return false;
			for( std::uint16_t index = 0; a.m_last_alloc != reference_buckets_t::invalid && index <= a.m_last_alloc; ++index ) {
				if( stock.valid_surface( index ) || fast.valid_surface( index ) )
					return false;
			}
			std::size_t free_count = 0;
			for( std::uint16_t index = b.m_first_free; index != reference_buckets_t::invalid; index = fast.surfaces[ index ].m_next ) {
				if( ++free_count > fast.surfaces.size( ) )
					return false;
			}
			if( a.m_last_alloc != reference_buckets_t::invalid && free_count != static_cast< std::size_t >( a.m_last_alloc ) + 1u )
				return false;
		}
		return true;
	}

}

int main( ) {
	const bool address        = test_address_and_stack_frame( );
	const bool relative_call  = test_relative_call_encoding( );
	const bool pattern        = test_pattern_scanner( );
	const bool hook           = test_hook_chain( );
	const bool callsite       = test_call_site_patch( );
	const bool callsite_chain = test_call_site_chain( );
	const bool hash           = test_known_hash( );

	const bool overlay_abi      = test_overlay_abi_contract( );
	const bool metrics          = test_metrics( );
	const bool overlay          = test_overlay_metrics( );
	const bool phase            = test_frame_phase_carry( );
	const bool qpc              = test_qpc( );
	const bool flat_alpha       = test_flat_texture_alpha( );
	const bool flat_color       = test_flat_texture_dominant_color( );
	const bool gamma_curve      = test_monitor_gamma_curve( );
	const bool intensity        = test_r_intensity_clamp( );
	const bool viewport_scale   = test_viewport_scale_rects( );
	const bool degenerate_clamp = test_degenerate_viewport_clamp( );
	const bool commit_patch     = test_viewport_commit_signature( );
	const bool nvidia_vendor    = test_nvidia_vendor_filter( );
	const bool dxvk_fp          = test_dxvk_fingerprints( );
	const bool frustum          = test_projected_frustum_views( );
	const bool flashlight_flush = test_flashlight_bucket_flush( );
	const bool xinput_throttle  = test_xinput_throttle( );
	std::printf( "address=%s rel32=%s pattern=%s hook_chain=%s callsite=%s callsite_chain=%s "
	             "hash=%s overlay_abi=%s metrics=%s overlay=%s "
	             "phase=%s qpc=%s flat_alpha=%s flat_color=%s gamma_curve=%s intensity=%s viewport_scale=%s degenerate_clamp=%s commit_patch=%s nvidia_vendor=%s dxvk_fp=%s frustum=%s flashlight_flush=%s xinput_throttle=%s\n",
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
	             intensity ? "ok" : "FAIL",
	             viewport_scale ? "ok" : "FAIL",
	             degenerate_clamp ? "ok" : "FAIL",
	             commit_patch ? "ok" : "FAIL",
	             nvidia_vendor ? "ok" : "FAIL",
	             dxvk_fp ? "ok" : "FAIL",
	             frustum ? "ok" : "FAIL",
	             flashlight_flush ? "ok" : "FAIL",
	             xinput_throttle ? "ok" : "FAIL" );
	return address && relative_call && pattern && hook && callsite && callsite_chain && hash && overlay_abi && metrics && overlay && phase && qpc && flat_alpha && flat_color && gamma_curve &&
	               intensity && viewport_scale && degenerate_clamp && commit_patch && nvidia_vendor && dxvk_fp && frustum && flashlight_flush && xinput_throttle
	           ? 0
	           : 1;
}
