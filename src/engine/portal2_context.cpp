#include "portal2_context.h"

#include "build_23973718.h"
#include "interface_factory.h"

namespace p2boost {

	void portal2_modules_t::refresh( ) noexcept {
		m_engine          = Module{ L"engine.dll" };
		m_client          = Module{ L"client.dll" };
		m_material_system = Module{ L"materialsystem.dll" };
		m_surface         = Module{ L"vguimatsurface.dll" };
		m_vgui            = Module{ L"vgui2.dll" };
		m_shader_api      = Module{ L"shaderapivk.dll" };
		m_dxvk_d3d9       = Module{ L"dxvk_d3d9.dll" };
		m_vulkan          = Module{ L"vulkan-1.dll" };
		m_vstdlib         = Module{ L"vstdlib.dll" };
		m_tier0           = Module{ L"tier0.dll" };
		m_plugin          = Module::containing( reinterpret_cast< const void* >( &find_dxvk_device ) );
	}

	void Portal2Interfaces::connect( sdk::create_interface_fn_t engine_factory, const portal2_modules_t& modules ) noexcept {
		disconnect( );

		const sdk::InterfaceFactory engine{ engine_factory };
		const sdk::InterfaceFactory client{ modules.m_client };
		const sdk::InterfaceFactory materials{ modules.m_material_system };
		const sdk::InterfaceFactory surface{ modules.m_surface };
		const sdk::InterfaceFactory vgui{ modules.m_vgui };

		m_cvar            = engine.get< sdk::ICvar >( );
		m_engine_client   = engine.get< sdk::IVEngineClient >( );
		m_model_render    = engine.get< sdk::IVModelRender >( );
		m_engine_vgui     = engine.get< sdk::IEngineVGui >( );
		m_base_client     = client.get< sdk::IBaseClientDLL >( );
		m_material_system = materials.get< sdk::IMaterialSystem >( );
		m_surface         = surface.get< sdk::ISurface >( );
		m_scheme_manager  = vgui.get< sdk::ISchemeManager >( );

		if( m_cvar )
			return;

		// older Source branches also expose ICvar through a vstdlib factory getter.
		using get_factory_fn_t = sdk::create_interface_fn_t( __cdecl* )( );
		const auto get_factory = modules.m_vstdlib.export_function< get_factory_fn_t >( "VStdLib_GetICVarFactory" );
		if( get_factory )
			m_cvar = sdk::InterfaceFactory{ get_factory( ) }.get< sdk::ICvar >( );
	}

	void Portal2Interfaces::disconnect( ) noexcept {
		m_cvar            = nullptr;
		m_engine_client   = nullptr;
		m_model_render    = nullptr;
		m_engine_vgui     = nullptr;
		m_base_client     = nullptr;
		m_material_system = nullptr;
		m_surface         = nullptr;
		m_scheme_manager  = nullptr;
	}

	IDirect3DDevice9* find_dxvk_device( const portal2_modules_t& modules ) noexcept {
		const Address device_xref = modules.m_shader_api.find( portal2::signature::flat_texture_device );
		if( !device_xref )
			return nullptr;

		return device_xref.at< IDirect3DDevice9* >( portal2::xref::flat_texture_device_global );
	}

}
