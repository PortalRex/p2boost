#pragma once

#include "interfaces.h"
#include "module.h"
#include "sdk.h"

#include <d3d9.h>

namespace p2boost {

	// one snapshot of the Portal 2 images used by production code.
	// every Module is borrowed from the Windows loader and owns no lifetime.
	struct portal2_modules_t {
		void refresh( ) noexcept;

		Module m_engine;
		Module m_client;
		Module m_material_system;
		Module m_surface;
		Module m_vgui;
		Module m_shader_api;
		Module m_dxvk_d3d9;
		Module m_vulkan;
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
		sdk::ICvar* m_cvar{ };
		sdk::IVEngineClient* m_engine_client{ };
		sdk::IVModelRender* m_model_render{ };
		sdk::IEngineVGui* m_engine_vgui{ };
		sdk::IBaseClientDLL* m_base_client{ };
		sdk::IMaterialSystem* m_material_system{ };
		sdk::ISurface* m_surface{ };
		sdk::ISchemeManager* m_scheme_manager{ };
	};

	// resolve the one live D3D9 device stored by Portal 2's Vulkan shader API.
	IDirect3DDevice9* find_dxvk_device( const portal2_modules_t& modules ) noexcept;

}
