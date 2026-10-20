#pragma once

#include "auto_update.h"
#include "console_commands.h"
#include "demo_provenance.h"
#include "display_adjustments.h"
#include "flashlight_buckets.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "nvidia_profile.h"
#include "occlusion_skip.h"
#include "partner_view.h"
#include "portal2_context.h"
#include "profiler_overlay.h"
#include "projected_shadow_cache.h"
#include "queued_frames.h"
#include "runtime_verification.h"
#include "sdk.h"
#include "shadow_depth_reuse.h"
#include "vgui_screen_cache.h"
#include "vgui_screen_shadow_skip.h"
#include "xinput_throttle.h"

#include <array>
#include <cstddef>

namespace p2boost {

	// every performance change, controlled through p2boost_fix. each one also
	// follows the global p2boost_enable switch.
	enum class fix_t {
		frame_limiter,
		shadow_cache,
		multicore,
		flashlight_flush,
		xinput_throttle,
		occlusion_skip,
		vgui_shadow_skip,
		shadow_depth_reuse,
		shadow_cull,
		partner_view_fps,
		shadow_depth_fps,
		vgui_screen_fps,
		frame_queue_depth,
		count
	};

	struct fix_info_t {
		const char* m_name;
		int         m_default;
		int         m_maximum;
		const char* m_help;
	};

	inline constexpr fix_info_t fix_table[]{
	    { "frame_limiter", 1, 1, "carry fps_max deadline overshoot instead of dropping it" },
	    { "shadow_cache", 1, 1, "skip unchanged projected-light receiver rebuilds" },
	    { "multicore", 1, 1, "keep mat_queue_mode 2 (multicore rendering)" },
	    { "flashlight_flush", 1, 1, "flush flashlight surface buckets in O(used)" },
	    { "xinput_throttle", 1, 1, "re-probe empty controller slots once per second" },
	    { "occlusion_skip", 1, 1, "skip the occlusion pass on maps without occluders" },
	    { "vgui_shadow_skip", 1, 1, "keep in-world VGUI screens out of shadow-depth maps" },
	    { "shadow_depth_reuse", 1, 1, "reuse depth maps another view rendered this frame" },
	    { "shadow_cull", 1, 1, "skip depth maps whose light volume misses the view" },
	    { "partner_view_fps", 120, 1000, "co-op partner's view (+remote_view, or the split-screen player not in control) renders per second (0 = every frame)" },
	    { "shadow_depth_fps", 120, 1000, "also reuse shadow-depth maps across frames, refreshed this often (0 = off)" },
	    { "vgui_screen_fps", 0, 1000, "opt-in: in-world screens (lobby boards, signs) repaint at most this often and are drawn from a cached texture in between (0 = off); glowing or translucent screen art composites differently" },
	    { "frame_queue_depth", 2, 2, "frames the render thread may run behind the game (1 = the engine's one)" },
	};

	inline constexpr std::size_t fix_count{ static_cast< std::size_t >( fix_t::count ) };
	static_assert( std::size( fix_table ) == fix_count, "every fix needs a table entry" );

	inline std::array< int, fix_count > default_fixes( ) noexcept {
		std::array< int, fix_count > values{ };
		for( std::size_t index = 0; index < fix_count; ++index )
			values[ index ] = fix_table[ index ].m_default;
		return values;
	}

	// the server plugin Portal 2 loads: owns every fix and feature, installs
	// them on Load, routes engine callbacks to them, and serves the console
	// commands. Lifecycle is in plugin.cpp, commands in plugin_commands.cpp.
	class Plugin final : public sdk::IServerPluginCallbacks {
	public:
		bool              Load( sdk::create_interface_fn_t interface_factory, sdk::create_interface_fn_t ) override;
		void              Unload( ) override;
		void              Pause( ) override;
		void              UnPause( ) override;
		const char*       GetPluginDescription( ) override;
		void              LevelInit( const char* ) override;
		void              ServerActivate( sdk::edict_t*, int, int client_max ) override;
		void              GameFrame( bool simulating ) override;
		void              LevelShutdown( ) override;
		void              ClientFullyConnect( sdk::edict_t* ) override;
		void              ClientActive( sdk::edict_t* ) override;
		void              ClientDisconnect( sdk::edict_t* ) override;
		void              ClientPutInServer( sdk::edict_t*, const char* ) override;
		void              SetCommandClient( int ) override;
		void              ClientSettingsChanged( sdk::edict_t* ) override;
		sdk::PluginResult ClientConnect( bool*, sdk::edict_t*, const char*, const char*, char*, int ) override;
		sdk::PluginResult ClientCommand( sdk::edict_t*, const sdk::CCommand& args ) override;
		sdk::PluginResult NetworkIDValidated( const char*, const char* ) override;
		void              OnQueryCvarValueFinished( int, sdk::edict_t*, sdk::QueryStatus, const char*, const char* ) override;
		void              OnEdictAllocated( sdk::edict_t* ) override;
		void              OnEdictFreed( const sdk::edict_t* ) override;

	private:
		// plugin.cpp
		void install_fixes( );
		void require( bool installed, const char* error );
		void report_load( ) const;
		void invalidate_render_caches( );
		void enforce_render_threading( ) const noexcept;
		void restore_render_threading( ) const noexcept;
		bool can_enable( ) const noexcept;
		void apply_enabled_state( ) noexcept;
		int  fix( fix_t which ) const noexcept;
		bool fix_available( fix_t which ) const noexcept;

		// plugin_commands.cpp: one entry per console command; registration
		// and dispatch both read the table.
		struct command_t {
			const char* m_name;
			const char* m_help;
			sdk::PluginResult ( Plugin::*m_handler )( const sdk::CCommand& );
		};
		static const command_t commands[];

		bool              register_commands( );
		void              print_fix( fix_t which, bool counters ) const;
		void              print_depth_views( ) const;
		void              print_errors( const char* indent ) const;
		sdk::PluginResult status_command( const sdk::CCommand& args );
		sdk::PluginResult enable_command( const sdk::CCommand& args );
		sdk::PluginResult fix_command( const sdk::CCommand& args );
		sdk::PluginResult overlay_command( const sdk::CCommand& args );
		sdk::PluginResult trace_command( const sdk::CCommand& args );
		sdk::PluginResult update_command( const sdk::CCommand& args );
		sdk::PluginResult flat_textures_command( const sdk::CCommand& args );
		sdk::PluginResult intensity_command( const sdk::CCommand& args );
		static void __cdecl command_thunk( const sdk::CCommand& command );

		static Plugin* m_instance;

		verification_result_t        m_verification;
		portal2_modules_t            m_modules;
		Portal2Interfaces            m_interfaces;
		nvidia_profile_result_t      m_nvidia_profile;
		FrameMetrics                 m_metrics;
		FrameLimiter                 m_frame_limiter;
		ProjectedShadowCache         m_shadow_cache;
		FlashlightBuckets            m_flashlight_buckets;
		XInputThrottle               m_xinput_throttle;
		OcclusionSkip                m_occlusion_skip;
		VguiScreenShadowSkip         m_vgui_screen_shadow_skip;
		VguiScreenCache              m_vgui_screen_cache;
		QueuedFrames                 m_queued_frames;
		ShadowDepthReuse             m_shadow_depth_reuse;
		DisplayAdjustments           m_display_adjustments;
		PartnerView                  m_partner_view;
		AutoUpdate                   m_auto_update;
		DemoProvenance               m_demo_provenance;
		FlatTextures                 m_flat_textures;
		ProfilerOverlay              m_overlay;
		ConsoleCommandRegistry       m_commands;
		IDirect3DDevice9*            m_device{ };
		bool                         m_enabled{ };
		bool                         m_last_focused{ };
		std::array< int, fix_count > m_fixes{ default_fixes( ) };
		mutable con_var_t*           m_queue_mode{ };
		mutable int                  m_last_queue_mode{ -1 };
		mutable int                  m_original_queue_mode{ -1 };
		mutable bool                 m_queue_mode_forced{ };
	};

}
