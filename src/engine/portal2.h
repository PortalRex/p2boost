#pragma once

#include <cstddef>

namespace p2boost::portal2 {

	// Ghidra-verified signatures for Windows build 23973718. the release scans
	// these exact instruction sequences instead of relying on preferred RVAs.
	namespace signature {
		constexpr auto frame_complete = "84 DB 5B 74 0C 8B 0D ? ? ? ? FF 15 ? ? ? ? 0F 57 C0 F3 0F 11 46 18 5E 8B E5 5D C3";

		// CShaderDeviceDx8::CreateD3DDevice stores the returned DXVK device
		// immediately before initializing its concrete shader-device wrapper.
		constexpr auto flat_texture_device = "53 8B CE E8 ? ? ? ? 68 ? ? ? ? 8B CE 89 1D ? ? ? ? E8 ? ? ? ? 8B 55 08";

		constexpr auto shadow_manager = "50 57 53 B9 ? ? ? ? E8 ? ? ? ? 5F 5B 5E 5D C3";

		constexpr auto flashlight_models = "6A 00 68 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 83 79 30 00 8D 86 A0 00 00 00";
	}

	// operand offsets inside the unique signatures above.
	namespace xref {
		constexpr std::ptrdiff_t frame_complete{ 17 };
		constexpr std::ptrdiff_t flat_texture_device_global{ 17 };
		constexpr std::ptrdiff_t shadow_manager_object{ 4 };
		constexpr std::ptrdiff_t flashlight_models_global{ 15 };
	}

	// recovered class and stack fields used by the two production hooks.
	namespace frame {
		constexpr std::ptrdiff_t accumulator{ 0x18 };
		constexpr size_t		 target_local{ 8 };
	}

	namespace shadow {
		constexpr std::ptrdiff_t entries{ 0x2c };
		constexpr std::ptrdiff_t flashlight_models{ 0x30 };
		constexpr size_t		 entry_size{ 0x114 };
		constexpr std::ptrdiff_t remove_dirty_call{ 0x75 };
	}

	// VMT indices describe ABI layout rather than process addresses.
	namespace vmt {
		constexpr size_t update_projected{ 24 };
		constexpr size_t engine_is_recording_demo{ 79 };
		constexpr size_t engine_execute_client_cmd{ 104 };
		constexpr size_t render_view_vgui_paint{ 36 };

		// VMaterialSystem080 and public D3D9 ABI methods used by the optional
		// flat-texture feature. these describe interface layout, not addresses.
		constexpr size_t reload_textures{ 79 };
		constexpr size_t reload_materials{ 80 };
		// Portal 2's current public interface layout. SAR independently uses the
		// same GetRenderContext method for build 9568/current Steam builds.
		constexpr size_t get_render_context{ 113 };
		constexpr size_t render_context_bind{ 9 };
		constexpr size_t material_texture_group{ 1 };
		constexpr size_t material_shader_name{ 49 };
		constexpr size_t d3d9_create_texture{ 23 };
		constexpr size_t d3d9_update_surface{ 30 };
		constexpr size_t d3d9_update_texture{ 31 };
		constexpr size_t d3d9_set_texture{ 65 };
		constexpr size_t d3d9_texture_release{ 2 };
		constexpr size_t d3d9_texture_get_surface_level{ 18 };
		constexpr size_t d3d9_texture_lock{ 19 };
		constexpr size_t d3d9_texture_unlock{ 20 };
		constexpr size_t d3d9_surface_release{ 2 };
		constexpr size_t d3d9_surface_lock{ 13 };
		constexpr size_t d3d9_surface_unlock{ 14 };
	}

}
