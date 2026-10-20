#pragma once

#include <cstddef>
#include <cstdint>

// materialsystem.dll internals the frame-queue ring (fixes/queued_frames)
// works with: CMaterialSystem fields, the thread pool's CJob and tier0's
// CMemoryStack layouts, internal vtable slots, and operand positions inside
// CMaterialSystem::EndFrame. Recovered from Portal 2 build 23973718; every
// function they come from is identical in Portal Stories: Mel
// (research/tools/compare_functions.py).
namespace p2boost::material_system {

	// CMaterialSystem layout around the queued-context ring, and the
	// EndFrame operands everything else is read from.
	constexpr std::ptrdiff_t hardware_context{ 0x22f4 };
	constexpr std::ptrdiff_t queued_contexts{ 0x2534 };
	constexpr std::ptrdiff_t queued_context_size{ 0x2b8 };
	constexpr std::ptrdiff_t current_context_index{ 0x2aa4 };
	constexpr std::ptrdiff_t thread_mode{ 0x2aa8 };
	constexpr std::ptrdiff_t default_thread_mode{ 0x2aac };
	constexpr std::ptrdiff_t threading_allowed{ 0x2bb5 };
	constexpr std::ptrdiff_t force_single_threaded{ 0x2bb4 };
	constexpr std::ptrdiff_t in_end_frame{ 0x2bb3 };
	constexpr std::ptrdiff_t active_job{ 0x2bbc };
	constexpr std::ptrdiff_t pending_deletions{ 0x2bcc };
	constexpr std::ptrdiff_t vprof_enabled{ 0x100c };
	constexpr std::ptrdiff_t cvar_value{ 0x30 };
	constexpr std::ptrdiff_t job_flags{ 0x1c };
	constexpr std::ptrdiff_t job_complete_event{ 0x28 };
	constexpr std::size_t    job_size{ 0x44 };
	constexpr std::uint8_t   job_flag_queue{ 8 };

	// CMemoryStack: its size and the Init parameters copied from the
	// engine's first render-data stack; the third stack overlays the
	// path buffer after the engine's two, which is only used while the
	// first render context initialises.
	constexpr std::ptrdiff_t render_data_stack_size{ 0x28 };
	constexpr std::ptrdiff_t stack_max_size{ 0x18 };
	constexpr std::ptrdiff_t stack_alignment{ 0x1c };
	constexpr std::ptrdiff_t stack_commit_size{ 0x20 };
	constexpr std::ptrdiff_t lock_render_data_index{ 0x05 };
	constexpr std::ptrdiff_t lock_render_data_stacks{ 0x1e };
	constexpr std::ptrdiff_t render_data_buffer_size{ 0x104 };

	// virtual slots: IMaterialSystem's frame gate and per-frame hook, the
	// queued context's end-frame hook and call-queue getter, the shader
	// API's threading query, the thread pool's AddJob, and refcounting.
	constexpr std::size_t vmt_materials_can_end_frame{ 128 };
	constexpr std::size_t vmt_materials_frame_hook{ 19 };
	constexpr std::size_t vmt_context_end_frame_hook{ 221 };
	constexpr std::size_t vmt_context_call_queue{ 225 };
	constexpr std::size_t vmt_shader_threading_ok{ 143 };
	constexpr std::size_t vmt_pool_add_job{ 14 };
	constexpr std::size_t vmt_add_ref{ 0 };
	constexpr std::size_t vmt_release{ 1 };

	// operand offsets inside CMaterialSystem::EndFrame, each checked
	// against the opcode byte before it at install.
	struct operand_t {
		std::ptrdiff_t m_offset;
		std::uint8_t   m_opcode;
	};
	constexpr operand_t op_in_main_thread{ 0x0b, 0x15 };
	constexpr operand_t op_vprof{ 0x2d, 0x0d };
	constexpr operand_t op_tls{ 0x59, 0xb9 };
	constexpr operand_t op_queue_mode{ 0x84, 0xa1 };
	constexpr operand_t op_shader_api{ 0x9f, 0x0d };
	constexpr operand_t op_job_finished{ 0x124, 0xe8 };
	constexpr operand_t op_job_wait{ 0x131, 0xe8 };
	constexpr operand_t op_end_marker{ 0x171, 0x68 };
	constexpr operand_t op_queue_functor{ 0x186, 0xe8 };
	constexpr operand_t op_begin_queue{ 0x1a9, 0xe8 };
	constexpr operand_t op_begin_marker{ 0x1c9, 0x68 };
	constexpr operand_t op_allocate{ 0x1ff, 0xe8 };
	constexpr operand_t op_thread_execute{ 0x215, 0xb8 };
	constexpr operand_t op_job_construct{ 0x227, 0xe8 };
	constexpr operand_t op_priority{ 0x237, 0x0d };
	constexpr operand_t op_thread_pool{ 0x247, 0x15 };
	constexpr operand_t op_end_queue{ 0x3ba, 0xe8 };
	constexpr operand_t op_context_init{ 0x476, 0xe8 };
	constexpr operand_t op_context_shutdown{ 0x536, 0xe8 };

}
