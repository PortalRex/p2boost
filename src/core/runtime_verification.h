#pragma once

#include "portal2_context.h"

#include <array>
#include <string>
#include <vector>

namespace p2boost {

	struct module_fingerprint_t {
		const char* m_name;
		const char* m_relative_path;
		const char* m_sha256;
	};

	struct verification_result_t {
		bool                       m_supported{ };
		bool                       m_dxvk_vulkan{ };
		bool                       m_dxvk_shipped{ true };
		std::string                m_dxvk_sha256;
		std::string                m_game_root;
		std::vector< std::string > m_errors;
	};

	// return every binary that supplies a production hook or private overlay ABI.
	// hooks are never installed for an approximate match.
	const std::array< module_fingerprint_t, 7 >& supported_build_23973718( );

	// exact SHA-256 values accepted for bin\dxvk_d3d9.dll alongside the shipped
	// binary. Portal 2's bundled 2020-era DXVK is CPU-bound; these newer builds
	// were validated against the same shaderapivk interface before approval.
	const std::array< const char*, 1 >& supported_dxvk_replacements_23973718( );

	// hash a file without preventing Steam or the game from updating it.
	std::string sha256_file( const std::wstring& path );

	// validate both the game build and Portal 2's shipped Vulkan/DXVK backend.
	verification_result_t verify_runtime( const portal2_modules_t& modules );

}
