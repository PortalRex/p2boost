#pragma once

#include <cstddef>
#include <cstdint>

namespace p2boost::sdk {

using CreateInterfaceFn = void* (*)(const char*, int*);
struct edict_t;

enum PluginResult {
    Continue = 0,
    Override,
    Stop,
};

enum QueryStatus {
    ValueIntact = 0,
    CvarNotFound,
    NotACvar,
    CvarProtected,
};

class CCommand {
public:
    int ArgC() const noexcept { return argc_; }
    const char* Arg(int index) const noexcept {
        return index >= 0 && index < argc_ && argv_[index] ? argv_[index] : "";
    }

private:
    int argc_;
    int argv0_size_;
    char args_buffer_[512];
    char argv_buffer_[512];
    const char* argv_[64];
};

class IServerPluginCallbacks {
public:
    virtual bool Load(CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory) = 0;
    virtual void Unload() = 0;
    virtual void Pause() = 0;
    virtual void UnPause() = 0;
    virtual const char* GetPluginDescription() = 0;
    virtual void LevelInit(const char* map_name) = 0;
    virtual void ServerActivate(edict_t* edict_list, int edict_count, int client_max) = 0;
    virtual void GameFrame(bool simulating) = 0;
    virtual void LevelShutdown() = 0;
    virtual void ClientFullyConnect(edict_t* edict) = 0;
    virtual void ClientActive(edict_t* entity) = 0;
    virtual void ClientDisconnect(edict_t* entity) = 0;
    virtual void ClientPutInServer(edict_t* entity, const char* player_name) = 0;
    virtual void SetCommandClient(int index) = 0;
    virtual void ClientSettingsChanged(edict_t* edict) = 0;
    virtual PluginResult ClientConnect(bool* allow_connect, edict_t* entity, const char* name,
                                       const char* address, char* reject, int max_reject_length) = 0;
    virtual PluginResult ClientCommand(edict_t* entity, const CCommand& args) = 0;
    virtual PluginResult NetworkIDValidated(const char* user_name, const char* network_id) = 0;
    virtual void OnQueryCvarValueFinished(int cookie, edict_t* player, QueryStatus status,
                                          const char* cvar_name, const char* cvar_value) = 0;
    virtual void OnEdictAllocated(edict_t* edict) = 0;
    virtual void OnEdictFreed(const edict_t* edict) = 0;
};

} // namespace p2boost::sdk
