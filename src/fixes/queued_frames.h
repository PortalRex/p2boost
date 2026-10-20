#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace p2boost {

	// with mat_queue_mode 2 CMaterialSystem::EndFrame hands the frame's queued
	// render context to a thread-pool job and starts recording the next frame
	// into the other of its two queued contexts, so it first waits for the job
	// that is still replaying that context. Any frame whose render job runs
	// longer than the main thread's next frame (a second split-screen view, a
	// shadow refresh) stalls the main thread: 11% of it in the split-screen
	// lobby, 16% inside the slow frames. This replaces the mode-2 frame flip
	// with a ring of three contexts (the engine's two and one copy) so two
	// jobs can be in flight; each job waits for the one before it on the
	// worker thread, which keeps device calls serialized and keeps the
	// engine's own m_pActiveAsyncJob meaningful. Any other state (another
	// thread mode, a pending mode switch, VProf) drains the ring and lets the
	// engine's EndFrame run unchanged.
	class QueuedFrames {
	public:
		~QueuedFrames( );

		QueuedFrames( )                                = default;
		QueuedFrames( const QueuedFrames& )            = delete;
		QueuedFrames& operator=( const QueuedFrames& ) = delete;

		bool install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces );
		void remove( );

		// frames in flight on the render thread: 1 is the engine's behaviour.
		void set_depth( int depth ) noexcept {
			m_depth.store( depth < 1 ? 1 : ( depth > 2 ? 2 : depth ), std::memory_order_relaxed );
		}

		int depth( ) const noexcept {
			return m_depth.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_end_frame.installed( );
		}

		std::uint64_t ring_frames( ) const noexcept {
			return m_ring_frames.load( std::memory_order_relaxed );
		}

		std::uint64_t drained_count( ) const noexcept {
			return m_drained.load( std::memory_order_relaxed );
		}

		// called by a frame job on the worker thread.
		void execute_job( void* context, void* previous_job );

	private:
		using end_frame_fn_t      = void( __thiscall* )( void* );
		using begin_frame_fn_t    = void( __thiscall* )( void*, float );
		using bool_query_fn_t     = bool( __thiscall* )( void* );
		using void_fn_t           = void( __thiscall* )( void* );
		using set_int_fn_t        = void( __thiscall* )( void*, int );
		using job_finished_fn_t   = bool( __thiscall* )( void* );
		using job_wait_fn_t       = bool( __thiscall* )( void*, unsigned int );
		using queue_functor_fn_t  = void( __thiscall* )( void*, void*, void* );
		using begin_queue_fn_t    = void( __thiscall* )( void*, void* );
		using end_queue_fn_t      = void( __thiscall* )( void*, bool );
		using context_init_fn_t   = void( __thiscall* )( void*, void*, void* );
		using context_shut_fn_t   = void( __fastcall* )( void* );
		using context_ctor_fn_t   = void*(__fastcall*)( void* );
		using stack_init_fn_t     = bool( __thiscall* )( void*, const char*, int, int, int, int );
		using stack_free_fn_t     = void( __thiscall* )( void*, bool );
		using stack_term_fn_t     = void( __thiscall* )( void* );
		using allocate_fn_t       = void*(__cdecl*)( std::size_t );
		using job_construct_fn_t  = void*(__thiscall*)( void*, void*, const char* );
		using thread_execute_fn_t = void( __thiscall* )( void*, void* );
		using in_main_thread_fn_t = bool( __cdecl* )( );

		static void __fastcall EndFrame( void* self, void* );
		static void __fastcall BeginFrame( void* self, void*, float frame_time );

		bool  fast_path_allowed( void* self ) const noexcept;
		void* recording_context( void* self ) const noexcept;
		void  set_recording_context( void* context ) noexcept;
		bool  ensure_private_context( void* self );
		bool  ensure_third_stack( );
		void* render_data_stack( std::size_t index ) const noexcept;
		void  drain( void* self );
		void  release_job( void*& job ) noexcept;
		void* create_job( void* self, void* context, void* previous );
		void  run_ring( void* self );

		static QueuedFrames* m_instance;

		VmtHook m_end_frame;
		VmtHook m_begin_frame;
		void*   m_materials{ };

		// everything below is read from CMaterialSystem::EndFrame's own body.
		in_main_thread_fn_t m_in_main_thread{ };
		void**              m_vprof{ };
		unsigned long       m_tls_index{ };
		void**              m_queue_mode_cvar{ };
		void**              m_shader_api{ };
		job_finished_fn_t   m_job_finished{ };
		job_wait_fn_t       m_job_wait{ };
		void*               m_end_marker{ };
		void*               m_begin_marker{ };
		queue_functor_fn_t  m_queue_functor{ };
		begin_queue_fn_t    m_begin_queue{ };
		allocate_fn_t       m_allocate{ };
		thread_execute_fn_t m_thread_execute{ };
		job_construct_fn_t  m_job_construct{ };
		void**              m_priority_cvar{ };
		void***             m_thread_pool{ };
		end_queue_fn_t      m_end_queue{ };
		context_init_fn_t   m_context_init{ };
		context_shut_fn_t   m_context_shutdown{ };
		context_ctor_fn_t   m_context_construct{ };

		// the engine double-buffers the render data its queued calls point
		// into (bone matrices, decals) in two static CMemoryStacks indexed by
		// a static int; with three frames in flight a third is needed, and
		// there is room for it in the unused buffer that follows the two.
		int*            m_render_data_index{ };
		std::uint8_t*   m_render_data_stacks{ };
		stack_init_fn_t m_stack_init{ };
		stack_free_fn_t m_stack_free_all{ };
		stack_term_fn_t m_stack_term{ };
		bool            m_third_stack{ };
		// the stack the frame after the next BeginFrame records into, or -1
		// when the engine's own flip is right.
		int m_render_data_target{ -1 };

		// the ring: the engine's two queued contexts and the private third.
		std::array< void*, 3 > m_contexts{ };
		std::array< void*, 3 > m_jobs{ };
		std::array< int, 3 >   m_job_order{ };
		// the render-data stack each context's frames record into.
		std::array< int, 3 > m_context_stack{ 0, 1, 2 };
		void*                m_private{ };
		int                  m_order{ };
		bool                 m_active{ };

		std::atomic_int              m_depth{ 2 };
		std::atomic< std::uint64_t > m_ring_frames{ };
		std::atomic< std::uint64_t > m_drained{ };
	};

}
