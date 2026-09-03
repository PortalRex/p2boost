#pragma once

#include <cstdint>

namespace p2boost {

	// Source console color codes embedded in a format string. the engine console
	// panel renders text from the last code onward in the matching color, so a
	// single message can carry several colored segments.
	enum class console_color : std::uint8_t {
		normal = 0x01,  // default text color
		red    = 0x02,  // error
		green  = 0x03,  // success
		blue   = 0x04,  // informational
		yellow = 0x05,  // warning
	};

	// "p2boost" prefix already colored, message follows the given color codes.
	void console_print( const char* format, ... );

	// prints with the whole message in one color and resets to normal afterwards.
	void console_print_color( console_color color, const char* format, ... );

}