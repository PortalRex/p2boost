#include "build_fingerprint.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace p2boost {
namespace {

std::wstring ModulePath(HMODULE module) {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size == path.size())
        return {};
    path.resize(size);
    return path;
}

std::string Narrow(const std::wstring& input) {
    if (input.empty())
        return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        input.c_str(),
                        static_cast<int>(input.size()),
                        output.data(),
                        size,
                        nullptr,
                        nullptr);
    return output;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return value;
}

} // namespace

const std::array<ModuleFingerprint, 5>& SupportedBuild23973718() {
    static const std::array<ModuleFingerprint, 5> modules{{
        {"engine.dll",
         "bin\\engine.dll",
         "5196eace78d5f2f47f8ce39a49b0efd15374a2b996b272642f0493ace42c24dc"},
        {"shaderapidx9.dll",
         "bin\\shaderapidx9.dll",
         "0879d15b4f9cf9f55bb86d874dc88b6edf4d8dfc825e77d3bd1619a2df250439"},
        {"materialsystem.dll",
         "bin\\materialsystem.dll",
         "8b8bbdb77e72a23bef8e67f0b9800327338e125ba0fbfae68feb2dc864af4fbf"},
        {"client.dll",
         "portal2\\bin\\client.dll",
         "2ff27c2878c258b2c16a0982e30b0d2f1bad9d213e42cc778dc2a2925c625137"},
        {"server.dll",
         "portal2\\bin\\server.dll",
         "4c3ecc83bbea0864660a49322d1d5188cdc00fe43a7ea56f1ca8a0c94356bb4c"},
    }};
    return modules;
}

std::string Sha256File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("CreateFileW failed");

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD result_size = 0;
    std::vector<unsigned char> object;
    std::array<unsigned char, 32> digest{};
    try {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
        if (BCryptGetProperty(algorithm,
                              BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size),
                              &result_size,
                              0) < 0)
            throw std::runtime_error("BCryptGetProperty failed");
        object.resize(object_size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0)
            throw std::runtime_error("BCryptCreateHash failed");
        std::array<unsigned char, 1 << 16> buffer{};
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
                throw std::runtime_error("ReadFile failed");
            if (read == 0)
                break;
            if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
                throw std::runtime_error("BCryptHashData failed");
        }
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("BCryptFinishHash failed");
    } catch (...) {
        if (hash)
            BCryptDestroyHash(hash);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        throw;
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    std::ostringstream text;
    for (unsigned char byte : digest)
        text << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return text.str();
}

VerificationResult VerifyRuntime() {
    VerificationResult result;
    const HMODULE engine = GetModuleHandleW(L"engine.dll");
    if (!engine) {
        result.errors.emplace_back("engine.dll is not loaded");
        return result;
    }
    std::wstring engine_path = ModulePath(engine);
    const std::wstring suffix = L"\\bin\\engine.dll";
    if (engine_path.size() <= suffix.size() ||
        Lower(engine_path.substr(engine_path.size() - suffix.size())) != suffix) {
        result.errors.emplace_back("cannot derive game root from engine.dll");
        return result;
    }
    std::wstring root = engine_path.substr(0, engine_path.size() - suffix.size());
    result.game_root = Narrow(root);
    for (const auto& module : SupportedBuild23973718()) {
        const std::wstring relative(module.relative_path,
                                    module.relative_path + strlen(module.relative_path));
        const std::wstring path = root + L"\\" + relative;
        try {
            const std::string actual = Sha256File(path);
            if (actual != module.sha256)
                result.errors.emplace_back(std::string(module.name) +
                                           " SHA-256 mismatch: " + actual);
        } catch (const std::exception& error) {
            result.errors.emplace_back(std::string(module.name) +
                                       " verification failed: " + error.what());
        }
    }
    result.supported = result.errors.empty();
    result.sar_loaded = GetModuleHandleW(L"sar.dll") != nullptr;
    const HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    const HMODULE vulkan_shader = GetModuleHandleW(L"shaderapivk.dll");
    if (d3d9) {
        const std::wstring d3d_path = Lower(ModulePath(d3d9));
        result.native_d3d9 =
            vulkan_shader == nullptr &&
            (d3d_path.find(L"\\windows\\system32\\d3d9.dll") != std::wstring::npos ||
             d3d_path.find(L"\\windows\\syswow64\\d3d9.dll") != std::wstring::npos);
    }
    if (!result.native_d3d9)
        result.errors.emplace_back("native Microsoft D3D9 runtime is not active");
    return result;
}

} // namespace p2boost
