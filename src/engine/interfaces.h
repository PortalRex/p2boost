#pragma once

#include "portal2.h"

#include <cstddef>
#include <type_traits>

namespace p2boost::sdk {

	// base class for Source interfaces owned by the engine. it contains no data,
	// so a pointer returned by CreateInterface can be used as the typed wrapper.
	class Interface {
	public:
		// return the engine-owned VMT for validation or hook placement.
		__forceinline void** methods( ) noexcept {
			return *reinterpret_cast< void*** >( this );
		}

		__forceinline void* method( std::size_t index ) noexcept {
			return methods( )[ index ];
		}

	protected:
		// call one known Source ABI method without repeating casts at each use.
		template < std::size_t index, typename return_t, typename... args_t >
		__forceinline return_t call( args_t... args ) {
			using method_t = return_t( __thiscall* )( void*, args_t... );
			auto target	   = reinterpret_cast< method_t >( method( index ) );

			if constexpr( std::is_void_v< return_t > )
				target( this, args... );
			else
				return target( this, args... );
		}
	};

	// VEngineCvar007 methods used to own p2boost console commands.
	class Cvar final : public Interface {
	public:
		__forceinline void RegisterCommand( void* command ) {
			call< 9, void >( command );
		}

		__forceinline void UnregisterCommand( void* command ) {
			call< 10, void >( command );
		}

		__forceinline void* FindCommand( const char* name ) {
			return call< 13, void* >( name );
		}
	};

	// VEngineClient015 methods used to place provenance inside recorded demos.
	class EngineClient final : public Interface {
	public:
		__forceinline int GetMaxClients( ) {
			return call< portal2::vmt::engine_get_max_clients, int >( );
		}

		__forceinline bool IsRecordingDemo( ) {
			return call< portal2::vmt::engine_is_recording_demo, bool >( );
		}

		__forceinline void ExecuteClientCmd( const char* command ) {
			call< portal2::vmt::engine_execute_client_cmd, void >( command );
		}

		__forceinline int GetActiveSplitScreenPlayerSlot( ) {
			return call< portal2::vmt::engine_active_split_screen_slot, int >( );
		}
	};

	// VEngineModel016 owns model-instance transfers. A ragdoll snatches its
	// source entity's instance through ChangeInstance.
	class ModelRender final : public Interface {};

	// VEngineVGui001's private extension owns the final UI paint callback used
	// by SourceAutoRecord and the p2boost profiler.
	class EngineVGui final : public Interface {};

	class Surface final : public Interface {
	public:
		__forceinline int GetFontTall( unsigned long font ) {
			return call< portal2::vmt::surface_get_font_tall, int >( font );
		}
	};

	class Scheme final : public Interface {
	public:
		__forceinline unsigned long GetFont( const char* name, bool proportional ) {
			return call< portal2::vmt::scheme_get_font, unsigned long >( name, proportional );
		}
	};

	class SchemeManager final : public Interface {
	public:
		__forceinline Scheme* GetIScheme( unsigned long handle ) {
			return call< portal2::vmt::scheme_manager_get_scheme, Scheme* >( handle );
		}
	};

	// VEngineRenderView013 exposes the optional VGui_Paint renderer guard.
	class RenderView final : public Interface {};

	// VClient016 owns client entity updates on both the listen-server host and
	// a remote Orange client. FrameStageNotify is hooked by verified VMT index.
	class BaseClient final : public Interface {};

	// IShadowMgr exposes the immutable state associated with an engine shadow
	// handle. The optimizer reads only the priority bit already consumed by
	// Portal 2's active-depth-list implementation.
	class ShadowManager final : public Interface {
	public:
		__forceinline void RemoveAllShadowsFromModel( unsigned short handle ) {
			call< portal2::vmt::shadow_remove_all_from_model, void >( handle );
		}

		__forceinline const void* GetFlashlightState( unsigned short handle ) {
			return call< portal2::vmt::shadow_get_flashlight_state, const void* >( handle );
		}
	};

	class Material final : public Interface {
	public:
		// expose only the material metadata needed by the classifier.
		__forceinline const char* GetTextureGroup( ) {
			return call< portal2::vmt::material_texture_group, const char* >( );
		}

		__forceinline const char* GetShaderName( ) {
			return call< portal2::vmt::material_shader_name, const char* >( );
		}
	};

	class RenderContext final : public Interface {
	public:
		// release the temporary reference returned by GetRenderContext.
		__forceinline int Release( ) {
			return call< 1, int >( );
		}
	};

	// VMaterialSystem080 methods needed by the optional flat-texture mode.
	class MaterialSystem final : public Interface {
	public:
		__forceinline void ReloadTextures( ) {
			call< portal2::vmt::reload_textures, void >( );
		}

		__forceinline void ReloadMaterials( const char* filter = nullptr ) {
			call< portal2::vmt::reload_materials, void >( filter );
		}

		__forceinline RenderContext* GetRenderContext( ) {
			return call< portal2::vmt::get_render_context, RenderContext* >( );
		}
	};

}
