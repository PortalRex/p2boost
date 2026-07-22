#include "build_fingerprint.hpp"
#include "console.hpp"
#include "console_commands.hpp"
#include "frame_pacer.hpp"
#include "metrics.hpp"
#include "portal_trace.hpp"
#include "renderer_trace.hpp"
#include "sdk.hpp"

#include <Windows.h>
#include <cstring>
#include <string>

namespace p2boost {

class Plugin final : public sdk::IServerPluginCallbacks {
public:
    bool Load(sdk::CreateInterfaceFn interface_factory, sdk::CreateInterfaceFn) override {
        instance_ = this;
        if (interface_factory) engine_server_ = interface_factory("VEngineServer022", nullptr);
        if (!commands_.Connect(interface_factory) ||
            !commands_.Add("p2boost_status", "Print p2boost runtime state.", &CommandThunk) ||
            !commands_.Add("p2boost_enable", "Enable or disable verified fixes.", &CommandThunk) ||
            !commands_.Add("p2boost_trace", "Enable or disable diagnostic frame tracing.", &CommandThunk) ||
            !commands_.Add("p2boost_dump_metrics", "Write diagnostic timings to CSV.", &CommandThunk)) {
            ConsolePrint("[p2boost] failed to register console commands\n");
            instance_ = nullptr;
            return false;
        }
        verification_ = VerifyRuntime();
        if (verification_.supported && verification_.native_d3d9 && !renderer_trace_.Install(&metrics_))
            verification_.errors.emplace_back("renderer trace callsite signature mismatch");
        if (verification_.supported && verification_.native_d3d9 && !renderer_trace_.FocusSyncAvailable())
            verification_.errors.emplace_back("material-system focus synchronization signature mismatch");
        if (verification_.supported && verification_.native_d3d9 && !renderer_trace_.StaticLightingFixAvailable())
            verification_.errors.emplace_back("static-lighting restore callsite signature mismatch");
        if (verification_.supported && verification_.native_d3d9 && !portal_trace_.Install(&metrics_))
            verification_.errors.emplace_back("portal trace callsite signature mismatch");
        enabled_ = verification_.supported && verification_.native_d3d9 &&
                   renderer_trace_.FocusSyncAvailable() && renderer_trace_.StaticLightingFixAvailable();
        if (verification_.supported && verification_.native_d3d9 && !frame_pacer_.Install()) {
            verification_.errors.emplace_back("frame-pacer callsite signature mismatch");
            enabled_ = false;
        }
        frame_pacer_.SetEnabled(enabled_);
        renderer_trace_.SetFixEnabled(enabled_);
        ConsolePrint("[p2boost] loaded v%s; frame_pacer=%s\n", P2BOOST_VERSION,
                     frame_pacer_.Installed() ? (enabled_ ? "enabled" : "disabled") : "unavailable");
        for (const auto& error : verification_.errors) ConsolePrint("[p2boost] %s\n", error.c_str());
        return true;
    }
    void Unload() override { commands_.Disconnect(); portal_trace_.Remove(); renderer_trace_.Remove(); frame_pacer_.Remove(); active_client_ = nullptr; engine_server_ = nullptr; instance_ = nullptr; ConsolePrint("[p2boost] unloaded\n"); }
    void Pause() override { enabled_ = false; frame_pacer_.SetEnabled(false); renderer_trace_.SetFixEnabled(false); }
    void UnPause() override { enabled_ = verification_.supported && verification_.native_d3d9 && renderer_trace_.FocusSyncAvailable() && renderer_trace_.StaticLightingFixAvailable(); frame_pacer_.SetEnabled(enabled_); renderer_trace_.SetFixEnabled(enabled_); }
    const char* GetPluginDescription() override { return "p2boost native-D3D9 performance patch v" P2BOOST_VERSION; }
    void LevelInit(const char*) override {}
    void ServerActivate(sdk::edict_t*, int, int client_max) override { metrics_.SetCoop(client_max > 1); }
    void GameFrame(bool simulating) override {
        metrics_.Frame(simulating);
    }
    void LevelShutdown() override { metrics_.SetCoop(false); }
    void ClientFullyConnect(sdk::edict_t* entity) override { if (!active_client_) active_client_ = entity; }
    void ClientActive(sdk::edict_t* entity) override { if (!active_client_) active_client_ = entity; }
    void ClientDisconnect(sdk::edict_t* entity) override { if (active_client_ == entity) active_client_ = nullptr; }
    void ClientPutInServer(sdk::edict_t*, const char*) override {}
    void SetCommandClient(int) override {}
    void ClientSettingsChanged(sdk::edict_t*) override {}
    sdk::PluginResult ClientConnect(bool*, sdk::edict_t*, const char*, const char*, char*, int) override { return sdk::Continue; }
    sdk::PluginResult ClientCommand(sdk::edict_t*, const sdk::CCommand& args) override {
        const char* command = args.Arg(0);
        if (_stricmp(command, "p2boost_status") == 0) {
            const bool sar_loaded = GetModuleHandleW(L"sar.dll") != nullptr;
            ConsolePrint("[p2boost] build=23973718 supported=%d native_d3d9=%d enabled=%d pacer_installed=%d tracing=%d sar=%d samples=%zu focus_sync=%d lighting_fix=%d lighting_skips=%u root=%s\n",
                verification_.supported, verification_.native_d3d9, enabled_, frame_pacer_.Installed(),
                metrics_.Tracing(), sar_loaded, metrics_.Count(), renderer_trace_.FocusSyncAvailable(),
                renderer_trace_.StaticLightingFixAvailable(), renderer_trace_.StaticLightingSkips(),
                verification_.game_root.c_str());
            return sdk::Stop;
        }
        if (_stricmp(command, "p2boost_enable") == 0) {
            if (args.ArgC() != 2) ConsolePrint("usage: p2boost_enable 0|1\n");
            else if (strcmp(args.Arg(1), "0") == 0) enabled_ = false;
            else if (strcmp(args.Arg(1), "1") == 0 && verification_.supported && verification_.native_d3d9 && frame_pacer_.Installed() && renderer_trace_.FocusSyncAvailable() && renderer_trace_.StaticLightingFixAvailable()) enabled_ = true;
            else ConsolePrint("[p2boost] cannot enable: unsupported build/backend or invalid value\n");
            frame_pacer_.SetEnabled(enabled_);
            renderer_trace_.SetFixEnabled(enabled_);
            return sdk::Stop;
        }
        if (_stricmp(command, "p2boost_trace") == 0) {
            if (args.ArgC() == 2 && (strcmp(args.Arg(1), "0") == 0 || strcmp(args.Arg(1), "1") == 0))
                metrics_.SetTracing(strcmp(args.Arg(1), "1") == 0);
            else ConsolePrint("usage: p2boost_trace 0|1\n");
            return sdk::Stop;
        }
        if (_stricmp(command, "p2boost_dump_metrics") == 0) {
            const std::string path = verification_.game_root + "\\p2boost_metrics.csv";
            ConsolePrint("[p2boost] metrics %s: %s\n", metrics_.Dump(path) ? "written" : "write failed", path.c_str());
            return sdk::Stop;
        }
        return sdk::Continue;
    }
    sdk::PluginResult NetworkIDValidated(const char*, const char*) override { return sdk::Continue; }
    void OnQueryCvarValueFinished(int, sdk::edict_t*, sdk::QueryStatus, const char*, const char*) override {}
    void OnEdictAllocated(sdk::edict_t*) override {}
    void OnEdictFreed(const sdk::edict_t*) override {}

private:
    static void __cdecl CommandThunk(const sdk::CCommand& command) {
        if (instance_) instance_->ClientCommand(nullptr, command);
    }
    static Plugin* instance_;
    VerificationResult verification_;
    Metrics metrics_;
    FramePacer frame_pacer_;
    PortalTrace portal_trace_;
    RendererTrace renderer_trace_;
    ConsoleCommandRegistry commands_;
    void* engine_server_ = nullptr;
    sdk::edict_t* active_client_ = nullptr;
    bool enabled_ = false;
};

Plugin* Plugin::instance_ = nullptr;

Plugin g_plugin;
} // namespace p2boost

extern "C" __declspec(dllexport) void* CreateInterface(const char* name, int* return_code) {
    if (name && strcmp(name, "ISERVERPLUGINCALLBACKS003") == 0) {
        if (return_code) *return_code = 0;
        return &p2boost::g_plugin;
    }
    if (return_code) *return_code = 1;
    return nullptr;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
