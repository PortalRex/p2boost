#include "plugin.h"

#include "console.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace p2boost {

	namespace {

		// parse the exact 0/1 syntax shared by toggle commands.
		bool parse_toggle( const sdk::CCommand& args, bool& enabled ) noexcept {
			if( args.ArgC( ) != 2 )
				return false;

			if( std::strcmp( args.Arg( 1 ), "0" ) == 0 ) {
				enabled = false;
				return true;
			}

			if( std::strcmp( args.Arg( 1 ), "1" ) == 0 ) {
				enabled = true;
				return true;
			}

			return false;
		}

		// accept only the documented profiler modes instead of coercing input.
		bool parse_profiler_mode( const sdk::CCommand& args, profiler_mode_t& mode ) noexcept {
			if( args.ArgC( ) != 2 )
				return false;

			if( std::strcmp( args.Arg( 1 ), "0" ) == 0 )
				mode = profiler_mode_t::off;
			else if( std::strcmp( args.Arg( 1 ), "1" ) == 0 )
				mode = profiler_mode_t::full;
			else if( std::strcmp( args.Arg( 1 ), "2" ) == 0 )
				mode = profiler_mode_t::fps;
			else
				return false;

			return true;
		}

		// parse one bounded integer.
		bool parse_bounded( const char* text, int minimum, int maximum, int& value ) noexcept {
			char*      end{ };
			const long parsed = std::strtol( text, &end, 10 );
			if( end == text || *end != '\0' || parsed < minimum || parsed > maximum )
				return false;

			value = static_cast< int >( parsed );
			return true;
		}

	}

	const Plugin::command_t Plugin::commands[]{
	    { "p2boost_status", "Print p2boost's state and the counters of every fix.", &Plugin::status_command },
	    { "p2boost_enable", "Global switch for every performance fix: p2boost_enable 0|1.", &Plugin::enable_command },
	    { "p2boost_fix", "List fixes, or read or set one: p2boost_fix [name] [value].", &Plugin::fix_command },
	    { "p2boost_overlay", "Frame-time overlay: 0 off, 1 full profiler, 2 FPS counter.", &Plugin::overlay_command },
	    { "p2boost_trace", "Record frame times; p2boost_trace 0 writes p2boost_metrics.csv.", &Plugin::trace_command },
	    { "p2boost_update", "Update p2boost to the latest release. Optional 'force' reinstalls.", &Plugin::update_command },
	    { "p2boost_flat_textures", "Optional visual mode with solid-color surfaces: 0|1.", &Plugin::flat_textures_command },
	    { "r_intensity", "Scale final-frame RGB brightness (0.0 to 2.0, 1.0 neutral).", &Plugin::intensity_command },
	};

	bool Plugin::register_commands( ) {
		if( !m_commands.connect( m_interfaces.cvar( ) ) )
			return false;

		for( const command_t& command : commands )
			if( !m_commands.add( command.m_name, command.m_help, &command_thunk ) )
				return false;
		return true;
	}

	sdk::PluginResult Plugin::ClientCommand( sdk::edict_t*, const sdk::CCommand& args ) {
		// dispatch owned commands and leave every other command untouched.
		for( const command_t& command : commands )
			if( _stricmp( args.Arg( 0 ), command.m_name ) == 0 )
				return ( this->*command.m_handler )( args );
		return sdk::Continue;
	}

	void __cdecl Plugin::command_thunk( const sdk::CCommand& command ) {
		// Source invokes a C callback, so forward through the live instance.
		if( m_instance )
			m_instance->ClientCommand( nullptr, command );
	}

	void Plugin::print_errors( const char* indent ) const {
		for( const auto& error : m_verification.m_errors )
			console_print( "[p2boost] %s- %s\n", indent, error.c_str( ) );
	}

	// print a readable snapshot for support and benchmark logs.
	sdk::PluginResult Plugin::status_command( const sdk::CCommand& ) {
		const std::string game_label = m_verification.m_game.empty( ) ? std::string{ } : std::string{ " (" } + m_verification.m_game + ")";
		console_print_color( m_verification.m_supported ? console_color::green : console_color::red,
		                     "[p2boost] p2boost %s: %s%s\n", P2BOOST_VERSION,
		                     m_verification.m_supported ? "supported build" : "UNSUPPORTED BUILD", game_label.c_str( ) );
		console_print( "[p2boost]   backend   %s, dxvk %s (%s)\n",
		               m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "NOT ACTIVE",
		               m_verification.m_dxvk_shipped    ? "shipped"
		               : m_verification.m_dxvk_approved ? "approved replacement"
		                                                : "unrecognized",
		               m_verification.m_dxvk_sha256.c_str( ) );
		console_print( "[p2boost]   nvidia    %s (%s:%d)\n", nvidia_profile_state_name( m_nvidia_profile.m_state ),
		               m_nvidia_profile.m_operation, m_nvidia_profile.m_status );
		console_print_color( m_enabled ? console_color::green : console_color::yellow,
		                     "[p2boost]   fixes     %s (p2boost_enable)\n", m_enabled ? "enabled" : "disabled" );
		for( std::size_t index = 0; index < fix_count; ++index )
			print_fix( static_cast< fix_t >( index ), true );
		console_print( "[p2boost]   display   gamma passes %llu, viewport passes %llu, r_intensity %g%s\n",
		               static_cast< unsigned long long >( m_display_adjustments.gamma_passes( ) ),
		               static_cast< unsigned long long >( m_display_adjustments.viewport_passes( ) ),
		               m_display_adjustments.intensity( ),
		               m_display_adjustments.available( ) ? "" : " (unavailable)" );
		console_print( "[p2boost]   flat      %s (%zu of %zu textures ready)\n",
		               m_flat_textures.available( ) ? ( m_flat_textures.enabled( ) ? "on" : "off" ) : "unavailable",
		               m_flat_textures.ready_texture_count( ),
		               m_flat_textures.texture_count( ) );
		console_print( "[p2boost]   overlay   %s, trace %s, demo marker %s\n",
		               m_overlay.available( ) ? "available" : "unavailable",
		               m_metrics.tracing( ) ? "on" : "off",
		               m_demo_provenance.available( ) ? "ready" : "unavailable" );
		if( !m_verification.m_errors.empty( ) ) {
			console_print( "[p2boost]   issues:\n" );
			print_errors( "    " );
		}

		return sdk::Stop;
	}

	// toggle only the benchmark-proven performance fixes.
	sdk::PluginResult Plugin::enable_command( const sdk::CCommand& args ) {
		bool enabled{ };
		if( !parse_toggle( args, enabled ) ) {
			console_print( "usage: p2boost_enable 0|1\n" );
			return sdk::Stop;
		}

		if( enabled && !can_enable( ) ) {
			console_print( "[p2boost] cannot enable: unsupported build or backend\n" );
			return sdk::Stop;
		}

		m_enabled = enabled;
		apply_enabled_state( );
		return sdk::Stop;
	}

	// list every fix, or read or set one by name.
	sdk::PluginResult Plugin::fix_command( const sdk::CCommand& args ) {
		if( args.ArgC( ) == 1 ) {
			for( std::size_t index = 0; index < fix_count; ++index )
				print_fix( static_cast< fix_t >( index ), false );
			return sdk::Stop;
		}

		std::size_t index = 0;
		while( index < fix_count && _stricmp( fix_table[ index ].m_name, args.Arg( 1 ) ) != 0 )
			++index;
		if( index == fix_count || args.ArgC( ) > 3 ) {
			console_print( "usage: p2boost_fix [name] [value]; run p2boost_fix for the list\n" );
			return sdk::Stop;
		}

		const fix_t which = static_cast< fix_t >( index );
		if( args.ArgC( ) == 3 ) {
			int value{ };
			if( !parse_bounded( args.Arg( 2 ), 0, fix_table[ index ].m_maximum, value ) ) {
				console_print( "usage: p2boost_fix %s <0..%d>\n", fix_table[ index ].m_name, fix_table[ index ].m_maximum );
				return sdk::Stop;
			}
			if( value > 0 && !fix_available( which ) ) {
				console_print( "[p2boost] %s is unavailable on this build\n", fix_table[ index ].m_name );
				return sdk::Stop;
			}

			m_fixes[ index ] = value;
			apply_enabled_state( );
		}

		print_fix( which, true );
		if( which == fix_t::shadow_depth_fps && args.ArgC( ) == 2 )
			print_depth_views( );
		return sdk::Stop;
	}

	// one line per fix: value, default, availability, and with counters
	// what it has done so far.
	void Plugin::print_fix( fix_t which, bool counters ) const {
		const fix_info_t& info = fix_table[ static_cast< std::size_t >( which ) ];
		char              detail[ 240 ]{ };
		if( counters ) {
			switch( which ) {
			case fix_t::shadow_cache:
				std::snprintf( detail, sizeof( detail ), "%u hits", m_shadow_cache.hit_count( ) );
				break;
			case fix_t::shadow_cull:
				std::snprintf( detail, sizeof( detail ), "%u culled", m_shadow_cache.culled_count( ) );
				break;
			case fix_t::multicore:
				if( const con_var_t* queue = m_commands.find_variable( "mat_queue_mode" ) )
					std::snprintf( detail, sizeof( detail ), "mat_queue_mode %d", queue->value_integer( ) );
				break;
			case fix_t::xinput_throttle:
				std::snprintf( detail, sizeof( detail ), "%llu probes skipped", static_cast< unsigned long long >( m_xinput_throttle.skipped_count( ) ) );
				break;
			case fix_t::occlusion_skip:
				std::snprintf( detail, sizeof( detail ), "%llu passes skipped", static_cast< unsigned long long >( m_occlusion_skip.skipped_count( ) ) );
				break;
			case fix_t::vgui_shadow_skip:
				std::snprintf( detail, sizeof( detail ), "%llu draws skipped", static_cast< unsigned long long >( m_vgui_screen_shadow_skip.skipped_count( ) ) );
				break;
			case fix_t::shadow_depth_reuse:
				std::snprintf( detail, sizeof( detail ), "%llu rendered, %llu reused",
				               static_cast< unsigned long long >( m_shadow_depth_reuse.rendered_count( ) ),
				               static_cast< unsigned long long >( m_shadow_depth_reuse.reused_count( ) ) );
				break;
			case fix_t::shadow_depth_fps: {
				int cached = 0;
				for( const auto& view : m_shadow_depth_reuse.views( ) )
					cached += view.m_frame >= 0;
				std::snprintf( detail, sizeof( detail ), "%d lights cached", cached );
				break;
			}
			case fix_t::partner_view_fps:
				std::snprintf( detail, sizeof( detail ), "%llu rendered, %llu reused",
				               static_cast< unsigned long long >( m_partner_view.rendered_count( ) ),
				               static_cast< unsigned long long >( m_partner_view.reused_count( ) ) );
				break;
			case fix_t::vgui_screen_fps:
				std::snprintf( detail, sizeof( detail ), "%llu painted, %llu reused",
				               static_cast< unsigned long long >( m_vgui_screen_cache.painted_count( ) ),
				               static_cast< unsigned long long >( m_vgui_screen_cache.reused_count( ) ) );
				break;
			case fix_t::frame_queue_depth:
				std::snprintf( detail, sizeof( detail ), "%llu ring frames, %llu drains",
				               static_cast< unsigned long long >( m_queued_frames.ring_frames( ) ),
				               static_cast< unsigned long long >( m_queued_frames.drained_count( ) ) );
				break;
			default:
				break;
			}
		}

		console_print( "[p2boost]   %-18s %4d (default %d)%s  %s%s%s\n", info.m_name, fix( which ), info.m_default,
		               fix_available( which ) ? "" : " unavailable", detail, detail[ 0 ] ? " - " : "", info.m_help );
	}

	// diagnostic for shadow_depth_fps: which lights the depth cache holds.
	void Plugin::print_depth_views( ) const {
		for( const auto& view : m_shadow_depth_reuse.views( ) ) {
			if( view.m_frame < 0 )
				continue;
			console_print( "[p2boost]     light origin %.1f %.1f %.1f angles %.1f %.1f %.1f fov %.1f frame %d\n",
			               view.m_origin[ 0 ], view.m_origin[ 1 ], view.m_origin[ 2 ],
			               view.m_angles[ 0 ], view.m_angles[ 1 ], view.m_angles[ 2 ],
			               view.m_fov, view.m_frame );
		}
	}

	// select the full profiler, compact FPS counter, or hidden mode.
	sdk::PluginResult Plugin::overlay_command( const sdk::CCommand& args ) {
		profiler_mode_t mode{ };
		if( !parse_profiler_mode( args, mode ) )
			console_print( "usage: p2boost_overlay 0|1|2\n" );
		else
			m_overlay.set_mode( mode, m_metrics );

		if( !m_overlay.available( ) )
			console_print( "[p2boost] profiler unavailable: local client paint interfaces are missing\n" );

		return sdk::Stop;
	}

	// record frame times; stopping writes them beside the game executable.
	sdk::PluginResult Plugin::trace_command( const sdk::CCommand& args ) {
		bool enabled{ };
		if( !parse_toggle( args, enabled ) ) {
			console_print( "usage: p2boost_trace 0|1\n" );
			return sdk::Stop;
		}

		m_metrics.set_tracing( enabled );
		if( enabled ) {
			console_print( "[p2boost] tracing frames; p2boost_trace 0 writes p2boost_metrics.csv\n" );
			return sdk::Stop;
		}

		const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
		console_print( "[p2boost] metrics %s: %s\n", m_metrics.dump( path ) ? "written" : "write failed", path.c_str( ) );
		return sdk::Stop;
	}

	// download and install the latest p2boost release in the background.
	sdk::PluginResult Plugin::update_command( const sdk::CCommand& args ) {
		const bool force = args.ArgC( ) == 2 && _stricmp( args.Arg( 1 ), "force" ) == 0;
		if( args.ArgC( ) > 2 || ( args.ArgC( ) == 2 && !force ) ) {
			console_print( "usage: p2boost_update [force]\n" );
			return sdk::Stop;
		}

		if( m_auto_update.busy( ) ) {
			console_print( "[p2boost] an update or update check is already running\n" );
			return sdk::Stop;
		}

		m_auto_update.update( force );
		return sdk::Stop;
	}

	// rebuild optional flat companions when the visual mode changes.
	sdk::PluginResult Plugin::flat_textures_command( const sdk::CCommand& args ) {
		bool enabled{ };
		if( !parse_toggle( args, enabled ) ) {
			console_print( "usage: p2boost_flat_textures 0|1\n" );
			return sdk::Stop;
		}

		if( !m_flat_textures.set_enabled( enabled ) )
			console_print( "[p2boost] flat textures unavailable on this renderer/build\n" );
		else
			console_print( "[p2boost] flat textures %s; color maps are solid, normals are neutral, and UI/dynamic inputs are preserved\n",
			               enabled ? "enabled" : "disabled" );

		return sdk::Stop;
	}

	// set or print the Quake-style final-frame RGB brightness multiplier.
	sdk::PluginResult Plugin::intensity_command( const sdk::CCommand& args ) {
		if( args.ArgC( ) == 1 ) {
			console_print( "[p2boost] r_intensity = %g (0.0 to 2.0, 1.0 neutral)\n", m_display_adjustments.intensity( ) );
			return sdk::Stop;
		}

		char*       end{ };
		const float value = args.ArgC( ) == 2 ? std::strtof( args.Arg( 1 ), &end ) : -1.0f;
		if( args.ArgC( ) != 2 || end == args.Arg( 1 ) || *end != '\0' || value != value || value < min_intensity || value > max_intensity ) {
			console_print( "usage: r_intensity <0.0..2.0>\n" );
			return sdk::Stop;
		}

		m_display_adjustments.set_intensity( value );
		console_print( "[p2boost] r_intensity = %g\n", m_display_adjustments.intensity( ) );
		return sdk::Stop;
	}

}
