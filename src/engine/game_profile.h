#pragma once

#include "portal2_context.h"

#include <cstddef>

namespace p2boost::game {

	// Everything p2boost knows about one supported game: the signature strings
	// and every recovered operand/struct offset. The member initializers below
	// are the base game (Portal 2 build 23973718); a mod profile inherits them
	// and overrides only the fields that differ. Features read the active game
	// through game::active(), so they never know which game is running.
	class profile_t {
	public:
		virtual ~profile_t( ) = default;

		const char* name{ "Portal 2" };

		// client.dll location relative to the game root, used to verify that
		// the loaded copy belongs to this installation.
		const char* client_path{ "portal2\\bin\\client.dll" };

		const char* frame_complete{ "84 DB 5B 74 0C 8B 0D ? ? ? ? FF 15 ? ? ? ? 0F 57 C0 F3 0F 11 46 18 5E 8B E5 5D C3" };
		const char* overlay_engine_get_max_clients{ "E8 ? ? ? ? 8B 80 10 01 00 00 C3" };
		const char* overlay_engine_vgui_paint{ "55 8B EC 83 EC 28 53 56 8B F1" };
		const char* overlay_surface_get_font_tall{ "55 8B EC 8B 45 08 50 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 5D C2 04 00" };
		const char* overlay_surface_paint_traverse{ "55 8B EC 8B 55 08 83 EC 2C 56 8B F1" };
		const char* overlay_surface_start_drawing{ "55 8B EC 83 EC 10 80 3D ? ? ? ? 00 56 57 8B F9" };
		const char* overlay_surface_finish_drawing{ "56 6A 00 E8 ? ? ? ? 8B 0D ? ? ? ? 8B 01 8B 90 C4 01 00 00" };
		const char* overlay_surface_draw_colored_text{ "55 8B EC 8B 4D 28 8B 55 24 8D 45 2C" };
		const char* flat_texture_device{ "53 8B CE E8 ? ? ? ? 68 ? ? ? ? 8B CE 89 1D ? ? ? ? E8 ? ? ? ? 8B 55 08" };
		const char* viewport_commit_skip{ "83 7E 08 00 74 ?? 83 7E 0C 00 74 ?? A1 ?? ?? ?? ?? 8B 08 8B 91 BC 00 00 00" };
		const char* shadow_manager{ "50 57 53 B9 ? ? ? ? E8 ? ? ? ? 5F 5B 5E 5D C3" };
		const char* flashlight_models{ "6A 00 68 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 83 79 30 00 8D 86 A0 00 00 00" };
		const char* active_shadow_depth_call{ "8D 4D EC 51 8D 95 88 F6 FF FF 52 68 00 04 00 00 50 8B CE C7 45 EC 00 00 00 00 E8 ? ? ? ? 8B 15" };
		const char* active_shadow_depth_list{ "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC B8 88 11 00 00" };
		const char* active_shadow_manager{ "0F B7 3A 8B 0D ? ? ? ? 69 FF 14 01 00 00 03 7E 2C" };
		// CViewRender::Render; its address is vtable slot 5 and RenderView follows.
		const char* view_render{ "55 8B EC 81 EC 70 01 00 00 53 56 57 8B F9 8B 0D ? ? ? ? 89 7D F4 FF 15 ? ? ? ? 33 F6 88 45 FB 84 C0 74 1A 8B 0D ? ? ? ? 6A 04 56 68 ? ? ? ? 56 68" };
		// GetSplitScreenViewPlayer(slot): local-slot bitmask, else the remote
		// (+remote_view) player assigned to that slot.
		const char* split_view_player{ "55 8B EC 8B 45 08 8B C8 83 E1 1F BA 01 00 00 00 D3 E2 8B C8 C1 F9 05 85 14 8D ? ? ? ? 74 0B 50 E8 ? ? ? ? 83 C4 04 5D C3 8B 04 85 ? ? ? ? 5D C3" };

		// CShadowMgr::ClearAllFlashlightMaterialBuckets, matched in full. its two
		// calls are the m_Elements multilist and m_UsedSortIDs RemoveAll.
		const char* flashlight_bucket_flush{ "53 8B D9 80 BB B8 41 01 00 00 75 59 0F B7 83 9C 41 01 00 B9 FF FF 00 00 66 3B C1 74 48 8B 8B 90 41 01 00 56 57 0F B7 C0 8D 34 40 C1 E6 08 FF 84 31 8C 02 00 00 8D BC 31 40 02 00 00 8D 4F 30 E8 ? ? ? ? 8B CF E8 ? ? ? ? 8B 8B 90 41 01 00 0F B7 84 31 F2 02 00 00 BA FF FF 00 00 66 3B C2 75 C2 5F 5E 5B C3" };

		// inputsystem PollXDevices: the indirect XInputGetState call for each slot.
		const char* xinput_get_state_call{ "8B 47 2C 03 C0 8D 4C C7 0C 51 53 FF 15 ? ? ? ? 85 C0 0F 84" };

		// engine RecomputeOccluderEdgeList loading host_state.worldbrush and its
		// numoccluders, and the client's ExtractOccludedRenderables with its sole
		// call inside BuildRenderablesList.
		const char* world_occluder_count{ "A1 ? ? ? ? 8B 50 24 8B 58 2C 89 55 D8 8B 50 3C 8B 40 28 48" };
		const char* occluded_renderables_call{ "8B 55 FC 52 56 50 8B CF E8 ? ? ? ? 8B 8D ? ? ? ? 8B 95" };
		const char* occluded_renderables{ "55 8B EC 83 EC 08 F6 05 ? ? ? ? 01 75 16 83 0D ? ? ? ? 01 68 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 33 C0 39 41 30 75 09 8B 45 08 8B E5 5D C2 0C 00" };

		std::ptrdiff_t xref_frame_complete{ 17 };
		std::ptrdiff_t xref_world_brush_pointer{ 1 };
		std::ptrdiff_t xref_occluded_renderables_call{ 8 };
		std::ptrdiff_t world_occluder_count_offset{ 0x28 };
		std::ptrdiff_t xref_xinput_get_state_pointer{ 13 };
		std::ptrdiff_t xref_flashlight_elements_remove_all{ 64 };
		std::ptrdiff_t xref_flashlight_sort_ids_remove_all{ 71 };
		std::ptrdiff_t xref_flat_texture_device_global{ 17 };
		std::ptrdiff_t xref_shadow_manager_object{ 4 };
		std::ptrdiff_t xref_flashlight_models_global{ 15 };
		std::ptrdiff_t xref_active_shadow_depth_call{ 26 };
		std::ptrdiff_t xref_active_shadow_manager_global{ 5 };
		std::ptrdiff_t xref_engine_client_cmd_cbuf_add_text_operand{ 46 };
		std::ptrdiff_t xref_split_view_local_mask{ 26 };
		std::ptrdiff_t xref_split_view_remote_players{ 46 };

		std::ptrdiff_t frame_accumulator{ 0x18 };
		std::ptrdiff_t frame_target_local{ 8 };

		std::ptrdiff_t shadow_entries{ 0x2c };
		std::ptrdiff_t shadow_flashlight_models{ 0x30 };
		std::ptrdiff_t shadow_remove_dirty_call{ 0x75 };
		std::ptrdiff_t shadow_engine_handle{ 0x04 };
		std::ptrdiff_t shadow_world_to_shadow{ 0x0c };
		std::ptrdiff_t shadow_high_priority{ 0x94 };
		std::ptrdiff_t shadow_target_entity{ 0xa0 };

		// engine CShadowMgr: m_FlashlightStates (16-byte aligned FlashlightInfo_t
		// nodes) and m_bSinglePassFlashlightStateEnabled.
		std::ptrdiff_t shadow_flashlight_states{ 0x14190 };
		std::ptrdiff_t shadow_flashlight_head{ 0x1419c };
		std::ptrdiff_t shadow_single_pass_flashlight{ 0x141b8 };
		std::ptrdiff_t flashlight_state_size{ 0x300 };
		std::ptrdiff_t flashlight_state_next{ 0x2f2 };
		std::ptrdiff_t flashlight_material_buckets{ 0x240 };

		std::ptrdiff_t view_fov{ 0x68 };
		std::ptrdiff_t view_origin{ 0x70 };
		std::ptrdiff_t view_angles{ 0x7c };
		std::ptrdiff_t view_near_z{ 0x88 };
		std::ptrdiff_t view_far_z{ 0x8c };
		std::ptrdiff_t view_aspect_ratio{ 0x98 };

		std::ptrdiff_t vmt_destroy_shadow{ 17 };
		std::ptrdiff_t vmt_update_projected{ 24 };
		std::ptrdiff_t vmt_view_render{ 5 };
		std::ptrdiff_t vmt_render_view{ 6 };

		// CViewRender keeps one CViewSetup per split-screen slot at this+4 with a
		// 0x104 stride; x, y, width, and height are its first four even ints.
		std::ptrdiff_t view_render_slots{ 4 };
		std::ptrdiff_t view_render_slot_stride{ 0x104 };
		std::ptrdiff_t view_x{ 0x00 };
		std::ptrdiff_t view_y{ 0x08 };
		std::ptrdiff_t view_width{ 0x10 };
		std::ptrdiff_t view_height{ 0x18 };

		std::ptrdiff_t surface_start_drawing_operand{ 127 };
		std::ptrdiff_t surface_finish_drawing_operand{ 603 };
	};

	// the base game; constructed from the default member initializers above.
	class portal2_t : public profile_t {
	public:
		portal2_t( ) = default;
	};

	// mod profiles inherit the base game and override only what differs.
	// each lives in its own translation unit; see portal_stories_mel.cpp.
	class portal_stories_mel_t final : public portal2_t {
	public:
		portal_stories_mel_t( );
	};

	// CClientShadow entry size. compile-time because cached_state_t stores one
	// whole entry; shared by every supported game and verified through the
	// 69 FF 14 01 00 00 imul inside active_shadow_manager.
	inline constexpr std::ptrdiff_t shadow_entry_size{ 0x114 };

	// public SDK ABI values that never differ between games.
	inline constexpr int frame_stage_net_update_start{ 1 };
	inline constexpr int frame_stage_net_update_end{ 4 };
	inline constexpr int paint_mode_ui_panels{ 1 << 0 };

	// identify the running game from the loaded client module and make it
	// active. returns null when no profile matches.
	const profile_t* detect( const portal2_modules_t& modules ) noexcept;

	// the active game; null before detect succeeds.
	const profile_t* active( ) noexcept;

	// the built-in profiles, for tests and install-time probing.
	const profile_t& portal2( ) noexcept;
	const profile_t& portal_stories_mel( ) noexcept;

}