#pragma once

#include <array>
#include <string>
#include <vector>

namespace p2boost {

	struct ModuleFingerprint {
		const char* m_name;
		const char* m_relative_path;
		const char* m_sha256;
	};

	struct VerificationResult {
		bool m_supported{ };
		bool m_dxvk_vulkan{ };
		std::string m_game_root;
		std::vector< std::string > m_errors;
	};

	// Return the exact engine, client, Vulkan renderer, and DXVK files supported
	// by this release. Hooks are never installed for an approximate match.
	const std::array< ModuleFingerprint, 4 >& supported_build_23973718( );

	// Hash a file without preventing Steam or the game from updating it.
	std::string sha256_file( const std::wstring& path );

	// Validate both the game build and Portal 2's shipped Vulkan/DXVK backend.
	VerificationResult verify_runtime( );

}
