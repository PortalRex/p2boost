#include "frame_metrics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <vector>

namespace p2boost {
	namespace {

		double percentile( std::vector< double > values, double percentile_value ) {
			if( values.empty( ) )
				return 0.0;

			std::sort( values.begin( ), values.end( ) );
			const size_t index = static_cast< size_t >( std::ceil( percentile_value * static_cast< double >( values.size( ) ) ) - 1.0 );
			return values[ std::min( index, values.size( ) - 1 ) ];
		}

	}

	void FrameMetrics::set_tracing( bool tracing_enabled ) noexcept {
		m_tracing.store( tracing_enabled, std::memory_order_relaxed );
	}

	void FrameMetrics::record_frame( ) noexcept {
		// Leave diagnostics completely dormant unless the trace or overlay is on.
		// The first collected frame establishes a timestamp and is omitted.
		if( !collecting( ) ) {
			m_last_qpc = 0;
			return;
		}

		const Qpc::tick_t now = Qpc::now( );
		const Qpc::tick_t previous = m_last_qpc;
		m_last_qpc = now;
		if( previous == 0 )
			return;

		Sample sample;
		sample.m_qpc = now;
		sample.m_frame_ms = Qpc::milliseconds( now - previous );
		sample.m_simulating = m_simulating.load( std::memory_order_relaxed );
		sample.m_focused = m_focused.load( std::memory_order_relaxed );
		sample.m_coop = m_coop.load( std::memory_order_relaxed );

		{
			std::lock_guard< std::mutex > pending_lock( m_pending_mutex );
			sample.m_shadow_ms = m_pending_shadow_ms;
			sample.m_shadow_updates = m_pending_shadow_updates;
			sample.m_shadow_hits = m_pending_shadow_hits;
			m_pending_shadow_ms = 0.0;
			m_pending_shadow_updates = 0;
			m_pending_shadow_hits = 0;
		}

		std::lock_guard< std::mutex > sample_lock( m_samples_mutex );
		m_samples[ m_cursor ] = sample;
		m_cursor = ( m_cursor + 1 ) % capacity;
		m_count = std::min( m_count + 1, capacity );
	}

	void FrameMetrics::record_shadow_update( double milliseconds, bool cache_hit ) noexcept {
		if( !collecting( ) )
			return;

		std::lock_guard< std::mutex > lock( m_pending_mutex );
		m_pending_shadow_ms += milliseconds;
		++m_pending_shadow_updates;
		if( cache_hit )
			++m_pending_shadow_hits;
	}

	OverlayMetrics FrameMetrics::overlay_metrics( ) const noexcept {
		OverlayMetrics result;
		std::vector< Sample > samples;

		{
			std::lock_guard< std::mutex > lock( m_samples_mutex );
			if( m_count == 0 )
				return result;

			const size_t start = m_count == capacity ? m_cursor : 0;
			const Qpc::tick_t newest = m_samples[ ( m_cursor + capacity - 1 ) % capacity ].m_qpc;
			const Qpc::tick_t one_second = Qpc::ticks( 1000.0 );
			for( size_t index = 0; index < m_count; ++index ) {
				const Sample& sample = m_samples[ ( start + index ) % capacity ];
				if( newest - sample.m_qpc <= one_second )
					samples.push_back( sample );
			}
		}

		if( samples.empty( ) )
			return result;

		std::vector< double > frame_times;
		frame_times.reserve( samples.size( ) );
		for( const Sample& sample : samples ) {
			frame_times.push_back( sample.m_frame_ms );
			result.m_shadow_ms += sample.m_shadow_ms;
			result.m_shadow_updates += sample.m_shadow_updates;
			result.m_shadow_hits += sample.m_shadow_hits;
			if( sample.m_frame_ms >= 8.3 )
				++result.m_hitch_count;
		}

		const Sample& latest = samples.back( );
		result.m_frame_ms = latest.m_frame_ms;
		result.m_average_ms =
			std::accumulate( frame_times.begin( ), frame_times.end( ), 0.0 ) / static_cast< double >( frame_times.size( ) );
		result.m_p95_ms = percentile( frame_times, 0.95 );
		result.m_p99_ms = percentile( frame_times, 0.99 );
		result.m_max_ms = *std::max_element( frame_times.begin( ), frame_times.end( ) );
		result.m_focused = latest.m_focused;
		result.m_coop = latest.m_coop;

		result.m_graph_count = std::min( samples.size( ), OverlayMetrics::graph_capacity );
		for( size_t index = 0; index < result.m_graph_count; ++index )
			result.m_graph_ms[ index ] = samples[ samples.size( ) - result.m_graph_count + index ].m_frame_ms;
		return result;
	}

	bool FrameMetrics::dump( const std::string& path ) const {
		std::vector< Sample > samples;
		{
			std::lock_guard< std::mutex > lock( m_samples_mutex );
			samples.reserve( m_count );
			const size_t start = m_count == capacity ? m_cursor : 0;
			for( size_t index = 0; index < m_count; ++index )
				samples.push_back( m_samples[ ( start + index ) % capacity ] );
		}

		std::ofstream output( path, std::ios::trunc );
		if( !output )
			return false;

		output << "qpc,frame_ms,shadow_ms,shadow_updates,shadow_cache_hits,simulating,focused,coop\n"
			   << std::fixed << std::setprecision( 6 );
		for( const Sample& sample : samples ) {
			output << sample.m_qpc << ',' << sample.m_frame_ms << ',' << sample.m_shadow_ms << ',' << sample.m_shadow_updates << ','
				   << sample.m_shadow_hits << ',' << ( sample.m_simulating ? 1 : 0 ) << ',' << ( sample.m_focused ? 1 : 0 ) << ','
				   << ( sample.m_coop ? 1 : 0 ) << '\n';
		}
		return output.good( );
	}

	size_t FrameMetrics::count( ) const noexcept {
		std::lock_guard< std::mutex > lock( m_samples_mutex );
		return m_count;
	}

}
