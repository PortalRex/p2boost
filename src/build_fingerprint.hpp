#pragma once

#include <array>
#include <string>
#include <vector>

namespace p2boost {

struct ModuleFingerprint {
    const char* name;
    const char* relative_path;
    const char* sha256;
};

struct VerificationResult {
    bool supported = false;
    bool native_d3d9 = false;
    bool sar_loaded = false;
    std::string game_root;
    std::vector<std::string> errors;
};

const std::array<ModuleFingerprint, 5>& SupportedBuild23973718();
std::string Sha256File(const std::wstring& path);
VerificationResult VerifyRuntime();

} // namespace p2boost
