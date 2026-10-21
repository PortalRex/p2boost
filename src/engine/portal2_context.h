#pragma once

#include "interfaces.h"
#include "module.h"
#include "sdk.h"

#include <d3d9.h>

namespace p2boost {

	// the D3D9 implementation the game's shader API was started with: the
	// Vulkan shader API over DXVK (-vulkan), or the original D3D9 shader API
	// over the system's d3d9.dll. both expose the same IDirect3DDevice9 ABI.
	enum class render_backend_t { none, dxvk_vulkan, native_d3d9 };

	const char* render_backend_name( render_backend_t backend ) noexcept;

	// one snapshot of the Portal 2 images used by production code.
	// every Module is borrowed from the Windows loader and owns no lifetime.
	struct portal2_modules_t {
		void refresh( ) noexcept;

		// which shader API the loader mapped; identity checks live in
		// verify_runtime.
		render_backend_t backend( ) const noexcept;

		Module m_engine;
		Module m_input_system;
		Module m_client;
		Module m_material_system;
		Module m_surface;
		Module m_vgui;
		Module m_shader_api;
		Module m_dxvk_d3d9;
		Module m_vulkan;
		Module m_d3d9;
		Module m_vstdlib;
		Module m_tier0;
		Module m_plugin;
	};

	// acquire every named Source interface once during plugin startup.
	// features receive these non-owning pointers and retain their own hooks.
	class Portal2Interfaces {
	public:
		void connect( sdk::create_interface_fn_t engine_factory, const portal2_modules_t& modules ) noexcept;
		void disconnect( ) noexcept;

		sdk::ICvar* cvar( ) const noexcept {
			return m_cvar;
		}

		sdk::IVEngineClient* engine_client( ) const noexcept {
			return m_engine_client;
		}

		sdk::IVModelRender* model_render( ) const noexcept {
			return m_model_render;
		}

		sdk::IEngineVGui* engine_vgui( ) const noexcept {
			return m_engine_vgui;
		}

		sdk::IBaseClientDLL* base_client( ) const noexcept {
			return m_base_client;
		}

		sdk::IMaterialSystem* material_system( ) const noexcept {
			return m_material_system;
		}

		sdk::ISurface* surface( ) const noexcept {
			return m_surface;
		}

		sdk::ISchemeManager* scheme_manager( ) const noexcept {
			return m_scheme_manager;
		}

	private:
		sdk::ICvar*           m_cvar{ };
		sdk::IVEngineClient*  m_engine_client{ };
		sdk::IVModelRender*   m_model_render{ };
		sdk::IEngineVGui*     m_engine_vgui{ };
		sdk::IBaseClientDLL*  m_base_client{ };
		sdk::IMaterialSystem* m_material_system{ };
		sdk::ISurface*        m_surface{ };
		sdk::ISchemeManager*  m_scheme_manager{ };
	};

	// resolve the one live D3D9 device stored by Portal 2's shader API. the
	// Vulkan and D3D9 shader APIs are the same build and share the signature.
	IDirect3DDevice9* find_device( const portal2_modules_t& modules ) noexcept;

}
