#pragma once

#include "sdk.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace p2boost {

// Portal 2's ConCommand ABI predates the public Source SDK 2013 ICvar layout.
// These fields and slots are validated against the shipped build and SAR's
// public Portal 2 implementation. The engine-owned vtable is copied from an
// existing command before registration.
struct ConsoleCommandBase {
    ConsoleCommandBase(const char* name, const char* help, int flags);
    virtual void DestroyStub() {}
    virtual bool IsCommandStub() const { return false; }

    ConsoleCommandBase* next;
    bool registered;
    std::uint8_t padding[3];
    const char* name;
    const char* help;
    int flags;
};

struct ConsoleCommand final : ConsoleCommandBase {
    using Callback = void(__cdecl*)(const sdk::CCommand&);
    using CompletionCallback = int(__cdecl*)(const char*, char (*)[64]);

    ConsoleCommand(const char* name, const char* help, Callback callback);

    Callback callback;
    CompletionCallback completion_callback;
    std::uint8_t callback_flags;
    std::uint8_t callback_padding[3];
};

static_assert(sizeof(ConsoleCommandBase) == 24, "Portal 2 ConCommandBase ABI changed");
static_assert(sizeof(ConsoleCommand) == 36, "Portal 2 ConCommand ABI changed");

class ConsoleCommandRegistry {
public:
    using Callback = ConsoleCommand::Callback;
    bool Connect(sdk::CreateInterfaceFn engine_factory);
    bool Add(const char* name, const char* help, Callback callback);
    void Disconnect();
    ~ConsoleCommandRegistry() { Disconnect(); }

private:
    void* cvar_ = nullptr;
    void* command_vtable_ = nullptr;
    std::vector<std::unique_ptr<ConsoleCommand>> commands_;
};

} // namespace p2boost
