#include "metrics.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cstring>

namespace p2boost {

Metrics::Metrics() { QueryPerformanceFrequency(&frequency_); }

void Metrics::RecordReacquire(double milliseconds, const char* reason, long long begin_qpc,
                              long long end_qpc) noexcept {
    if (!tracing_) return;
    pending_reacquire_ms_ += milliseconds;
    if (pending_reacquire_begin_qpc_ == 0 || begin_qpc < pending_reacquire_begin_qpc_)
        pending_reacquire_begin_qpc_ = begin_qpc;
    if (end_qpc > pending_reacquire_end_qpc_) pending_reacquire_end_qpc_ = end_qpc;
    pending_reacquire_reason_ = reason && std::strcmp(reason, "NeedsReset") == 0 ? 1 : 2;
}

void Metrics::RecordReacquireStage(size_t stage, double milliseconds) noexcept {
    if (!tracing_ || stage >= pending_reacquire_stages_ms_.size()) return;
    pending_reacquire_stages_ms_[stage] += milliseconds;
}

void Metrics::RecordPortalRender(double milliseconds) noexcept {
    if (!tracing_) return;
    pending_portal_render_ms_ += milliseconds;
    ++pending_portal_render_count_;
}

void Metrics::RecordShadowUpdate(double milliseconds, bool same_projection, bool same_full,
                                 bool skipped) noexcept {
    if (!tracing_) return;
    pending_shadow_update_ms_ += milliseconds;
    ++pending_shadow_update_count_;
    if (same_projection) ++pending_shadow_same_projection_count_;
    if (same_full) ++pending_shadow_same_full_count_;
    if (skipped) ++pending_shadow_skipped_count_;
}

void Metrics::RecordRenderWorker(long long begin_qpc, long long end_qpc,
                                 unsigned long long cycles, unsigned int queued_calls,
                                 unsigned int parameter_bytes, unsigned int vertex_bytes,
                                 unsigned int index_bytes, unsigned int hardware_sync_count,
                                 long long hardware_sync_qpc, long long max_hardware_sync_qpc,
                                 unsigned long long hardware_sync_cycles, int sync_index_before,
                                 unsigned int sync_pending_before, unsigned int sync_pending_after,
                                 unsigned int present_count, long long present_qpc,
                                 long long max_present_qpc
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
                                 , unsigned int draw_count, long long draw_qpc,
                                 long long max_draw_qpc, unsigned int max_draw_method,
                                 unsigned int max_draw_primitive_type,
                                 unsigned int max_draw_primitive_count,
                                 unsigned int max_draw_vertex_count,
                                 void* const* max_draw_stack,
                                 unsigned short max_draw_stack_depth
                                 , const unsigned int* command_keys,
                                 const unsigned int* command_counts,
                                 unsigned int dynamic_light_count,
                                 long long dynamic_light_qpc,
                                 unsigned int dynamic_light_unique_lights,
                                 unsigned int dynamic_light_unique_surfaces,
                                 std::uint32_t dynamic_light_first_light,
                                 std::uint32_t dynamic_light_first_surface,
                                 std::uint32_t dynamic_light_first_flags,
                                 std::uint8_t dynamic_light_first_bool_a,
                                 std::uint8_t dynamic_light_first_bool_b,
                                 const std::uint32_t* dynamic_light_words
#endif
                                 ) noexcept {
    if (!Tracing()) return;
    std::lock_guard<std::mutex> lock(render_worker_mutex_);
    if (pending_render_worker_begin_qpc_ == 0 || begin_qpc < pending_render_worker_begin_qpc_)
        pending_render_worker_begin_qpc_ = begin_qpc;
    if (end_qpc > pending_render_worker_end_qpc_) pending_render_worker_end_qpc_ = end_qpc;
    pending_render_worker_qpc_ += end_qpc - begin_qpc;
    pending_render_worker_cycles_ += cycles;
    ++pending_render_worker_count_;
    pending_render_worker_queued_calls_ += queued_calls;
    pending_render_worker_parameter_bytes_ += parameter_bytes;
    pending_render_worker_vertex_bytes_ += vertex_bytes;
    pending_render_worker_index_bytes_ += index_bytes;
    RenderWorkerSample sample{
        begin_qpc, end_qpc, cycles, queued_calls, parameter_bytes, vertex_bytes, index_bytes,
        hardware_sync_count, hardware_sync_qpc, max_hardware_sync_qpc, hardware_sync_cycles,
        sync_index_before, sync_pending_before, sync_pending_after,
        present_count, present_qpc, max_present_qpc,
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
        draw_count, draw_qpc, max_draw_qpc, max_draw_method, max_draw_primitive_type,
        max_draw_primitive_count, max_draw_vertex_count, {}, max_draw_stack_depth, {}, {},
        dynamic_light_count, dynamic_light_qpc, dynamic_light_unique_lights,
        dynamic_light_unique_surfaces, dynamic_light_first_light,
        dynamic_light_first_surface, dynamic_light_first_flags,
        dynamic_light_first_bool_a, dynamic_light_first_bool_b, {},
#endif
        GetCurrentThreadId()};
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    const auto depth = (std::min)(static_cast<size_t>(max_draw_stack_depth),
                                  sample.max_draw_stack.size());
    for (size_t index = 0; index < depth; ++index)
        sample.max_draw_stack[index] = reinterpret_cast<std::uintptr_t>(max_draw_stack[index]);
    for (size_t index = 0; index < sample.command_keys.size(); ++index) {
        sample.command_keys[index] = command_keys[index];
        sample.command_counts[index] = command_counts[index];
    }
    std::copy_n(dynamic_light_words, sample.dynamic_light_words.size(),
                sample.dynamic_light_words.begin());
#endif
    render_worker_samples_[render_worker_cursor_] = sample;
    render_worker_cursor_ = (render_worker_cursor_ + 1) % Capacity;
    if (render_worker_count_ < Capacity) ++render_worker_count_;
}

void Metrics::Frame(bool simulating) noexcept {
    if (!Tracing()) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double delta = last_qpc_ == 0 ? 0.0 :
        static_cast<double>(now.QuadPart - last_qpc_) * 1000.0 / static_cast<double>(frequency_.QuadPart);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    const bool focused = foreground_process == GetCurrentProcessId();
    long long render_worker_begin_qpc = 0;
    long long render_worker_end_qpc = 0;
    long long render_worker_qpc = 0;
    unsigned long long render_worker_cycles = 0;
    unsigned int render_worker_count = 0;
    unsigned long long render_worker_queued_calls = 0;
    unsigned long long render_worker_parameter_bytes = 0;
    unsigned long long render_worker_vertex_bytes = 0;
    unsigned long long render_worker_index_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(render_worker_mutex_);
        render_worker_begin_qpc = pending_render_worker_begin_qpc_;
        render_worker_end_qpc = pending_render_worker_end_qpc_;
        render_worker_qpc = pending_render_worker_qpc_;
        render_worker_cycles = pending_render_worker_cycles_;
        render_worker_count = pending_render_worker_count_;
        render_worker_queued_calls = pending_render_worker_queued_calls_;
        render_worker_parameter_bytes = pending_render_worker_parameter_bytes_;
        render_worker_vertex_bytes = pending_render_worker_vertex_bytes_;
        render_worker_index_bytes = pending_render_worker_index_bytes_;
        pending_render_worker_begin_qpc_ = 0;
        pending_render_worker_end_qpc_ = 0;
        pending_render_worker_qpc_ = 0;
        pending_render_worker_cycles_ = 0;
        pending_render_worker_count_ = 0;
        pending_render_worker_queued_calls_ = 0;
        pending_render_worker_parameter_bytes_ = 0;
        pending_render_worker_vertex_bytes_ = 0;
        pending_render_worker_index_bytes_ = 0;
    }
    samples_[cursor_] = {now.QuadPart, delta, pending_reacquire_begin_qpc_,
                         pending_reacquire_end_qpc_, pending_reacquire_ms_, pending_reacquire_stages_ms_,
                         pending_portal_render_ms_, pending_portal_render_count_,
                         pending_shadow_update_ms_, pending_shadow_update_count_,
                         pending_shadow_same_projection_count_, pending_shadow_same_full_count_,
                         pending_shadow_skipped_count_,
                         render_worker_begin_qpc, render_worker_end_qpc,
                         static_cast<double>(render_worker_qpc) * 1000.0 /
                             static_cast<double>(frequency_.QuadPart),
                         render_worker_cycles, render_worker_count, render_worker_queued_calls,
                         render_worker_parameter_bytes, render_worker_vertex_bytes,
                         render_worker_index_bytes,
                         pending_reacquire_reason_, simulating, focused, coop_};
    pending_reacquire_ms_ = 0.0;
    pending_reacquire_begin_qpc_ = 0;
    pending_reacquire_end_qpc_ = 0;
    pending_reacquire_stages_ms_.fill(0.0);
    pending_portal_render_ms_ = 0.0;
    pending_portal_render_count_ = 0;
    pending_shadow_update_ms_ = 0.0;
    pending_shadow_update_count_ = 0;
    pending_shadow_same_projection_count_ = 0;
    pending_shadow_same_full_count_ = 0;
    pending_shadow_skipped_count_ = 0;
    pending_reacquire_reason_ = 0;
    cursor_ = (cursor_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    last_qpc_ = now.QuadPart;
}

bool Metrics::Dump(const std::string& path) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << "qpc,delta_ms,reacquire_begin_qpc,reacquire_end_qpc,reacquire_ms,shader_device_ms,frame_sync_ms,resource_callbacks_ms,"
              "portal_render_ms,portal_render_count,shadow_update_ms,shadow_update_count,shadow_same_projection_count,shadow_same_full_count,shadow_skipped_count,render_worker_begin_qpc,render_worker_end_qpc,render_worker_ms,render_worker_cycles,render_worker_count,render_worker_queued_calls,render_worker_parameter_bytes,render_worker_vertex_bytes,render_worker_index_bytes,reacquire_reason,simulating,focused,coop\n"
           << std::fixed << std::setprecision(6);
    const size_t start = count_ == Capacity ? cursor_ : 0;
    for (size_t index = 0; index < count_; ++index) {
        const auto& sample = samples_[(start + index) % Capacity];
        output << sample.qpc << ',' << sample.delta_ms << ',' << sample.reacquire_begin_qpc << ','
               << sample.reacquire_end_qpc << ',' << sample.reacquire_ms << ','
               << sample.reacquire_stages_ms[0] << ',' << sample.reacquire_stages_ms[1] << ','
               << sample.reacquire_stages_ms[2] << ',' << sample.portal_render_ms << ','
               << sample.portal_render_count << ',' << sample.shadow_update_ms << ','
               << sample.shadow_update_count << ',' << sample.shadow_same_projection_count << ','
               << sample.shadow_same_full_count << ',' << sample.shadow_skipped_count << ','
               << sample.render_worker_begin_qpc << ',' << sample.render_worker_end_qpc << ','
               << sample.render_worker_ms << ',' << sample.render_worker_cycles << ','
               << sample.render_worker_count << ',' << sample.render_worker_queued_calls << ','
               << sample.render_worker_parameter_bytes << ',' << sample.render_worker_vertex_bytes << ','
               << sample.render_worker_index_bytes << ','
               << sample.reacquire_reason << ','
               << (sample.simulating ? 1 : 0) << ','
               << (sample.focused ? 1 : 0) << ',' << (sample.coop ? 1 : 0) << '\n';
    }
    const bool frame_output_good = output.good();
    output.close();
    const size_t separator = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    const std::string worker_path = dot != std::string::npos &&
        (separator == std::string::npos || dot > separator) ?
        path.substr(0, dot) + "_render_workers" + path.substr(dot) :
        path + "_render_workers.csv";
    std::ofstream worker_output(worker_path, std::ios::trunc);
    if (!worker_output) return false;
    worker_output << "begin_qpc,end_qpc,wall_ms,cycles,queued_calls,parameter_bytes,vertex_bytes,index_bytes,hardware_sync_count,hardware_sync_ms,max_hardware_sync_ms,hardware_sync_cycles,sync_index_before,sync_pending_before,sync_pending_after,present_count,present_ms,max_present_ms"
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
                  << ",draw_count,draw_ms,max_draw_ms,max_draw_method,max_draw_primitive_type,max_draw_primitive_count,max_draw_vertex_count,draw_stack_0,draw_stack_1,draw_stack_2,draw_stack_3,draw_stack_4,draw_stack_5,draw_stack_6,draw_stack_7,draw_stack_8,draw_stack_9,draw_stack_10,draw_stack_11,draw_stack_12,draw_stack_13,draw_stack_14,draw_stack_15"
                  << ",command_key_0,command_count_0,command_key_1,command_count_1,command_key_2,command_count_2,command_key_3,command_count_3,command_key_4,command_count_4,command_key_5,command_count_5,command_key_6,command_count_6,command_key_7,command_count_7,command_key_8,command_count_8,command_key_9,command_count_9,command_key_10,command_count_10,command_key_11,command_count_11,command_key_12,command_count_12,command_key_13,command_count_13,command_key_14,command_count_14,command_key_15,command_count_15"
                  << ",dynamic_light_count,dynamic_light_ms,dynamic_light_unique_lights,dynamic_light_unique_surfaces,dynamic_light_ptr,dynamic_light_surface_ptr,dynamic_light_update_flags,dynamic_light_bool_a,dynamic_light_bool_b,dynamic_light_flags,dynamic_light_origin_x,dynamic_light_origin_y,dynamic_light_origin_z,dynamic_light_radius,dynamic_light_color,dynamic_light_die,dynamic_light_decay,dynamic_light_minlight,dynamic_light_key,dynamic_light_style"
#endif
                  << ",thread_id\n"
                  << std::fixed << std::setprecision(6);
    {
        std::lock_guard<std::mutex> lock(render_worker_mutex_);
        const size_t worker_start = render_worker_count_ == Capacity ? render_worker_cursor_ : 0;
        for (size_t index = 0; index < render_worker_count_; ++index) {
            const auto& sample = render_worker_samples_[(worker_start + index) % Capacity];
            worker_output << sample.begin_qpc << ',' << sample.end_qpc << ','
                          << static_cast<double>(sample.end_qpc - sample.begin_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart)
                          << ',' << sample.cycles << ',' << sample.queued_calls << ','
                          << sample.parameter_bytes << ',' << sample.vertex_bytes << ','
                          << sample.index_bytes << ',' << sample.hardware_sync_count << ','
                          << static_cast<double>(sample.hardware_sync_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart) << ','
                          << static_cast<double>(sample.max_hardware_sync_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart) << ','
                          << sample.hardware_sync_cycles << ',' << sample.sync_index_before << ','
                          << sample.sync_pending_before << ',' << sample.sync_pending_after << ','
                          << sample.present_count << ','
                          << static_cast<double>(sample.present_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart) << ','
                          << static_cast<double>(sample.max_present_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart);
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
            worker_output << ',' << sample.draw_count << ','
                          << static_cast<double>(sample.draw_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart) << ','
                          << static_cast<double>(sample.max_draw_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart) << ','
                          << sample.max_draw_method << ',' << sample.max_draw_primitive_type << ','
                          << sample.max_draw_primitive_count << ',' << sample.max_draw_vertex_count;
            for (const auto address : sample.max_draw_stack)
                worker_output << ",0x" << std::hex << address << std::dec;
            for (size_t command = 0; command < sample.command_keys.size(); ++command)
                worker_output << ",0x" << std::hex << sample.command_keys[command] << std::dec
                              << ',' << sample.command_counts[command];
            worker_output << ',' << sample.dynamic_light_count << ','
                          << static_cast<double>(sample.dynamic_light_qpc) * 1000.0 /
                                 static_cast<double>(frequency_.QuadPart)
                          << ',' << sample.dynamic_light_unique_lights
                          << ',' << sample.dynamic_light_unique_surfaces;
            worker_output << ",0x" << std::hex << sample.dynamic_light_first_light
                          << ",0x" << sample.dynamic_light_first_surface
                          << ",0x" << sample.dynamic_light_first_flags << std::dec
                          << ',' << static_cast<unsigned int>(sample.dynamic_light_first_bool_a)
                          << ',' << static_cast<unsigned int>(sample.dynamic_light_first_bool_b);
            const auto light_float = [&sample](size_t word) {
                float value = 0.0f;
                std::memcpy(&value, &sample.dynamic_light_words[word], sizeof(value));
                return value;
            };
            worker_output << ",0x" << std::hex << sample.dynamic_light_words[0] << std::dec
                          << ',' << light_float(1) << ',' << light_float(2)
                          << ',' << light_float(3) << ',' << light_float(4)
                          << ",0x" << std::hex << sample.dynamic_light_words[5] << std::dec
                          << ',' << light_float(6) << ',' << light_float(7)
                          << ',' << light_float(8)
                          << ',' << static_cast<std::int32_t>(sample.dynamic_light_words[9])
                          << ',' << static_cast<std::int32_t>(sample.dynamic_light_words[10]);
#endif
            worker_output << ',' << sample.thread_id << '\n';
        }
    }
    return frame_output_good && worker_output.good();
}

} // namespace p2boost
