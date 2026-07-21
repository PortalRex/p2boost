#include "console_commands.hpp"

#include <Windows.h>

namespace p2boost {
namespace {
constexpr std::size_t kRegisterConCommandSlot = 9;
constexpr std::size_t kUnregisterConCommandSlot = 10;
constexpr std::size_t kFindCommandBaseSlot = 13;

template <typename T>
T VFunc(void* object, std::size_t slot) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(object))[slot]);
}
} // namespace

ConsoleCommandBase::ConsoleCommandBase(const char* command_name, const char* command_help, int command_flags)
    : next(nullptr), registered(false), padding{}, name(command_name), help(command_help), flags(command_flags) {}

ConsoleCommand::ConsoleCommand(const char* command_name, const char* command_help, Callback command_callback)
    : ConsoleCommandBase(command_name, command_help, 0),
      callback(command_callback), completion_callback(nullptr), callback_flags(0x02), callback_padding{} {}

bool ConsoleCommandRegistry::Connect(sdk::CreateInterfaceFn engine_factory) {
    if (cvar_) return true;
    if (engine_factory) cvar_ = engine_factory("VEngineCvar007", nullptr);
    if (!cvar_) {
        HMODULE vstdlib = GetModuleHandleW(L"vstdlib.dll");
        if (!vstdlib) return false;
        using GetFactoryFn = sdk::CreateInterfaceFn(__cdecl*)();
        auto get_factory = reinterpret_cast<GetFactoryFn>(GetProcAddress(vstdlib, "VStdLib_GetICVarFactory"));
        if (!get_factory) return false;
        sdk::CreateInterfaceFn factory = get_factory();
        if (factory) cvar_ = factory("VEngineCvar007", nullptr);
    }
    if (!cvar_) return false;

    using FindCommandBaseFn = void*(__thiscall*)(void*, const char*);
    auto find = VFunc<FindCommandBaseFn>(cvar_, kFindCommandBaseSlot);
    void* reference = find(cvar_, "listdemo");
    if (!reference) {
        cvar_ = nullptr;
        return false;
    }
    command_vtable_ = *reinterpret_cast<void**>(reference);
    return command_vtable_ != nullptr;
}

bool ConsoleCommandRegistry::Add(const char* name, const char* help, Callback callback) {
    if (!cvar_ || !command_vtable_ || !callback) return false;
    auto command = std::make_unique<ConsoleCommand>(name, help, callback);
    *reinterpret_cast<void**>(command.get()) = command_vtable_;
    using RegisterFn = void(__thiscall*)(void*, ConsoleCommandBase*);
    VFunc<RegisterFn>(cvar_, kRegisterConCommandSlot)(cvar_, command.get());
    command->registered = true;
    commands_.push_back(std::move(command));
    return true;
}

void ConsoleCommandRegistry::Disconnect() {
    if (cvar_) {
        using UnregisterFn = void(__thiscall*)(void*, ConsoleCommandBase*);
        auto unregister = VFunc<UnregisterFn>(cvar_, kUnregisterConCommandSlot);
        for (auto& command : commands_) {
            if (command->registered) unregister(cvar_, command.get());
            command->registered = false;
        }
    }
    commands_.clear();
    command_vtable_ = nullptr;
    cvar_ = nullptr;
}

} // namespace p2boost
