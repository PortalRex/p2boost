#pragma once

namespace p2boost {

	// compact color value passed between profiler layout and surface drawing.
	struct color_t {
		int red{ };
		int green{ };
		int blue{ };
		int alpha{ 255 };
	};

}
