#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <string>

namespace p2boost {

class Metrics {
public:
    static constexpr size_t Capacity = 1u << 16;
    Metrics();
    void SetTracing(bool tracing) noexcept { tracing_ = tracing; }
    bool Tracing() const noexcept { return tracing_; }
    void SetCoop(bool coop) noexcept { coop_ = coop; }
    void Frame(bool simulating) noexcept;
    bool Dump(const std::string& path) const;
    size_t Count() const noexcept { return count_; }

private:
    struct Sample { long long qpc; double delta_ms; bool simulating; bool focused; bool coop; };
    LARGE_INTEGER frequency_{};
    long long last_qpc_ = 0;
    std::array<Sample, Capacity> samples_{};
    size_t cursor_ = 0;
    size_t count_ = 0;
    bool tracing_ = false;
    bool coop_ = false;
};

} // namespace p2boost
