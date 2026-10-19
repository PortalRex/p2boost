#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// IMaterial exposes only immutable classification data used at bind time.
	class IMaterial final : public Interface {
	public:
		static constexpr std::size_t get_texture_group_name_slot{ 1 };
		static constexpr std::size_t get_shader_name_slot{ 49 };

		const char* GetTextureGroupName( ) {
			using function_t = const char*(__thiscall*)( void* );
			return invoke< get_texture_group_name_slot, function_t >( );
		}

		const char* GetShaderName( ) {
			using function_t = const char*(__thiscall*)( void* );
			return invoke< get_shader_name_slot, function_t >( );
		}
	};

	// ICallQueue runs a CFunctor in render-thread order. the queue AddRefs the
	// functor (slot 0), Releases the caller's reference (slot 1) before
	// returning, and later invokes it through functor slot 3.
	class ICallQueue final : public Interface {
	public:
		static constexpr std::size_t queue_functor_slot{ 0 };

		void QueueFunctor( void* functor ) {
			using function_t = void( __thiscall* )( void*, void* );
			invoke< queue_functor_slot, function_t >( functor );
		}
	};

	// IMatRenderContext owns material binding and a temporary reference count.
	class IMatRenderContext final : public Interface {
	public:
		static constexpr std::size_t release_slot{ 1 };
		static constexpr std::size_t bind_slot{ 9 };
		// CMatQueuedRenderContext returns &m_CallQueueExternal (lea eax,[ecx+X];
		// ret); the immediate context returns null (xor eax,eax; ret).
		static constexpr std::size_t get_call_queue_slot{ 142 };

		int Release( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< release_slot, function_t >( );
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
		// FindMaterial( name, group, complain, filter ), FindTexture( name, group,
		// complain, creation flags ) and CreateNamedRenderTargetTextureEx2( name,
		// width, height, size mode, format, depth mode, texture flags, rt flags ).
		static constexpr std::size_t find_material_slot{ 82 };
		static constexpr std::size_t find_texture_slot{ 89 };
		static constexpr std::size_t create_named_render_target_slot{ 97 };

		void* FindMaterial( const char* name, const char* group, bool complain, const char* filter ) {
			using function_t = void*( __thiscall* )( void*, const char*, const char*, bool, const char* );
			return invoke< find_material_slot, function_t >( name, group, complain, filter );
		}

		void* CreateNamedRenderTargetTextureEx2( const char* name, int width, int height, int size_mode, int format, int depth, unsigned int texture_flags, unsigned int target_flags ) {
			using function_t = void*( __thiscall* )( void*, const char*, int, int, int, int, int, unsigned int, unsigned int );
			return invoke< create_named_render_target_slot, function_t >( name, width, height, size_mode, format, depth, texture_flags, target_flags );
		}

		Address FindMaterialAddress( ) const noexcept {
			return method( find_material_slot );
		}

		Address CreateNamedRenderTargetAddress( ) const noexcept {
			return method( create_named_render_target_slot );
		}

		void* FindTexture( const char* name, const char* group, bool complain, int flags ) {
			using function_t = void*( __thiscall* )( void*, const char*, const char*, bool, int );
			return invoke< find_texture_slot, function_t >( name, group, complain, flags );
		}

		Address FindTextureAddress( ) const noexcept {
			return method( find_texture_slot );
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

}
