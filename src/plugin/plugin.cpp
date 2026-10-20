#include "plugin.h"

#include "console.h"
#include "game_window.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace p2boost {

	Plugin* Plugin::m_instance{ };

	bool Plugin::Load( sdk::create_interface_fn_t interface_factory, sdk::create_interface_fn_t ) {
		// publish the instance before engine-owned command callbacks can fire.
		m_instance = this;

		// configure the NVIDIA application profile before installing engine hooks.
		m_modules.refresh( );
		m_verification = verify_runtime( m_modules );
		m_device       = find_dxvk_device( m_modules );
		if( m_verification.m_dxvk_vulkan ) {
			m_nvidia_profile = configure_nvidia_profile( m_device );
			if( m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
				m_nvidia_profile = configure_nvidia_profile_elevated( );

			if( m_nvidia_profile.m_state == nvidia_profile_state_t::applied ) {
				m_instance = nullptr;
				restart_after_nvidia_profile_update( );
				return false;
			}
		}

		// acquire every versioned Source interface exactly once for this load.
		m_interfaces.connect( interface_factory, m_modules );

		// register the public command surface as one reversible transaction.
		if( !register_commands( ) ) {
			console_print( "[p2boost] failed to register console commands\n" );
			m_commands.disconnect( );
			m_interfaces.disconnect( );
			m_instance = nullptr;
			return false;
		}

		if( m_verification.m_supported )
			install_fixes( );

		// enable performance fixes only when the complete supported set is ready.
		m_enabled = can_enable( );
		apply_enabled_state( );
		m_last_focused = game_is_foreground( );
		m_metrics.set_focused( m_last_focused );
		report_load( );
		return true;
	}

	// every feature validates its own signatures and stays off on failure;
	// a failure is listed under p2boost_status issues.
	void Plugin::install_fixes( ) {
		// Portal 2 shipped these public SDK rendering controls with FCVAR_CHEAT.
		// removing only that flag makes the original engine path usable while
		// preserving archive, client, bounds, and replication behavior.
		require( m_commands.remove_flags( "mat_viewportscale", cvar_flag_cheat ) &&
		             m_commands.remove_flags( "mat_viewportupscale", cvar_flag_cheat ) &&
		             m_commands.remove_flags( "r_drawparticles", cvar_flag_cheat ),
		         "cvars unavailable" );

		require( m_overlay.connect( m_modules, m_interfaces, m_commands, &m_metrics ), "profiler client-paint interface unavailable" );
		require( m_demo_provenance.connect( m_modules, m_interfaces.engine_client( ), &m_enabled, &m_flat_textures ), "demo-recorder hooks unavailable" );
		require( m_frame_limiter.install( m_modules, &m_metrics ), "frame-limiter signature mismatch" );
		require( m_shadow_cache.install( m_modules, m_interfaces, &m_metrics ), "projected-shadow signature mismatch" );
		require( m_flashlight_buckets.install( m_modules ), "flashlight-bucket signature mismatch" );
		require( m_xinput_throttle.install( m_modules ), "inputsystem XInput signature mismatch" );
		require( m_occlusion_skip.install( m_modules ), "occlusion-pass signature mismatch" );
		require( m_vgui_screen_shadow_skip.install( m_modules ), "vgui-screen shadow-skip signature mismatch" );
		require( m_shadow_depth_reuse.install( m_modules, m_interfaces ), "optional shadow-depth reuse unavailable" );
		require( m_queued_frames.install( m_modules, m_interfaces ), "optional frame-queue ring unavailable" );
		require( m_vgui_screen_cache.install( m_modules, m_interfaces, m_vgui_screen_shadow_skip.draw_model_slot( ), m_verification.m_game_root ),
		         "optional vgui-screen cache unavailable" );
		require( m_flat_textures.install( m_modules, m_interfaces, m_device ), "optional flat-texture hooks unavailable" );
		require( m_display_adjustments.install( m_commands, m_device, &m_metrics ), "display-adjustment hooks unavailable" );
		require( m_display_adjustments.install_viewport_commit_patch( m_modules ), "viewport-commit signature mismatch" );
		require( m_partner_view.install( m_modules, m_interfaces, m_commands, m_device,
		                                 m_display_adjustments.available( ) ? &m_display_adjustments : nullptr ),
		         "optional partner-view hooks unavailable" );

		// a device Reset leaves every depth texture's contents undefined.
		if( m_shadow_depth_reuse.installed( ) && m_display_adjustments.available( ) )
			m_display_adjustments.add_reset_listener( []( void* context ) { static_cast< ShadowDepthReuse* >( context )->invalidate( ); },
			                                          &m_shadow_depth_reuse );

		// depth reuse needs to know whose view each depth map renders for.
		if( m_shadow_depth_reuse.installed( ) && m_shadow_cache.installed( ) )
			m_shadow_cache.set_view_listener( []( void* context, const void* view ) { static_cast< ShadowDepthReuse* >( context )->begin_view( view ); },
			                                  &m_shadow_depth_reuse );
	}

	void Plugin::require( bool installed, const char* error ) {
		if( !installed )
			m_verification.m_errors.emplace_back( error );
	}

	void Plugin::report_load( ) const {
		const std::string load_label = m_verification.m_game.empty( ) ? std::string{ } : std::string{ " (" } + m_verification.m_game + ")";
		console_print_color( console_color::green, "[p2boost] p2boost v%s loaded%s\n", P2BOOST_VERSION, load_label.c_str( ) );
		if( !m_verification.m_supported ) {
			console_print_color( console_color::red, "[p2boost] ERROR: the running game build is not supported. no hooks were installed.\n" );
			print_errors( "  " );
		} else if( !m_verification.m_errors.empty( ) ) {
			console_print_color( console_color::yellow, "[p2boost] loaded with partial features:\n" );
			print_errors( "  " );
		}

		console_print( "[p2boost] backend=%s fixes=%s\n",
		               m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "unsupported",
		               m_enabled ? "enabled" : "disabled" );
		if( m_verification.m_dxvk_vulkan && !m_verification.m_dxvk_shipped )
			console_print( "[p2boost] dxvk_d3d9.dll is %s (%s); performance measurements use this exact binary\n",
			               m_verification.m_dxvk_approved ? "an approved replacement build" : "an unrecognized build",
			               m_verification.m_dxvk_sha256.c_str( ) );
		if( m_nvidia_profile.m_state == nvidia_profile_state_t::unavailable || m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
			console_print( "[p2boost] NVIDIA profile setup %s at %s (%d); no driver settings were changed\n",
			               nvidia_profile_state_name( m_nvidia_profile.m_state ),
			               m_nvidia_profile.m_operation,
			               m_nvidia_profile.m_status );
	}

	void Plugin::Unload( ) {
		// the forced queue mode belongs to the plugin; hand it back first.
		restore_render_threading( );

		// remove feature callbacks before releasing their shared engine bindings.
		// the partner view and depth reuse register reset listeners on the display hooks.
		m_display_adjustments.remove_reset_listener( &m_shadow_depth_reuse );
		m_shadow_cache.set_view_listener( nullptr, nullptr );
		m_partner_view.remove( );
		m_display_adjustments.remove( );
		m_flat_textures.remove( );
		m_overlay.set_mode( profiler_mode_t::off, m_metrics );
		m_overlay.disconnect( );
		m_demo_provenance.disconnect( );
		m_shadow_cache.remove( );
		m_flashlight_buckets.remove( );
		m_xinput_throttle.remove( );
		m_occlusion_skip.remove( );
		m_vgui_screen_cache.remove( );
		m_queued_frames.remove( );
		m_vgui_screen_shadow_skip.remove( );
		m_shadow_depth_reuse.remove( );
		m_frame_limiter.remove( );
		m_commands.disconnect( );
		m_interfaces.disconnect( );
		m_device          = nullptr;
		m_instance        = nullptr;
		m_queue_mode      = nullptr;
		m_last_queue_mode = -1;
		console_print( "[p2boost] unloaded\n" );
	}

	void Plugin::Pause( ) {
		m_enabled = false;
		apply_enabled_state( );
	}

	void Plugin::UnPause( ) {
		m_enabled = can_enable( );
		apply_enabled_state( );
	}

	const char* Plugin::GetPluginDescription( ) {
		return "p2boost DXVK/Vulkan performance patch v" P2BOOST_VERSION;
	}

	// the video options' "Multicore rendering: Disabled" saves mat_queue_mode
	// 0, and Portal Stories: Mel's maps force 0 through a logic_auto entity on
	// every spawn, which runs after LevelInit. forcing 2 moves draw submission
	// onto the render worker. the command is executed through the engine's own
	// client-command path so the change callback always runs; the per-frame
	// check re-asserts the mode after any map or device reset overrides it.
	void Plugin::enforce_render_threading( ) const noexcept {
		if( !m_enabled || !fix( fix_t::multicore ) )
			return;

		if( !m_queue_mode )
			m_queue_mode = m_commands.find_variable( "mat_queue_mode" );
		if( !m_queue_mode || m_queue_mode->value_integer( ) >= 2 )
			return;

		if( sdk::IVEngineClient* engine_client = m_interfaces.engine_client( ) ) {
			const int before = m_queue_mode->value_integer( );
			// remember the game's own setting once so disabling restores it.
			if( !m_queue_mode_forced ) {
				m_original_queue_mode = before;
				m_queue_mode_forced   = true;
			}
			engine_client->ExecuteClientCmd( "mat_queue_mode 2" );
			const int after = m_queue_mode->value_integer( );
			if( after != 2 )
				console_print( "[p2boost] mat_queue_mode %d -> 2 failed\n", before );
			else if( before != 2 && before != m_last_queue_mode )
				console_print( "[p2boost] mat_queue_mode %d -> 2 (multicore rendering enabled)\n", before );
			m_last_queue_mode = after;
		}
	}

	// hand mat_queue_mode back to the value the game had before forcing.
	void Plugin::restore_render_threading( ) const noexcept {
		if( !m_queue_mode_forced )
			return;

		if( sdk::IVEngineClient* engine_client = m_interfaces.engine_client( ) ) {
			char command[ 32 ];
			std::snprintf( command, sizeof( command ), "mat_queue_mode %d", m_original_queue_mode );
			engine_client->ExecuteClientCmd( command );
			m_queue_mode_forced = false;
			m_last_queue_mode   = -1;
			console_print( "[p2boost] mat_queue_mode restored to %d\n", m_original_queue_mode );
		}
	}

	// caches whose contents a new map, or a display transition (DXVK can
	// recreate swapchain-backed resources), can leave stale.
	void Plugin::invalidate_render_caches( ) {
		m_shadow_cache.invalidate_all( );
		m_shadow_depth_reuse.invalidate( );
		m_vgui_screen_cache.invalidate( );
		m_partner_view.invalidate( );
	}

	void Plugin::LevelInit( const char* ) {
		invalidate_render_caches( );
		enforce_render_threading( );
	}

	void Plugin::ServerActivate( sdk::edict_t*, int, int client_max ) {
		// the activated server owns a new receiver and entity lifetime.
		m_shadow_cache.invalidate_all( );
		m_metrics.set_coop( client_max > 1 );
	}

	void Plugin::GameFrame( bool simulating ) {
		// mirror only the state needed by diagnostics.
		m_metrics.set_simulating( simulating );

		// Mel's logic_auto re-forces mat_queue_mode 0 after map spawn;
		// re-assert multicore every frame so the override never sticks.
		enforce_render_threading( );

		const bool focused = game_is_foreground( );
		m_metrics.set_focused( focused );
		if( focused != m_last_focused ) {
			invalidate_render_caches( );
			enforce_render_threading( );
			m_last_focused = focused;
		}
	}

	void Plugin::LevelShutdown( ) {
		// release map-owned state before Source begins destroying its resources.
		m_metrics.set_simulating( false );
		m_metrics.set_coop( false );
		m_flat_textures.discard_pending_uploads( );
		m_shadow_cache.invalidate_all( );
	}

	void Plugin::ClientFullyConnect( sdk::edict_t* ) {
		// a completed sign-on can replace the entire client entity set.
		m_shadow_cache.invalidate( );
	}

	void Plugin::ClientActive( sdk::edict_t* ) {
		// the active transition makes newly received renderables visible.
		m_shadow_cache.invalidate( );
	}

	void Plugin::ClientDisconnect( sdk::edict_t* ) {
		// never retain receivers owned by a departing co-op player.
		m_shadow_cache.invalidate( );
	}

	void Plugin::ClientPutInServer( sdk::edict_t*, const char* ) {
		// a joining player introduces a new renderable hierarchy.
		m_shadow_cache.invalidate( );
	}

	void Plugin::SetCommandClient( int ) {
	}

	void Plugin::ClientSettingsChanged( sdk::edict_t* ) {
	}

	sdk::PluginResult Plugin::ClientConnect( bool*, sdk::edict_t*, const char*, const char*, char*, int ) {
		return sdk::Continue;
	}

	sdk::PluginResult Plugin::NetworkIDValidated( const char*, const char* ) {
		return sdk::Continue;
	}

	void Plugin::OnQueryCvarValueFinished( int, sdk::edict_t*, sdk::QueryStatus, const char*, const char* ) {
	}

	void Plugin::OnEdictAllocated( sdk::edict_t* ) {
		// handles may be reused, so allocation begins a new cache lifetime.
		m_shadow_cache.invalidate( );
	}

	void Plugin::OnEdictFreed( const sdk::edict_t* ) {
		// receiver lists must not retain a pointer to a destroyed renderable.
		m_shadow_cache.invalidate( );
	}

	bool Plugin::can_enable( ) const noexcept {
		// partial activation would make A/B results impossible to interpret.
		return m_verification.m_supported && m_verification.m_dxvk_vulkan && m_frame_limiter.installed( ) &&
		       m_shadow_cache.installed( );
	}

	void Plugin::apply_enabled_state( ) noexcept {
		// p2boost_enable is the global A/B switch; p2boost_fix controls each change.
		auto on = [ this ]( fix_t which ) {
			return m_enabled && fix( which ) != 0;
		};

		m_frame_limiter.set_enabled( on( fix_t::frame_limiter ) );
		m_shadow_cache.set_enabled( on( fix_t::shadow_cache ) );
		m_shadow_cache.set_exact_cull( on( fix_t::shadow_cull ) );
		if( m_partner_view.reuse_available( ) )
			m_partner_view.set_max_fps( m_enabled ? fix( fix_t::partner_view_fps ) : 0 );
		if( on( fix_t::multicore ) )
			enforce_render_threading( );
		else
			restore_render_threading( );
		m_flashlight_buckets.set_enabled( on( fix_t::flashlight_flush ) );
		m_xinput_throttle.set_enabled( on( fix_t::xinput_throttle ) );
		m_occlusion_skip.set_enabled( on( fix_t::occlusion_skip ) );
		m_vgui_screen_shadow_skip.set_enabled( on( fix_t::vgui_shadow_skip ) );
		m_shadow_depth_reuse.set_enabled( on( fix_t::shadow_depth_reuse ) );
		m_shadow_depth_reuse.set_max_fps( on( fix_t::shadow_depth_reuse ) ? fix( fix_t::shadow_depth_fps ) : 0 );
		m_vgui_screen_cache.set_max_fps( m_enabled ? fix( fix_t::vgui_screen_fps ) : 0 );
		m_queued_frames.set_depth( m_enabled ? fix( fix_t::frame_queue_depth ) : 1 );
		m_shadow_cache.set_stable_depth_order( on( fix_t::shadow_depth_reuse ) && m_shadow_depth_reuse.installed( ) );
		m_overlay.set_fixes_enabled( m_enabled );
	}

	int Plugin::fix( fix_t which ) const noexcept {
		return m_fixes[ static_cast< std::size_t >( which ) ];
	}

	// a fix whose hooks could not be installed on this build stays off.
	bool Plugin::fix_available( fix_t which ) const noexcept {
		switch( which ) {
		case fix_t::frame_limiter:
			return m_frame_limiter.installed( );
		case fix_t::shadow_cache:
		case fix_t::shadow_cull:
			return m_shadow_cache.installed( );
		case fix_t::multicore:
			return true;
		case fix_t::flashlight_flush:
			return m_flashlight_buckets.installed( );
		case fix_t::xinput_throttle:
			return m_xinput_throttle.installed( );
		case fix_t::occlusion_skip:
			return m_occlusion_skip.installed( );
		case fix_t::vgui_shadow_skip:
			return m_vgui_screen_shadow_skip.installed( );
		case fix_t::shadow_depth_reuse:
		case fix_t::shadow_depth_fps:
			return m_shadow_depth_reuse.installed( );
		case fix_t::partner_view_fps:
			return m_partner_view.reuse_available( );
		case fix_t::vgui_screen_fps:
			return m_vgui_screen_cache.installed( );
		case fix_t::frame_queue_depth:
			return m_queued_frames.installed( );
		default:
			return false;
		}
	}

	Plugin g_plugin;

}

extern "C" __declspec( dllexport ) void* CreateInterface( const char* name, int* return_code ) {
	// expose only the callback interface requested by Portal 2's plugin loader.
	if( name && std::strcmp( name, "ISERVERPLUGINCALLBACKS003" ) == 0 ) {
		if( return_code )
			*return_code = 0;
		return &p2boost::g_plugin;
	}

	if( return_code )
		*return_code = 1;
	return nullptr;
}

BOOL APIENTRY DllMain( HMODULE, DWORD, LPVOID ) {
	return TRUE;
}
