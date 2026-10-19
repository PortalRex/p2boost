#include "queued_frames.h"

#include "address.h"
#include "game_profile.h"
#include "interfaces.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <malloc.h>

namespace p2boost {

	QueuedFrames* QueuedFrames::m_instance{ };

	namespace {
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

		// context fields CMatQueuedRenderContext::Init fills, and the two
		// CUtlVectors the copy must not share with the engine's context.

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

		template < typename function_t >
		function_t vfunc( const void* object, std::size_t slot ) noexcept {
			return reinterpret_cast< function_t >( ( *static_cast< void* const* const* >( object ) )[ slot ] );
		}

		using void_fn_t  = void( __thiscall* )( void* );
		using bool_fn_t  = bool( __thiscall* )( void* );
		using ptr_fn_t   = void*( __thiscall* )( void* );
		using int_fn_t   = int( __thiscall* )( void* );
		using set_fn_t   = void( __thiscall* )( void*, int );
		using add_fn_t   = void( __thiscall* )( void*, void* );

		int add_ref( void* object ) noexcept {
			return vfunc< int_fn_t >( object, vmt_add_ref )( object );
		}

		int release( void* object ) noexcept {
			return vfunc< int_fn_t >( object, vmt_release )( object );
		}

		// the functor a frame job runs: CFunctor's layout is AddRef, Release, a
		// destructor the job never calls, then operator().
		class frame_functor_t final {
		public:
			frame_functor_t( QueuedFrames* owner, void* context, void* previous ) noexcept
			    : m_owner{ owner }, m_context{ context }, m_previous{ previous } {
			}

			virtual int AddRef( ) {
				return m_references.fetch_add( 1, std::memory_order_acq_rel ) + 1;
			}

			virtual int Release( ) {
				const int remaining = m_references.fetch_sub( 1, std::memory_order_acq_rel ) - 1;
				if( remaining == 0 )
					delete this;
				return remaining;
			}

			virtual void Destroy( ) {
			}

			virtual void Run( ) {
				if( m_owner )
					m_owner->execute_job( m_context, m_previous );
				m_previous = nullptr;
			}

		private:
			std::atomic_int m_references{ 1 };
			QueuedFrames*   m_owner;
			void*           m_context;
			void*           m_previous;
		};

		bool operand_matches( Address function, const operand_t& operand ) noexcept {
			return function.add( operand.m_offset - 1 ).at< std::uint8_t >( ) == operand.m_opcode;
		}

		Address rel32_target( Address function, const operand_t& operand ) noexcept {
			return function.add( operand.m_offset ).rel32( );
		}

		template < typename value_t >
		value_t immediate( Address function, const operand_t& operand ) noexcept {
			return function.add( operand.m_offset ).at< value_t >( );
		}
	}

	// RESEARCH: first-chance crash log with the faulting module and a stack sample.
	volatile int g_ring_step{ };
	// RESEARCH: 1 = two engine contexts only (the stock flip, reimplemented).
	int          g_x_ring_mode{ };
	void*        g_replay_context{ };
	void*        g_record_context{ };
	DWORD        g_main_thread{ };
	int*         g_render_data_index{ };
	std::uint8_t* g_render_data_stacks{ };
	int          g_third_stack_state{ };
	namespace {
		LONG CALLBACK crash_log( EXCEPTION_POINTERS* info ) {
			if( !info || !info->ExceptionRecord )
				return EXCEPTION_CONTINUE_SEARCH;
			const DWORD code = info->ExceptionRecord->ExceptionCode;
			if( code == 0x406D1388 || code == 0xE06D7363 || code == 0x40010006 || code == 0x4001000A )
				return EXCEPTION_CONTINUE_SEARCH;
			static bool once = false;
			if( once )
				return EXCEPTION_CONTINUE_SEARCH;
			once = true;
			HANDLE file = CreateFileA( "C:/Users/Rex/Documents/GitHub/p2boost/artifacts/p2boost_crash.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
			if( file == INVALID_HANDLE_VALUE )
				return EXCEPTION_CONTINUE_SEARCH;
			char  text[ 4096 ];
			int   length = 0;
			auto  line   = [ & ]( const void* address ) {
                HMODULE module{ };
                char    name[ 260 ]{ };
                GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast< const char* >( address ), &module );
                if( module )
                    GetModuleFileNameA( module, name, sizeof( name ) );
                length += std::snprintf( text + length, sizeof( text ) - length, "%p %s+0x%x\n", address, name, module ? static_cast< unsigned >( reinterpret_cast< std::uintptr_t >( address ) - reinterpret_cast< std::uintptr_t >( module ) ) : 0u );
			};
			length += std::snprintf( text, sizeof( text ), "exception %08lx thread %lu (main %lu) ring step %d mode %d replay %p record %p at eip, then stack words:\n", code, GetCurrentThreadId( ), g_main_thread, g_ring_step, g_x_ring_mode, g_replay_context, g_record_context );
			line( reinterpret_cast< const void* >( info->ContextRecord->Eip ) );
			if( g_render_data_index && g_render_data_stacks ) {
				length += std::snprintf( text + length, sizeof( text ) - length, "render data index %d third %d\n", *g_render_data_index, g_third_stack_state );
				for( int which = 0; which < 3 && length < 3000; ++which ) {
					const auto* words = reinterpret_cast< const std::uint32_t* >( g_render_data_stacks + which * 0x28 );
					length += std::snprintf( text + length, sizeof( text ) - length, "stack %d:", which );
					for( int word = 0; word < 10; ++word )
						length += std::snprintf( text + length, sizeof( text ) - length, " %08x", words[ word ] );
					length += std::snprintf( text + length, sizeof( text ) - length, "\n" );
				}
			}
			const auto* stack = reinterpret_cast< const void* const* >( info->ContextRecord->Esp );
			for( int index = 0; index < 48 && length < 3800; ++index ) {
				if( IsBadReadPtr( stack + index, sizeof( void* ) ) )
					break;
				line( stack[ index ] );
			}
			DWORD written{ };
			WriteFile( file, text, static_cast< DWORD >( length ), &written, nullptr );
			CloseHandle( file );
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}

	QueuedFrames::~QueuedFrames( ) {
		remove( );
	}

	bool QueuedFrames::install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces ) {
		sdk::IMaterialSystem* materials = interfaces.material_system( );
		if( m_instance || m_end_frame.installed( ) || !modules.m_material_system || !modules.m_tier0 || !materials )
			return false;

		const game::profile_t* game      = game::active( );
		const Module&          module    = modules.m_material_system;
		const Address          end_frame = module.find( game->materials_end_frame );
		const Address          begin_frame = module.find( game->materials_begin_frame );
		const Address          ctor      = module.find( game->materials_queued_context_ctor );
		const Address          lock_data = module.find( game->materials_lock_render_data );
		const Address          stack_init = module.find( game->materials_memory_stack_init );
		const Address          stack_free = module.find( game->materials_memory_stack_free_all );
		const Address          stack_term = module.find( game->materials_memory_stack_term );
		if( !end_frame || !begin_frame || !ctor || !lock_data || !stack_init || !stack_free || !stack_term )
			return false;

		const Address render_data_index  = lock_data.add( lock_render_data_index ).at< Address >( );
		const Address render_data_stacks = lock_data.add( lock_render_data_stacks ).at< Address >( );
		if( !module.contains( render_data_index, sizeof( int ) )
		    || !module.contains( render_data_stacks, 2 * render_data_stack_size + render_data_buffer_size ) )
			return false;

		// the live object's table may be a copy another plugin installed, so
		// find EndFrame in the table the engine actually calls through.
		void** live       = *static_cast< void*** >( static_cast< void* >( materials ) );
		void** slot       = nullptr;
		void** begin_slot = nullptr;
		for( std::size_t index = 0; live && index < 200 && ( !slot || !begin_slot ); ++index ) {
			if( live[ index ] == end_frame.as< void* >( ) )
				slot = live + index;
			if( live[ index ] == begin_frame.as< void* >( ) )
				begin_slot = live + index;
		}
		if( !slot || !begin_slot )
			return false;

		constexpr operand_t operands[]{ op_in_main_thread, op_vprof, op_tls, op_queue_mode, op_shader_api, op_job_finished, op_job_wait,
			                            op_end_marker, op_queue_functor, op_begin_queue, op_begin_marker, op_allocate, op_thread_execute,
			                            op_job_construct, op_priority, op_thread_pool, op_end_queue, op_context_init, op_context_shutdown };
		for( const operand_t& operand : operands ) {
			if( !operand_matches( end_frame, operand ) )
				return false;
		}

		const Address in_main_thread = immediate< Address >( end_frame, op_in_main_thread ).at< Address >( );
		const Address tls_object     = immediate< Address >( end_frame, op_tls );
		const Address thread_pool    = immediate< Address >( end_frame, op_thread_pool ).at< Address >( );
		if( !modules.m_tier0.contains( in_main_thread ) || !module.contains( tls_object, sizeof( unsigned long ) ) || !thread_pool )
			return false;

		m_in_main_thread   = in_main_thread.as< in_main_thread_fn_t >( );
		m_vprof            = immediate< Address >( end_frame, op_vprof ).as< void** >( );
		m_tls_index        = tls_object.at< unsigned long >( );
		m_queue_mode_cvar  = immediate< Address >( end_frame, op_queue_mode ).as< void** >( );
		m_shader_api       = immediate< Address >( end_frame, op_shader_api ).as< void** >( );
		m_job_finished     = rel32_target( end_frame, op_job_finished ).as< job_finished_fn_t >( );
		m_job_wait         = rel32_target( end_frame, op_job_wait ).as< job_wait_fn_t >( );
		m_end_marker       = immediate< Address >( end_frame, op_end_marker ).as< void* >( );
		m_begin_marker     = immediate< Address >( end_frame, op_begin_marker ).as< void* >( );
		m_queue_functor    = rel32_target( end_frame, op_queue_functor ).as< queue_functor_fn_t >( );
		m_begin_queue      = rel32_target( end_frame, op_begin_queue ).as< begin_queue_fn_t >( );
		m_allocate         = rel32_target( end_frame, op_allocate ).as< allocate_fn_t >( );
		m_thread_execute   = immediate< Address >( end_frame, op_thread_execute ).as< thread_execute_fn_t >( );
		m_job_construct    = rel32_target( end_frame, op_job_construct ).as< job_construct_fn_t >( );
		m_priority_cvar    = immediate< Address >( end_frame, op_priority ).as< void** >( );
		m_thread_pool      = thread_pool.as< void*** >( );
		m_end_queue        = rel32_target( end_frame, op_end_queue ).as< end_queue_fn_t >( );
		m_context_init     = rel32_target( end_frame, op_context_init ).as< context_init_fn_t >( );
		m_context_shutdown = rel32_target( end_frame, op_context_shutdown ).as< context_shut_fn_t >( );
		m_context_construct = ctor.as< context_ctor_fn_t >( );
		m_render_data_index  = render_data_index.as< int* >( );
		m_render_data_stacks = render_data_stacks.as< std::uint8_t* >( );
		m_stack_init         = stack_init.as< stack_init_fn_t >( );
		m_stack_free_all     = stack_free.as< stack_free_fn_t >( );
		m_stack_term         = stack_term.as< stack_term_fn_t >( );
		g_render_data_index  = m_render_data_index;
		g_render_data_stacks = m_render_data_stacks;
		m_materials        = materials;

		// every code target must be material-system code and the TLS slot must
		// hold a render context (or nothing) on this thread.
		const void* const targets[]{ reinterpret_cast< const void* >( m_job_finished ), reinterpret_cast< const void* >( m_job_wait ),
			                         m_end_marker, m_begin_marker, reinterpret_cast< const void* >( m_queue_functor ),
			                         reinterpret_cast< const void* >( m_begin_queue ), reinterpret_cast< const void* >( m_allocate ),
			                         reinterpret_cast< const void* >( m_thread_execute ), reinterpret_cast< const void* >( m_job_construct ),
			                         reinterpret_cast< const void* >( m_end_queue ), reinterpret_cast< const void* >( m_context_init ),
			                         reinterpret_cast< const void* >( m_context_shutdown ) };
		for( const void* target : targets ) {
			if( !module.contains( Address{ const_cast< void* >( target ) } ) )
				return false;
		}
		AddVectoredExceptionHandler( 1, &crash_log );
		g_main_thread = GetCurrentThreadId( );
		m_contexts = { Address{ materials }.add( queued_contexts ).as< void* >( ),
		               Address{ materials }.add( queued_contexts + queued_context_size ).as< void* >( ), nullptr };
		const void* current = TlsGetValue( m_tls_index );
		if( current && current != Address{ materials }.add( hardware_context ).as< void* >( ) && !recording_context( materials ) )
			return false;
		m_jobs     = { };
		m_active   = false;
		m_instance = this;
		if( !m_end_frame.install( slot, reinterpret_cast< void* >( &EndFrame ) ) ) {
			m_instance  = nullptr;
			m_materials = nullptr;
			return false;
		}
		if( !m_begin_frame.install( begin_slot, reinterpret_cast< void* >( &BeginFrame ) ) ) {
			m_end_frame.remove( );
			m_instance  = nullptr;
			m_materials = nullptr;
			return false;
		}
		return true;
	}

	void QueuedFrames::remove( ) {
		if( m_instance == this && m_materials && m_active && m_in_main_thread && m_in_main_thread( ) )
			drain( m_materials );
		m_begin_frame.remove( );
		if( m_third_stack && !m_active && *m_render_data_index < 2 ) {
			m_stack_term( render_data_stack( 2 ) );
			std::memset( render_data_stack( 2 ), 0, render_data_stack_size );
			m_third_stack = false;
		}
		m_end_frame.remove( );
		if( m_private ) {
			if( m_context_shutdown )
				m_context_shutdown( m_private );
			_aligned_free( m_private );
			m_private = nullptr;
		}
		m_contexts  = { };
		m_jobs      = { };
		m_active    = false;
		m_materials = nullptr;
		if( m_instance == this )
			m_instance = nullptr;
	}

	// the context this thread records into: one of the ring, or none.
	void* QueuedFrames::recording_context( void* ) const noexcept {
		void* current = TlsGetValue( m_tls_index );
		for( void* context : m_contexts )
			if( context && context == current )
				return current;
		return nullptr;
	}

	void QueuedFrames::set_recording_context( void* context ) noexcept {
		TlsSetValue( m_tls_index, context );
	}

	// the fast path reproduces EndFrame's own decision: queued threaded mode
	// now and after this frame, no profiler scope to balance, and a frame the
	// engine would process at all.
	bool QueuedFrames::fast_path_allowed( void* self ) const noexcept {
		const Address materials{ self };
		m_last_mode.store( materials.at< int >( thread_mode ), std::memory_order_relaxed );
		if( depth( ) < 2 ) {
			m_last_reason.store( 1, std::memory_order_relaxed );
			return false;
		}
		if( !m_in_main_thread( ) ) {
			m_last_reason.store( 2, std::memory_order_relaxed );
			return false;
		}
		if( materials.at< int >( thread_mode ) != 2 ) {
			m_last_reason.store( 3, std::memory_order_relaxed );
			return false;
		}
		if( !vfunc< bool_fn_t >( self, vmt_materials_can_end_frame )( self ) ) {
			m_last_reason.store( 4, std::memory_order_relaxed );
			return false;
		}
		if( *m_vprof && Address{ *m_vprof }.at< int >( vprof_enabled ) != 0 ) {
			m_last_reason.store( 5, std::memory_order_relaxed );
			return false;
		}

		int desired = Address{ *m_queue_mode_cvar }.at< int >( cvar_value );
		if( desired < 0 )
			desired = materials.at< int >( default_thread_mode );
		void* shader_api = *m_shader_api;
		if( !shader_api || !vfunc< bool_fn_t >( shader_api, vmt_shader_threading_ok )( shader_api ) || !materials.at< std::uint8_t >( threading_allowed ) ) {
			m_last_reason.store( 6, std::memory_order_relaxed );
			return false;
		}
		if( materials.at< std::uint8_t >( force_single_threaded ) || materials.at< int >( pending_deletions ) != 0 ) {
			m_last_reason.store( 7, std::memory_order_relaxed );
			return false;
		}
		m_last_reason.store( desired == 2 ? 0 : 8, std::memory_order_relaxed );
		return desired == 2;
	}

	// the third context is constructed and initialised exactly as the
	// engine's two embedded ones are.
	bool QueuedFrames::ensure_private_context( void* self ) {
		if( m_private )
			return true;
		if( !ensure_third_stack( ) )
			return false;
		g_ring_step = 1;

		void* context = _aligned_malloc( queued_context_size, 16 );
		if( !context )
			return false;
		std::memset( context, 0, queued_context_size );
		m_context_construct( context );
		g_ring_step = 2;
		m_context_init( context, self, Address{ self }.add( hardware_context ).as< void* >( ) );
		g_ring_step = 3;
		m_private      = context;
		m_contexts[ 2 ] = context;
		return true;
	}

	void* QueuedFrames::render_data_stack( std::size_t index ) const noexcept {
		return m_render_data_stacks + index * render_data_stack_size;
	}

	// the third render-data stack, set up like the engine's two.
	bool QueuedFrames::ensure_third_stack( ) {
		if( m_third_stack )
			return true;
		const Address engine_stack{ render_data_stack( 0 ) };
		void*         stack = render_data_stack( 2 );
		std::memset( stack, 0, render_data_stack_size );
		g_third_stack_state = 1;
		if( !m_stack_init( stack, nullptr, engine_stack.at< int >( stack_max_size ), engine_stack.at< int >( stack_commit_size ), 0,
		                   engine_stack.at< int >( stack_alignment ) ) ) {
			g_third_stack_state = 2;
			return false;
		}
		g_third_stack_state = 3;
		m_third_stack = true;
		return true;
	}

	void QueuedFrames::release_job( void*& job ) noexcept {
		if( job ) {
			release( job );
			job = nullptr;
		}
	}

	// a CFunctorJob around our functor, exactly as EndFrame builds its own.
	void* QueuedFrames::create_job( void* self, void* context, void* previous ) {
		void* memory = m_allocate( job_size );
		if( !memory )
			return nullptr;
		if( previous )
			add_ref( previous );
		auto* functor = new( std::nothrow ) frame_functor_t( this, context, previous );
		if( !functor ) {
			if( previous )
				release( previous );
			return nullptr;
		}
		void* job = m_job_construct( memory, functor, nullptr );
		if( *m_priority_cvar && Address{ *m_priority_cvar }.at< int >( cvar_value ) != 0 )
			Address{ job }.add( job_flags ).as< std::uint8_t* >( )[ 0 ] |= job_flag_queue;
		( void )self;
		return job;
	}

	// worker thread: finish the frame before this one, then replay this one.
	void QueuedFrames::execute_job( void* context, void* previous_job ) {
		g_ring_step = 40;
		if( previous_job ) {
			// a plain wait on the job's completion event: the pool's YieldWait
			// would run other queued jobs on this worker meanwhile.
			if( !m_job_finished( previous_job ) ) {
				HANDLE event = Address{ previous_job }.at< HANDLE >( job_complete_event );
				if( event )
					WaitForSingleObject( event, INFINITE );
				while( !m_job_finished( previous_job ) )
					Sleep( 0 );
			}
			release( previous_job );
		}
		g_ring_step = 41;
		g_replay_context = context;
		m_thread_execute( m_materials, context );
		g_ring_step = 42;
	}

	// one queued-mode frame flip with three contexts: hand the recorded
	// context to a job, record the next frame into the context whose job is
	// the oldest, after waiting for that one only.
	void QueuedFrames::run_ring( void* self ) {
		g_ring_step = 10;
		const Address materials{ self };
		void*         recording = recording_context( self );
		if( !recording ) {
			// not one of ours: the engine must be mid-switch; take its view.
			recording = Address{ materials }.add( queued_contexts + materials.at< int >( current_context_index ) * queued_context_size ).as< void* >( );
			set_recording_context( recording );
		}

		if( !m_active ) {
			// adopt the engine's in-flight job: it replays the context that is
			// not the recording one.
			m_jobs      = { };
			m_job_order = { };
			void* engine_job = materials.at< void* >( active_job );
			for( std::size_t index = 0; index < 2; ++index ) {
				if( m_contexts[ index ] != recording && engine_job ) {
					add_ref( engine_job );
					m_jobs[ index ]      = engine_job;
					m_job_order[ index ] = ++m_order;
				}
			}
			m_active = true;
		}

		std::size_t recording_index = 0;
		while( recording_index < m_contexts.size( ) && m_contexts[ recording_index ] != recording )
			++recording_index;

		// EndFrame's own per-frame hooks on the recording context and the system.
		g_ring_step = 11;
		vfunc< void_fn_t >( recording, vmt_context_end_frame_hook )( recording );
		g_ring_step = 12;
		vfunc< set_fn_t >( self, vmt_materials_frame_hook )( self, 0 );
		g_ring_step = 13;

		// the next recording context: the one whose job is oldest (or none).
		std::size_t next = recording_index;
		int         best = 0x7fffffff;
		for( std::size_t index = 0; index < m_contexts.size( ); ++index ) {
			if( index == recording_index || !m_contexts[ index ] )
				continue;
			const int order = m_jobs[ index ] ? m_job_order[ index ] : -1;
			if( order < best ) {
				best = order;
				next = index;
			}
		}
		g_ring_step = 14;
		if( m_jobs[ next ] ) {
			if( !m_job_finished( m_jobs[ next ] ) )
				m_job_wait( m_jobs[ next ], 0xffffffff );
			release_job( m_jobs[ next ] );
		}
		g_ring_step = 15;

		// the newest job is the one this frame's job must follow.
		void* newest       = nullptr;
		int   newest_order = -1;
		for( std::size_t index = 0; index < m_contexts.size( ); ++index )
			if( m_jobs[ index ] && m_job_order[ index ] > newest_order ) {
				newest       = m_jobs[ index ];
				newest_order = m_job_order[ index ];
			}

		void* shader_api = *m_shader_api;
		void* queue      = vfunc< ptr_fn_t >( recording, vmt_context_call_queue )( recording );
		g_ring_step = 16;
		m_queue_functor( queue, shader_api, m_end_marker );
		g_ring_step = 17;
		m_begin_queue( m_contexts[ next ], recording );
		g_ring_step = 18;
		queue = vfunc< ptr_fn_t >( m_contexts[ next ], vmt_context_call_queue )( m_contexts[ next ] );
		m_queue_functor( queue, shader_api, m_begin_marker );
		g_ring_step = 19;
		g_record_context = m_contexts[ next ];
		set_recording_context( m_contexts[ next ] );
		// the next frame's render data lives in the stack numbered like its
		// context, so the stacks rotate with the contexts and a stack is
		// reused only after the job that read it was waited for above. The
		// engine's BeginFrame flips the index as 1 - index and frees that
		// stack: leave the index so the flip lands on the next stack, or for
		// the third stack on one of its own (nothing allocates until
		// BeginFrame, where the hook below moves it on).
		m_render_data_target = static_cast< int >( next );
		*m_render_data_index = next < 2 ? 1 - static_cast< int >( next ) : 0;
		if( next < 2 )
			materials.add( current_context_index ).as< int* >( )[ 0 ] = static_cast< int >( next );

		g_ring_step = 20;
		void* job = create_job( self, recording, newest );
		g_ring_step = 21;
		if( job ) {
			m_jobs[ recording_index ]      = job;
			m_job_order[ recording_index ] = ++m_order;
			add_ref( job );
			void*& engine_job = materials.add( active_job ).as< void** >( )[ 0 ];
			if( engine_job )
				release( engine_job );
			engine_job = job;
			g_ring_step = 22;
			vfunc< add_fn_t >( *m_thread_pool, vmt_pool_add_job )( *m_thread_pool, job );
		}
		g_ring_step = 23;
		materials.add( in_end_frame ).as< std::uint8_t* >( )[ 0 ] = 0;
		m_ring_frames.fetch_add( 1, std::memory_order_relaxed );
		g_ring_step = 24;
	}

	// back to the engine's two-context state: every job finished, and the
	// frame recorded into the private context replayed here as mode 1 does.
	void QueuedFrames::drain( void* self ) {
		if( !m_active )
			return;
		g_ring_step = 30;
		m_drained.fetch_add( 1, std::memory_order_relaxed );

		const Address materials{ self };
		for( int round = 0; round < 3; ++round ) {
			std::size_t oldest = m_contexts.size( );
			int         order  = 0x7fffffff;
			for( std::size_t index = 0; index < m_contexts.size( ); ++index )
				if( m_jobs[ index ] && m_job_order[ index ] < order ) {
					order  = m_job_order[ index ];
					oldest = index;
				}
			if( oldest == m_contexts.size( ) )
				break;
			if( !m_job_finished( m_jobs[ oldest ] ) )
				m_job_wait( m_jobs[ oldest ], 0xffffffff );
			release_job( m_jobs[ oldest ] );
		}

		// every job has finished: only the frame still recording owns render
		// data, and if that is the third stack the engine's next flip must
		// land on one of its own two.
		m_render_data_target = -1;
		if( *m_render_data_index > 1 )
			*m_render_data_index = 0;

		g_ring_step = 31;
		void* recording = recording_context( self );
		if( recording == m_private && m_private ) {
			g_ring_step = 32;
			const int   index   = materials.at< int >( current_context_index ) & 1;
			void*       engine  = m_contexts[ index ];
			m_begin_queue( engine, m_private );
			set_recording_context( Address{ self }.add( hardware_context ).as< void* >( ) );
			m_end_queue( m_private, true );
			set_recording_context( engine );
			materials.add( current_context_index ).as< int* >( )[ 0 ] = index;
			g_ring_step = 33;
		} else if( recording == m_contexts[ 0 ] || recording == m_contexts[ 1 ] ) {
			materials.add( current_context_index ).as< int* >( )[ 0 ] = recording == m_contexts[ 1 ] ? 1 : 0;
		}
		m_active = false;
	}

	void QueuedFrames::debug( char* buffer, std::size_t size ) const noexcept {
		void** live = m_materials ? *static_cast< void*** >( m_materials ) : nullptr;
		std::snprintf( buffer, size, "slot %p=%p hook %p live[42] %p (%p)", static_cast< const void* >( m_end_frame.method( ) ),
		               m_end_frame.method( ) ? *m_end_frame.method( ) : nullptr, reinterpret_cast< const void* >( &EndFrame ),
		               live ? live[ 42 ] : nullptr, static_cast< const void* >( live ) );
	}

	// after the engine's flip, move the render data to the stack of the
	// context recording this frame when that is the third one.
	void __fastcall QueuedFrames::BeginFrame( void* self, void*, float frame_time ) {
		QueuedFrames* ring     = m_instance;
		const auto    original = ring ? ring->m_begin_frame.previous< begin_frame_fn_t >( ) : nullptr;
		if( !original )
			return;
		original( self, frame_time );
		if( ring->m_active && ring->m_render_data_target >= 0 && *ring->m_render_data_index != ring->m_render_data_target ) {
			*ring->m_render_data_index = ring->m_render_data_target;
			ring->m_stack_free_all( ring->render_data_stack( ring->m_render_data_target ), false );
		}
	}

	void __fastcall QueuedFrames::EndFrame( void* self, void* ) {
		QueuedFrames* ring     = m_instance;
		const auto    original = ring ? ring->m_end_frame.previous< end_frame_fn_t >( ) : nullptr;
		if( !original )
			return;

		ring->m_hook_calls.fetch_add( 1, std::memory_order_relaxed );
		if( ring->fast_path_allowed( self ) && ( g_x_ring_mode == 1 || ring->m_private || ring->ensure_private_context( self ) ) ) {
			ring->run_ring( self );
			return;
		}
		ring->drain( self );
		original( self );
	}

}
