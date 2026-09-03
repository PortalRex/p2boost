#include "auto_update.h"

#include "console.h"
#include "portal2_context.h"
#include "runtime_verification.h"

#include <Windows.h>
#include <winhttp.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment( lib, "winhttp.lib" )

namespace p2boost {
	namespace {
		// GitHub repository the nightly and tagged releases are published to. the
		// list endpoint returns prereleases too, unlike /releases/latest which
		// 404s when the newest build is the rolling nightly.
		constexpr const char* repo_releases = "https://api.github.com/repos/aIIison/p2boost/releases?per_page=10";

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

		// split "https://host/path" into its host and path components for WinHTTP.
		bool split_url( const char* url, std::wstring& host, std::wstring& path ) {
			const char* scheme_end = std::strstr( url, "://" );
			if( !scheme_end )
				return false;

			const char* host_start = scheme_end + 3;
			const char* path_start = std::strchr( host_start, '/' );
			if( !path_start )
				return false;

			const std::string host_part{ host_start, path_start };
			const std::string path_part{ path_start };

			host = std::wstring( host_part.begin( ), host_part.end( ) );
			path = std::wstring( path_part.begin( ), path_part.end( ) );
			return !host.empty( ) && !path.empty( );
		}

		// read one HTTP response into memory with WinHTTP. follows redirects manually
		// because WinHTTP drops the Authorization header when redirecting across
		// hosts, and GitHub moves release downloads to objects.githubusercontent.com.
		bool http_get( const char* url, std::string& body, bool binary ) {
			const HINTERNET session = WinHttpOpen( L"p2boost-updater",
			                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			                                       WINHTTP_NO_PROXY_NAME,
			                                       WINHTTP_NO_PROXY_BYPASS,
			                                       0 );
			if( !session ) {
				console_print( "[p2boost] WinHttpOpen failed (%lu)\n", GetLastError( ) );
				return false;
			}

			// keep the check snappy instead of waiting on WinHTTP's defaults.
			WinHttpSetTimeouts( session, 10000, 10000, 10000, 10000 );

			// GitHub requires a descriptive User-Agent on API requests and
			// authenticates private repositories through an optional token. the
			// asset endpoint returns the binary only with the octet-stream accept,
			// while the metadata endpoints reject that media type.
			std::wstring headers = L"User-Agent: p2boost/" P2BOOST_VERSION L"\r\n";
			headers += binary ? L"Accept: application/octet-stream\r\n" : L"Accept: application/vnd.github+json\r\n";
			char token[ 256 ]{ };
			if( GetEnvironmentVariableA( "GITHUB_TOKEN", token, sizeof( token ) ) <= 0 )
				GetEnvironmentVariableA( "GH_TOKEN", token, sizeof( token ) );
			if( token[ 0 ] ) {
				headers += L"Authorization: Bearer ";
				headers.append( token, token + std::strlen( token ) );
				headers += L"\r\n";
			}

			std::string current = url;
			bool        success = false;
			for( int hop = 0; hop < 5 && !success; ++hop ) {
				std::wstring host;
				std::wstring path;
				if( !split_url( current.c_str( ), host, path ) )
					break;

				const HINTERNET connect = WinHttpConnect( session, host.c_str( ), INTERNET_DEFAULT_HTTPS_PORT, 0 );
				if( !connect ) {
					console_print( "[p2boost] WinHttpConnect failed (%lu)\n", GetLastError( ) );
					break;
				}

				const HINTERNET request = WinHttpOpenRequest( connect,
				                                              L"GET",
				                                              path.c_str( ),
				                                              nullptr,
				                                              WINHTTP_NO_REFERER,
				                                              WINHTTP_DEFAULT_ACCEPT_TYPES,
				                                              WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH );
				if( !request ) {
					console_print( "[p2boost] WinHttpOpenRequest failed (%lu)\n", GetLastError( ) );
					WinHttpCloseHandle( connect );
					break;
				}

				if( !WinHttpSendRequest( request, headers.c_str( ), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) ||
				    !WinHttpReceiveResponse( request, nullptr ) ) {
					console_print( "[p2boost] WinHttpSendRequest failed (%lu)\n", GetLastError( ) );
					WinHttpCloseHandle( request );
					WinHttpCloseHandle( connect );
					break;
				}

				DWORD status_code{ };
				DWORD status_size = sizeof( status_code );
				if( !WinHttpQueryHeaders( request,
				                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				                          WINHTTP_HEADER_NAME_BY_INDEX,
				                          &status_code,
				                          &status_size,
				                          WINHTTP_NO_HEADER_INDEX ) ) {
					WinHttpCloseHandle( request );
					WinHttpCloseHandle( connect );
					break;
				}

				if( status_code >= 300 && status_code < 400 ) {
					// read the Location header and re-issue with the token intact.
					wchar_t    location[ 1024 ]{ };
					DWORD      location_size = sizeof( location );
					const bool located       = WinHttpQueryHeaders( request,
					                                                WINHTTP_QUERY_LOCATION,
					                                                WINHTTP_HEADER_NAME_BY_INDEX,
					                                                location,
					                                                &location_size,
					                                                WINHTTP_NO_HEADER_INDEX );
					WinHttpCloseHandle( request );
					WinHttpCloseHandle( connect );
					if( !located || !location[ 0 ] )
						break;

					location[ 1023 ] = L'\0';
					std::string redirected;
					for( const wchar_t* character = location; *character; ++character )
						redirected.push_back( static_cast< char >( *character ) );
					current = redirected;
					continue;
				}

				if( status_code == 200 ) {
					char  buffer[ 1 << 15 ];
					DWORD bytes_read{ };
					while( WinHttpReadData( request, buffer, sizeof( buffer ), &bytes_read ) && bytes_read ) {
						body.append( buffer, bytes_read );
						bytes_read = 0;
					}
					success = true;
				} else if( status_code == 404 ) {
					console_print( "[p2boost] the p2boost repository or its releases are not publicly reachable\n" );
				} else {
					console_print( "[p2boost] update request returned status %lu\n", status_code );
				}

				WinHttpCloseHandle( request );
				WinHttpCloseHandle( connect );
			}

			WinHttpCloseHandle( session );
			return success;
		}

		// parse the API asset url for one asset name from the GitHub release JSON.
		// the api.github.com asset endpoint is used for downloads because the
		// github.com releases/download URL returns 404 for private repositories
		// even with a read token. the API escapes strings with backslash quotes.
		bool asset_url( const std::string& json, const char* asset_name, std::string& url ) {
			const std::string needle = std::string( "\"name\":\"" ) + asset_name + "\"";
			const size_t      found  = json.find( needle );
			if( found == std::string::npos )
				return false;

			const std::string url_marker = "\"url\":\"";
			const size_t      url_start  = json.rfind( url_marker, found );
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
			return url.find( "api.github.com" ) != std::string::npos;
		}

		// the releases list endpoint returns a JSON array. split the first release
		// object (newest first) from the array so asset search never crosses into
		// an older release.
		std::string first_release_object( const std::string& json ) {
			const size_t array_start = json.find( '[' );
			if( array_start == std::string::npos )
				return json;

			const size_t object_start = json.find( '{', array_start );
			if( object_start == std::string::npos )
				return { };

			int         depth     = 0;
			bool        in_string = false;
			const char* data      = json.data( );
			for( size_t index = object_start; index < json.size( ); ++index ) {
				const char character = data[ index ];
				if( character == '"' && ( index == 0 || data[ index - 1 ] != '\\' ) )
					in_string = !in_string;
				else if( !in_string ) {
					if( character == '{' )
						++depth;
					else if( character == '}' && --depth == 0 )
						return json.substr( object_start, index - object_start + 1 );
				}
			}
			return json.substr( object_start );
		}

		// the nightly tag carries no version number; it is always the freshest
		// build and supersedes any tagged release. dotted tags compare normally.
		bool release_newer( const std::string& tag, const std::string& current_version ) {
			if( tag == "nightly" )
				return true;

			std::string candidate = tag;
			if( !candidate.empty( ) && candidate[ 0 ] == 'v' )
				candidate.erase( 0, 1 );
			return version_newer( candidate, current_version );
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
		if( !http_get( repo_releases, release_json, false ) ) {
			console_print( "[p2boost] failed to reach the GitHub releases API\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		// pick the newest release that carries both raw assets. older releases
		// published before the workflow attached them are skipped.
		std::string latest_tag;
		std::string dll_url;
		std::string sha_url;
		{
			std::string remaining = release_json;
			while( !remaining.empty( ) ) {
				const std::string release = first_release_object( remaining );
				if( release.empty( ) )
					break;

				const std::string tag_marker  = "\"tag_name\":\"";
				const size_t      tag_start   = release.find( tag_marker );
				const size_t      tag_content = tag_start == std::string::npos ? std::string::npos : tag_start + tag_marker.size( );
				const size_t      tag_end     = tag_content == std::string::npos ? std::string::npos : release.find( '"', tag_content );

				std::string tag;
				std::string candidate_dll;
				std::string candidate_sha;
				if( tag_end != std::string::npos )
					tag = release.substr( tag_content, tag_end - tag_content );
				asset_url( release, dll_asset_name, candidate_dll );
				asset_url( release, sha_asset_name, candidate_sha );

				if( !tag.empty( ) && !candidate_dll.empty( ) && !candidate_sha.empty( ) ) {
					latest_tag = tag;
					dll_url    = candidate_dll;
					sha_url    = candidate_sha;
					break;
				}

				// advance past this object to the next one in the array.
				const size_t next = remaining.find( release ) + release.size( );
				remaining         = next <= remaining.size( ) ? remaining.substr( next ) : std::string{ };
			}
		}

		if( latest_tag.empty( ) || dll_url.empty( ) || sha_url.empty( ) ) {
			console_print( "[p2boost] no release exposes the raw p2boost.dll asset\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		console_print( "[p2boost] latest version is %s (running %s)\n", latest_tag.c_str( ), P2BOOST_VERSION );

		if( !force && !release_newer( latest_tag, P2BOOST_VERSION ) ) {
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
		if( !http_get( dll_url.c_str( ), dll_data, true ) || dll_data.empty( ) ) {
			console_print( "[p2boost] failed to download the new p2boost.dll\n" );
			updater->m_busy.store( false, std::memory_order_relaxed );
			return;
		}

		std::string expected_sha;
		if( !http_get( sha_url.c_str( ), expected_sha, true ) ) {
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