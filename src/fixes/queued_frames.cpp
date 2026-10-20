#include "queued_frames.h"

#include "address.h"
#include "functor.h"
#include "game_profile.h"
#include "interfaces.h"
#include "material_system.h"
#include "vmt.h"

#include <Windows.h>

#include <cstring>
#include <malloc.h>

namespace p2boost {

	QueuedFrames* QueuedFrames::m_instance{ };

	namespace {
		using namespace material_system;

		using void_fn_t = void( __thiscall* )( void* );
		using bool_fn_t = bool( __thiscall* )( void* );
		using ptr_fn_t  = void*(__thiscall*)( void* );
		using int_fn_t  = int( __thiscall* )( void* );
		using set_fn_t  = void( __thiscall* )( void*, int );
		using add_fn_t  = void( __thiscall* )( void*, void* );

		int add_ref( void* object ) noexcept {
			return vfunc< int_fn_t >( object, vmt_add_ref )( object );
		}

		int release( void* object ) noexcept {
			return vfunc< int_fn_t >( object, vmt_release )( object );
		}

		// the functor a frame job runs: replay one recorded context after the
		// job before it.
		class frame_functor_t final : public sdk::Functor< frame_functor_t > {
		public:
			frame_functor_t( QueuedFrames* owner, void* context, void* previous ) noexcept
			    : m_owner{ owner }, m_context{ context }, m_previous{ previous } {
			}

			void Run( ) override {
				if( m_owner )
					m_owner->execute_job( m_context, m_previous );
				m_previous = nullptr;
			}

		private:
			QueuedFrames* m_owner;
			void*         m_context;
			void*         m_previous;
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

	QueuedFrames::~QueuedFrames( ) {
		remove( );
	}

	bool QueuedFrames::install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces ) {
		sdk::IMaterialSystem* materials = interfaces.material_system( );
		if( m_instance || m_end_frame.installed( ) || !modules.m_material_system || !modules.m_tier0 || !materials )
			return false;

		const game::profile_t* game        = game::active( );
		const Module&          module      = modules.m_material_system;
		const Address          end_frame   = module.find( game->materials_end_frame );
		const Address          begin_frame = module.find( game->materials_begin_frame );
		const Address          ctor        = module.find( game->materials_queued_context_ctor );
		const Address          lock_data   = module.find( game->materials_lock_render_data );
		const Address          stack_init  = module.find( game->materials_memory_stack_init );
		const Address          stack_free  = module.find( game->materials_memory_stack_free_all );
		const Address          stack_term  = module.find( game->materials_memory_stack_term );
		if( !end_frame || !begin_frame || !ctor || !lock_data || !stack_init || !stack_free || !stack_term )
			return false;

		const Address render_data_index  = lock_data.add( lock_render_data_index ).at< Address >( );
		const Address render_data_stacks = lock_data.add( lock_render_data_stacks ).at< Address >( );
		if( !module.contains( render_data_index, sizeof( int ) ) || !module.contains( render_data_stacks, 2 * render_data_stack_size + render_data_buffer_size ) )
			return false;

		// the live object's table may be a copy another plugin installed, so
		// find EndFrame in the table the engine actually calls through.
		void** live       = vmt_of( materials );
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

		m_in_main_thread     = in_main_thread.as< in_main_thread_fn_t >( );
		m_vprof              = immediate< Address >( end_frame, op_vprof ).as< void** >( );
		m_tls_index          = tls_object.at< unsigned long >( );
		m_queue_mode_cvar    = immediate< Address >( end_frame, op_queue_mode ).as< void** >( );
		m_shader_api         = immediate< Address >( end_frame, op_shader_api ).as< void** >( );
		m_job_finished       = rel32_target( end_frame, op_job_finished ).as< job_finished_fn_t >( );
		m_job_wait           = rel32_target( end_frame, op_job_wait ).as< job_wait_fn_t >( );
		m_end_marker         = immediate< Address >( end_frame, op_end_marker ).as< void* >( );
		m_begin_marker       = immediate< Address >( end_frame, op_begin_marker ).as< void* >( );
		m_queue_functor      = rel32_target( end_frame, op_queue_functor ).as< queue_functor_fn_t >( );
		m_begin_queue        = rel32_target( end_frame, op_begin_queue ).as< begin_queue_fn_t >( );
		m_allocate           = rel32_target( end_frame, op_allocate ).as< allocate_fn_t >( );
		m_thread_execute     = immediate< Address >( end_frame, op_thread_execute ).as< thread_execute_fn_t >( );
		m_job_construct      = rel32_target( end_frame, op_job_construct ).as< job_construct_fn_t >( );
		m_priority_cvar      = immediate< Address >( end_frame, op_priority ).as< void** >( );
		m_thread_pool        = thread_pool.as< void*** >( );
		m_end_queue          = rel32_target( end_frame, op_end_queue ).as< end_queue_fn_t >( );
		m_context_init       = rel32_target( end_frame, op_context_init ).as< context_init_fn_t >( );
		m_context_shutdown   = rel32_target( end_frame, op_context_shutdown ).as< context_shut_fn_t >( );
		m_context_construct  = ctor.as< context_ctor_fn_t >( );
		m_render_data_index  = render_data_index.as< int* >( );
		m_render_data_stacks = render_data_stacks.as< std::uint8_t* >( );
		m_stack_init         = stack_init.as< stack_init_fn_t >( );
		m_stack_free_all     = stack_free.as< stack_free_fn_t >( );
		m_stack_term         = stack_term.as< stack_term_fn_t >( );
		m_materials          = materials;

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
		m_contexts          = { Address{ materials }.add( queued_contexts ).as< void* >( ),
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
		if( depth( ) < 2 || !m_in_main_thread( ) || materials.at< int >( thread_mode ) != 2 )
			return false;
		if( !vfunc< bool_fn_t >( self, vmt_materials_can_end_frame )( self ) )
			return false;
		if( *m_vprof && Address{ *m_vprof }.at< int >( vprof_enabled ) != 0 )
			return false;

		int desired = Address{ *m_queue_mode_cvar }.at< int >( cvar_value );
		if( desired < 0 )
			desired = materials.at< int >( default_thread_mode );
		void* shader_api = *m_shader_api;
		if( !shader_api || !vfunc< bool_fn_t >( shader_api, vmt_shader_threading_ok )( shader_api ) || !materials.at< std::uint8_t >( threading_allowed ) )
			return false;
		if( materials.at< std::uint8_t >( force_single_threaded ) || materials.at< int >( pending_deletions ) != 0 )
			return false;
		return desired == 2;
	}

	// the third context is constructed and initialised exactly as the
	// engine's two embedded ones are.
	bool QueuedFrames::ensure_private_context( void* self ) {
		if( m_private )
			return true;
		if( !ensure_third_stack( ) )
			return false;

		void* context = _aligned_malloc( queued_context_size, 16 );
		if( !context )
			return false;
		std::memset( context, 0, queued_context_size );
		m_context_construct( context );
		m_context_init( context, self, Address{ self }.add( hardware_context ).as< void* >( ) );
		m_private       = context;
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
		if( !m_stack_init( stack, nullptr, engine_stack.at< int >( stack_max_size ), engine_stack.at< int >( stack_commit_size ), 0,
		                   engine_stack.at< int >( stack_alignment ) ) )
			return false;
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
		(void)self;
		return job;
	}

	// worker thread: finish the frame before this one, then replay this one.
	void QueuedFrames::execute_job( void* context, void* previous_job ) {
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
		m_thread_execute( m_materials, context );
	}

	// one queued-mode frame flip with three contexts: hand the recorded
	// context to a job, record the next frame into the context whose job is
	// the oldest, after waiting for that one only.
	void QueuedFrames::run_ring( void* self ) {
		const Address materials{ self };
		void*         recording = recording_context( self );
		if( !recording ) {
			// not one of ours: the engine must be mid-switch; take its view.
			recording = Address{ materials }.add( queued_contexts + materials.at< int >( current_context_index ) * queued_context_size ).as< void* >( );
			set_recording_context( recording );
		}

		if( !m_active ) {
			// adopt the engine's in-flight job: it replays the context that is
			// not the recording one. the engine flips the context index only
			// in queued EndFrames but the stack index in every BeginFrame, so
			// after single-threaded frames (Mel's maps force mat_queue_mode 0
			// after spawn) context k need not record into stack k: take each
			// context's stack from the index the frame was recorded with.
			const int recorded_stack = *m_render_data_index == 1 ? 1 : 0;
			for( std::size_t index = 0; index < 2; ++index )
				m_context_stack[ index ] = m_contexts[ index ] == recording ? recorded_stack : 1 - recorded_stack;
			m_context_stack[ 2 ] = 2;
			m_jobs               = { };
			m_job_order          = { };
			void* engine_job     = materials.at< void* >( active_job );
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
		vfunc< void_fn_t >( recording, vmt_context_end_frame_hook )( recording );
		vfunc< set_fn_t >( self, vmt_materials_frame_hook )( self, 0 );

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
		if( m_jobs[ next ] ) {
			if( !m_job_finished( m_jobs[ next ] ) )
				m_job_wait( m_jobs[ next ], 0xffffffff );
			release_job( m_jobs[ next ] );
		}

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
		m_queue_functor( queue, shader_api, m_end_marker );
		m_begin_queue( m_contexts[ next ], recording );
		queue = vfunc< ptr_fn_t >( m_contexts[ next ], vmt_context_call_queue )( m_contexts[ next ] );
		m_queue_functor( queue, shader_api, m_begin_marker );
		set_recording_context( m_contexts[ next ] );
		// the next frame's render data lives in its context's stack, so the
		// stacks rotate with the contexts and a stack is reused only after the
		// job that read it was waited for above. The
		// index itself moves in the BeginFrame hook: until then it stays on
		// the stack just recorded, as the engine leaves it, so the engine's
		// single current-alloc-point still belongs to the indexed stack.
		m_render_data_target = m_context_stack[ next ];
		if( next < 2 )
			materials.add( current_context_index ).as< int* >( )[ 0 ] = static_cast< int >( next );

		void* job = create_job( self, recording, newest );
		if( job ) {
			m_jobs[ recording_index ]      = job;
			m_job_order[ recording_index ] = ++m_order;
			add_ref( job );
			void*& engine_job = materials.add( active_job ).as< void** >( )[ 0 ];
			if( engine_job )
				release( engine_job );
			engine_job = job;
			vfunc< add_fn_t >( *m_thread_pool, vmt_pool_add_job )( *m_thread_pool, job );
		}
		materials.add( in_end_frame ).as< std::uint8_t* >( )[ 0 ] = 0;
		m_ring_frames.fetch_add( 1, std::memory_order_relaxed );
	}

	// back to the engine's two-context state: every job finished, and the
	// frame recorded into the private context replayed here as mode 1 does.
	void QueuedFrames::drain( void* self ) {
		if( !m_active )
			return;
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

		void* recording = recording_context( self );
		if( recording == m_private && m_private ) {
			const int index  = materials.at< int >( current_context_index ) & 1;
			void*     engine = m_contexts[ index ];
			m_begin_queue( engine, m_private );
			set_recording_context( Address{ self }.add( hardware_context ).as< void* >( ) );
			m_end_queue( m_private, true );
			set_recording_context( engine );
			materials.add( current_context_index ).as< int* >( )[ 0 ] = index;
		} else if( recording == m_contexts[ 0 ] || recording == m_contexts[ 1 ] ) {
			materials.add( current_context_index ).as< int* >( )[ 0 ] = recording == m_contexts[ 1 ] ? 1 : 0;
		}
		m_active = false;
	}

	// the engine's flip (MarkRenderDataUnused) sets index = 1 - index, frees
	// that stack, and BeginFrame records into it. steer it onto the stack of
	// the context recording this frame, whose job was waited for, so the
	// flip never resets a stack a job in flight still reads (Portal Stories:
	// Mel crashed in studiorender within seconds of a load).
	//
	// two engine slots must hold the right stacks for the call: the one the
	// index names before the flip must hold the stack just recorded, as the
	// engine leaves it (CMatRenderContext::OnRenderDataUnreferenced frees the
	// indexed stack whenever the shared lock count drops to zero); the one
	// the flip lands on must hold the target. the third stack has no slot of
	// its own, so it is parked in whichever of the two needs it and swapped
	// back after the call.
	void __fastcall QueuedFrames::BeginFrame( void* self, void*, float frame_time ) {
		QueuedFrames* ring     = m_instance;
		const auto    original = ring ? ring->m_begin_frame.previous< begin_frame_fn_t >( ) : nullptr;
		if( !original )
			return;

		const int target = ring->m_render_data_target;
		int&      index  = *ring->m_render_data_index;
		if( !ring->m_active || target < 0 ) {
			original( self, frame_time );
			return;
		}

		const int recorded = index;
		const int landing  = target < 2 ? target : ( recorded == 1 ? 0 : 1 );
		const int before   = 1 - landing;
		const int park     = target == 2 ? landing : ( recorded == 2 ? before : -1 );

		std::uint8_t* const third = static_cast< std::uint8_t* >( ring->render_data_stack( 2 ) );
		std::uint8_t* const slot  = park >= 0 ? static_cast< std::uint8_t* >( ring->render_data_stack( park ) ) : nullptr;
		std::uint8_t        engine_stack[ render_data_stack_size ];
		if( slot ) {
			std::memcpy( engine_stack, slot, render_data_stack_size );
			std::memcpy( slot, third, render_data_stack_size );
		}
		index = before;
		original( self, frame_time );
		// BeginFrame returns early outside the main thread or mid-frame.
		const bool flipped = index == landing;
		if( slot ) {
			std::memcpy( third, slot, render_data_stack_size );
			std::memcpy( slot, engine_stack, render_data_stack_size );
		}
		index = flipped ? target : recorded;
	}

	void __fastcall QueuedFrames::EndFrame( void* self, void* ) {
		QueuedFrames* ring     = m_instance;
		const auto    original = ring ? ring->m_end_frame.previous< end_frame_fn_t >( ) : nullptr;
		if( !original )
			return;

		if( ring->fast_path_allowed( self ) && ( ring->m_private || ring->ensure_private_context( self ) ) ) {
			ring->run_ring( self );
			return;
		}
		ring->drain( self );
		original( self );
	}

}
