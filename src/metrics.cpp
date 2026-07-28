#include "metrics.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>

namespace p2boost {

	void Metrics::record_reacquire( double milliseconds, const char* reason, Qpc::tick_t begin_qpc, Qpc::tick_t end_qpc ) noexcept {
		if( !collecting( ) )
			return;
		// merge nested restore work into the current frame sample.
		m_pending_reacquire_ms += milliseconds;
		if( m_pending_reacquire_begin_qpc == 0 || begin_qpc < m_pending_reacquire_begin_qpc )
			m_pending_reacquire_begin_qpc = begin_qpc;
		if( end_qpc > m_pending_reacquire_end_qpc )
			m_pending_reacquire_end_qpc = end_qpc;
		m_pending_reacquire_reason = reason && std::strcmp( reason, "NeedsReset" ) == 0 ? 1 : 2;
	}

	void Metrics::record_reacquire_stage( size_t stage, double milliseconds ) noexcept {
		if( !collecting( ) || stage >= m_pending_reacquire_stages_ms.size( ) )
			return;
		m_pending_reacquire_stages_ms[ stage ] += milliseconds;
	}

	void Metrics::record_portal_render( double milliseconds ) noexcept {
		if( !collecting( ) )
			return;
		m_pending_portal_render_ms += milliseconds;
		++m_pending_portal_render_count;
	}

	void Metrics::record_shadow_update( double milliseconds, bool same_projection, bool same_full, bool skipped ) noexcept {
		if( !collecting( ) )
			return;
		m_pending_shadow_update_ms += milliseconds;
		++m_pending_shadow_update_count;
		if( same_projection )
			++m_pending_shadow_same_projection_count;
		if( same_full )
			++m_pending_shadow_same_full_count;
		if( skipped )
			++m_pending_shadow_skipped_count;
	}

	void Metrics::record_render_worker( RenderWorkerMetrics sample ) noexcept {
		if( !collecting( ) )
			return;
		// render workers can finish off-thread, so their ring is synchronized.
		std::lock_guard< std::mutex > lock( m_render_worker_mutex );
		if( m_pending_render_worker_begin_qpc == 0 || sample.m_begin_qpc < m_pending_render_worker_begin_qpc )
			m_pending_render_worker_begin_qpc = sample.m_begin_qpc;
		if( sample.m_end_qpc > m_pending_render_worker_end_qpc )
			m_pending_render_worker_end_qpc = sample.m_end_qpc;
		m_pending_render_worker_qpc += sample.m_end_qpc - sample.m_begin_qpc;
		m_pending_render_worker_cycles += sample.m_cycles;
		++m_pending_render_worker_count;
		m_pending_render_worker_queued_calls += sample.m_queued_calls;
		m_pending_render_worker_parameter_bytes += sample.m_parameter_bytes;
		m_pending_render_worker_vertex_bytes += sample.m_vertex_bytes;
		m_pending_render_worker_index_bytes += sample.m_index_bytes;
		sample.m_thread_id								  = GetCurrentThreadId( );
		m_render_worker_samples[ m_render_worker_cursor ] = sample;
		m_render_worker_cursor							  = ( m_render_worker_cursor + 1 ) % capacity;
		if( m_render_worker_count < capacity )
			++m_render_worker_count;
	}

	void Metrics::record_world_surface( double milliseconds, unsigned int flags ) noexcept {
		if( !collecting( ) )
			return;

		// The world builder executes on the main thread, so its frame-local
		// counters need no lock. Preserve the union of view flags for correlation.
		m_pending_world_surface_ms += milliseconds;
		++m_pending_world_surface_count;
		m_pending_world_surface_flags |= flags;
	}

	void Metrics::record_pvs_decompress(
		double milliseconds, std::uintptr_t model, int cluster, int type, size_t bytes, std::uint64_t hash ) noexcept {
		if( !tracing( ) )
			return;

		m_pending_pvs_decompress_ms += milliseconds;
		++m_pending_pvs_decompress_count;

		// A repeated exact key in one rendered frame is the only candidate that
		// could safely reuse decompressed bytes. The output hash proves equality.
		for( size_t index = 0; index < m_pvs_observation_count; ++index ) {
			const auto& previous = m_pvs_observations[ index ];
			if( previous.m_model == model && previous.m_cluster == cluster && previous.m_type == type && previous.m_bytes == bytes ) {
				++m_pending_pvs_same_input_count;
				if( previous.m_hash == hash )
					++m_pending_pvs_same_output_count;
				return;
			}
		}

		if( m_pvs_observation_count < m_pvs_observations.size( ) )
			m_pvs_observations[ m_pvs_observation_count++ ] = { model, cluster, type, bytes, hash };
	}

	void Metrics::record_portal_mesh( double milliseconds, unsigned int portals ) noexcept {
		if( !collecting( ) )
			return;

		m_pending_portal_mesh_ms += milliseconds;
		++m_pending_portal_mesh_count;
		m_pending_portal_mesh_portals += portals;
	}

	void Metrics::record_portal_movement_spatial( double milliseconds ) noexcept {
		if( !collecting( ) )
			return;

		m_pending_portal_movement_spatial_ms += milliseconds;
		++m_pending_portal_movement_spatial_count;
	}

	void Metrics::record_portal_laser( double milliseconds ) noexcept {
		if( !collecting( ) )
			return;

		m_pending_portal_laser_ms += milliseconds;
		++m_pending_portal_laser_count;
	}

	void Metrics::record_animation_events( double milliseconds ) noexcept {
		if( collecting( ) ) {
			m_pending_animation_events_ms += milliseconds;
			++m_pending_animation_events_count;
		}
	}

	void Metrics::record_sound_lookup( double milliseconds ) noexcept {
		if( collecting( ) ) {
			m_pending_sound_lookup_ms += milliseconds;
			++m_pending_sound_lookup_count;
		}
	}

	void Metrics::record_read_ubit_var( double milliseconds ) noexcept {
		if( collecting( ) ) {
			m_pending_read_ubit_var_ms += milliseconds;
			++m_pending_read_ubit_var_count;
		}
	}

	void Metrics::frame( ) noexcept {
		if( !collecting( ) )
			return;
		const Qpc::tick_t now				 = Qpc::now( );
		const double	  delta				 = m_last_qpc == 0 ? 0.0 : Qpc::milliseconds( now - m_last_qpc );
		DWORD			  foreground_process = 0;
		GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
		const bool		   focused						 = foreground_process == GetCurrentProcessId( );
		long long		   render_worker_begin_qpc		 = 0;
		long long		   render_worker_end_qpc		 = 0;
		long long		   render_worker_qpc			 = 0;
		unsigned long long render_worker_cycles			 = 0;
		unsigned int	   render_worker_count			 = 0;
		unsigned long long render_worker_queued_calls	 = 0;
		unsigned long long render_worker_parameter_bytes = 0;
		unsigned long long render_worker_vertex_bytes	 = 0;
		unsigned long long render_worker_index_bytes	 = 0;
		// snapshot and clear worker totals while producers are excluded.
		{
			std::lock_guard< std::mutex > lock( m_render_worker_mutex );
			render_worker_begin_qpc					= m_pending_render_worker_begin_qpc;
			render_worker_end_qpc					= m_pending_render_worker_end_qpc;
			render_worker_qpc						= m_pending_render_worker_qpc;
			render_worker_cycles					= m_pending_render_worker_cycles;
			render_worker_count						= m_pending_render_worker_count;
			render_worker_queued_calls				= m_pending_render_worker_queued_calls;
			render_worker_parameter_bytes			= m_pending_render_worker_parameter_bytes;
			render_worker_vertex_bytes				= m_pending_render_worker_vertex_bytes;
			render_worker_index_bytes				= m_pending_render_worker_index_bytes;
			m_pending_render_worker_begin_qpc		= 0;
			m_pending_render_worker_end_qpc			= 0;
			m_pending_render_worker_qpc				= 0;
			m_pending_render_worker_cycles			= 0;
			m_pending_render_worker_count			= 0;
			m_pending_render_worker_queued_calls	= 0;
			m_pending_render_worker_parameter_bytes = 0;
			m_pending_render_worker_vertex_bytes	= 0;
			m_pending_render_worker_index_bytes		= 0;
		}
		// commit all pending counters as one chronological frame record.
		m_samples[ m_cursor ] = { now,
			delta,
			m_pending_reacquire_begin_qpc,
			m_pending_reacquire_end_qpc,
			m_pending_reacquire_ms,
			m_pending_reacquire_stages_ms,
			m_pending_portal_render_ms,
			m_pending_portal_render_count,
			m_pending_shadow_update_ms,
			m_pending_shadow_update_count,
			m_pending_shadow_same_projection_count,
			m_pending_shadow_same_full_count,
			m_pending_shadow_skipped_count,
			render_worker_begin_qpc,
			render_worker_end_qpc,
			Qpc::milliseconds( render_worker_qpc ),
			render_worker_cycles,
			render_worker_count,
			render_worker_queued_calls,
			render_worker_parameter_bytes,
			render_worker_vertex_bytes,
			render_worker_index_bytes,
			m_pending_world_surface_ms,
			m_pending_world_surface_count,
			m_pending_world_surface_flags,
			m_pending_pvs_decompress_ms,
			m_pending_pvs_decompress_count,
			m_pending_pvs_same_input_count,
			m_pending_pvs_same_output_count,
			m_pending_portal_mesh_ms,
			m_pending_portal_mesh_count,
			m_pending_portal_mesh_portals,
			m_pending_portal_movement_spatial_ms,
			m_pending_portal_movement_spatial_count,
			m_pending_portal_laser_ms,
			m_pending_portal_laser_count,
			m_pending_animation_events_ms,
			m_pending_animation_events_count,
			m_pending_sound_lookup_ms,
			m_pending_sound_lookup_count,
			m_pending_read_ubit_var_ms,
			m_pending_read_ubit_var_count,
			m_pending_reacquire_reason,
			m_simulating.load( std::memory_order_relaxed ),
			focused,
			m_coop };
		// counters below belong only to the frame that was just committed.
		m_pending_reacquire_ms		  = 0.0;
		m_pending_reacquire_begin_qpc = 0;
		m_pending_reacquire_end_qpc	  = 0;
		m_pending_reacquire_stages_ms.fill( 0.0 );
		m_pending_portal_render_ms				= 0.0;
		m_pending_portal_render_count			= 0;
		m_pending_shadow_update_ms				= 0.0;
		m_pending_shadow_update_count			= 0;
		m_pending_shadow_same_projection_count	= 0;
		m_pending_shadow_same_full_count		= 0;
		m_pending_shadow_skipped_count			= 0;
		m_pending_world_surface_ms				= 0.0;
		m_pending_world_surface_count			= 0;
		m_pending_world_surface_flags			= 0;
		m_pending_pvs_decompress_ms				= 0.0;
		m_pending_pvs_decompress_count			= 0;
		m_pending_pvs_same_input_count			= 0;
		m_pending_pvs_same_output_count			= 0;
		m_pvs_observation_count					= 0;
		m_pending_portal_mesh_ms				= 0.0;
		m_pending_portal_mesh_count				= 0;
		m_pending_portal_mesh_portals			= 0;
		m_pending_portal_movement_spatial_ms	= 0.0;
		m_pending_portal_movement_spatial_count = 0;
		m_pending_portal_laser_ms				= 0.0;
		m_pending_portal_laser_count			= 0;
		m_pending_animation_events_ms			= 0.0;
		m_pending_animation_events_count		= 0;
		m_pending_sound_lookup_ms				= 0.0;
		m_pending_sound_lookup_count			= 0;
		m_pending_read_ubit_var_ms				= 0.0;
		m_pending_read_ubit_var_count			= 0;
		m_pending_reacquire_reason				= 0;
		m_cursor								= ( m_cursor + 1 ) % capacity;
		if( m_count < capacity )
			++m_count;
		m_last_qpc = now;
	}

	OverlayMetrics Metrics::overlay_metrics( ) const noexcept {
		OverlayMetrics result{ };
		if( m_count == 0 )
			return result;

		// One second covers a full high-refresh pacing window without allowing
		// old map-load spikes to remain on screen indefinitely.
		constexpr size_t					  window_capacity = 1024;
		std::array< double, window_capacity > frame_times{ };
		std::array< double, window_capacity > render_tail_times{ };
		size_t								  frame_count		= 0;
		size_t								  render_tail_count = 0;
		double								  frame_total		= 0.0;
		const size_t						  newest			= ( m_cursor + capacity - 1 ) % capacity;
		const auto&							  latest			= m_samples[ newest ];
		result.m_frame_ms										= latest.m_delta_ms;
		result.m_focused										= latest.m_focused;
		result.m_coop											= latest.m_coop;

		const Qpc::tick_t cutoff = latest.m_qpc - Qpc::ticks( 1000.0 );
		for( size_t offset = 0; offset < m_count && frame_count < window_capacity; ++offset ) {
			const auto& sample = m_samples[ ( newest + capacity - offset ) % capacity ];
			if( sample.m_qpc < cutoff )
				break;
			if( sample.m_delta_ms > 0.0 && sample.m_simulating ) {
				frame_times[ frame_count++ ] = sample.m_delta_ms;
				frame_total += sample.m_delta_ms;
				result.m_portal_ms += sample.m_portal_render_ms;
				result.m_portal_count += sample.m_portal_render_count;
				result.m_render_worker_ms += sample.m_render_worker_ms;
				result.m_render_worker_count += sample.m_render_worker_count;
				result.m_shadow_skips += sample.m_shadow_skipped_count;

				// A completed worker normally belongs to the interval ending at
				// this frame marker. The remainder identifies main-thread work
				// that another render worker cannot hide.
				if( sample.m_render_worker_end_qpc > 0 && sample.m_render_worker_end_qpc <= sample.m_qpc ) {
					const double render_tail = Qpc::milliseconds( sample.m_qpc - sample.m_render_worker_end_qpc );
					if( render_tail <= sample.m_delta_ms )
						render_tail_times[ render_tail_count++ ] = render_tail;
				}
				// Preserve a short newest-to-oldest graph independently of the
				// percentile array, which is sorted below.
				if( result.m_graph_count < result.m_graph_ms.size( ) )
					result.m_graph_ms[ result.m_graph_count++ ] = sample.m_delta_ms;
			}
			if( sample.m_reacquire_ms > 0.0 )
				++result.m_focus_events;
		}

		if( frame_count == 0 )
			return result;
		std::sort( frame_times.begin( ), frame_times.begin( ) + frame_count );
		result.m_average_ms			   = frame_total / static_cast< double >( frame_count );
		result.m_p99_ms				   = frame_times[ ( frame_count - 1 ) * 99 / 100 ];
		result.m_max_ms				   = frame_times[ frame_count - 1 ];
		result.m_render_worker_percent = frame_total > 0.0 ? result.m_render_worker_ms * 100.0 / frame_total : 0.0;
		if( render_tail_count > 0 ) {
			std::sort( render_tail_times.begin( ), render_tail_times.begin( ) + render_tail_count );
			result.m_render_tail_p99_ms = render_tail_times[ ( render_tail_count - 1 ) * 99 / 100 ];
		}
		return result;
	}

	bool Metrics::dump( const std::string& path ) const {
		// frame samples are written oldest-first even after the ring wraps.
		std::ofstream output( path, std::ios::trunc );
		if( !output )
			return false;
		output << "qpc,delta_ms,reacquire_begin_qpc,reacquire_end_qpc,reacquire_ms,shader_device_ms,"
				  "frame_sync_ms,resource_callbacks_ms,"
				  "portal_render_ms,portal_render_count,shadow_update_ms,shadow_update_count,shadow_"
				  "same_projection_count,shadow_same_full_count,shadow_skipped_count,render_worker_"
				  "begin_qpc,render_worker_end_qpc,render_worker_ms,render_worker_cycles,render_worker_"
				  "count,render_worker_queued_calls,render_worker_parameter_bytes,render_worker_vertex_"
				  "bytes,render_worker_index_bytes,world_surface_ms,world_surface_count,world_surface_flags,pvs_decompress_ms,"
				  "pvs_decompress_count,pvs_same_input_count,pvs_same_output_count,portal_mesh_ms,"
				  "portal_mesh_count,portal_mesh_portals,portal_movement_spatial_ms,portal_movement_spatial_count,"
				  "portal_laser_ms,portal_laser_count,"
				  "animation_events_ms,animation_events_count,sound_lookup_ms,sound_lookup_count,read_ubit_var_ms,"
				  "read_ubit_var_count,"
				  "reacquire_reason,simulating,focused,coop\n"
			   << std::fixed << std::setprecision( 6 );
		const size_t start = m_count == capacity ? m_cursor : 0;
		for( size_t index = 0; index < m_count; ++index ) {
			const auto& sample = m_samples[ ( start + index ) % capacity ];
			output << sample.m_qpc << ',' << sample.m_delta_ms << ',' << sample.m_reacquire_begin_qpc << ',' << sample.m_reacquire_end_qpc
				   << ',' << sample.m_reacquire_ms << ',' << sample.m_reacquire_stages_ms[ 0 ] << ',' << sample.m_reacquire_stages_ms[ 1 ]
				   << ',' << sample.m_reacquire_stages_ms[ 2 ] << ',' << sample.m_portal_render_ms << ',' << sample.m_portal_render_count
				   << ',' << sample.m_shadow_update_ms << ',' << sample.m_shadow_update_count << ','
				   << sample.m_shadow_same_projection_count << ',' << sample.m_shadow_same_full_count << ','
				   << sample.m_shadow_skipped_count << ',' << sample.m_render_worker_begin_qpc << ',' << sample.m_render_worker_end_qpc
				   << ',' << sample.m_render_worker_ms << ',' << sample.m_render_worker_cycles << ',' << sample.m_render_worker_count << ','
				   << sample.m_render_worker_queued_calls << ',' << sample.m_render_worker_parameter_bytes << ','
				   << sample.m_render_worker_vertex_bytes << ',' << sample.m_render_worker_index_bytes << ',' << sample.m_world_surface_ms
				   << ',' << sample.m_world_surface_count << ",0x" << std::hex << sample.m_world_surface_flags << std::dec << ','
				   << sample.m_pvs_decompress_ms << ',' << sample.m_pvs_decompress_count << ',' << sample.m_pvs_same_input_count << ','
				   << sample.m_pvs_same_output_count << ',' << sample.m_portal_mesh_ms << ',' << sample.m_portal_mesh_count << ','
				   << sample.m_portal_mesh_portals << ',' << sample.m_portal_movement_spatial_ms << ','
				   << sample.m_portal_movement_spatial_count << ',' << sample.m_portal_laser_ms << ',' << sample.m_portal_laser_count << ','
				   << sample.m_animation_events_ms << ',' << sample.m_animation_events_count << ',' << sample.m_sound_lookup_ms << ','
				   << sample.m_sound_lookup_count << ',' << sample.m_read_ubit_var_ms << ',' << sample.m_read_ubit_var_count << ','
				   << sample.m_reacquire_reason << ',' << ( sample.m_simulating ? 1 : 0 ) << ',' << ( sample.m_focused ? 1 : 0 ) << ','
				   << ( sample.m_coop ? 1 : 0 ) << '\n';
		}
		const bool frame_output_good = output.good( );
		output.close( );
		// place detailed worker records beside the requested frame CSV.
		const size_t	  separator	  = path.find_last_of( "/\\" );
		const size_t	  dot		  = path.find_last_of( '.' );
		const std::string worker_path = dot != std::string::npos && ( separator == std::string::npos || dot > separator )
											? path.substr( 0, dot ) + "_render_workers" + path.substr( dot )
											: path + "_render_workers.csv";
		std::ofstream	  worker_output( worker_path, std::ios::trunc );
		if( !worker_output )
			return false;
		worker_output << "begin_qpc,end_qpc,wall_ms,cycles,queued_calls,parameter_bytes,vertex_bytes,index_bytes,"
						 "hardware_sync_count,hardware_sync_ms,max_hardware_sync_ms,hardware_sync_cycles,sync_"
						 "index_before,sync_pending_before,sync_pending_after,present_count,present_ms,max_"
						 "present_ms"
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
					  << ",draw_count,draw_ms,first_resource_draw_count,first_resource_draw_ms,first_pipeline_draw_"
						 "count,first_pipeline_draw_ms,first_texture_count,index_buffer_lock_count,index_buffer_lock_ms,max_index_"
						 "buffer_lock_ms,vertex_buffer_lock_count,vertex_buffer_lock_ms,max_vertex_buffer_lock_ms,occlusion_query_"
						 "create_count,occlusion_query_create_ms,max_occlusion_query_create_ms,occlusion_query_destroy_count,"
						 "occlusion_query_destroy_ms,max_occlusion_query_destroy_ms,texture_create_count,texture_create_ms,"
						 "max_texture_create_ms,vertex_shader_create_count,vertex_shader_create_ms,max_vertex_shader_create_ms,"
						 "pixel_shader_create_count,pixel_shader_create_ms,max_pixel_shader_create_ms,max_draw_ms,max_"
						 "draw_method,max_draw_primitive_type,max_draw_"
						 "primitive_count,max_draw_vertex_count,max_draw_vertex_shader,max_draw_pixel_shader,max_"
						 "draw_vertex_shader_hash,max_draw_pixel_shader_hash,max_"
						 "draw_texture_0,max_draw_texture_1,max_draw_texture_2,max_draw_texture_3,max_draw_first_"
						 "vertex_shader,max_draw_first_pixel_shader,max_draw_first_pipeline,max_draw_first_"
						 "texture_mask,max_draw_texture0_type,max_draw_texture0_format,max_draw_texture0_width,"
						 "max_draw_texture0_height,max_draw_texture0_usage,max_draw_texture0_pool,max_draw_"
						 "texture0_levels,max_draw_texture0_hash,draw_stack_0,draw_stack_1,draw_stack_2,draw_"
						 "stack_3,draw_stack_4,draw_stack_5,draw_stack_6,draw_stack_7,draw_stack_8,draw_stack_9,"
						 "draw_stack_10,draw_stack_11,draw_stack_12,draw_stack_13,draw_stack_14,draw_stack_15"
					  << ",command_execute_count,command_execute_ms,slow_command_key,slow_command_ms,command_key_0,"
						 "command_count_0,command_ms_0,command_key_1,command_count_1,command_ms_1,command_key_2,"
						 "command_count_2,command_ms_2,command_key_3,command_count_3,command_ms_3,command_key_4,"
						 "command_count_4,command_ms_4,command_key_5,command_count_5,command_ms_5,command_key_6,"
						 "command_count_6,command_ms_6,command_key_7,command_count_7,command_ms_7,command_key_8,"
						 "command_count_8,command_ms_8,command_key_9,command_count_9,command_ms_9,command_key_10,"
						 "command_count_10,command_ms_10,command_key_11,command_count_11,command_ms_11,command_key_12,"
						 "command_count_12,command_ms_12,command_key_13,command_count_13,command_ms_13,command_key_14,"
						 "command_count_14,command_ms_14,command_key_15,command_count_15,command_ms_15"
					  << ",dynamic_light_count,dynamic_light_ms,dynamic_light_unique_lights,dynamic_light_unique_"
						 "surfaces,dynamic_light_ptr,dynamic_light_surface_ptr,dynamic_light_update_flags,"
						 "dynamic_light_bool_a,dynamic_light_bool_b,dynamic_light_flags,dynamic_light_origin_x,"
						 "dynamic_light_origin_y,dynamic_light_origin_z,dynamic_light_radius,dynamic_light_color,"
						 "dynamic_light_die,dynamic_light_decay,dynamic_light_minlight,dynamic_light_key,dynamic_"
						 "light_style"
#endif
					  << ",thread_id\n"
					  << std::fixed << std::setprecision( 6 );
		// hold the producer lock while copying the worker ring to disk.
		{
			std::lock_guard< std::mutex > lock( m_render_worker_mutex );
			const size_t				  worker_start = m_render_worker_count == capacity ? m_render_worker_cursor : 0;
			for( size_t index = 0; index < m_render_worker_count; ++index ) {
				const auto& sample = m_render_worker_samples[ ( worker_start + index ) % capacity ];
				worker_output << sample.m_begin_qpc << ',' << sample.m_end_qpc << ','
							  << Qpc::milliseconds( sample.m_end_qpc - sample.m_begin_qpc ) << ',' << sample.m_cycles << ','
							  << sample.m_queued_calls << ',' << sample.m_parameter_bytes << ',' << sample.m_vertex_bytes << ','
							  << sample.m_index_bytes << ',' << sample.m_hardware_sync_count << ','
							  << Qpc::milliseconds( sample.m_hardware_sync_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_hardware_sync_qpc ) << ',' << sample.m_hardware_sync_cycles << ','
							  << sample.m_sync_index_before << ',' << sample.m_sync_pending_before << ',' << sample.m_sync_pending_after
							  << ',' << sample.m_present_count << ',' << Qpc::milliseconds( sample.m_present_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_present_qpc );
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
				worker_output << ',' << sample.m_draw_count << ',' << Qpc::milliseconds( sample.m_draw_qpc ) << ','
							  << sample.m_first_resource_draw_count << ',' << Qpc::milliseconds( sample.m_first_resource_draw_qpc ) << ','
							  << sample.m_first_pipeline_draw_count << ',' << Qpc::milliseconds( sample.m_first_pipeline_draw_qpc ) << ','
							  << sample.m_first_texture_count << ',' << sample.m_index_buffer_lock_count << ','
							  << Qpc::milliseconds( sample.m_index_buffer_lock_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_index_buffer_lock_qpc ) << ',' << sample.m_vertex_buffer_lock_count << ','
							  << Qpc::milliseconds( sample.m_vertex_buffer_lock_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_vertex_buffer_lock_qpc ) << ',' << sample.m_occlusion_query_create_count
							  << ',' << Qpc::milliseconds( sample.m_occlusion_query_create_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_occlusion_query_create_qpc ) << ','
							  << sample.m_occlusion_query_destroy_count << ',' << Qpc::milliseconds( sample.m_occlusion_query_destroy_qpc )
							  << ',' << Qpc::milliseconds( sample.m_max_occlusion_query_destroy_qpc ) << ','
							  << sample.m_texture_create_count << ',' << Qpc::milliseconds( sample.m_texture_create_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_texture_create_qpc ) << ',' << sample.m_vertex_shader_create_count << ','
							  << Qpc::milliseconds( sample.m_vertex_shader_create_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_vertex_shader_create_qpc ) << ',' << sample.m_pixel_shader_create_count
							  << ',' << Qpc::milliseconds( sample.m_pixel_shader_create_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_pixel_shader_create_qpc ) << ','
							  << Qpc::milliseconds( sample.m_max_draw_qpc ) << ',' << sample.m_max_draw_method << ','
							  << sample.m_max_draw_primitive_type << ',' << sample.m_max_draw_primitive_count << ','
							  << sample.m_max_draw_vertex_count;
				worker_output << ",0x" << std::hex << sample.m_max_draw_vertex_shader << ",0x" << sample.m_max_draw_pixel_shader << ",0x"
							  << sample.m_max_draw_vertex_shader_hash << ",0x" << sample.m_max_draw_pixel_shader_hash;
				for( const auto texture : sample.m_max_draw_textures )
					worker_output << ",0x" << texture;
				worker_output << std::dec << ',' << static_cast< unsigned int >( sample.m_max_draw_first_vertex_shader ) << ','
							  << static_cast< unsigned int >( sample.m_max_draw_first_pixel_shader ) << ','
							  << static_cast< unsigned int >( sample.m_max_draw_first_pipeline ) << ','
							  << static_cast< unsigned int >( sample.m_max_draw_first_texture_mask ) << ','
							  << sample.m_max_draw_texture0_type << ",0x" << std::hex << sample.m_max_draw_texture0_format << std::dec
							  << ',' << sample.m_max_draw_texture0_width << ',' << sample.m_max_draw_texture0_height << ",0x" << std::hex
							  << sample.m_max_draw_texture0_usage << std::dec << ',' << sample.m_max_draw_texture0_pool << ','
							  << sample.m_max_draw_texture0_levels << ",0x" << std::hex << sample.m_max_draw_texture0_hash << std::dec;
				for( const auto address : sample.m_max_draw_stack )
					worker_output << ",0x" << std::hex << address << std::dec;
				worker_output << ',' << sample.m_command_execute_count << ',' << Qpc::milliseconds( sample.m_command_execute_qpc ) << ",0x"
							  << std::hex << sample.m_slow_command_key << std::dec << ',' << Qpc::milliseconds( sample.m_slow_command_qpc );
				for( size_t command = 0; command < sample.m_command_keys.size( ); ++command )
					worker_output << ",0x" << std::hex << sample.m_command_keys[ command ] << std::dec << ','
								  << sample.m_command_counts[ command ] << ',' << Qpc::milliseconds( sample.m_command_qpc[ command ] );
				worker_output << ',' << sample.m_dynamic_light_count << ',' << Qpc::milliseconds( sample.m_dynamic_light_qpc ) << ','
							  << sample.m_dynamic_light_unique_lights << ',' << sample.m_dynamic_light_unique_surfaces;
				worker_output << ",0x" << std::hex << sample.m_dynamic_light_first_light << ",0x" << sample.m_dynamic_light_first_surface
							  << ",0x" << sample.m_dynamic_light_first_flags << std::dec << ','
							  << static_cast< unsigned int >( sample.m_dynamic_light_first_bool_a ) << ','
							  << static_cast< unsigned int >( sample.m_dynamic_light_first_bool_b );
				// light payloads are raw words so the capture preserves exact bits.
				const auto light_float = [ &sample ]( size_t word ) {
					float value = 0.0f;
					std::memcpy( &value, &sample.m_dynamic_light_words[ word ], sizeof( value ) );
					return value;
				};
				worker_output << ",0x" << std::hex << sample.m_dynamic_light_words[ 0 ] << std::dec << ',' << light_float( 1 ) << ','
							  << light_float( 2 ) << ',' << light_float( 3 ) << ',' << light_float( 4 ) << ",0x" << std::hex
							  << sample.m_dynamic_light_words[ 5 ] << std::dec << ',' << light_float( 6 ) << ',' << light_float( 7 ) << ','
							  << light_float( 8 ) << ',' << static_cast< std::int32_t >( sample.m_dynamic_light_words[ 9 ] ) << ','
							  << static_cast< std::int32_t >( sample.m_dynamic_light_words[ 10 ] );
#endif
				worker_output << ',' << sample.m_thread_id << '\n';
			}
		}
		return frame_output_good && worker_output.good( );
	}

}
