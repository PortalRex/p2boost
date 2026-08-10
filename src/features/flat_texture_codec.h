#pragma once

#include <d3d9.h>

#include <cstdint>

namespace p2boost {

	// one solid sRGB color selected from a texture's dominant source tone.
	struct flat_color_t {
		std::uint8_t m_red{ 255 };
		std::uint8_t m_green{ 255 };
		std::uint8_t m_blue{ 255 };
	};

	// pure helpers keep texture decoding testable without Portal 2 or a device.
	bool         flat_texture_format_supported( D3DFORMAT format ) noexcept;
	flat_color_t classify_flat_texture(
	    D3DFORMAT format, const void* bits, int pitch, unsigned int width, unsigned int height ) noexcept;
	bool write_flat_texture( D3DFORMAT    format,
	                         void*        destination,
	                         int          destination_pitch,
	                         const void*  source,
	                         int          source_pitch,
	                         unsigned int width,
	                         unsigned int height,
	                         flat_color_t color ) noexcept;

}
