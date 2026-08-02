#include "runtime_verification.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace p2boost {
	namespace {

		// read a loaded module path without imposing MAX_PATH.
		std::wstring module_path( HMODULE module ) {
			std::wstring path( 32768, L'\0' );
			const DWORD	 size = GetModuleFileNameW( module, path.data( ), static_cast< DWORD >( path.size( ) ) );
			if( size == 0 || size == path.size( ) )
				return { };

			path.resize( size );
			return path;
		}

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

		bool is_loaded_from( const wchar_t* module_name, const std::wstring& expected_path ) {
			const HMODULE module = GetModuleHandleW( module_name );
			return module && lower( module_path( module ) ) == lower( expected_path );
		}

	}

	const std::array< ModuleFingerprint, 4 >& supported_build_23973718( ) {
		// only modules that define hooked code or the required backend belong in
		// the production fingerprint. Native-D3D9 research modules are excluded.
		static const std::array< ModuleFingerprint, 4 > modules{ {
			{ "engine.dll", "bin\\engine.dll", "5196eace78d5f2f47f8ce39a49b0efd15374a2b996b272642f0493ace42c24dc" },
			{ "client.dll", "portal2\\bin\\client.dll", "2ff27c2878c258b2c16a0982e30b0d2f1bad9d213e42cc778dc2a2925c625137" },
			{ "shaderapivk.dll", "bin\\shaderapivk.dll", "aaefa05d9c02049a0cca67a9181f71f5a7710fcc21474bd55c0e1deabba76e84" },
			{ "dxvk_d3d9.dll", "bin\\dxvk_d3d9.dll", "7bf7c95d81ff33b951ff985037401ff5a792474f1e3b9c5d2f521affb92b9073" },
		} };
		return modules;
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

		BCRYPT_ALG_HANDLE				algorithm{ };
		BCRYPT_HASH_HANDLE				hash{ };
		DWORD							object_size{ };
		DWORD							result_size{ };
		std::vector< unsigned char >	object;
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

	VerificationResult verify_runtime( ) {
		VerificationResult result;

		// derive the installation root from the module the loader actually mapped.
		const HMODULE engine = GetModuleHandleW( L"engine.dll" );
		if( !engine ) {
			result.m_errors.emplace_back( "engine.dll is not loaded" );
			return result;
		}

		const std::wstring engine_path = module_path( engine );
		const std::wstring suffix	   = L"\\bin\\engine.dll";
		if( engine_path.size( ) <= suffix.size( ) || lower( engine_path.substr( engine_path.size( ) - suffix.size( ) ) ) != suffix ) {
			result.m_errors.emplace_back( "cannot derive the Portal 2 root from engine.dll" );
			return result;
		}

		const std::wstring root = engine_path.substr( 0, engine_path.size( ) - suffix.size( ) );
		result.m_game_root		= narrow( root );

		// reject any changed production dependency before hooks can be installed.
		for( const auto& module : supported_build_23973718( ) ) {
			const std::wstring path = root + L"\\" + widen_ascii( module.m_relative_path );
			try {
				const std::string actual = sha256_file( path );
				if( actual != module.m_sha256 )
					result.m_errors.emplace_back( std::string( module.m_name ) + " SHA-256 mismatch: " + actual );
			} catch( const std::exception& error ) {
				result.m_errors.emplace_back( std::string( module.m_name ) + " verification failed: " + error.what( ) );
			}
		}

		// filenames alone are insufficient; verify the loaded backend paths too.
		const std::wstring shader_api = root + L"\\bin\\shaderapivk.dll";
		const std::wstring dxvk		  = root + L"\\bin\\dxvk_d3d9.dll";
		result.m_dxvk_vulkan		  = is_loaded_from( L"shaderapivk.dll", shader_api ) && is_loaded_from( L"dxvk_d3d9.dll", dxvk ) &&
							   GetModuleHandleW( L"vulkan-1.dll" ) != nullptr;
		if( !result.m_dxvk_vulkan )
			result.m_errors.emplace_back( "Portal 2's shipped DXVK/Vulkan backend is not active; launch with -vulkan" );

		result.m_supported = result.m_errors.empty( );
		return result;
	}

}
