#include "auto_update.h"

#include "console.h"
#include "portal2_context.h"
#include "runtime_verification.h"

#include <Windows.h>
#include <wininet.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment( lib, "wininet.lib" )

namespace p2boost {
	namespace {
		// GitHub repository the nightly and tagged releases are published to.
		constexpr const char* repo_releases = "https://api.github.com/repos/aIIison/p2boost/releases/latest";

		// asset names published beside the drop-in zip by the release workflow.
		constexpr const char* dll_asset_name = "p2boost.dll";
		constexpr const char* sha_asset_name = "p2boost.dll.sha256";

		// compare dotted "1.2.3" version strings numerically.
		bool version_newer( const std::string& candidate, const std::string& current ) {
			auto parts = []( const std::string& version ) {
				std::vector< int > values;
				std::stringstream  stream{ version };
				std::string        part;
				while( std::getline( stream, part, '.' ) ) {
					int value{ };
					if( std::from_chars( part.data( ), part.data( ) + part.size( ), value ).ec != std::errc{ } )
						return std::vector< int >{ };
					values.push_back( value );
				}
				return values;
			};

			const std::vector< int > left  = parts( candidate );
			const std::vector< int > right = parts( current );
			if( left.empty( ) || right.empty( ) )
				return false;

			const size_t count = std::max( left.size( ), right.size( ) );
			for( size_t index = 0; index < count; ++index ) {
				const int a = index < left.size( ) ? left[ index ] : 0;
				const int b = index < right.size( ) ? right[ index ] : 0;
				if( a != b )
					return a > b;
			}
			return false;
		}

		// read one HTTP response into memory with WinINet. returns false on any
		// transport or status failure so callers never trust partial data.
		bool http_get( const char* url, std::string& body ) {
			const HINTERNET session = InternetOpenW( L"p2boost-updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0 );
			if( !session )
				return false;

			bool            success = false;
			const HINTERNET request = InternetOpenUrlA( session, url, nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0 );
			if( request ) {
				char  buffer[ 1 << 15 ];
				DWORD bytes_read{ };
				while( InternetReadFile( request, buffer, sizeof( buffer ), &bytes_read ) && bytes_read ) {
					body.append( buffer, bytes_read );
					bytes_read = 0;
				}

				DWORD status_code{ };
				DWORD status_size = sizeof( status_code );
				success           = HttpQueryInfo( request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_size, nullptr ) &&
				          status_code == 200;
				InternetCloseHandle( request );
			}
			InternetCloseHandle( session );
			return success;
		}

		// parse the "browser_download_url" value for one asset name from the
		// GitHub release JSON. the API escapes strings with backslash quotes.
		bool asset_url( const std::string& json, const char* asset_name, std::string& url ) {
			const std::string needle = std::string( "\"name\":\"" ) + asset_name + "\"";
			const size_t      found  = json.find( needle );
			if( found == std::string::npos )
				return false;

			const std::string url_marker = "\"browser_download_url\":\"";
			const size_t      url_start  = json.find( url_marker, found + needle.size( ) );
			if( url_start == std::string::npos )
				return false;

			const size_t content_start = url_start + url_marker.size( );
			const size_t content_end   = json.find( '"', content_start );
			if( content_end == std::string::npos )
				return false;

			std::string value = json.substr( content_start, content_end - content_start );
			std::string unescaped;
			unescaped.reserve( value.size( ) );
			for( size_t index = 0; index < value.size( ); ++index ) {
				if( value[ index ] == '\\' && index + 1 < value.size( ) )
					unescaped.push_back( value[ ++index ] );
				else
					unescaped.push_back( value[ index ] );
			}
			url = unescaped;
			return !url.empty( );
		}

		// find the installed p2boost.dll next to the game executable. the game
		// root is derived from engine.dll the same way runtime verification does.
		std::wstring installed_dll_path( ) {
			HMODULE module{ };
			if( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                         reinterpret_cast< const wchar_t* >( &installed_dll_path ),
			                         &module ) )
				return { };

			wchar_t path[ MAX_PATH ]{ };
			if( !GetModuleFileNameW( module, path, MAX_PATH ) )
				return { };
			return path;
		}
	}

	AutoUpdate::~AutoUpdate( ) {
		if( m_worker.joinable( ) )
			m_worker.join( );
	}

	void AutoUpdate::update( bool force ) {
		if( m_busy.exchange( true ) )
			return;

		if( m_worker.joinable( ) )
			m_worker.join( );
		m_worker = std::thread( AutoUpdateDispatch, this, force );
	}

	void AutoUpdateDispatch( AutoUpdate* updater, bool force ) {
		console_print( "[p2boost] checking for updates...\n" );

		std::string release_json;
		if( !http_get( repo_releases, release_json ) ) {
			console_print( "[p2boost] failed to reach the GitHub releases API\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		std::string latest_tag;
		std::string dll_url;
		std::string sha_url;
		{
			const std::string tag_marker = "\"tag_name\":\"";
			const size_t      tag_start  = release_json.find( tag_marker );
			if( tag_start != std::string::npos ) {
				const size_t tag_content = tag_start + tag_marker.size( );
				const size_t tag_end     = release_json.find( '"', tag_content );
				if( tag_end != std::string::npos )
					latest_tag = release_json.substr( tag_content, tag_end - tag_content );
			}
			asset_url( release_json, dll_asset_name, dll_url );
			asset_url( release_json, sha_asset_name, sha_url );
		}

		if( latest_tag.empty( ) || dll_url.empty( ) || sha_url.empty( ) ) {
			console_print( "[p2boost] the latest release does not expose the raw p2boost.dll asset\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		// release tags are "v1.2.0"; compare only the numeric part.
		std::string latest_version = latest_tag;
		if( !latest_version.empty( ) && latest_version[ 0 ] == 'v' )
			latest_version.erase( 0, 1 );

		console_print( "[p2boost] latest version is %s (running %s)\n", latest_tag.c_str( ), P2BOOST_VERSION );

		if( !force && !version_newer( latest_version, P2BOOST_VERSION ) ) {
			console_print( "[p2boost] already up to date\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		const std::wstring installed = installed_dll_path( );
		if( installed.empty( ) ) {
			console_print( "[p2boost] cannot locate the installed p2boost.dll\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		// download to temporary files beside the target so the final rename is
		// atomic on the same volume.
		const std::wstring directory = std::filesystem::path{ installed }.parent_path( ).wstring( );
		const std::wstring dll_temp  = directory + L"\\p2boost.dll.download";
		const std::wstring sha_temp  = directory + L"\\p2boost.dll.sha256.download";

		console_print( "[p2boost] downloading %s...\n", dll_url.c_str( ) );
		std::string dll_data;
		if( !http_get( dll_url.c_str( ), dll_data ) || dll_data.empty( ) ) {
			console_print( "[p2boost] failed to download the new p2boost.dll\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		std::string expected_sha;
		if( !http_get( sha_url.c_str( ), expected_sha ) ) {
			console_print( "[p2boost] failed to download the release checksum\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		// the sha asset contains the 64-character digest, optionally followed by
		// a filename; keep only the leading hex.
		std::string digest;
		for( const char character : expected_sha ) {
			if( std::isxdigit( static_cast< unsigned char >( character ) ) )
				digest.push_back( character );
			else
				break;
		}

		{
			std::ofstream dll_file{ dll_temp, std::ios::binary };
			dll_file.write( dll_data.data( ), static_cast< std::streamsize >( dll_data.size( ) ) );
		}
		if( !std::filesystem::exists( dll_temp ) ) {
			console_print( "[p2boost] failed to stage the downloaded p2boost.dll\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		std::string actual_sha;
		try {
			actual_sha = sha256_file( dll_temp );
		} catch( const std::runtime_error& ) {
			console_print( "[p2boost] failed to hash the downloaded p2boost.dll\n" );
			std::filesystem::remove( dll_temp );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		if( digest.size( ) != 64 || _stricmp( digest.c_str( ), actual_sha.c_str( ) ) != 0 ) {
			console_print( "[p2boost] checksum mismatch: expected %s, got %s; refusing to install\n",
			               digest.empty( ) ? "<missing>" : digest.c_str( ),
			               actual_sha.c_str( ) );
			std::filesystem::remove( dll_temp );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		// replace the loaded image on disk. the running copy stays valid because
		// Windows mapped the file; the new image loads on the next launch.
		const std::wstring backup = installed + L".old-auto";
		std::filesystem::remove( backup );

		// the mapped image must allow delete sharing; MoveFileExW also replaces
		// an existing target atomically on the same volume.
		if( !MoveFileExW( installed.c_str( ), backup.c_str( ), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) {
			console_print( "[p2boost] failed to move the installed p2boost.dll (%lu)\n", GetLastError( ) );
			std::filesystem::remove( dll_temp );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		if( !MoveFileExW( dll_temp.c_str( ), installed.c_str( ), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) {
			console_print( "[p2boost] failed to install the new p2boost.dll (%lu); restoring the previous copy\n", GetLastError( ) );
			MoveFileExW( backup.c_str( ), installed.c_str( ), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		console_print( "[p2boost] installed %s. restart the game to load it.\n", latest_tag.c_str( ) );
		std::filesystem::remove( sha_temp );

		updater->m_busy.store( false, std::memory_order_relaxed );
	}

}