#include "metrics.hpp"

#include <fstream>
#include <iomanip>

namespace p2boost {

Metrics::Metrics() { QueryPerformanceFrequency(&frequency_); }

void Metrics::Frame(bool simulating) noexcept {
    if (!tracing_) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double delta = last_qpc_ == 0 ? 0.0 :
        static_cast<double>(now.QuadPart - last_qpc_) * 1000.0 / static_cast<double>(frequency_.QuadPart);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    const bool focused = foreground_process == GetCurrentProcessId();
    samples_[cursor_] = {now.QuadPart, delta, simulating, focused, coop_};
    cursor_ = (cursor_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    last_qpc_ = now.QuadPart;
}

bool Metrics::Dump(const std::string& path) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << "qpc,delta_ms,simulating,focused,coop\n" << std::fixed << std::setprecision(6);
    const size_t start = count_ == Capacity ? cursor_ : 0;
    for (size_t index = 0; index < count_; ++index) {
        const auto& sample = samples_[(start + index) % Capacity];
        output << sample.qpc << ',' << sample.delta_ms << ',' << (sample.simulating ? 1 : 0) << ','
               << (sample.focused ? 1 : 0) << ',' << (sample.coop ? 1 : 0) << '\n';
    }
    return output.good();
}

} // namespace p2boost
