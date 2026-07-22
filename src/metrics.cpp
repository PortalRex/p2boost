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

	void Metrics::frame( bool simulating ) noexcept {
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
			m_pending_reacquire_reason,
			simulating,
			focused,
			m_coop };
		// counters below belong only to the frame that was just committed.
		m_pending_reacquire_ms		  = 0.0;
		m_pending_reacquire_begin_qpc = 0;
		m_pending_reacquire_end_qpc	  = 0;
		m_pending_reacquire_stages_ms.fill( 0.0 );
		m_pending_portal_render_ms			   = 0.0;
		m_pending_portal_render_count		   = 0;
		m_pending_shadow_update_ms			   = 0.0;
		m_pending_shadow_update_count		   = 0;
		m_pending_shadow_same_projection_count = 0;
		m_pending_shadow_same_full_count	   = 0;
		m_pending_shadow_skipped_count		   = 0;
		m_pending_reacquire_reason			   = 0;
		m_cursor							   = ( m_cursor + 1 ) % capacity;
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
		size_t								  frame_count = 0;
		double								  frame_total = 0.0;
		const size_t						  newest	  = ( m_cursor + capacity - 1 ) % capacity;
		const auto&							  latest	  = m_samples[ newest ];
		result.m_frame_ms								  = latest.m_delta_ms;
		result.m_focused								  = latest.m_focused;
		result.m_coop									  = latest.m_coop;

		const Qpc::tick_t cutoff = latest.m_qpc - Qpc::ticks( 1000.0 );
		for( size_t offset = 0; offset < m_count && frame_count < window_capacity; ++offset ) {
			const auto& sample = m_samples[ ( newest + capacity - offset ) % capacity ];
			if( sample.m_qpc < cutoff )
				break;
			if( sample.m_delta_ms > 0.0 && sample.m_simulating ) {
				frame_times[ frame_count++ ] = sample.m_delta_ms;
				frame_total += sample.m_delta_ms;
			}
			result.m_portal_ms += sample.m_portal_render_ms;
			result.m_portal_count += sample.m_portal_render_count;
			result.m_render_worker_ms += sample.m_render_worker_ms;
			result.m_shadow_skips += sample.m_shadow_skipped_count;
			if( sample.m_reacquire_ms > 0.0 )
				++result.m_focus_events;
		}

		if( frame_count == 0 )
			return result;
		std::sort( frame_times.begin( ), frame_times.begin( ) + frame_count );
		result.m_average_ms = frame_total / static_cast< double >( frame_count );
		result.m_p99_ms		= frame_times[ ( frame_count - 1 ) * 99 / 100 ];
		result.m_max_ms		= frame_times[ frame_count - 1 ];
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
				  "bytes,render_worker_index_bytes,reacquire_reason,simulating,focused,coop\n"
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
				   << sample.m_render_worker_vertex_bytes << ',' << sample.m_render_worker_index_bytes << ',' << sample.m_reacquire_reason
				   << ',' << ( sample.m_simulating ? 1 : 0 ) << ',' << ( sample.m_focused ? 1 : 0 ) << ',' << ( sample.m_coop ? 1 : 0 )
				   << '\n';
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
					  << ",draw_count,draw_ms,max_draw_ms,max_draw_method,max_draw_primitive_type,max_draw_"
						 "primitive_count,max_draw_vertex_count,max_draw_vertex_shader,max_draw_pixel_shader,max_"
						 "draw_texture_0,max_draw_texture_1,max_draw_texture_2,max_draw_texture_3,max_draw_first_"
						 "vertex_shader,max_draw_first_pixel_shader,max_draw_first_pipeline,max_draw_first_"
						 "texture_mask,max_draw_texture0_type,max_draw_texture0_format,max_draw_texture0_width,"
						 "max_draw_texture0_height,max_draw_texture0_usage,max_draw_texture0_pool,max_draw_"
						 "texture0_levels,max_draw_texture0_hash,draw_stack_0,draw_stack_1,draw_stack_2,draw_"
						 "stack_3,draw_stack_4,draw_stack_5,draw_stack_6,draw_stack_7,draw_stack_8,draw_stack_9,"
						 "draw_stack_10,draw_stack_11,draw_stack_12,draw_stack_13,draw_stack_14,draw_stack_15"
					  << ",command_key_0,command_count_0,command_key_1,command_count_1,command_key_2,command_"
						 "count_2,command_key_3,command_count_3,command_key_4,command_count_4,command_key_5,"
						 "command_count_5,command_key_6,command_count_6,command_key_7,command_count_7,command_"
						 "key_8,command_count_8,command_key_9,command_count_9,command_key_10,command_count_10,"
						 "command_key_11,command_count_11,command_key_12,command_count_12,command_key_13,command_"
						 "count_13,command_key_14,command_count_14,command_key_15,command_count_15"
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
							  << Qpc::milliseconds( sample.m_max_draw_qpc ) << ',' << sample.m_max_draw_method << ','
							  << sample.m_max_draw_primitive_type << ',' << sample.m_max_draw_primitive_count << ','
							  << sample.m_max_draw_vertex_count;
				worker_output << ",0x" << std::hex << sample.m_max_draw_vertex_shader << ",0x" << sample.m_max_draw_pixel_shader;
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
				for( size_t command = 0; command < sample.m_command_keys.size( ); ++command )
					worker_output << ",0x" << std::hex << sample.m_command_keys[ command ] << std::dec << ','
								  << sample.m_command_counts[ command ];
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
