#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// IMaterial: classification data read at bind time, and its reference count.
	class IMaterial final : public Interface {
	public:
		static constexpr std::size_t get_texture_group_name_slot{ 1 };
		static constexpr std::size_t increment_reference_count_slot{ 12 };
		static constexpr std::size_t get_shader_name_slot{ 49 };

		const char* GetTextureGroupName( ) {
			using function_t = const char*(__thiscall*)( void* );
			return invoke< get_texture_group_name_slot, function_t >( );
		}

		const char* GetShaderName( ) {
			using function_t = const char*(__thiscall*)( void* );
			return invoke< get_shader_name_slot, function_t >( );
		}

		void IncrementReferenceCount( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< increment_reference_count_slot, function_t >( );
		}
	};

	// ITexture: size, reference count, and creating its GPU surface.
	class ITexture final : public Interface {
	public:
		static constexpr std::size_t get_actual_width_slot{ 3 };
		static constexpr std::size_t get_actual_height_slot{ 4 };
		static constexpr std::size_t increment_reference_count_slot{ 10 };
		static constexpr std::size_t download_slot{ 13 };

		int GetActualWidth( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< get_actual_width_slot, function_t >( );
		}

		int GetActualHeight( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< get_actual_height_slot, function_t >( );
		}

		void IncrementReferenceCount( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< increment_reference_count_slot, function_t >( );
		}

		// Download( rect, additional creation flags ): create or refresh the GPU surface.
		void Download( ) {
			using function_t = void( __thiscall* )( void*, void*, int );
			invoke< download_slot, function_t >( nullptr, 0 );
		}
	};

	// ICallQueue runs a CFunctor (engine/functor.h) in render-thread order.
	class ICallQueue final : public Interface {
	public:
		static constexpr std::size_t queue_functor_slot{ 0 };

		void QueueFunctor( void* functor ) {
			using function_t = void( __thiscall* )( void*, void* );
			invoke< queue_functor_slot, function_t >( functor );
		}
	};

	// IMatRenderContext: material binding, render targets, clears, and a
	// temporary reference count. Hold one through RenderContextRef.
	class IMatRenderContext final : public Interface {
	public:
		static constexpr std::size_t release_slot{ 1 };
		static constexpr std::size_t begin_render_slot{ 2 };
		static constexpr std::size_t end_render_slot{ 3 };
		static constexpr std::size_t bind_slot{ 9 };
		static constexpr std::size_t clear_buffers_slot{ 12 };
		static constexpr std::size_t clear_color3_slot{ 73 };
		static constexpr std::size_t clear_color4_slot{ 74 };
		static constexpr std::size_t override_depth_enable_slot{ 75 };
		static constexpr std::size_t push_render_target_slot{ 108 };
		static constexpr std::size_t pop_render_target_slot{ 111 };
		// CMatQueuedRenderContext returns &m_CallQueueExternal (lea eax,[ecx+X];
		// ret); the immediate context returns null (xor eax,eax; ret).
		static constexpr std::size_t get_call_queue_slot{ 142 };

		int Release( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< release_slot, function_t >( );
		}

		void BeginRender( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< begin_render_slot, function_t >( );
		}

		void EndRender( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< end_render_slot, function_t >( );
		}

		void ClearBuffers( bool color, bool depth, bool stencil ) {
			using function_t = void( __thiscall* )( void*, bool, bool, bool );
			invoke< clear_buffers_slot, function_t >( color, depth, stencil );
		}

		void ClearColor3ub( unsigned char red, unsigned char green, unsigned char blue ) {
			using function_t = void( __thiscall* )( void*, unsigned char, unsigned char, unsigned char );
			invoke< clear_color3_slot, function_t >( red, green, blue );
		}

		void ClearColor4ub( unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha ) {
			using function_t = void( __thiscall* )( void*, unsigned char, unsigned char, unsigned char, unsigned char );
			invoke< clear_color4_slot, function_t >( red, green, blue, alpha );
		}

		void OverrideDepthEnable( bool enable, bool depth_enable, bool depth_test ) {
			using function_t = void( __thiscall* )( void*, bool, bool, bool );
			invoke< override_depth_enable_slot, function_t >( enable, depth_enable, depth_test );
		}

		void PushRenderTargetAndViewport( ITexture* texture, int x, int y, int width, int height ) {
			using function_t = void( __thiscall* )( void*, void*, int, int, int, int );
			invoke< push_render_target_slot, function_t >( texture, x, y, width, height );
		}

		void PopRenderTargetAndViewport( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< pop_render_target_slot, function_t >( );
		}

		ICallQueue* GetCallQueue( ) {
			using function_t = ICallQueue*(__thiscall*)( void* );
			return invoke< get_call_queue_slot, function_t >( );
		}

		Address GetCallQueueAddress( ) const noexcept {
			return method( get_call_queue_slot );
		}

		Address BindAddress( ) const noexcept {
			return method( bind_slot );
		}

		void** BindSlot( ) noexcept {
			return method_slot( bind_slot );
		}
	};

	// VMaterialSystem080 owns material reloads and render contexts.
	class IMaterialSystem final : public Interface {
	public:
		static constexpr const char* version{ "VMaterialSystem080" };
		static constexpr std::size_t reload_textures_slot{ 79 };
		static constexpr std::size_t reload_materials_slot{ 80 };
		static constexpr std::size_t get_render_context_slot{ 113 };
		// FindMaterial( name, group, complain, filter ) and
		// CreateNamedRenderTargetTextureEx2( name, width, height, size mode,
		// format, depth mode, texture flags, rt flags ).
		static constexpr std::size_t find_material_slot{ 82 };
		static constexpr std::size_t create_named_render_target_slot{ 97 };

		IMaterial* FindMaterial( const char* name, const char* group, bool complain, const char* filter ) {
			using function_t = IMaterial*(__thiscall*)( void*, const char*, const char*, bool, const char* );
			return invoke< find_material_slot, function_t >( name, group, complain, filter );
		}

		ITexture* CreateNamedRenderTargetTextureEx2( const char* name, int width, int height, int size_mode, int format, int depth, unsigned int texture_flags, unsigned int target_flags ) {
			using function_t = ITexture*(__thiscall*)( void*, const char*, int, int, int, int, int, unsigned int, unsigned int );
			return invoke< create_named_render_target_slot, function_t >( name, width, height, size_mode, format, depth, texture_flags, target_flags );
		}

		Address FindMaterialAddress( ) const noexcept {
			return method( find_material_slot );
		}

		Address CreateNamedRenderTargetAddress( ) const noexcept {
			return method( create_named_render_target_slot );
		}

		void ReloadTextures( ) {
			using function_t = void( __thiscall* )( void* );
			invoke< reload_textures_slot, function_t >( );
		}

		void ReloadMaterials( const char* filter = nullptr ) {
			using function_t = void( __thiscall* )( void*, const char* );
			invoke< reload_materials_slot, function_t >( filter );
		}

		IMatRenderContext* GetRenderContext( ) {
			using function_t = IMatRenderContext*(__thiscall*)( void* );
			return invoke< get_render_context_slot, function_t >( );
		}

		Address ReloadTexturesAddress( ) const noexcept {
			return method( reload_textures_slot );
		}

		Address ReloadMaterialsAddress( ) const noexcept {
			return method( reload_materials_slot );
		}

		Address GetRenderContextAddress( ) const noexcept {
			return method( get_render_context_slot );
		}
	};

	// the current render context for one scope, released on exit, as Source's
	// own CMatRenderContextPtr does. Null when the material system has none.
	class RenderContextRef {
	public:
		explicit RenderContextRef( IMaterialSystem* materials ) noexcept
		    : m_context{ materials ? materials->GetRenderContext( ) : nullptr } {
		}

		~RenderContextRef( ) {
			if( m_context )
				m_context->Release( );
		}

		RenderContextRef( const RenderContextRef& )            = delete;
		RenderContextRef& operator=( const RenderContextRef& ) = delete;

		explicit operator bool( ) const noexcept {
			return m_context != nullptr;
		}

		IMatRenderContext* operator->( ) const noexcept {
			return m_context;
		}

	private:
		IMatRenderContext* m_context;
	};

}
