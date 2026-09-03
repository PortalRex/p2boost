#include "runtime_verification.h"

#include "game_profile.h"
#include "game_profile.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace p2boost {
	namespace {

		// convert verified filesystem paths to UTF-8 for logs and commands.
		std::string narrow( const std::wstring& input ) {
			if( input.empty( ) )
				return { };

			const int size = WideCharToMultiByte( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), nullptr, 0, nullptr, nullptr );
			if( size <= 0 )
				return { };

			std::string output( static_cast< size_t >( size ), '\0' );
			WideCharToMultiByte( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), output.data( ), size, nullptr, nullptr );
			return output;
		}

		// normalize paths before comparing loader identity.
		std::wstring lower( std::wstring value ) {
			std::transform( value.begin( ), value.end( ), value.begin( ), []( wchar_t character ) {
				return static_cast< wchar_t >( std::towlower( character ) );
			} );
			return value;
		}

		std::wstring widen_ascii( const char* input ) {
			return std::wstring( input, input + std::strlen( input ) );
		}

		bool is_loaded_from( const Module& module, const std::wstring& expected_path ) {
			return module && lower( module.path( ) ) == lower( expected_path );
		}

	}

	const std::array< const char*, 1 >& shipped_dxvk_sha256( ) {
		static const std::array< const char*, 1 > hashes{ {
		    "7bf7c95d81ff33b951ff985037401ff5a792474f1e3b9c5d2f521affb92b9073",
		} };
		return hashes;
	}

	const std::array< const char*, 1 >& supported_dxvk_replacements( ) {
		// DXVK 3.0.2 x86 build. its parallel command stream and lower per-draw
		// translation cost directly reduce Portal 2's CPU-bound render path.
		static const std::array< const char*, 1 > replacements{ {
		    "44a2e749694128710cce3a545c954bd9d986dd6a8ea61bb08d931c2ef0190488",
		} };
		return replacements;
	}

	std::string sha256_file( const std::wstring& path ) {
		// permit Steam updates while hashing the currently installed file.
		const HANDLE file = CreateFileW( path.c_str( ),
		                                 GENERIC_READ,
		                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                                 nullptr,
		                                 OPEN_EXISTING,
		                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
		                                 nullptr );
		if( file == INVALID_HANDLE_VALUE )
			throw std::runtime_error( "CreateFileW failed" );

		BCRYPT_ALG_HANDLE               algorithm{ };
		BCRYPT_HASH_HANDLE              hash{ };
		DWORD                           object_size{ };
		DWORD                           result_size{ };
		std::vector< unsigned char >    object;
		std::array< unsigned char, 32 > digest{ };

		// own every BCrypt handle explicitly so all failure paths are reversible.
		try {
			if( BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) < 0 )
				throw std::runtime_error( "BCryptOpenAlgorithmProvider failed" );
			if( BCryptGetProperty( algorithm,
			                       BCRYPT_OBJECT_LENGTH,
			                       reinterpret_cast< PUCHAR >( &object_size ),
			                       sizeof( object_size ),
			                       &result_size,
			                       0 ) < 0 )
				throw std::runtime_error( "BCryptGetProperty failed" );

			object.resize( object_size );
			if( BCryptCreateHash( algorithm, &hash, object.data( ), object_size, nullptr, 0, 0 ) < 0 )
				throw std::runtime_error( "BCryptCreateHash failed" );

			std::array< unsigned char, 1 << 16 > buffer{ };
			for( ;; ) {
				DWORD bytes_read{ };
				if( !ReadFile( file, buffer.data( ), static_cast< DWORD >( buffer.size( ) ), &bytes_read, nullptr ) )
					throw std::runtime_error( "ReadFile failed" );
				if( bytes_read == 0 )
					break;
				if( BCryptHashData( hash, buffer.data( ), bytes_read, 0 ) < 0 )
					throw std::runtime_error( "BCryptHashData failed" );
			}

			if( BCryptFinishHash( hash, digest.data( ), static_cast< ULONG >( digest.size( ) ), 0 ) < 0 )
				throw std::runtime_error( "BCryptFinishHash failed" );
		} catch( ... ) {
			if( hash )
				BCryptDestroyHash( hash );
			if( algorithm )
				BCryptCloseAlgorithmProvider( algorithm, 0 );
			CloseHandle( file );
			throw;
		}

		BCryptDestroyHash( hash );
		BCryptCloseAlgorithmProvider( algorithm, 0 );
		CloseHandle( file );

		std::ostringstream output;
		for( const unsigned char byte : digest )
			output << std::hex << std::setw( 2 ) << std::setfill( '0' ) << static_cast< int >( byte );
		return output.str( );
	}

	verification_result_t verify_runtime( const portal2_modules_t& modules ) {
		verification_result_t result;

		// derive the installation root from the module the loader actually mapped.
		if( !modules.m_engine ) {
			result.m_errors.emplace_back( "engine.dll is not loaded" );
			return result;
		}

		const std::wstring engine_path = modules.m_engine.path( );
		const std::wstring suffix      = L"\\bin\\engine.dll";
		if( engine_path.size( ) <= suffix.size( ) || lower( engine_path.substr( engine_path.size( ) - suffix.size( ) ) ) != suffix ) {
			result.m_errors.emplace_back( "cannot derive the game root from engine.dll" );
			return result;
		}

		const std::wstring root = engine_path.substr( 0, engine_path.size( ) - suffix.size( ) );
		result.m_game_root      = narrow( root );

		// the game must map the shared engine modules from its own bin directory.
		const std::wstring material_system = root + L"\\bin\\materialsystem.dll";
		const std::wstring surface         = root + L"\\bin\\vguimatsurface.dll";
		const std::wstring vgui            = root + L"\\bin\\vgui2.dll";
		if( !is_loaded_from( modules.m_material_system, material_system ) )
			result.m_errors.emplace_back( "materialsystem.dll was not loaded from the game installation" );
		if( !is_loaded_from( modules.m_surface, surface ) )
			result.m_errors.emplace_back( "vguimatsurface.dll was not loaded from the game installation" );
		if( !is_loaded_from( modules.m_vgui, vgui ) )
			result.m_errors.emplace_back( "vgui2.dll was not loaded from the game installation" );

		// detection selects the running game and installs its signature strings
		// into portal2::signature; every check below reads the same names
		// regardless of which game is loaded.
		const game::profile_t* profile = game::detect( modules );
		if( !profile ) {
			result.m_errors.emplace_back( "client.dll was not loaded from a supported game installation" );
			return result;
		}
		result.m_game                  = profile->name;
		const std::wstring client_path = root + L"\\" + widen_ascii( profile->client_path );
		if( !is_loaded_from( modules.m_client, client_path ) )
			result.m_errors.emplace_back( "client.dll was not loaded from the game installation" );

		// the loaded bytes are the build fingerprint. every production hook site
		// must resolve to one unique signature, so a changed build disables only
		// the exact feature whose offsets moved instead of the whole plugin.
		if( !modules.m_engine.find( game::active( )->frame_complete ) )
			result.m_errors.emplace_back( "frame limiter signature not found in engine.dll" );
		if( !modules.m_shader_api.find( game::active( )->flat_texture_device ) )
			result.m_errors.emplace_back( "DXVK device signature not found in shaderapivk.dll" );
		if( !modules.m_shader_api.find( game::active( )->viewport_commit_skip ) )
			result.m_errors.emplace_back( "viewport-commit signature not found in shaderapivk.dll" );
		if( !modules.m_client.find( game::active( )->shadow_manager ) ||
		    !modules.m_client.find( game::active( )->flashlight_models ) ||
		    !modules.m_client.find( game::active( )->active_shadow_depth_call ) ||
		    !modules.m_client.find( game::active( )->active_shadow_manager ) )
			result.m_errors.emplace_back( "projected-shadow signature not found in client.dll" );
		if( !modules.m_client.find( game::active( )->active_shadow_depth_list ) )
			result.m_errors.emplace_back( "active-shadow list signature not found in client.dll" );

		// the dxvk hash only labels which binary the metrics describe. any dxvk
		// that satisfies the shipped shaderapivk interface keeps working, so a
		// Steam update to dxvk_d3d9.dll never invalidates the plugin.
		try {
			result.m_dxvk_sha256 = sha256_file( root + L"\\bin\\dxvk_d3d9.dll" );
			for( const char* shipped : shipped_dxvk_sha256( ) )
				result.m_dxvk_shipped = result.m_dxvk_shipped || result.m_dxvk_sha256 == shipped;
			for( const char* replacement : supported_dxvk_replacements( ) )
				result.m_dxvk_approved = result.m_dxvk_approved || result.m_dxvk_sha256 == replacement;
			result.m_dxvk_approved = result.m_dxvk_approved || result.m_dxvk_shipped;
		} catch( ... ) {
			// the backend identity check below reports the real problem.
		}

		// filenames alone are insufficient; verify the loaded backend paths too.
		const std::wstring shader_api = root + L"\\bin\\shaderapivk.dll";
		const std::wstring dxvk       = root + L"\\bin\\dxvk_d3d9.dll";
		result.m_dxvk_vulkan          = is_loaded_from( modules.m_shader_api, shader_api ) &&
		                       is_loaded_from( modules.m_dxvk_d3d9, dxvk ) && modules.m_vulkan;
		if( !result.m_dxvk_vulkan )
			result.m_errors.emplace_back( "the game's DXVK/Vulkan backend is not active; launch with -vulkan" );

		result.m_supported = result.m_errors.empty( );
		return result;
	}

}