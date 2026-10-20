#pragma once

namespace p2boost {

	// the frame rate the profiler overlay grades frames against. fps_max caps
	// the game when it is set, and vsync caps it at the display's refresh rate.
	// an uncapped game (fps_max 0, vsync off) is graded against the refresh
	// rate too: the display never shows more frames than that.
	struct frame_target_t {
		double      m_fps;
		const char* m_source;

		double frame_ms( ) const noexcept {
			return 1000.0 / m_fps;
		}

		// a frame two target frames long or longer is a visibly dropped frame.
		double dropped_ms( ) const noexcept {
			return 2.0 * frame_ms( );
		}
	};

	// used when Windows cannot report the display's refresh rate.
	inline constexpr double fallback_refresh_rate{ 60.0 };

	inline frame_target_t frame_target( double fps_max, bool vsync, double refresh_rate ) noexcept {
		const double display = refresh_rate >= 1.0 ? refresh_rate : fallback_refresh_rate;
		const bool   capped  = fps_max >= 1.0;
		if( vsync && ( !capped || fps_max > display ) )
			return { display, "vsync" };
		if( capped )
			return { fps_max, "fps_max" };
		return { display, "display" };
	}

	enum class frame_grade_t {
		good,
		warning,
		bad,
	};

	// bad: the last second dropped a frame. warning: it fell short of the
	// target, on average by more than 5% or at the 99th percentile by more
	// than a quarter of a frame.
	inline frame_grade_t grade_frames( double average_ms, double p99_ms, unsigned int dropped_frames, const frame_target_t& target ) noexcept {
		if( dropped_frames > 0 )
			return frame_grade_t::bad;
		if( average_ms * 0.95 > target.frame_ms( ) || p99_ms > target.frame_ms( ) * 1.25 )
			return frame_grade_t::warning;
		return frame_grade_t::good;
	}

	// one graph column: how much of the target frame time a frame used.
	inline char graph_character( double milliseconds, const frame_target_t& target ) noexcept {
		const double share = milliseconds / target.frame_ms( );
		if( share >= 2.0 )
			return '!';
		if( share >= 1.0 )
			return '^';
		if( share >= 0.5 )
			return '=';
		if( share >= 0.25 )
			return '-';
		return '.';
	}

}
