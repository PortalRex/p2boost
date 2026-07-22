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
		bool					   m_supported{ };
		bool					   m_native_d3d9{ };
		bool					   m_sar_loaded{ };
		std::string				   m_game_root;
		std::vector< std::string > m_errors;
	};

	// return the only build profile accepted by this release.
	const std::array< ModuleFingerprint, 5 >& supported_build_23973718( );

	// fingerprint an unmodified game module.
	std::string sha256_file( const std::wstring& path );

	// validate the live process before any hook is installed.
	VerificationResult verify_runtime( );

}
