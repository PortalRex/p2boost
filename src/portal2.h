#pragma once

#include <cstddef>

namespace p2boost::portal2 {

	// all values below belong to the verified windows build 23973718.
	namespace engine {
		// CEngine::Frame + 0x1a3: call immediately before the limiter accumulator is cleared.
		constexpr std::ptrdiff_t frame_complete{ 0x1fa043 };
		constexpr std::ptrdiff_t frame_accumulator{ 0x18 };
		constexpr size_t		 frame_target_local{ 8 };

		// shader-device restore xrefs to the two static-lighting cache rebuilds.
		constexpr std::ptrdiff_t restore_static_lighting{ 0xf8bb4 };
		constexpr std::ptrdiff_t restore_static_lighting_cache{ 0xf3adc };

		// queued dynamic-light functor vtables used by research builds.
		constexpr std::ptrdiff_t dynamic_light_unsigned_vtable{ 0x36e1dc };
		constexpr std::ptrdiff_t dynamic_light_signed_vtable{ 0x36e1fc };
	}

	namespace shader_api {
		// CShaderDeviceDx8::Present xrefs for D3DERR_DEVICENOTRESET and another-app-active.
		constexpr std::ptrdiff_t needs_reset_reacquire{ 0x247f7 };
		constexpr std::ptrdiff_t other_app_reacquire{ 0x2484b };

		// internal singleton pointers have no exported Source interface.
		constexpr std::ptrdiff_t shader_device{ 0xda594 };
		constexpr std::ptrdiff_t resource_callbacks{ 0xda5a4 };
		constexpr std::ptrdiff_t frame_sync{ 0xd5bb0 };
		constexpr std::ptrdiff_t d3d_device{ 0xda5d8 };

		// verified interface slots anchor the hooked methods.
		constexpr size_t hardware_sync_slot{ 136 };
		constexpr size_t present_slot{ 10 };
		constexpr size_t shader_device_slot{ 253 };
		constexpr size_t frame_sync_slot{ 3 };
		constexpr size_t resource_callbacks_slot{ 15 };
	}

	namespace material_system {
		// material-system interface slots resolved through CreateInterface.
		constexpr size_t lock_slot{ 116 };
		constexpr size_t unlock_slot{ 117 };

		// internal singleton pointers have no exported Source interface.
		constexpr std::ptrdiff_t shader_api{ 0x120720 };
		constexpr std::ptrdiff_t present_device{ 0x120728 };

		// CMaterialSystem::EndFrame xref that stores the render worker as MOV EAX, imm32.
		constexpr std::ptrdiff_t render_worker_pointer{ 0x12c84 };

		// queued-functor wrappers unwrapped only by research instrumentation.
		constexpr std::ptrdiff_t queued_functor_execute{ 0x13e90 };
		constexpr std::ptrdiff_t queued_functor_leaf{ 0xcfc0 };
	}

	namespace client {
		// CViewRender::RenderView and split-screen view xrefs to the same portal renderer.
		constexpr std::ptrdiff_t primary_portal_render{ 0x285c91 };
		constexpr std::ptrdiff_t secondary_portal_render{ 0x2c57db };

		// CClientShadowMgr has no exported interface in Portal 2.
		constexpr std::ptrdiff_t shadow_manager{ 0x9df090 };
		constexpr std::ptrdiff_t flashlight_models{ 0x9dea24 };
		constexpr size_t		 update_projected_slot{ 24 };

		// second direct call in CClientShadowMgr::UpdateProjectedTexture removes its dirty handle.
		constexpr std::ptrdiff_t remove_dirty_call{ 0x75 };
	}

	// recovered field offsets for the queued material render context.
	namespace queued_context {
		constexpr std::ptrdiff_t command_head{ 0x1f0 };
		constexpr std::ptrdiff_t parameter_current{ 0x1f8 };
		constexpr std::ptrdiff_t parameter_start{ 0x204 };
		constexpr std::ptrdiff_t vertex_current{ 0x264 };
		constexpr std::ptrdiff_t vertex_start{ 0x270 };
		constexpr std::ptrdiff_t index_current{ 0x28c };
		constexpr std::ptrdiff_t index_start{ 0x298 };
	}

	// recovered fields adjacent to CShaderAPIDx8::ForceHardwareSync.
	namespace hardware_sync {
		constexpr std::ptrdiff_t index{ -0x18c };
		constexpr std::ptrdiff_t pending_a{ -0x190 };
		constexpr std::ptrdiff_t pending_b{ -0x18f };
	}

	// recovered CClientShadowMgr and projected-shadow entry layout.
	namespace shadow {
		constexpr std::ptrdiff_t entries{ 0x2c };
		constexpr std::ptrdiff_t flashlight_models{ 0x30 };
		constexpr size_t		 entry_size{ 0x114 };
	}

	// native D3D9 slots used only by the research build.
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
