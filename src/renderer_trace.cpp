#include "renderer_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2.h"
#include "qpc.h"
#include "renderer_research.h"
#include "x86.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace p2boost {
	RendererTrace* RendererTrace::m_instance{ };

	namespace {
		bool has_bytes( const Address address, const std::initializer_list< std::uint8_t >& bytes ) {
			return address && std::memcmp( address.as< const void* >( ), bytes.begin( ), bytes.size( ) ) == 0;
		}

		struct WorkerTrace : RenderWorkerMetrics {
			bool m_active{ };
		};
		thread_local WorkerTrace g_worker_trace;

	}

	RendererTrace::~RendererTrace( ) {
		remove( );
	}

	bool RendererTrace::install( Metrics* metrics ) {
		if( m_instance || !metrics )
			return false;
		const Address shader_module( GetModuleHandleW( L"shaderapidx9.dll" ) );
		const Address materials( GetModuleHandleW( L"materialsystem.dll" ) );
		if( !shader_module || !materials )
			return false;
		auto* material_module = materials.as< std::uint8_t* >( );

		// focus recovery crosses the shader api and material system.
		if( material_module ) {
			using CreateInterfaceFn = void*(__cdecl*)( const char*, int* );
			auto factory			= reinterpret_cast< CreateInterfaceFn >(
				   GetProcAddress( reinterpret_cast< HMODULE >( material_module ), "CreateInterface" ) );
			void* material_system = factory ? factory( "VMaterialSystem080", nullptr ) : nullptr;
			if( material_system ) {
				auto** vtable	  = *reinterpret_cast< void*** >( material_system );
				m_material_system = material_system;
				if( !vtable )
					return false;
				const Address material_lock{ vtable[ portal2::slot::material_lock ] };
				const Address material_unlock{ vtable[ portal2::slot::material_unlock ] };
				if( has_bytes( material_lock, { 0x56, 0x8b, 0xf1, 0x57 } ) &&
					has_bytes( material_unlock, { 0x55, 0x8b, 0xec, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x8b, 0xf1 } ) ) {
					m_material_lock	  = material_lock.as< MaterialLock_t >( );
					m_material_unlock = material_unlock.as< MaterialUnlock_t >( );
				}
			}
		}

		// resolve both Present xrefs without module-relative addresses.
		const Address needs_reset_match = pattern::find_unique( shader_module, portal2::signature::needs_reset );
		const Address other_app_match	= pattern::find_unique( shader_module, portal2::signature::other_app );
		const Address needs_reset		= needs_reset_match.add( portal2::xref::reacquire_call );
		const Address other_app			= other_app_match.add( portal2::xref::reacquire_call );

		// both xrefs must still reach the same private reacquire routine.
		const Address reacquire = needs_reset.rel32( 1 );
		if( !has_bytes( needs_reset, { 0xe8 } ) || !has_bytes( other_app, { 0xe8 } ) || !reacquire || other_app.rel32( 1 ) != reacquire )
			return false;
		m_original = reacquire.as< Reacquire_t >( );
		const Address engine( GetModuleHandleW( L"engine.dll" ) );
		if( !engine )
			return false;

		// locate both static-lighting calls by their surrounding restore logic.
		const Address lighting_site =
			pattern::find_unique( engine, portal2::signature::restore_static_lighting ).add( portal2::xref::restore_static_lighting );
		const Address lighting_cache_site = pattern::find_unique( engine, portal2::signature::restore_static_lighting_cache )
												.add( portal2::xref::restore_static_lighting_cache );
		if( !has_bytes( lighting_site, { 0xe8 } ) || !has_bytes( lighting_cache_site, { 0xe8 } ) )
			return false;
		m_lighting_original		  = lighting_site.rel32( 1 ).as< StaticLightingRestore_t >( );
		m_lighting_cache_original = lighting_cache_site.rel32( 1 ).as< StaticLightingRestore_t >( );
		auto expected_call		  = [ & ]( Address call ) { return x86::relative_call( call, Address( m_original ) ); };
		m_metrics				  = metrics;
		m_removing.store( false, std::memory_order_release );
		m_instance		  = this;
		auto install_slot = []( VtableSlotHook& hook, void* object, size_t index, void* replacement ) {
			if( !object )
				return false;
			auto** vtable = *reinterpret_cast< void*** >( object );
			return vtable && hook.install( vtable + index, replacement );
		};

		// recover private shader objects from unique absolute-operand xrefs.
		const Address shader_device_match  = pattern::find_unique( shader_module, portal2::signature::shader_device );
		const Address shader_device_global = shader_device_match.add( portal2::xref::shader_device_global ).at< Address >( );
		void*		  shader_device		   = shader_device_global.to< void* >( );

		const Address resource_callbacks_global = needs_reset_match.add( portal2::xref::resource_callbacks_global ).at< Address >( );
		void*		  resource_callbacks		= resource_callbacks_global.to< void* >( );

		const Address frame_sync_match = pattern::find_unique( shader_module, portal2::signature::frame_sync );
		void*		  frame_sync	   = frame_sync_match.add( portal2::xref::frame_sync_object ).at< Address >( ).as< void* >( );

		// one material-system function exposes both related device globals.
		const Address material_devices		= pattern::find_unique( materials, portal2::signature::material_devices );
		const Address present_device_global = material_devices.add( portal2::xref::present_device_global ).at< Address >( );
		const Address shader_api_global		= material_devices.add( portal2::xref::shader_api_global ).at< Address >( );
		void*		  present_device		= present_device_global.to< void* >( );
		void*		  shader_api			= shader_api_global.to< void* >( );
		if( !shader_api || !present_device ) {
			remove( );
			return false;
		}
		auto** shader_api_vtable = *reinterpret_cast< void*** >( shader_api );
		auto** present_vtable	 = *reinterpret_cast< void*** >( present_device );

		// reject interfaces whose recovered slots do not match the expected entries.
		if( !shader_api_vtable || !present_vtable ) {
			remove( );
			return false;
		}
		const Address hardware_sync{ shader_api_vtable[ portal2::slot::hardware_sync ] };
		const Address present{ present_vtable[ portal2::slot::present ] };
		// Present uses a stack frame before saving ESI; validate its complete stable prologue.
		if( !has_bytes( hardware_sync, { 0xa1 } ) || !has_bytes( present, { 0x55, 0x8b, 0xec, 0x83, 0xec, 0x40, 0x56, 0x8b, 0xf1 } ) ) {
			remove( );
			return false;
		}
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		// Draw/pipeline hooks exist only in instrumented builds. They measure and
		// identify stalls; none of this code is linked into the release DLL.
		// recover the native D3D9 device through a unique shader-api xref.
		const Address d3d_device_match	= pattern::find_unique( shader_module, portal2::signature::d3d_device );
		const Address d3d_device_global = d3d_device_match.add( portal2::xref::d3d_device_global ).at< Address >( );
		void*		  d3d_device		= d3d_device_global.to< void* >( );

		// resolve the two dynamic-light vtables from their constructors.
		const Address unsigned_vtable = pattern::find_unique( engine, portal2::signature::dynamic_light_unsigned )
											.add( portal2::xref::dynamic_light_unsigned_vtable )
											.at< Address >( );
		const Address signed_vtable = pattern::find_unique( engine, portal2::signature::dynamic_light_signed )
										  .add( portal2::xref::dynamic_light_signed_vtable )
										  .at< Address >( );
		// unwrap these two functors only while collecting research metrics.
		m_queued_functor_execute = pattern::find_unique( materials, portal2::signature::queued_functor_execute ).as< void* >( );
		m_queued_functor_leaf	 = pattern::find_unique( materials, portal2::signature::queued_functor_leaf ).as< void* >( );
		if( !d3d_device || !unsigned_vtable || !signed_vtable || !m_queued_functor_execute || !m_queued_functor_leaf ) {
			remove( );
			return false;
		}

		// slot three is the shared Execute method embedded by both constructors.
		auto** unsigned_execute_slot	 = unsigned_vtable.as< void** >( ) + 3;
		auto** signed_execute_slot		 = signed_vtable.as< void** >( ) + 3;
		m_dynamic_light_execute_original = reinterpret_cast< DynamicLightExecute_t >( *unsigned_execute_slot );
		const bool draw_hooks =
			d3d_device && install_slot( m_set_texture, d3d_device, portal2::d3d9::set_texture, reinterpret_cast< void* >( &SetTexture ) ) &&
			install_slot( m_set_vertex_shader,
				d3d_device,
				portal2::d3d9::set_vertex_shader,
				reinterpret_cast< void* >( &SetVertexShader ) ) &&
			install_slot( m_set_pixel_shader, d3d_device, portal2::d3d9::set_pixel_shader, reinterpret_cast< void* >( &SetPixelShader ) ) &&
			install_slot( m_draw_primitive, d3d_device, portal2::d3d9::draw_primitive, reinterpret_cast< void* >( &DrawPrimitive ) ) &&
			install_slot( m_draw_indexed_primitive,
				d3d_device,
				portal2::d3d9::draw_indexed_primitive,
				reinterpret_cast< void* >( &DrawIndexedPrimitive ) ) &&
			install_slot( m_draw_primitive_up,
				d3d_device,
				portal2::d3d9::draw_primitive_up,
				reinterpret_cast< void* >( &DrawPrimitiveUp ) ) &&
			install_slot( m_draw_indexed_primitive_up,
				d3d_device,
				portal2::d3d9::draw_indexed_primitive_up,
				reinterpret_cast< void* >( &DrawIndexedPrimitiveUp ) );
		const bool dynamic_light_hooks =
			*unsigned_execute_slot == *signed_execute_slot &&
			m_dynamic_light_execute_unsigned.install( unsigned_execute_slot, reinterpret_cast< void* >( &DynamicLightExecute ) ) &&
			m_dynamic_light_execute_signed.install( signed_execute_slot, reinterpret_cast< void* >( &DynamicLightExecute ) );
#else
		const bool draw_hooks		   = true;
		const bool dynamic_light_hooks = true;
#endif
		const auto lighting_call	   = x86::relative_call( lighting_site, Address( m_lighting_original ) );
		const auto lighting_cache_call = x86::relative_call( lighting_cache_site, Address( m_lighting_cache_original ) );

		// The queue stores the worker callback as a MOV EAX, imm32 instruction.
		// Preserve that complete instruction as the signature for reversible A/B.
		const Address render_worker_match = pattern::find_unique( materials, portal2::signature::render_worker );
		const Address render_worker_site  = render_worker_match.add( portal2::xref::render_worker_pointer );
		if( render_worker_site.at< std::uint8_t >( ) != 0xb8 ) {
			remove( );
			return false;
		}

		std::vector< std::uint8_t > render_worker_bytes( 5 );
		std::memcpy( render_worker_bytes.data( ), render_worker_site.as< const void* >( ), render_worker_bytes.size( ) );
		m_render_worker_original = render_worker_site.add( 1 ).at< RenderWorker_t >( );

		std::vector< std::uint8_t > hardware_sync_bytes( 5 );
		std::memcpy( hardware_sync_bytes.data( ), hardware_sync.as< const void* >( ), hardware_sync_bytes.size( ) );

		// Installation is transactional: any failed signature or hook rolls back
		// every hook already installed by this instance.
		if( !m_needs_reset.install( needs_reset.as< void* >( ), expected_call( needs_reset ), reinterpret_cast< void* >( &Reacquire ) ) ||
			!m_other_app.install( other_app.as< void* >( ), expected_call( other_app ), reinterpret_cast< void* >( &Reacquire ) ) ||
			!m_lighting_restore.install( lighting_site.as< void* >( ),
				lighting_call,
				reinterpret_cast< void* >( &StaticLightingRestore ) ) ||
			!m_lighting_cache_restore.install( lighting_cache_site.as< void* >( ),
				lighting_cache_call,
				reinterpret_cast< void* >( &StaticLightingCacheRestore ) ) ||
			!install_slot( m_shader_device, shader_device, portal2::slot::shader_device, reinterpret_cast< void* >( &ShaderDevice ) ) ||
			!install_slot( m_frame_sync, frame_sync, portal2::slot::frame_sync, reinterpret_cast< void* >( &FrameSync ) ) ||
			!install_slot( m_resource_callbacks,
				resource_callbacks,
				portal2::slot::resource_callbacks,
				reinterpret_cast< void* >( &ResourceCallbacks ) ) ||
			!m_hardware_sync.install( hardware_sync.as< void* >( ), hardware_sync_bytes, reinterpret_cast< void* >( &HardwareSync ) ) ||
			!m_present.install( present_vtable + portal2::slot::present, reinterpret_cast< void* >( &Present ) ) || !draw_hooks ||
			!dynamic_light_hooks ||
			!m_render_worker.install( render_worker_site.as< void* >( ),
				render_worker_bytes,
				1,
				reinterpret_cast< void* >( &RenderWorker ) ) ) {
			remove( );
			return false;
		}
		return true;
	}

	void RendererTrace::remove( ) {
		// disable callbacks first, then unwind hooks in reverse installation order.
		m_removing.store( true, std::memory_order_release );
		const bool worker_installed = m_render_worker.installed( );
		m_render_worker.remove( );
		// The material render queue runs on a worker thread. Its call target can
		// already have been fetched when the immediate is restored, so require a
		// short zero-callback grace period before this DLL can be unloaded.
		unsigned int quiet_milliseconds = worker_installed ? 0 : 50;
		while( quiet_milliseconds < 50 ) {
			if( m_active_render_workers.load( std::memory_order_acquire ) == 0 ) {
				++quiet_milliseconds;
			} else {
				quiet_milliseconds = 0;
			}
			Sleep( 1 );
		}
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		m_dynamic_light_execute_signed.remove( );
		m_dynamic_light_execute_unsigned.remove( );
		m_draw_indexed_primitive_up.remove( );
		m_draw_primitive_up.remove( );
		m_draw_indexed_primitive.remove( );
		m_draw_primitive.remove( );
		m_set_pixel_shader.remove( );
		m_set_vertex_shader.remove( );
		m_set_texture.remove( );
		m_queued_functor_execute		 = nullptr;
		m_queued_functor_leaf			 = nullptr;
		m_dynamic_light_execute_original = nullptr;
#endif
		m_present.remove( );
		m_hardware_sync.remove( );
		m_resource_callbacks.remove( );
		m_frame_sync.remove( );
		m_shader_device.remove( );
		m_lighting_cache_restore.remove( );
		m_lighting_restore.remove( );
		m_other_app.remove( );
		m_needs_reset.remove( );
		m_metrics				  = nullptr;
		m_original				  = nullptr;
		m_lighting_original		  = nullptr;
		m_lighting_cache_original = nullptr;
		m_render_worker_original  = nullptr;
		m_reacquire_depth.store( 0 );
		m_active_render_workers.store( 0, std::memory_order_release );
		m_material_system = nullptr;
		m_material_lock	  = nullptr;
		m_material_unlock = nullptr;
		m_fix_enabled.store( false );
		m_lighting_skips.store( 0 );
		if( m_instance == this )
			m_instance = nullptr;
	}

	bool RendererTrace::installed( ) const noexcept {
		return m_needs_reset.installed( ) && m_other_app.installed( );
	}

	void RendererTrace::set_fix_enabled( bool enabled ) noexcept {
		m_fix_enabled.store( enabled, std::memory_order_relaxed );
	}

	bool RendererTrace::focus_sync_available( ) const noexcept {
		return m_material_lock && m_material_unlock;
	}

	bool RendererTrace::static_lighting_fix_available( ) const noexcept {
		return m_lighting_restore.installed( ) && m_lighting_cache_restore.installed( );
	}

	unsigned int RendererTrace::static_lighting_skips( ) const noexcept {
		return m_lighting_skips.load( std::memory_order_relaxed );
	}

	void __fastcall RendererTrace::RenderWorker( void* self, void*, void* queued_context ) {
		// wrap one complete queued-material execution with low-overhead counters.
		auto* instance = m_instance;
		if( !instance )
			return;
		instance->m_active_render_workers.fetch_add( 1, std::memory_order_acq_rel );
		struct ActiveWorkerGuard {
			RendererTrace* instance;
			~ActiveWorkerGuard( ) {
				instance->m_active_render_workers.fetch_sub( 1, std::memory_order_acq_rel );
			}
		} guard{ instance };
		auto previous = instance->m_render_worker_original;
		if( !previous )
			return;
		const bool tracing = !instance->m_removing.load( std::memory_order_acquire ) && instance->m_metrics &&
							 instance->m_metrics->tracing( ) && queued_context;
		if( !tracing ) {
			previous( self, queued_context );
			return;
		}

		auto* context  = static_cast< std::uint8_t* >( queued_context );
		g_worker_trace = { };
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		renderer_research::reset_worker_state( );
#endif
		unsigned int queued_calls = 0;
		for( auto* node = *reinterpret_cast< std::uint8_t** >( context + portal2::queued_context::command_head );
			node && queued_calls < 1000000;
			node = *reinterpret_cast< std::uint8_t** >( node ) ) {
			++queued_calls;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
			renderer_research::count_queued_command( g_worker_trace,
				node,
				instance->m_queued_functor_execute,
				instance->m_queued_functor_leaf );
#endif
		}
		const auto byte_delta = [ context ]( size_t current, size_t start ) {
			const int value = *reinterpret_cast< int* >( context + current ) - *reinterpret_cast< int* >( context + start );
			return value > 0 ? static_cast< unsigned int >( value ) : 0u;
		};
		const unsigned int parameter_bytes =
			byte_delta( portal2::queued_context::parameter_current, portal2::queued_context::parameter_start );
		const unsigned int vertex_bytes = byte_delta( portal2::queued_context::vertex_current, portal2::queued_context::vertex_start );
		const unsigned int index_bytes	= byte_delta( portal2::queued_context::index_current, portal2::queued_context::index_start );
		unsigned long long begin_cycles = 0;
		unsigned long long end_cycles	= 0;
		const Qpc::tick_t  begin		= Qpc::now( );
		QueryThreadCycleTime( GetCurrentThread( ), &begin_cycles );
		g_worker_trace.m_active = true;
		previous( self, queued_context );
		g_worker_trace.m_active = false;
		QueryThreadCycleTime( GetCurrentThread( ), &end_cycles );
		const Qpc::tick_t end			 = Qpc::now( );
		g_worker_trace.m_begin_qpc		 = begin;
		g_worker_trace.m_end_qpc		 = end;
		g_worker_trace.m_cycles			 = end_cycles - begin_cycles;
		g_worker_trace.m_queued_calls	 = queued_calls;
		g_worker_trace.m_parameter_bytes = parameter_bytes;
		g_worker_trace.m_vertex_bytes	 = vertex_bytes;
		g_worker_trace.m_index_bytes	 = index_bytes;
		instance->m_metrics->record_render_worker( g_worker_trace );
	}

	void __fastcall RendererTrace::HardwareSync( void* self, void* ) {
		// attribute GPU synchronization only while a traced render worker is active.
		auto* instance = m_instance;
		if( !instance )
			return;
		auto previous = reinterpret_cast< void( __thiscall* )( void* ) >( instance->m_hardware_sync.previous( ) );
		if( !previous )
			return;
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			previous( self );
			return;
		}
		auto*			   bytes		  = static_cast< std::uint8_t* >( self );
		const int		   index_before	  = *reinterpret_cast< int* >( bytes + portal2::hardware_sync::index );
		const unsigned int pending_before = static_cast< unsigned int >( *( bytes + portal2::hardware_sync::pending_a ) != 0 ) |
											( static_cast< unsigned int >( *( bytes + portal2::hardware_sync::pending_b ) != 0 ) << 1 );
		unsigned long long begin_cycles = 0;
		unsigned long long end_cycles	= 0;
		const Qpc::tick_t  begin		= Qpc::now( );
		QueryThreadCycleTime( GetCurrentThread( ), &begin_cycles );
		previous( self );
		QueryThreadCycleTime( GetCurrentThread( ), &end_cycles );
		const Qpc::tick_t elapsed = Qpc::now( ) - begin;
		++g_worker_trace.m_hardware_sync_count;
		g_worker_trace.m_hardware_sync_qpc += elapsed;
		g_worker_trace.m_hardware_sync_cycles += end_cycles - begin_cycles;
		if( elapsed > g_worker_trace.m_max_hardware_sync_qpc ) {
			g_worker_trace.m_max_hardware_sync_qpc = elapsed;
			g_worker_trace.m_sync_index_before	   = index_before;
			g_worker_trace.m_sync_pending_before   = pending_before;
			g_worker_trace.m_sync_pending_after =
				static_cast< unsigned int >( *( bytes + portal2::hardware_sync::pending_a ) != 0 ) |
				( static_cast< unsigned int >( *( bytes + portal2::hardware_sync::pending_b ) != 0 ) << 1 );
		}
	}

	void __fastcall RendererTrace::Present( void* self, void* ) {
		// separate presentation time from other work performed by the queue.
		auto* instance = m_instance;
		if( !instance )
			return;
		auto previous = reinterpret_cast< void( __thiscall* )( void* ) >( instance->m_present.previous( ) );
		if( !previous )
			return;
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			previous( self );
			return;
		}
		const Qpc::tick_t begin = Qpc::now( );
		previous( self );
		const Qpc::tick_t elapsed = Qpc::now( ) - begin;
		++g_worker_trace.m_present_count;
		g_worker_trace.m_present_qpc += elapsed;
		if( elapsed > g_worker_trace.m_max_present_qpc )
			g_worker_trace.m_max_present_qpc = elapsed;
	}

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
	void __fastcall RendererTrace::DynamicLightExecute( void* self, void* ) {
		// capture identity and cost before chaining to the original command.
		auto* instance = m_instance;
		auto  previous = instance ? instance->m_dynamic_light_execute_original : nullptr;
		if( !previous )
			return;
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			previous( self );
			return;
		}
		renderer_research::capture_dynamic_light( g_worker_trace, self );
		const Qpc::tick_t begin = Qpc::now( );
		previous( self );
		const Qpc::tick_t end = Qpc::now( );
		++g_worker_trace.m_dynamic_light_count;
		g_worker_trace.m_dynamic_light_qpc += end - begin;
	}

	// mirror texture bindings without retaining COM references.
	HRESULT __stdcall RendererTrace::SetTexture( IDirect3DDevice9* self, DWORD stage, IDirect3DBaseTexture9* texture ) {
		auto* instance = m_instance;
		auto  previous = instance ? reinterpret_cast< decltype( &SetTexture ) >( instance->m_set_texture.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const HRESULT result = previous( self, stage, texture );
		if( SUCCEEDED( result ) )
			renderer_research::set_texture( stage, texture );
		return result;
	}

	HRESULT __stdcall RendererTrace::SetVertexShader( IDirect3DDevice9* self, IDirect3DVertexShader9* shader ) {
		// mirror the currently bound vertex shader for draw attribution.
		auto* instance = m_instance;
		auto  previous = instance ? reinterpret_cast< decltype( &SetVertexShader ) >( instance->m_set_vertex_shader.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const HRESULT result = previous( self, shader );
		if( SUCCEEDED( result ) )
			renderer_research::set_vertex_shader( shader );
		return result;
	}

	HRESULT __stdcall RendererTrace::SetPixelShader( IDirect3DDevice9* self, IDirect3DPixelShader9* shader ) {
		// mirror the currently bound pixel shader for draw attribution.
		auto* instance = m_instance;
		auto  previous = instance ? reinterpret_cast< decltype( &SetPixelShader ) >( instance->m_set_pixel_shader.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const HRESULT result = previous( self, shader );
		if( SUCCEEDED( result ) )
			renderer_research::set_pixel_shader( shader );
		return result;
	}

	// time the real D3D9 call and retain state only for the slowest draw.
	HRESULT __stdcall RendererTrace::DrawPrimitive(
		IDirect3DDevice9* self, D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count ) {
		auto* instance = m_instance;
		auto  previous = instance ? reinterpret_cast< decltype( &DrawPrimitive ) >( instance->m_draw_primitive.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const auto pipeline = renderer_research::observe_pipeline( );
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return previous( self, primitive_type, start_vertex, primitive_count );
		const Qpc::tick_t begin	 = Qpc::now( );
		const HRESULT	  result = previous( self, primitive_type, start_vertex, primitive_count );
		renderer_research::record_draw( g_worker_trace,
			begin,
			portal2::d3d9::draw_primitive,
			primitive_type,
			primitive_count,
			0,
			pipeline );
		return result;
	}

	// indexed draws retain their conservative vertex span for comparison.
	HRESULT __stdcall RendererTrace::DrawIndexedPrimitive( IDirect3DDevice9* self,
		D3DPRIMITIVETYPE													 primitive_type,
		INT																	 base_vertex_index,
		UINT																 min_vertex_index,
		UINT																 vertex_count,
		UINT																 start_index,
		UINT																 primitive_count ) {
		auto* instance = m_instance;
		auto  previous =
			 instance ? reinterpret_cast< decltype( &DrawIndexedPrimitive ) >( instance->m_draw_indexed_primitive.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const auto pipeline = renderer_research::observe_pipeline( );
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return previous( self, primitive_type, base_vertex_index, min_vertex_index, vertex_count, start_index, primitive_count );
		const Qpc::tick_t begin = Qpc::now( );
		const HRESULT	  result =
			previous( self, primitive_type, base_vertex_index, min_vertex_index, vertex_count, start_index, primitive_count );
		renderer_research::record_draw( g_worker_trace,
			begin,
			portal2::d3d9::draw_indexed_primitive,
			primitive_type,
			primitive_count,
			vertex_count,
			pipeline );
		return result;
	}

	// user-memory draws are separate because the driver copies their data.
	HRESULT __stdcall RendererTrace::DrawPrimitiveUp(
		IDirect3DDevice9* self, D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void* vertex_data, UINT vertex_stride ) {
		auto* instance = m_instance;
		auto  previous = instance ? reinterpret_cast< decltype( &DrawPrimitiveUp ) >( instance->m_draw_primitive_up.previous( ) ) : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const auto pipeline = renderer_research::observe_pipeline( );
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return previous( self, primitive_type, primitive_count, vertex_data, vertex_stride );
		const Qpc::tick_t begin	 = Qpc::now( );
		const HRESULT	  result = previous( self, primitive_type, primitive_count, vertex_data, vertex_stride );
		renderer_research::record_draw( g_worker_trace,
			begin,
			portal2::d3d9::draw_primitive_up,
			primitive_type,
			primitive_count,
			0,
			pipeline );
		return result;
	}

	// capture the indexed UP variant before forwarding every argument.
	HRESULT __stdcall RendererTrace::DrawIndexedPrimitiveUp( IDirect3DDevice9* self,
		D3DPRIMITIVETYPE													   primitive_type,
		UINT																   min_vertex_index,
		UINT																   vertex_count,
		UINT																   primitive_count,
		const void*															   index_data,
		D3DFORMAT															   index_format,
		const void*															   vertex_data,
		UINT																   vertex_stride ) {
		auto* instance = m_instance;
		auto  previous = instance
							 ? reinterpret_cast< decltype( &DrawIndexedPrimitiveUp ) >( instance->m_draw_indexed_primitive_up.previous( ) )
							 : nullptr;
		if( !previous )
			return D3DERR_INVALIDCALL;
		const auto pipeline = renderer_research::observe_pipeline( );
		if( !g_worker_trace.m_active || !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return previous( self,
				primitive_type,
				min_vertex_index,
				vertex_count,
				primitive_count,
				index_data,
				index_format,
				vertex_data,
				vertex_stride );
		const Qpc::tick_t begin	 = Qpc::now( );
		const HRESULT	  result = previous( self,
			primitive_type,
			min_vertex_index,
			vertex_count,
			primitive_count,
			index_data,
			index_format,
			vertex_data,
			vertex_stride );
		renderer_research::record_draw( g_worker_trace,
			begin,
			portal2::d3d9::draw_indexed_primitive_up,
			primitive_type,
			primitive_count,
			vertex_count,
			pipeline );
		return result;
	}
#endif

	// serialize focus recovery with material-system access when available.
	void __fastcall RendererTrace::Reacquire( void* self, void*, unsigned char release, unsigned char restore, const char* reason ) {
		RendererTrace* instance = m_instance;
		if( !instance || !instance->m_original )
			return;
		void* material_lock = nullptr;
		if( instance->m_fix_enabled.load( ) && instance->m_material_system && instance->m_material_lock && instance->m_material_unlock ) {
			material_lock = instance->m_material_lock( instance->m_material_system );
		}
		const bool		  tracing	= instance->m_metrics && instance->m_metrics->tracing( );
		const Qpc::tick_t begin_qpc = tracing ? Qpc::now( ) : 0;
		instance->m_reacquire_depth.fetch_add( 1 );
		if( !tracing ) {
			instance->m_original( self, release, restore, reason );
			instance->m_reacquire_depth.fetch_sub( 1 );
			if( material_lock )
				instance->m_material_unlock( instance->m_material_system, material_lock );
			return;
		}
		instance->m_original( self, release, restore, reason );
		const Qpc::tick_t end_qpc = Qpc::now( );
		instance->m_reacquire_depth.fetch_sub( 1 );
		instance->m_metrics->record_reacquire( Qpc::milliseconds( end_qpc - begin_qpc ), reason, begin_qpc, end_qpc );
		if( material_lock )
			instance->m_material_unlock( instance->m_material_system, material_lock );
	}

	void __cdecl RendererTrace::StaticLightingRestore( ) {
		// skip only the redundant nested restore proven to run during reacquire.
		auto* instance = m_instance;
		if( !instance || !instance->m_lighting_original )
			return;
		if( instance->m_fix_enabled.load( ) && instance->m_reacquire_depth.load( ) != 0 ) {
			instance->m_lighting_skips.fetch_add( 1 );
			return;
		}
		instance->m_lighting_original( );
	}

	void __cdecl RendererTrace::StaticLightingCacheRestore( ) {
		// apply the same guard to the companion static-lighting cache rebuild.
		auto* instance = m_instance;
		if( !instance || !instance->m_lighting_cache_original )
			return;
		if( instance->m_fix_enabled.load( ) && instance->m_reacquire_depth.load( ) != 0 ) {
			instance->m_lighting_skips.fetch_add( 1 );
			return;
		}
		instance->m_lighting_cache_original( );
	}

	void __fastcall RendererTrace::ShaderDevice( void* self, void* ) {
		// record the first major device-recovery stage around its original slot.
		auto* instance = m_instance;
		if( !instance )
			return;
		auto			previous = reinterpret_cast< NoArgs_t >( instance->m_shader_device.previous( ) );
		const bool		timed	 = instance->m_reacquire_depth.load( ) != 0 && instance->m_metrics && instance->m_metrics->tracing( );
		const Stopwatch timer{ timed };
		previous( self );
		if( timed )
			instance->m_metrics->record_reacquire_stage( 0, timer.elapsed_ms( ) );
	}

	void __fastcall RendererTrace::FrameSync( void* self, void* ) {
		// record frame synchronization without changing engine behavior.
		auto* instance = m_instance;
		if( !instance )
			return;
		auto			previous = reinterpret_cast< NoArgs_t >( instance->m_frame_sync.previous( ) );
		const bool		timed	 = instance->m_reacquire_depth.load( ) != 0 && instance->m_metrics && instance->m_metrics->tracing( );
		const Stopwatch timer{ timed };
		previous( self );
		if( timed )
			instance->m_metrics->record_reacquire_stage( 1, timer.elapsed_ms( ) );
	}

	void __fastcall RendererTrace::ResourceCallbacks( void* self, void*, void* callback, int context ) {
		// record resource callbacks as the final focus-recovery stage.
		auto* instance = m_instance;
		if( !instance )
			return;
		auto			previous = reinterpret_cast< ResourceCallbacks_t >( instance->m_resource_callbacks.previous( ) );
		const bool		timed	 = instance->m_reacquire_depth.load( ) != 0 && instance->m_metrics && instance->m_metrics->tracing( );
		const Stopwatch timer{ timed };
		previous( self, callback, context );
		if( timed )
			instance->m_metrics->record_reacquire_stage( 2, timer.elapsed_ms( ) );
	}
}
