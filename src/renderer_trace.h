#pragma once

#include "hook.h"

#include <atomic>

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
#include <d3d9.h>
#endif

namespace p2boost {

	class Metrics;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
	void __cdecl command_execute_hook( );
#endif

	// instruments renderer stalls and owns the reversible focus fixes.
	class RendererTrace {
	public:
		~RendererTrace( );

		// install or remove every verified renderer hook as one transaction.
		bool install( Metrics* metrics );
		void remove( );

		bool		 installed( ) const noexcept;
		void		 set_fix_enabled( bool enabled ) noexcept;
		bool		 focus_sync_available( ) const noexcept;
		bool		 static_lighting_fix_available( ) const noexcept;
		unsigned int static_lighting_skips( ) const noexcept;

	private:
		using Reacquire_t			  = void( __thiscall* )( void*, unsigned char, unsigned char, const char* );
		using NoArgs_t				  = void( __thiscall* )( void* );
		using ResourceCallbacks_t	  = void( __thiscall* )( void*, void*, int );
		using MaterialLock_t		  = void*(__thiscall*)( void* );
		using MaterialUnlock_t		  = void( __thiscall* )( void*, void* );
		using RenderWorker_t		  = void( __thiscall* )( void*, void* );
		using StaticLightingRestore_t = void( __cdecl* )( );

		// callbacks keep engine-style names to make xrefs easy to recognize.
		static void __fastcall Reacquire( void* self, void*, unsigned char release, unsigned char restore, const char* reason );
		static void __fastcall ShaderDevice( void* self, void* );
		static void __fastcall FrameSync( void* self, void* );
		static void __fastcall ResourceCallbacks( void* self, void*, void* callback, int context );
		static void __fastcall RenderWorker( void* self, void*, void* queued_context );
		static void __fastcall HardwareSync( void* self, void* );
		static void __fastcall Present( void* self, void* );
		static void __cdecl StaticLightingRestore( );
		static void __cdecl StaticLightingCacheRestore( );

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		using DynamicLightExecute_t	  = void( __thiscall* )( void* );
		using CreateOcclusionQuery_t  = unsigned int( __thiscall* )( void* );
		using DestroyOcclusionQuery_t = void( __thiscall* )( void*, unsigned int );
		using IndexBufferLock_t		  = unsigned int( __thiscall* )( void*, int, char, void* );
		using VertexBufferLock_t	  = unsigned int( __thiscall* )( void*, int, unsigned int, void* );

		static void __fastcall DynamicLightExecute( void*, void* );
		static unsigned int __fastcall CreateOcclusionQuery( void*, void* );
		static void __fastcall DestroyOcclusionQuery( void*, void*, unsigned int );
		static unsigned int __fastcall IndexBufferLock( void*, void*, int, char, void* );
		static unsigned int __fastcall VertexBufferLock( void*, void*, int, unsigned int, void* );
		static void __cdecl CommandExecuteDispatch( void*, void* );
		friend void __cdecl command_execute_hook( );
		static HRESULT __stdcall CreateTexture(
			IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE* );
		static HRESULT __stdcall CreateVertexShader( IDirect3DDevice9*, const DWORD*, IDirect3DVertexShader9** );
		static HRESULT __stdcall CreatePixelShader( IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9** );
		static HRESULT __stdcall NativePresent( IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA* );
		static HRESULT __stdcall UpdateSurface( IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT* );
		static HRESULT __stdcall UpdateTexture( IDirect3DDevice9*, IDirect3DBaseTexture9*, IDirect3DBaseTexture9* );
		static HRESULT __stdcall StretchRect(
			IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE );
		static HRESULT __stdcall SetRenderTarget( IDirect3DDevice9*, DWORD, IDirect3DSurface9* );
		static HRESULT __stdcall SetDepthStencilSurface( IDirect3DDevice9*, IDirect3DSurface9* );
		static HRESULT __stdcall BeginScene( IDirect3DDevice9* );
		static HRESULT __stdcall EndScene( IDirect3DDevice9* );
		static HRESULT __stdcall Clear( IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD );
		static HRESULT __stdcall SetRenderState( IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD );
		static HRESULT __stdcall SetTexture( IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9* );
		static HRESULT __stdcall SetTextureStageState( IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD );
		static HRESULT __stdcall SetSamplerState( IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD );
		static HRESULT __stdcall SetVertexDeclaration( IDirect3DDevice9*, IDirect3DVertexDeclaration9* );
		static HRESULT __stdcall SetFvf( IDirect3DDevice9*, DWORD );
		static HRESULT __stdcall SetVertexShader( IDirect3DDevice9*, IDirect3DVertexShader9* );
		static HRESULT __stdcall SetStreamSource( IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT );
		static HRESULT __stdcall SetIndices( IDirect3DDevice9*, IDirect3DIndexBuffer9* );
		static HRESULT __stdcall SetPixelShader( IDirect3DDevice9*, IDirect3DPixelShader9* );
		static HRESULT __stdcall DrawPrimitive( IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT );
		static HRESULT __stdcall DrawIndexedPrimitive( IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT );
		static HRESULT __stdcall DrawPrimitiveUp( IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT );
		static HRESULT __stdcall DrawIndexedPrimitiveUp(
			IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT );
#endif

		static RendererTrace* m_instance;

		CallSitePatch		  m_needs_reset;
		CallSitePatch		  m_other_app;
		CallSitePatch		  m_lighting_restore;
		CallSitePatch		  m_lighting_cache_restore;
		VtableSlotHook		  m_shader_device;
		VtableSlotHook		  m_frame_sync;
		VtableSlotHook		  m_resource_callbacks;
		InlineDetour		  m_hardware_sync;
		VtableSlotHook		  m_present;
		PointerImmediatePatch m_render_worker;

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		VtableSlotHook		  m_create_texture;
		VtableSlotHook		  m_create_vertex_shader;
		VtableSlotHook		  m_create_pixel_shader;
		VtableSlotHook		  m_native_present;
		VtableSlotHook		  m_update_surface;
		VtableSlotHook		  m_update_texture;
		VtableSlotHook		  m_stretch_rect;
		VtableSlotHook		  m_set_render_target;
		VtableSlotHook		  m_set_depth_stencil_surface;
		VtableSlotHook		  m_begin_scene;
		VtableSlotHook		  m_end_scene;
		VtableSlotHook		  m_clear;
		VtableSlotHook		  m_set_render_state;
		VtableSlotHook		  m_set_texture_stage_state;
		VtableSlotHook		  m_set_sampler_state;
		VtableSlotHook		  m_draw_primitive;
		VtableSlotHook		  m_draw_indexed_primitive;
		VtableSlotHook		  m_draw_primitive_up;
		VtableSlotHook		  m_draw_indexed_primitive_up;
		VtableSlotHook		  m_set_texture;
		VtableSlotHook		  m_set_vertex_declaration;
		VtableSlotHook		  m_set_fvf;
		VtableSlotHook		  m_set_vertex_shader;
		VtableSlotHook		  m_set_stream_source;
		VtableSlotHook		  m_set_indices;
		VtableSlotHook		  m_set_pixel_shader;
		VtableSlotHook		  m_dynamic_light_execute_unsigned;
		VtableSlotHook		  m_dynamic_light_execute_signed;
		VtableSlotHook		  m_create_occlusion_query;
		VtableSlotHook		  m_destroy_occlusion_query;
		std::atomic_uint	  m_pending_occlusion_query_create_count{ };
		std::atomic_llong	  m_pending_occlusion_query_create_qpc{ };
		std::atomic_llong	  m_pending_max_occlusion_query_create_qpc{ };
		std::atomic_uint	  m_pending_occlusion_query_destroy_count{ };
		std::atomic_llong	  m_pending_occlusion_query_destroy_qpc{ };
		std::atomic_llong	  m_pending_max_occlusion_query_destroy_qpc{ };
		std::atomic_uint	  m_pending_texture_create_count{ };
		std::atomic_llong	  m_pending_texture_create_qpc{ };
		std::atomic_llong	  m_pending_max_texture_create_qpc{ };
		std::atomic_uint	  m_pending_vertex_shader_create_count{ };
		std::atomic_llong	  m_pending_vertex_shader_create_qpc{ };
		std::atomic_llong	  m_pending_max_vertex_shader_create_qpc{ };
		std::atomic_uint	  m_pending_pixel_shader_create_count{ };
		std::atomic_llong	  m_pending_pixel_shader_create_qpc{ };
		std::atomic_llong	  m_pending_max_pixel_shader_create_qpc{ };
		InlineDetour		  m_index_buffer_lock;
		InlineDetour		  m_vertex_buffer_lock;
		CallSitePatch		  m_command_execute;
		void*				  m_queued_functor_execute{ };
		void*				  m_queued_functor_leaf{ };
		void*				  m_queued_functor_one_argument{ };
		DynamicLightExecute_t m_dynamic_light_execute_original{ };
#endif

		RenderWorker_t			m_render_worker_original{ };
		Reacquire_t				m_original{ };
		StaticLightingRestore_t m_lighting_original{ };
		StaticLightingRestore_t m_lighting_cache_original{ };
		Metrics*				m_metrics{ };
		void*					m_material_system{ };
		MaterialLock_t			m_material_lock{ };
		MaterialUnlock_t		m_material_unlock{ };
		std::atomic_uint		m_reacquire_depth{ };
		std::atomic_uint		m_active_render_workers{ };
		std::atomic_bool		m_removing{ };
		std::atomic_bool		m_fix_enabled{ };
		std::atomic_uint		m_lighting_skips{ };
	};

}
