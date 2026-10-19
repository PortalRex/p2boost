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

		// C_VGuiScreen::DrawModel (renderable vtable slot 9, located through its
		// one vtable entry) and the g_CurrentViewID store in SetupCurrentView.
		const char*    vgui_screen_draw_model{ "55 8B EC 51 53 56 8B F1 8D 8E ? ? 00 00 E8 ? ? ? ? 8B D8 85 DB 75 0A 5E 33 C0 5B 8B E5 5D C2 08 00 8A 8E ? ? 00 00 8B 03 8B 40 6C" };
		const char*    current_view_id_store{ "8B 40 68 8D 55 0C 52 89 3D ? ? ? ? FF D0 8B 0D ? ? ? ?" };
		std::ptrdiff_t xref_current_view_id{ 9 };

		// ComputeShadowDepthTextures' slot search (cmp against the depth-slot
		// count) and CViewRender's per-view prologue reading gpGlobals->framecount.
		const char*    depth_slot_capacity{ "23 75 D8 8B 81 ? ? 00 00 3B F0 7D 21 8B 91 ? ? 00 00 80 3C 32 00 75 10 8B 91 ? ? 00 00" };
		std::ptrdiff_t xref_depth_slot_capacity{ 5 };
		const char*    globals_frame{ "55 8B EC 51 A1 ? ? ? ? 53 56 57 8B F9 8B 48 04 49 89 0D ? ? ? ? 8B 0D ? ? ? ? 8B 11" };
		std::ptrdiff_t xref_globals_pointer{ 5 };
		std::ptrdiff_t globals_framecount{ 4 };

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
		std::ptrdiff_t vmt_update_shadow_depth{ 29 };

		// CViewRender keeps one CViewSetup per split-screen slot at this+4 with a
		// 0x104 stride; x, y, width, and height are its first four even ints.
		std::ptrdiff_t view_render_slots{ 4 };
		std::ptrdiff_t view_render_slot_stride{ 0x104 };
		std::ptrdiff_t view_x{ 0x00 };
		std::ptrdiff_t view_y{ 0x08 };
		std::ptrdiff_t view_width{ 0x10 };
		std::ptrdiff_t view_height{ 0x18 };

		// each is followed by its m_nUnscaled* twin: the rect before
		// mat_viewportscale, which the slot's finished picture covers.
		std::ptrdiff_t view_unscaled_x{ 0x04 };
		std::ptrdiff_t view_unscaled_y{ 0x0c };
		std::ptrdiff_t view_unscaled_width{ 0x14 };
		std::ptrdiff_t view_unscaled_height{ 0x1c };

		// the remaining CViewSetup fields ComputeShadowDepthTextures writes for a
		// light: m_bOrtho, the ortho rect, flags, the projection block from fov
		// through the far planes, and the draw flags byte.
		std::ptrdiff_t view_ortho{ 0x20 };
		std::ptrdiff_t view_ortho_rect{ 0x24 };
		std::ptrdiff_t view_ortho_rect_end{ 0x34 };
		std::ptrdiff_t view_flags{ 0x34 };
		std::ptrdiff_t view_projection_end{ 0xbc };
		std::ptrdiff_t view_draw_flags{ 0x100 };

		std::ptrdiff_t surface_start_drawing_operand{ 127 };
		std::ptrdiff_t surface_finish_drawing_operand{ 603 };

		// CMatSystemSurface::DrawPanelIn3DSpace (IMatSystemSurface slot 164) and
		// the 3D StartDrawing it calls, which loads the panel-to-world matrix.
		const char* surface_draw_panel_in_3d{ "55 8B EC 53 8B 5D 08 56 57 8B F9 8B 07 8B 90 64 01 00 00 6A 00 53 FF D2 8B 0D ? ? ? ? 8B 01 8B 90 C4 01 00 00 FF D2" };
		const char* surface_start_drawing_3d{ "55 8B EC F3 0F 10 05 ? ? ? ? 8B 45 0C F3 0F 10 4D 18 81 EC 80 00 00 00 53 56 57 8B F9 8B 4D 10" };

		// CMaterialSystem::EndFrame (IMaterialSystem slot found through its vtable
		// entry); the queued-frame ring reads every address it needs from the
		// function body.
		// CMatQueuedRenderContext's constructor, run on the ring's third context.
		const char* materials_queued_context_ctor{ "56 57 8B F1 E8 ? ? ? ? 8D 8E F0 01 00 00 C7 06 ? ? ? ? C7 46 04 ? ? ? ? E8 ? ? ? ? 0F 57 C0 33 FF F3 0F 11 86 50 02 00 00" };
		// CMatRenderContextBase::LockRenderData: names the render-data stack
		// index and the two static CMemoryStacks the engine flips each frame.
		const char* materials_lock_render_data{ "55 8B EC 51 A1 ? ? ? ? 53 56 8D 34 80 8B 04 F5 ? ? ? ? 89 4D FC 8B 4D 08 8D 34 F5 ? ? ? ? 3B C8 0F 46 C8" };
		// CMemoryStack::Init, FreeAll and Term, for the ring's third stack.
		const char* materials_memory_stack_init{ "55 8B EC 8B 45 0C 83 EC 24 53 56 8B F1 8B 4D 18 83 C1 03 57 8B 7D 08 83 E1 FC C6 46 11 00 89 46 18 89 4E 1C" };
		const char* materials_memory_stack_free_all{ "55 8B EC 8B 51 0C 85 D2 74 ? 3B 51 04 73 ? 56 8B 71 1C 4E 8B C6 F7 D0 23 C6 03 C2 3B C2" };
		const char* materials_memory_stack_term{ "53 56 8B F1 8B 4E 0C 33 DB 3B CB 74 ? 3B 4E 04 73 ? 8B 56 1C 4A 8B C2 F7 D0 23 C2 03 C1 3B C1 72 ? 3B 06 77 ? 89 06 38 5E 11" };
		// CMaterialSystem::BeginFrame: flips the render-data stacks (1 - index).
		const char* materials_begin_frame{ "55 8B EC 83 EC 0C 57 8B F9 FF 15 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8B 07 8B 90 00 02 00 00 8B CF FF D2 84 C0 0F 85" };
		const char* materials_end_frame{ "55 8B EC 83 EC 0C 56 8B F1 FF 15 ? ? ? ? 84 C0 0F 84 ? ? ? ? 8B 06 8B 90 00 02 00 00 8B CE FF D2 84 C0 0F 84" };

		// CMaterialSystem::m_bAllocatingRenderTargets: CreateNamedRenderTarget*
		// refuses outside Begin/EndRenderTargetAllocation; DXVK needs neither the
		// device release End performs nor the pool ordering the block exists for.
		std::ptrdiff_t materials_allocating_render_targets{ 0x2bac };
		// IMaterial reference counting.
		std::ptrdiff_t vmt_material_add_ref{ 12 };

		// CMatSystemSurface fields: the current draw color, the surface extents
		// StartDrawing takes from the viewport, and the paint-mode flag byte
		// whose bit 1 marks a DrawPanelIn3DSpace in progress.
		std::ptrdiff_t surface_draw_color{ 0x2c };
		std::ptrdiff_t surface_extents{ 0x1c };
		std::ptrdiff_t surface_paint_flags{ 0x102dc };

		// ISurface slots DrawPanelIn3DSpace and DrawFullScreenBuffer use.
		std::ptrdiff_t vmt_surface_draw_set_color{ 14 };
		std::ptrdiff_t vmt_surface_draw_set_texture{ 37 };
		std::ptrdiff_t vmt_surface_create_texture_id{ 41 };
		std::ptrdiff_t vmt_surface_solve_traverse{ 89 };
		std::ptrdiff_t vmt_surface_draw_textured_sub_rect{ 103 };
		std::ptrdiff_t vmt_surface_draw_set_texture_material{ 165 };

		// the VPanel's Client() and IClientPanel's PaintTraverse and Repaint.
		std::ptrdiff_t vmt_vpanel_client{ 39 };
		std::ptrdiff_t vmt_client_panel_paint_traverse{ 3 };
		std::ptrdiff_t vmt_client_panel_repaint{ 4 };

		// IMatRenderContext slots: Release, BeginRender, EndRender, the depth
		// override DrawPanelIn3DSpace sets for the panel, and the render-target
		// and viewport push/pop. ITexture's actual width and height.
		std::ptrdiff_t vmt_context_release{ 1 };
		std::ptrdiff_t vmt_context_begin_render{ 2 };
		std::ptrdiff_t vmt_context_end_render{ 3 };
		std::ptrdiff_t vmt_context_override_depth{ 75 };
		std::ptrdiff_t vmt_context_push_render_target{ 108 };
		std::ptrdiff_t vmt_context_pop_render_target{ 111 };
		std::ptrdiff_t vmt_texture_actual_width{ 3 };
		std::ptrdiff_t vmt_texture_actual_height{ 4 };
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
	// view_id_t VIEW_SHADOW_DEPTH_TEXTURE, set by CShadowDepthView::Draw.
	inline constexpr int view_shadow_depth_texture{ 7 };

	// identify the running game from the loaded client module and make it
	// active. returns null when no profile matches.
	const profile_t* detect( const portal2_modules_t& modules ) noexcept;

	// the active game; null before detect succeeds.
	const profile_t* active( ) noexcept;

	// the built-in profiles, for tests and install-time probing.
	const profile_t& portal2( ) noexcept;
	const profile_t& portal_stories_mel( ) noexcept;

}