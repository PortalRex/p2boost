#include "game_profile.h"

#include <cwctype>
#include <string>

namespace p2boost::game {
	namespace {

		const profile_t* g_active{ };

		std::wstring lower( std::wstring value ) {
			for( wchar_t& character : value )
				character = static_cast< wchar_t >( std::towlower( character ) );
			return value;
		}

		bool path_ends_with( const std::wstring& path, const char* suffix ) {
			const std::wstring expected = lower( std::wstring( suffix, suffix + std::char_traits< char >::length( suffix ) ) );
			if( path.size( ) < expected.size( ) )
				return false;
			return path.compare( path.size( ) - expected.size( ), expected.size( ), expected ) == 0;
		}

	}

	const profile_t& portal2( ) noexcept {
		static const portal2_t instance;
		return instance;
	}

	const profile_t& portal_stories_mel( ) noexcept {
		static const portal_stories_mel_t instance;
		return instance;
	}

	const profile_t* detect( const portal2_modules_t& modules ) noexcept {
		g_active = nullptr;
		if( !modules.m_client )
			return nullptr;

		// the client binary sits in each game's own mod bin directory, so its
		// loader path identifies the game just like SAR's mod-directory check.
		// adding a game = add its class file and one detection line below.
		const std::wstring client_path = lower( modules.m_client.path( ) );
		if( path_ends_with( client_path, portal2( ).client_path ) ) {
			g_active = &portal2( );
		} else if( path_ends_with( client_path, portal_stories_mel( ).client_path ) ) {
			g_active = &portal_stories_mel( );
		}
		return g_active;
	}

	const profile_t* active( ) noexcept {
		return g_active;
	}

}