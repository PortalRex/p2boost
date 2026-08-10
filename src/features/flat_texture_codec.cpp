#include "flat_texture_codec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace p2boost {
	namespace {
		// unpacked color used only while classifying source pixels.
		struct rgb_t {
			std::uint8_t m_red{ };
			std::uint8_t m_green{ };
			std::uint8_t m_blue{ };
		};

		// per-channel histograms select the dominant surface color without accent bias.
		struct color_accumulator_t {
			std::array< std::uint64_t, 256 > m_red{ };
			std::array< std::uint64_t, 256 > m_green{ };
			std::array< std::uint64_t, 256 > m_blue{ };
			std::uint64_t                    m_samples{ };

			void add( rgb_t color ) noexcept {
				++m_red[ color.m_red ];
				++m_green[ color.m_green ];
				++m_blue[ color.m_blue ];
				++m_samples;
			}
		};

		// memcpy avoids unaligned reads and writes in compressed blocks.
		template < typename T >
		T read_value( const void* address ) noexcept {
			T value{ };
			std::memcpy( &value, address, sizeof( value ) );
			return value;
		}

		template < typename T >
		void write_value( void* address, T value ) noexcept {
			std::memcpy( address, &value, sizeof( value ) );
		}

		// expand packed integer channels with nearest-value rounding.
		std::uint8_t expand_bits( unsigned int value, unsigned int maximum ) noexcept {
			return static_cast< std::uint8_t >( ( value * 255u + maximum / 2u ) / maximum );
		}

		rgb_t decode_565( std::uint16_t value ) noexcept {
			return {
			    expand_bits( ( value >> 11 ) & 31u, 31u ),
			    expand_bits( ( value >> 5 ) & 63u, 63u ),
			    expand_bits( value & 31u, 31u ),
			};
		}

		std::uint16_t encode_565( flat_color_t color ) noexcept {
			const unsigned int red   = ( static_cast< unsigned int >( color.m_red ) * 31u + 127u ) / 255u;
			const unsigned int green = ( static_cast< unsigned int >( color.m_green ) * 63u + 127u ) / 255u;
			const unsigned int blue  = ( static_cast< unsigned int >( color.m_blue ) * 31u + 127u ) / 255u;
			return static_cast< std::uint16_t >( ( red << 11 ) | ( green << 5 ) | blue );
		}

		// decode the shared RGB block used by DXT1, DXT3, and DXT5.
		std::array< rgb_t, 4 > dxt_palette( const std::uint8_t* color_block, bool dxt1 ) noexcept {
			const std::uint16_t    first_value  = read_value< std::uint16_t >( color_block );
			const std::uint16_t    second_value = read_value< std::uint16_t >( color_block + 2 );
			const rgb_t            first        = decode_565( first_value );
			const rgb_t            second       = decode_565( second_value );
			std::array< rgb_t, 4 > palette{ first, second };

			if( dxt1 && first_value <= second_value ) {
				palette[ 2 ] = {
				    static_cast< std::uint8_t >( ( static_cast< unsigned int >( first.m_red ) + second.m_red ) / 2u ),
				    static_cast< std::uint8_t >( ( static_cast< unsigned int >( first.m_green ) + second.m_green ) / 2u ),
				    static_cast< std::uint8_t >( ( static_cast< unsigned int >( first.m_blue ) + second.m_blue ) / 2u ),
				};
				palette[ 3 ] = { };
				return palette;
			}

			palette[ 2 ] = {
			    static_cast< std::uint8_t >( ( 2u * first.m_red + second.m_red ) / 3u ),
			    static_cast< std::uint8_t >( ( 2u * first.m_green + second.m_green ) / 3u ),
			    static_cast< std::uint8_t >( ( 2u * first.m_blue + second.m_blue ) / 3u ),
			};
			palette[ 3 ] = {
			    static_cast< std::uint8_t >( ( first.m_red + 2u * second.m_red ) / 3u ),
			    static_cast< std::uint8_t >( ( first.m_green + 2u * second.m_green ) / 3u ),
			    static_cast< std::uint8_t >( ( first.m_blue + 2u * second.m_blue ) / 3u ),
			};
			return palette;
		}

		// reconstruct the eight-value DXT5 alpha lookup table.
		std::array< std::uint8_t, 8 > dxt5_alpha_palette( const std::uint8_t* block ) noexcept {
			std::array< std::uint8_t, 8 > palette{ };
			palette[ 0 ] = block[ 0 ];
			palette[ 1 ] = block[ 1 ];

			if( palette[ 0 ] > palette[ 1 ] ) {
				for( unsigned int index = 1; index <= 6; ++index ) {
					palette[ index + 1 ] = static_cast< std::uint8_t >( ( ( 7u - index ) * palette[ 0 ] + index * palette[ 1 ] ) / 7u );
				}
			} else {
				for( unsigned int index = 1; index <= 4; ++index ) {
					palette[ index + 1 ] = static_cast< std::uint8_t >( ( ( 5u - index ) * palette[ 0 ] + index * palette[ 1 ] ) / 5u );
				}
				palette[ 6 ] = 0;
				palette[ 7 ] = 255;
			}

			return palette;
		}

		unsigned int dxt_alpha( D3DFORMAT format, const std::uint8_t* block, unsigned int pixel ) noexcept {
			if( format == D3DFMT_DXT1 ) {
				const std::uint16_t first   = read_value< std::uint16_t >( block );
				const std::uint16_t second  = read_value< std::uint16_t >( block + 2 );
				const std::uint32_t indices = read_value< std::uint32_t >( block + 4 );
				const unsigned int  index   = ( indices >> ( pixel * 2u ) ) & 3u;
				return first <= second && index == 3u ? 0u : 255u;
			}

			if( format == D3DFMT_DXT3 ) {
				const std::uint64_t alpha = read_value< std::uint64_t >( block );
				return static_cast< unsigned int >( ( alpha >> ( pixel * 4u ) ) & 15u ) * 17u;
			}

			const std::array< std::uint8_t, 8 > palette = dxt5_alpha_palette( block );
			std::uint64_t                       indices{ };
			std::memcpy( &indices, block + 2, 6 );
			return palette[ ( indices >> ( pixel * 3u ) ) & 7u ];
		}

		flat_color_t finish_color( const color_accumulator_t& accumulator ) noexcept {
			if( !accumulator.m_samples )
				return { };

			// the median rejects small emissive or team-colored accents while retaining
			// the surface's actual dominant tone. alpha masks never bias the color.
			auto median = [ target = ( accumulator.m_samples + 1u ) / 2u ]( const auto& histogram ) {
				std::uint64_t samples{ };
				for( size_t value = 0; value < histogram.size( ); ++value ) {
					samples += histogram[ value ];
					if( samples >= target )
						return static_cast< std::uint8_t >( value );
				}
				return std::uint8_t{ };
			};

			return { median( accumulator.m_red ), median( accumulator.m_green ), median( accumulator.m_blue ) };
		}

		void accumulate_dxt( color_accumulator_t& accumulator,
		                     D3DFORMAT            format,
		                     const void*          bits,
		                     int                  pitch,
		                     unsigned int         width,
		                     unsigned int         height ) noexcept {
			const auto*        source     = static_cast< const std::uint8_t* >( bits );
			const unsigned int block_size = format == D3DFMT_DXT1 ? 8u : 16u;
			const unsigned int blocks_x   = std::max( 1u, ( width + 3u ) / 4u );
			const unsigned int blocks_y   = std::max( 1u, ( height + 3u ) / 4u );

			for( unsigned int block_y = 0; block_y < blocks_y; ++block_y ) {
				const auto* row = source + static_cast< size_t >( block_y ) * static_cast< size_t >( pitch );
				for( unsigned int block_x = 0; block_x < blocks_x; ++block_x ) {
					const auto*                  block       = row + static_cast< size_t >( block_x ) * block_size;
					const auto*                  color_block = block + ( format == D3DFMT_DXT1 ? 0u : 8u );
					const std::array< rgb_t, 4 > palette     = dxt_palette( color_block, format == D3DFMT_DXT1 );
					const std::uint32_t          indices     = read_value< std::uint32_t >( color_block + 4 );

					for( unsigned int local_y = 0; local_y < 4u; ++local_y ) {
						for( unsigned int local_x = 0; local_x < 4u; ++local_x ) {
							const unsigned int x = block_x * 4u + local_x;
							const unsigned int y = block_y * 4u + local_y;
							if( x >= width || y >= height )
								continue;
							const unsigned int pixel       = local_y * 4u + local_x;
							const unsigned int color_index = ( indices >> ( pixel * 2u ) ) & 3u;
							// DXT1 index three is genuinely transparent; DXT3/5 alpha often
							// carries material masks, so their RGB samples remain representative.
							if( format != D3DFMT_DXT1 || dxt_alpha( format, block, pixel ) )
								accumulator.add( palette[ color_index ] );
						}
					}
				}
			}
		}

		void flatten_dxt_color_block( std::uint8_t* destination, const std::uint8_t* source, flat_color_t color, bool dxt1 ) noexcept {
			const std::uint16_t target           = encode_565( color );
			const std::uint16_t source_first     = read_value< std::uint16_t >( source );
			const std::uint16_t source_second    = read_value< std::uint16_t >( source + 2 );
			const std::uint32_t source_indices   = read_value< std::uint32_t >( source + 4 );
			const bool          transparent_mode = dxt1 && source_first <= source_second;

			if( transparent_mode ) {
				// equal endpoints keep DXT1 in three-color/transparent mode. every
				// original index 3 remains transparent; every opaque texel becomes
				// the exact same RGB endpoint.
				write_value( destination, target );
				write_value( destination + 2, target );
				std::uint32_t indices{ };
				for( unsigned int pixel = 0; pixel < 16u; ++pixel ) {
					if( ( ( source_indices >> ( pixel * 2u ) ) & 3u ) == 3u )
						indices |= 3u << ( pixel * 2u );
				}
				write_value( destination + 4, indices );
				return;
			}

			// four-color mode requires endpoint zero to be numerically larger.
			// select one endpoint for every pixel so interpolation cannot re-add
			// texture detail. the black case uses endpoint one, which is true zero.
			if( target ) {
				write_value( destination, target );
				write_value< std::uint16_t >( destination + 2, 0 );
				write_value< std::uint32_t >( destination + 4, 0 );
			} else {
				write_value< std::uint16_t >( destination, 1 );
				write_value< std::uint16_t >( destination + 2, 0 );
				write_value< std::uint32_t >( destination + 4, 0x55555555u );
			}
		}

	}

	// report only formats with a tested alpha-preserving encoder.
	bool flat_texture_format_supported( D3DFORMAT format ) noexcept {
		switch( format ) {
		case D3DFMT_R8G8B8:
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_R5G6B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4:
		case D3DFMT_A8B8G8R8:
		case D3DFMT_X8B8G8R8:
		case D3DFMT_L8:
		case D3DFMT_A8L8:
		case D3DFMT_DXT1:
		case D3DFMT_DXT3:
		case D3DFMT_DXT5:
			return true;
		default:
			return false;
		}
	}

	// derive one stable majority tone from a complete source image.
	flat_color_t classify_flat_texture( D3DFORMAT format, const void* bits, int pitch, unsigned int width, unsigned int height ) noexcept {
		if( !bits || pitch <= 0 || !width || !height || !flat_texture_format_supported( format ) )
			return { };

		color_accumulator_t accumulator;
		if( format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5 ) {
			accumulate_dxt( accumulator, format, bits, pitch, width, height );
			return finish_color( accumulator );
		}

		const auto* source = static_cast< const std::uint8_t* >( bits );
		for( unsigned int y = 0; y < height; ++y ) {
			const auto* row = source + static_cast< size_t >( y ) * static_cast< size_t >( pitch );
			for( unsigned int x = 0; x < width; ++x ) {
				rgb_t color{ };
				switch( format ) {
				case D3DFMT_R8G8B8:
					color = { row[ x * 3u + 2u ], row[ x * 3u + 1u ], row[ x * 3u ] };
					break;
				case D3DFMT_A8R8G8B8:
				case D3DFMT_X8R8G8B8:
					color = { row[ x * 4u + 2u ], row[ x * 4u + 1u ], row[ x * 4u ] };
					break;
				case D3DFMT_A8B8G8R8:
				case D3DFMT_X8B8G8R8:
					color = { row[ x * 4u ], row[ x * 4u + 1u ], row[ x * 4u + 2u ] };
					break;
				case D3DFMT_R5G6B5: {
					color = decode_565( read_value< std::uint16_t >( row + x * 2u ) );
					break;
				}
				case D3DFMT_X1R5G5B5:
				case D3DFMT_A1R5G5B5: {
					const std::uint16_t value = read_value< std::uint16_t >( row + x * 2u );
					color                     = { expand_bits( ( value >> 10 ) & 31u, 31u ),
					                              expand_bits( ( value >> 5 ) & 31u, 31u ),
					                              expand_bits( value & 31u, 31u ) };
					break;
				}
				case D3DFMT_A4R4G4B4:
				case D3DFMT_X4R4G4B4: {
					const std::uint16_t value = read_value< std::uint16_t >( row + x * 2u );
					color                     = { expand_bits( ( value >> 8 ) & 15u, 15u ),
					                              expand_bits( ( value >> 4 ) & 15u, 15u ),
					                              expand_bits( value & 15u, 15u ) };
					break;
				}
				case D3DFMT_L8:
					color = { row[ x ], row[ x ], row[ x ] };
					break;
				case D3DFMT_A8L8:
					color = { row[ x * 2u ], row[ x * 2u ], row[ x * 2u ] };
					break;
				default:
					break;
				}
				accumulator.add( color );
			}
		}

		return finish_color( accumulator );
	}

	// replace RGB while copying every source alpha bit exactly.
	bool write_flat_texture( D3DFORMAT    format,
	                         void*        destination,
	                         int          destination_pitch,
	                         const void*  source,
	                         int          source_pitch,
	                         unsigned int width,
	                         unsigned int height,
	                         flat_color_t color ) noexcept {
		if( !destination || !source || destination_pitch <= 0 || source_pitch <= 0 || !width || !height ||
		    !flat_texture_format_supported( format ) )
			return false;

		auto*       output = static_cast< std::uint8_t* >( destination );
		const auto* input  = static_cast< const std::uint8_t* >( source );
		if( format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5 ) {
			const unsigned int block_size = format == D3DFMT_DXT1 ? 8u : 16u;
			const unsigned int blocks_x   = std::max( 1u, ( width + 3u ) / 4u );
			const unsigned int blocks_y   = std::max( 1u, ( height + 3u ) / 4u );
			for( unsigned int y = 0; y < blocks_y; ++y ) {
				auto*       destination_row = output + static_cast< size_t >( y ) * static_cast< size_t >( destination_pitch );
				const auto* source_row      = input + static_cast< size_t >( y ) * static_cast< size_t >( source_pitch );
				for( unsigned int x = 0; x < blocks_x; ++x ) {
					auto*       destination_block = destination_row + static_cast< size_t >( x ) * block_size;
					const auto* source_block      = source_row + static_cast< size_t >( x ) * block_size;
					if( format != D3DFMT_DXT1 )
						std::memcpy( destination_block, source_block, 8 );
					const unsigned int color_offset = format == D3DFMT_DXT1 ? 0u : 8u;
					flatten_dxt_color_block( destination_block + color_offset, source_block + color_offset, color, format == D3DFMT_DXT1 );
				}
			}
			return true;
		}

		for( unsigned int y = 0; y < height; ++y ) {
			auto*       destination_row = output + static_cast< size_t >( y ) * static_cast< size_t >( destination_pitch );
			const auto* source_row      = input + static_cast< size_t >( y ) * static_cast< size_t >( source_pitch );
			size_t      row_size{ };
			switch( format ) {
			case D3DFMT_R8G8B8:
				row_size = static_cast< size_t >( width ) * 3u;
				break;
			case D3DFMT_A8R8G8B8:
			case D3DFMT_X8R8G8B8:
			case D3DFMT_A8B8G8R8:
			case D3DFMT_X8B8G8R8:
				row_size = static_cast< size_t >( width ) * 4u;
				break;
			case D3DFMT_L8:
				row_size = width;
				break;
			default:
				row_size = static_cast< size_t >( width ) * 2u;
				break;
			}
			std::memcpy( destination_row, source_row, row_size );

			for( unsigned int x = 0; x < width; ++x ) {
				switch( format ) {
				case D3DFMT_R8G8B8:
					destination_row[ x * 3u ]      = color.m_blue;
					destination_row[ x * 3u + 1u ] = color.m_green;
					destination_row[ x * 3u + 2u ] = color.m_red;
					break;
				case D3DFMT_A8R8G8B8:
				case D3DFMT_X8R8G8B8:
					destination_row[ x * 4u ]      = color.m_blue;
					destination_row[ x * 4u + 1u ] = color.m_green;
					destination_row[ x * 4u + 2u ] = color.m_red;
					break;
				case D3DFMT_A8B8G8R8:
				case D3DFMT_X8B8G8R8:
					destination_row[ x * 4u ]      = color.m_red;
					destination_row[ x * 4u + 1u ] = color.m_green;
					destination_row[ x * 4u + 2u ] = color.m_blue;
					break;
				case D3DFMT_R5G6B5:
					write_value( destination_row + x * 2u, encode_565( color ) );
					break;
				case D3DFMT_X1R5G5B5:
				case D3DFMT_A1R5G5B5: {
					const std::uint16_t alpha  = read_value< std::uint16_t >( source_row + x * 2u ) & 0x8000u;
					const std::uint16_t red    = static_cast< std::uint16_t >( ( color.m_red * 31u + 127u ) / 255u );
					const std::uint16_t green  = static_cast< std::uint16_t >( ( color.m_green * 31u + 127u ) / 255u );
					const std::uint16_t blue   = static_cast< std::uint16_t >( ( color.m_blue * 31u + 127u ) / 255u );
					const std::uint16_t prefix = format == D3DFMT_A1R5G5B5 ? alpha : 0x8000u;
					write_value( destination_row + x * 2u, static_cast< std::uint16_t >( prefix | ( red << 10 ) | ( green << 5 ) | blue ) );
					break;
				}
				case D3DFMT_A4R4G4B4:
				case D3DFMT_X4R4G4B4: {
					const std::uint16_t source_value = read_value< std::uint16_t >( source_row + x * 2u );
					const std::uint16_t alpha        = format == D3DFMT_A4R4G4B4 ? source_value & 0xf000u : 0xf000u;
					const std::uint16_t red          = static_cast< std::uint16_t >( ( color.m_red * 15u + 127u ) / 255u );
					const std::uint16_t green        = static_cast< std::uint16_t >( ( color.m_green * 15u + 127u ) / 255u );
					const std::uint16_t blue         = static_cast< std::uint16_t >( ( color.m_blue * 15u + 127u ) / 255u );
					write_value( destination_row + x * 2u, static_cast< std::uint16_t >( alpha | ( red << 8 ) | ( green << 4 ) | blue ) );
					break;
				}
				case D3DFMT_L8:
					destination_row[ x ] = static_cast< std::uint8_t >( ( 54u * color.m_red + 183u * color.m_green + 19u * color.m_blue + 128u ) >> 8u );
					break;
				case D3DFMT_A8L8:
					destination_row[ x * 2u ] = static_cast< std::uint8_t >( ( 54u * color.m_red + 183u * color.m_green + 19u * color.m_blue + 128u ) >> 8u );
					break;
				default:
					return false;
				}
			}
		}

		return true;
	}
}
