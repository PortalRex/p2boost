#pragma once

#include <cstddef>

namespace p2boost::portal2 {

	// signatures below are unique in the verified windows build 23973718.
	namespace signature {

		// CEngine::Frame tail that clears the limiter accumulator.
		constexpr auto frame_complete = "84 DB 5B 74 0C 8B 0D ? ? ? ? FF 15 ? ? ? ? 0F 57 C0 F3 0F 11 46 18 5E 8B E5 5D C3";

		// shader-device restore xrefs to both static-lighting rebuilds.
		constexpr auto restore_static_lighting		 = "6A 01 E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? 8B 0D ? ? ? ? 8B 11 8B 42 3C FF D0";
		constexpr auto restore_static_lighting_cache = "57 8B F9 74 3C E8 ? ? ? ? 0F B7 47 18 3D FF FF 00 00 74 2C";

		// constructors that embed the two queued dynamic-light vtables.
		constexpr auto dynamic_light_unsigned = "8B 4D 0C C7 00 ? ? ? ? C7 40 08 ? ? ? ? C7 40 10 ? ? ? ? 89 58 18 88 50 50";
		constexpr auto dynamic_light_signed	  = "8B 4D FC C7 40 0C 01 00 00 00 89 48 14 C7 00 ? ? ? ? C7 40 08 ? ? ? ? C7 40 10";

		// CShaderDeviceDx8::Present paths for reset and another-app-active.
		constexpr auto needs_reset = "8B 0D ? ? ? ? 8B 11 8B 82 9C 00 00 00 6A 01 FF D0 68 ? ? ? ? 6A 01 6A 01 8B CE "
									 "E8 ? ? ? ? EB 12";
		constexpr auto other_app   = "8B 0D ? ? ? ? 8B 11 8B 82 9C 00 00 00 6A 01 FF D0 68 ? ? ? ? 6A 01 6A 01 8B CE E8 "
									 "? ? ? ? EB 06";

		// code xrefs that expose otherwise private shader-api objects.
		constexpr auto shader_device = "8B 0D ? ? ? ? 8B 11 8B 42 24 FF D0 8D 48 FF 8B 45 08 0F AC D8 0C";
		constexpr auto frame_sync	 = "B8 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC 8A 41 1C D0 E8 24 01 C3";
		constexpr auto d3d_device	 = "A1 ? ? ? ? 8B 10 8B 52 68 51 8B 4D FC 8D 7E 08 57 51 8B 4E 1C 6A 00 53 51 50 FF D2";

		// one function references the presentation and shader-api globals together.
		constexpr auto material_devices = "8D 45 C8 E8 ? ? ? ? 8B 0D ? ? ? ? 8B 11 8B 42 18 FF D0 8B 0D ? ? ? ? 8B 11 "
										  "8B 92 24 01 00 00";

		// CMaterialSystem::EndFrame embeds the queued render-worker callback.
		constexpr auto render_worker = "33 C9 51 8D 55 F4 52 51 B8 ? ? ? ? 50 56 E8 ? ? ? ? 83 C4 10";

		// research-only queued-functor wrappers.
		constexpr auto queued_functor_execute = "8B C1 8B 48 14 8B 40 10 FF E0 CC CC CC CC CC CC";
		constexpr auto queued_functor_leaf	  = "8B 01 FF 60 0C CC CC CC CC CC CC CC CC CC CC CC 55 8B EC";

		// primary and split-screen CViewRender xrefs to the portal renderer.
		constexpr auto primary_portal_render   = "8D 95 18 FB FF FF 52 8D 85 C0 FD FF FF 50 8B CE E8 ? ? ? ? 8B 0D";
		constexpr auto secondary_portal_render = "51 8D 95 94 FD FF FF 52 8B CB E8 ? ? ? ? A1";

		// client-shadow xrefs for the manager object and flashlight-model list.
		constexpr auto shadow_manager	 = "50 57 53 B9 ? ? ? ? E8 ? ? ? ? 5F 5B 5E 5D C3";
		constexpr auto flashlight_models = "6A 00 68 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 83 79 30 00 8D 86 A0 00 00 00";
	}

	// byte offsets locate operands inside the unique matches above.
	namespace xref {
		constexpr std::ptrdiff_t frame_complete{ 17 };
		constexpr std::ptrdiff_t restore_static_lighting{ 10 };
		constexpr std::ptrdiff_t restore_static_lighting_cache{ 5 };
		constexpr std::ptrdiff_t dynamic_light_unsigned_vtable{ 5 };
		constexpr std::ptrdiff_t dynamic_light_signed_vtable{ 15 };
		constexpr std::ptrdiff_t resource_callbacks_global{ 2 };
		constexpr std::ptrdiff_t reacquire_call{ 29 };
		constexpr std::ptrdiff_t shader_device_global{ 2 };
		constexpr std::ptrdiff_t frame_sync_object{ 1 };
		constexpr std::ptrdiff_t d3d_device_global{ 1 };
		constexpr std::ptrdiff_t present_device_global{ 10 };
		constexpr std::ptrdiff_t shader_api_global{ 23 };
		constexpr std::ptrdiff_t render_worker_pointer{ 8 };
		constexpr std::ptrdiff_t primary_portal_call{ 16 };
		constexpr std::ptrdiff_t secondary_portal_call{ 10 };
		constexpr std::ptrdiff_t shadow_manager_object{ 4 };
		constexpr std::ptrdiff_t flashlight_models_global{ 15 };
	}

	// verified interface slots are abi layout, not absolute addresses.
	namespace slot {
		constexpr size_t hardware_sync{ 136 };
		constexpr size_t present{ 10 };
		constexpr size_t shader_device{ 253 };
		constexpr size_t frame_sync{ 3 };
		constexpr size_t resource_callbacks{ 15 };
		constexpr size_t material_lock{ 116 };
		constexpr size_t material_unlock{ 117 };
		constexpr size_t update_projected{ 24 };
	}

	// fields recovered around CEngine::Frame's limiter bookkeeping.
	namespace frame {
		constexpr std::ptrdiff_t accumulator{ 0x18 };
		constexpr size_t		 target_local{ 8 };
	}

	// recovered fields for the queued material render context.
	namespace queued_context {
		constexpr std::ptrdiff_t command_head{ 0x1f0 };
		constexpr std::ptrdiff_t parameter_current{ 0x1f8 };
		constexpr std::ptrdiff_t parameter_start{ 0x204 };
		constexpr std::ptrdiff_t vertex_current{ 0x264 };
		constexpr std::ptrdiff_t vertex_start{ 0x270 };
		constexpr std::ptrdiff_t index_current{ 0x28c };
		constexpr std::ptrdiff_t index_start{ 0x298 };
	}

	// fields adjacent to CShaderAPIDx8::ForceHardwareSync.
	namespace hardware_sync {
		constexpr std::ptrdiff_t index{ -0x18c };
		constexpr std::ptrdiff_t pending_a{ -0x190 };
		constexpr std::ptrdiff_t pending_b{ -0x18f };
	}

	// CClientShadowMgr and projected-shadow entry layout.
	namespace shadow {
		constexpr std::ptrdiff_t entries{ 0x2c };
		constexpr std::ptrdiff_t flashlight_models{ 0x30 };
		constexpr size_t		 entry_size{ 0x114 };
		constexpr std::ptrdiff_t remove_dirty_call{ 0x75 };
	}

	// native D3D9 vtable slots used only by research instrumentation.
	namespace d3d9 {
		constexpr size_t set_texture{ 65 };
		constexpr size_t draw_primitive{ 81 };
		constexpr size_t draw_indexed_primitive{ 82 };
		constexpr size_t draw_primitive_up{ 83 };
		constexpr size_t draw_indexed_primitive_up{ 84 };
		constexpr size_t set_vertex_shader{ 92 };
		constexpr size_t set_pixel_shader{ 107 };
	}

}
