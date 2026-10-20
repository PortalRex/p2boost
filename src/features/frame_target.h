#pragma once

namespace p2boost {

	// the frame rate the profiler overlay grades frames against: the refresh
	// rate of the game's monitor, since the display never shows more frames
	// than that. fps_max stands in when Windows cannot report the rate.
	struct frame_target_t {
		double      m_fps;
		const char* m_source;

		double frame_ms( ) const noexcept {
			return 1000.0 / m_fps;
		}

		// a frame two target frames long or longer misses a whole refresh.
		double dropped_ms( ) const noexcept {
			return 2.0 * frame_ms( );
		}
	};

	// used when neither the display nor fps_max gives a rate.
	inline constexpr double fallback_refresh_rate{ 60.0 };

	inline frame_target_t frame_target( double refresh_rate, double fps_max ) noexcept {
		if( refresh_rate >= 1.0 )
			return { refresh_rate, "display" };
		if( fps_max >= 1.0 )
			return { fps_max, "fps_max" };
		return { fallback_refresh_rate, "default" };
	}

	enum class frame_grade_t {
		good,
		warning,
		bad,
	};

	// lenient on purpose: a single slow frame (a texture streaming in, a
	// checkpoint save) only warns. bad means the last second visibly
	// stuttered or ran well below the display.
	//   warning: average below 90% of the target, or any dropped frame.
	//   bad:     average below 75% of the target, or more than two dropped frames.
	inline frame_grade_t grade_frames( double average_ms, unsigned int dropped_frames, const frame_target_t& target ) noexcept {
		const double share = average_ms > 0.0 ? target.frame_ms( ) / average_ms : 0.0;
		if( share < 0.75 || dropped_frames > 2 )
			return frame_grade_t::bad;
		if( share < 0.9 || dropped_frames > 0 )
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
