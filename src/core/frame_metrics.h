#pragma once

#include "qpc.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

namespace p2boost {

	struct overlay_metrics_t {
		static constexpr size_t graph_capacity{ 64 };

		double m_frame_ms{ };
		double m_average_ms{ };
		double m_p95_ms{ };
		double m_p99_ms{ };
		double m_max_ms{ };
		double m_shadow_ms{ };
		unsigned int m_hitch_count{ };
		unsigned int m_shadow_updates{ };
		unsigned int m_shadow_hits{ };
		std::array< double, graph_capacity > m_graph_ms{ };
		size_t m_graph_count{ };
		bool m_focused{ };
		bool m_coop{ };
	};

	// lightweight metrics for the two shipping fixes. historical draw, fence,
	// portal, visibility, and resource probes live exclusively under research/.
	class FrameMetrics {
	public:
		static constexpr size_t capacity{ 1u << 16 };

		void set_tracing( bool tracing ) noexcept;

		bool tracing( ) const noexcept {
			return m_tracing.load( std::memory_order_relaxed );
		}

		void set_overlay_collection( bool collecting ) noexcept {
			m_overlay_collection.store( collecting, std::memory_order_relaxed );
		}

		bool collecting( ) const noexcept {
			return tracing( ) || m_overlay_collection.load( std::memory_order_relaxed );
		}

		void set_simulating( bool simulating ) noexcept {
			m_simulating.store( simulating, std::memory_order_relaxed );
		}

		void set_focused( bool focused ) noexcept {
			m_focused.store( focused, std::memory_order_relaxed );
		}

		void set_coop( bool coop ) noexcept {
			m_coop.store( coop, std::memory_order_relaxed );
		}

		void record_frame( ) noexcept;
		void record_shadow_update( double milliseconds, bool cache_hit ) noexcept;

		overlay_metrics_t overlay_metrics( ) const noexcept;
		bool dump( const std::string& path ) const;
		size_t count( ) const noexcept;

	private:
		struct sample_t {
			Qpc::tick_t m_qpc{ };
			double m_frame_ms{ };
			double m_shadow_ms{ };
			unsigned int m_shadow_updates{ };
			unsigned int m_shadow_hits{ };
			bool m_simulating{ };
			bool m_focused{ };
			bool m_coop{ };
		};

		mutable std::mutex m_samples_mutex;
		std::array< sample_t, capacity > m_samples{ };
		size_t m_cursor{ };
		size_t m_count{ };
		Qpc::tick_t m_last_qpc{ };

		mutable std::mutex m_pending_mutex;
		double m_pending_shadow_ms{ };
		unsigned int m_pending_shadow_updates{ };
		unsigned int m_pending_shadow_hits{ };

		std::atomic_bool m_tracing{ };
		std::atomic_bool m_overlay_collection{ };
		std::atomic_bool m_simulating{ };
		std::atomic_bool m_focused{ };
		std::atomic_bool m_coop{ };
	};

}
