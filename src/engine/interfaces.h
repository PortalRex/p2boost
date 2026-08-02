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

	// VDebugOverlay004 methods used by the lightweight profiler panel.
	class DebugOverlay final : public Interface {
	public:
		__forceinline void AddScreenText( float x, float y, float lifetime, int red, int green, int blue, int alpha, const char* text ) {
			call< 7, void >( x, y, lifetime, red, green, blue, alpha, text );
		}
	};

	class Material final : public Interface {
	public:
		// expose only the material metadata needed by the classifier.
		__forceinline const char* GetName( ) {
			return call< portal2::vmt::material_name, const char* >( );
		}

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
