#pragma once

#include <cstddef>

namespace p2boost::portal2 {

	// Ghidra-verified signatures for Windows build 23973718. the release scans
	// these exact instruction sequences instead of relying on preferred RVAs.
	namespace signature {
		constexpr auto frame_complete = "84 DB 5B 74 0C 8B 0D ? ? ? ? FF 15 ? ? ? ? 0F 57 C0 F3 0F 11 46 18 5E 8B E5 5D C3";

		// exact entry points reached through the overlay's private Windows ABIs.
		// these guards make every recovered VMT index and PaintTraverseEx helper
		// fail closed before the first HUD frame can call it.
		constexpr auto overlay_engine_get_max_clients    = "E8 ? ? ? ? 8B 80 10 01 00 00 C3";
		constexpr auto overlay_engine_vgui_paint         = "55 8B EC 83 EC 28 53 56 8B F1";
		constexpr auto overlay_surface_get_font_tall     = "55 8B EC 8B 45 08 50 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 5D C2 04 00";
		constexpr auto overlay_surface_paint_traverse    = "55 8B EC 8B 55 08 83 EC 2C 56 8B F1";
		constexpr auto overlay_surface_start_drawing     = "55 8B EC 83 EC 10 80 3D ? ? ? ? 00 56 57 8B F9";
		constexpr auto overlay_surface_finish_drawing    = "56 6A 00 E8 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 90 C4 01 00 00";
		constexpr auto overlay_surface_draw_colored_text = "55 8B EC 8B 4D 28 8B 55 24 8D 45 2C";

		// CShaderDeviceDx8::CreateD3DDevice stores the returned DXVK device
		// immediately before initializing its concrete shader-device wrapper.
		constexpr auto flat_texture_device = "53 8B CE E8 ? ? ? ? 68 ? ? ? ? 8B CE 89 1D ? ? ? ? E8 ? ? ? ? 8B 55 08";

		constexpr auto shadow_manager = "50 57 53 B9 ? ? ? ? E8 ? ? ? ? 5F 5B 5E 5D C3";

		constexpr auto flashlight_models = "6A 00 68 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 83 79 30 00 8D 86 A0 00 00 00";

		// ComputeShadowDepthTextures calls the private active-list builder after
		// preparing its 1024-handle output buffer. This is the only call site that
		// owns the per-view shadow-depth selection used by the renderer.
		constexpr auto active_shadow_depth_call = "8D 4D EC 51 8D 95 88 F6 FF FF 52 68 00 04 00 00 50 8B CE C7 45 EC 00 00 00 00 E8 ? ? ? ? 8B 15";

		// Portal 2's active-list implementation has a stack-aligned prologue due
		// to its 1024-float sort workspace. Validating the target prevents a nearby
		// private helper from ever being accepted as the selection function.
		constexpr auto active_shadow_depth_list = "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC B8 88 11 00 00";

		// the selected-list loop resolves IShadowMgr before reading the retained
		// FlashlightState_t. This xref gives the optimizer the public manager ABI
		// without embedding the address of Portal 2's private global.
		constexpr auto active_shadow_manager = "0F B7 3A 8B 0D ? ? ? ? 69 FF 14 01 00 00 03 7E 2C";
	}

	// operand offsets inside the unique signatures above.
	namespace xref {
		constexpr std::ptrdiff_t frame_complete{ 17 };
		constexpr std::ptrdiff_t flat_texture_device_global{ 17 };
		constexpr std::ptrdiff_t shadow_manager_object{ 4 };
		constexpr std::ptrdiff_t flashlight_models_global{ 15 };
		constexpr std::ptrdiff_t active_shadow_depth_call{ 26 };
		constexpr std::ptrdiff_t active_shadow_manager_global{ 5 };

		// VEngineClient015::ClientCmd contains one direct Cbuf_AddText call. SAR's
		// public Portal 2 10090 offsets independently identify this rel32 operand.
		constexpr std::ptrdiff_t engine_client_cmd_cbuf_add_text_operand{ 46 };
	}

	// recovered class and stack fields used by the two production hooks.
	namespace frame {
		constexpr std::ptrdiff_t accumulator{ 0x18 };
		constexpr size_t         target_local{ 8 };
	}

	namespace shadow {
		constexpr std::ptrdiff_t entries{ 0x2c };
		constexpr std::ptrdiff_t flashlight_models{ 0x30 };
		constexpr size_t         entry_size{ 0x114 };
		constexpr std::ptrdiff_t remove_dirty_call{ 0x75 };
		constexpr std::ptrdiff_t engine_handle{ 0x04 };
		constexpr std::ptrdiff_t world_to_shadow{ 0x0c };
		constexpr std::ptrdiff_t high_priority{ 0x94 };
	}

	// CViewSetup fields read by BuildActiveShadowDepthList in build 23973718.
	// keeping these verified fields together avoids pretending the much larger
	// private Portal 2 structure is reproduced in the public SDK wrapper.
	namespace view {
		constexpr std::ptrdiff_t fov{ 0x68 };
		constexpr std::ptrdiff_t origin{ 0x70 };
		constexpr std::ptrdiff_t angles{ 0x7c };
		constexpr std::ptrdiff_t near_z{ 0x88 };
		constexpr std::ptrdiff_t far_z{ 0x8c };
		constexpr std::ptrdiff_t aspect_ratio{ 0x98 };
	}

	// private CClientShadowMgr layout recovered for the supported build.
	namespace vmt {
		constexpr size_t update_projected{ 24 };
	}

	// ClientFrameStage_t values recovered from VClient016 and independently
	// matched against Valve's public Source SDK ordering.
	namespace frame_stage {
		constexpr int net_update_start{ 1 };
		constexpr int net_update_end{ 4 };
	}

	// PaintMode_t is part of the public render-view ABI.
	namespace paint_mode {
		constexpr int ui_panels{ 1 << 0 };
	}

	// rel32 call operands inside CMatSystemSurface::PaintTraverseEx. These are
	// the Windows offsets used by SAR for the supported Portal 2 surface ABI.
	namespace surface {
		constexpr std::ptrdiff_t start_drawing_operand{ 127 };
		constexpr std::ptrdiff_t finish_drawing_operand{ 603 };
	}

}
