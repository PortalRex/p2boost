#pragma once

#include "qpc.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace p2boost {

	// Small read-only summary consumed by the optional in-game profiler.
	struct OverlayMetrics {
		double		 m_frame_ms{ };
		double		 m_average_ms{ };
		double		 m_p99_ms{ };
		double		 m_max_ms{ };
		double		 m_portal_ms{ };
		double		 m_render_worker_ms{ };
		unsigned int m_portal_count{ };
		unsigned int m_focus_events{ };
		unsigned int m_shadow_skips{ };
		bool		 m_focused{ };
		bool		 m_coop{ };
	};

	// stores one execution of the queued material render worker.
	struct RenderWorkerMetrics {
		Qpc::tick_t		   m_begin_qpc{ };
		Qpc::tick_t		   m_end_qpc{ };
		unsigned long long m_cycles{ };
		unsigned int	   m_queued_calls{ };
		unsigned int	   m_parameter_bytes{ };
		unsigned int	   m_vertex_bytes{ };
		unsigned int	   m_index_bytes{ };
		unsigned int	   m_hardware_sync_count{ };
		Qpc::tick_t		   m_hardware_sync_qpc{ };
		Qpc::tick_t		   m_max_hardware_sync_qpc{ };
		unsigned long long m_hardware_sync_cycles{ };
		int				   m_sync_index_before{ -1 };
		unsigned int	   m_sync_pending_before{ };
		unsigned int	   m_sync_pending_after{ };
		unsigned int	   m_present_count{ };
		Qpc::tick_t		   m_present_qpc{ };
		Qpc::tick_t		   m_max_present_qpc{ };

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
		unsigned int					 m_draw_count{ };
		Qpc::tick_t						 m_draw_qpc{ };
		Qpc::tick_t						 m_max_draw_qpc{ };
		unsigned int					 m_max_draw_method{ };
		unsigned int					 m_max_draw_primitive_type{ };
		unsigned int					 m_max_draw_primitive_count{ };
		unsigned int					 m_max_draw_vertex_count{ };
		std::uint32_t					 m_max_draw_vertex_shader{ };
		std::uint32_t					 m_max_draw_pixel_shader{ };
		std::array< std::uint32_t, 4 >	 m_max_draw_textures{ };
		std::uint8_t					 m_max_draw_first_vertex_shader{ };
		std::uint8_t					 m_max_draw_first_pixel_shader{ };
		std::uint8_t					 m_max_draw_first_pipeline{ };
		std::uint8_t					 m_max_draw_first_texture_mask{ };
		std::uint32_t					 m_max_draw_texture0_type{ };
		std::uint32_t					 m_max_draw_texture0_format{ };
		std::uint32_t					 m_max_draw_texture0_width{ };
		std::uint32_t					 m_max_draw_texture0_height{ };
		std::uint32_t					 m_max_draw_texture0_usage{ };
		std::uint32_t					 m_max_draw_texture0_pool{ };
		std::uint32_t					 m_max_draw_texture0_levels{ };
		std::uint64_t					 m_max_draw_texture0_hash{ };
		std::array< std::uintptr_t, 16 > m_max_draw_stack{ };
		unsigned short					 m_max_draw_stack_depth{ };
		std::array< unsigned int, 16 >	 m_command_keys{ };
		std::array< unsigned int, 16 >	 m_command_counts{ };
		unsigned int					 m_dynamic_light_count{ };
		Qpc::tick_t						 m_dynamic_light_qpc{ };
		unsigned int					 m_dynamic_light_unique_lights{ };
		unsigned int					 m_dynamic_light_unique_surfaces{ };
		std::uint32_t					 m_dynamic_light_first_light{ };
		std::uint32_t					 m_dynamic_light_first_surface{ };
		std::uint32_t					 m_dynamic_light_first_flags{ };
		std::uint8_t					 m_dynamic_light_first_bool_a{ };
		std::uint8_t					 m_dynamic_light_first_bool_b{ };
		std::array< std::uint32_t, 11 >	 m_dynamic_light_words{ };
#endif

		unsigned int m_thread_id{ };
	};

	// owns fixed-size frame and render-worker diagnostic histories.
	class Metrics {
	public:
		static constexpr size_t capacity{ 1u << 16 };

		// control trace collection without allocating at runtime.
		void set_tracing( bool tracing ) noexcept {
			m_tracing.store( tracing, std::memory_order_relaxed );
		}

		bool tracing( ) const noexcept {
			return m_tracing.load( std::memory_order_relaxed );
		}

		void set_overlay_collection( bool collecting ) noexcept {
			m_overlay_collection.store( collecting, std::memory_order_relaxed );
		}

		// Build a bounded rolling summary without exposing the history ring.
		OverlayMetrics overlay_metrics( ) const noexcept;

		void set_coop( bool coop ) noexcept {
			m_coop = coop;
		}

		// Cache simulation state from the server tick callback.
		void set_simulating( bool simulating ) noexcept {
			m_simulating.store( simulating, std::memory_order_relaxed );
		}

		// Record one rendered engine frame and its pending events.
		void frame( ) noexcept;

		// record focus restore work and its three stages.
		void record_reacquire( double milliseconds, const char* reason, Qpc::tick_t begin_qpc, Qpc::tick_t end_qpc ) noexcept;
		void record_reacquire_stage( size_t stage, double milliseconds ) noexcept;

		// record renderer work attributed by the hooks.
		void record_portal_render( double milliseconds ) noexcept;
		void record_shadow_update( double milliseconds, bool same_projection, bool same_full, bool skipped ) noexcept;
		void record_render_worker( RenderWorkerMetrics sample ) noexcept;

		// write both fixed histories to csv.
		bool dump( const std::string& path ) const;

		size_t count( ) const noexcept {
			return m_count;
		}

	private:
		// stores one game-frame row.
		struct Sample {
			Qpc::tick_t				m_qpc{ };
			double					m_delta_ms{ };
			Qpc::tick_t				m_reacquire_begin_qpc{ };
			Qpc::tick_t				m_reacquire_end_qpc{ };
			double					m_reacquire_ms{ };
			std::array< double, 3 > m_reacquire_stages_ms{ };
			double					m_portal_render_ms{ };
			unsigned int			m_portal_render_count{ };
			double					m_shadow_update_ms{ };
			unsigned int			m_shadow_update_count{ };
			unsigned int			m_shadow_same_projection_count{ };
			unsigned int			m_shadow_same_full_count{ };
			unsigned int			m_shadow_skipped_count{ };
			Qpc::tick_t				m_render_worker_begin_qpc{ };
			Qpc::tick_t				m_render_worker_end_qpc{ };
			double					m_render_worker_ms{ };
			unsigned long long		m_render_worker_cycles{ };
			unsigned int			m_render_worker_count{ };
			unsigned long long		m_render_worker_queued_calls{ };
			unsigned long long		m_render_worker_parameter_bytes{ };
			unsigned long long		m_render_worker_vertex_bytes{ };
			unsigned long long		m_render_worker_index_bytes{ };
			int						m_reacquire_reason{ };
			bool					m_simulating{ };
			bool					m_focused{ };
			bool					m_coop{ };
		};

		Qpc::tick_t					   m_last_qpc{ };
		std::array< Sample, capacity > m_samples{ };
		size_t						   m_cursor{ };
		size_t						   m_count{ };
		std::atomic< bool >			   m_tracing{ };
		std::atomic< bool >			   m_overlay_collection{ };
		std::atomic< bool >			   m_simulating{ };
		bool						   m_coop{ };

		bool collecting( ) const noexcept {
			return tracing( ) || m_overlay_collection.load( std::memory_order_relaxed );
		}

		// pending events are consumed by the next frame.
		double					m_pending_reacquire_ms{ };
		Qpc::tick_t				m_pending_reacquire_begin_qpc{ };
		Qpc::tick_t				m_pending_reacquire_end_qpc{ };
		std::array< double, 3 > m_pending_reacquire_stages_ms{ };
		double					m_pending_portal_render_ms{ };
		unsigned int			m_pending_portal_render_count{ };
		double					m_pending_shadow_update_ms{ };
		unsigned int			m_pending_shadow_update_count{ };
		unsigned int			m_pending_shadow_same_projection_count{ };
		unsigned int			m_pending_shadow_same_full_count{ };
		unsigned int			m_pending_shadow_skipped_count{ };
		int						m_pending_reacquire_reason{ };

		// worker samples arrive from the material render thread.
		mutable std::mutex							m_render_worker_mutex;
		std::array< RenderWorkerMetrics, capacity > m_render_worker_samples{ };
		size_t										m_render_worker_cursor{ };
		size_t										m_render_worker_count{ };
		Qpc::tick_t									m_pending_render_worker_begin_qpc{ };
		Qpc::tick_t									m_pending_render_worker_end_qpc{ };
		Qpc::tick_t									m_pending_render_worker_qpc{ };
		unsigned long long							m_pending_render_worker_cycles{ };
		unsigned int								m_pending_render_worker_count{ };
		unsigned long long							m_pending_render_worker_queued_calls{ };
		unsigned long long							m_pending_render_worker_parameter_bytes{ };
		unsigned long long							m_pending_render_worker_vertex_bytes{ };
		unsigned long long							m_pending_render_worker_index_bytes{ };
	};

}
