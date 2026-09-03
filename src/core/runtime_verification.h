#pragma once

#include "portal2_context.h"

#include <array>
#include <string>
#include <vector>

namespace p2boost {

	struct verification_result_t {
		bool                       m_supported{ };
		bool                       m_dxvk_vulkan{ };
		bool                       m_dxvk_shipped{ };
		bool                       m_dxvk_approved{ };
		std::string                m_dxvk_sha256;
		std::string                m_game_root;
		std::vector< std::string > m_errors;
	};

	// exact SHA-256 of the dxvk_d3d9.dll the game ships, used only to label
	// the performance-metrics binary; it never gates support.
	const std::array< const char*, 1 >& shipped_dxvk_sha256( );

	// additional SHA-256 values accepted for bin\dxvk_d3d9.dll alongside the
	// shipped binary. Portal 2's bundled 2020-era DXVK is CPU-bound; these
	// newer builds were validated against the same shaderapivk interface.
	const std::array< const char*, 1 >& supported_dxvk_replacements( );

	// hash a file without preventing Steam or the game from updating it.
	std::string sha256_file( const std::wstring& path );

	// validate the game build from the loaded modules themselves: module
	// identity from the derived game root plus unique resolution of every
	// production hook signature. a changed build fails the exact signature it
	// broke instead of requiring a whole-build fingerprint update.
	verification_result_t verify_runtime( const portal2_modules_t& modules );

}