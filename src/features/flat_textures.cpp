#include "flat_textures.h"

#include "address.h"
#include "pattern.h"
#include "portal2.h"
#include "sdk.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace p2boost {
	namespace {
		// bind metadata is consumed by SetTexture in the same render-thread call chain.
		thread_local bool		   g_material_excluded{ };
		thread_local std::uint32_t g_flat_sampler_mask{ };
		thread_local std::uint32_t g_normal_sampler_mask{ };
		thread_local unsigned int  g_material_bind_depth{ };
		thread_local bool		   g_material_pass_active{ };
		thread_local unsigned int  g_vgui_paint_depth{ };

		// discard the pending world pass before VGui_Paint submits UI textures.
		void clear_material_pass( ) noexcept {
			// most draws occur without a pending Bind, so keep this hook nearly free.
			if( !g_material_pass_active )
				return;

			g_material_excluded	   = false;
			g_flat_sampler_mask	   = 0;
			g_normal_sampler_mask  = 0;
			g_material_pass_active = false;
		}

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
			std::uint64_t					 m_samples{ };

			void add( rgb_t color ) noexcept {
				++m_red[ color.m_red ];
				++m_green[ color.m_green ];
				++m_blue[ color.m_blue ];
				++m_samples;
			}
		};

		// material classification describes flat, normal, and excluded samplers.
		struct material_pass_t {
			std::uint32_t m_flat_samplers{ };
			std::uint32_t m_normal_samplers{ };
			bool		  m_excluded{ };
		};

		// perform a case-insensitive substring check on engine-owned names.
		bool contains_text( const char* value, const char* text ) noexcept {
			if( !value || !text )
				return false;
			const size_t text_length = std::strlen( text );
			for( const char* cursor = value; *cursor; ++cursor ) {
				if( _strnicmp( cursor, text, text_length ) == 0 )
					return true;
			}
			return false;
		}

		// identify texture roles from Valve's public shader implementations.
		material_pass_t classify_material_pass( sdk::Material* material ) noexcept {
			if( !material )
				return { };

			const char* group  = material->GetTextureGroup( );
			const char* shader = material->GetShaderName( );

			// engine-generated resources are inputs or outputs, not authored color maps.
			if( contains_text( group, "render target" ) || contains_text( group, "lightmap" ) || contains_text( group, "cubemap" ) )
				return { 0, 0, true };

			if( !shader )
				return { };

			// flatten only color-bearing samplers. blend/light-warp/detail control data,
			// lightmaps, framebuffer copies, and flashlight resources stay live.
			if( _stricmp( shader, "LightmappedGeneric" ) == 0 || _stricmp( shader, "WorldVertexTransition" ) == 0 ) {
				constexpr std::uint32_t flat   = ( 1u << 0u ) | ( 1u << 7u );
				constexpr std::uint32_t normal = ( 1u << 4u ) | ( 1u << 5u ) | ( 1u << 8u );
				return { flat, normal, false };
			}

			if( _stricmp( shader, "VertexLitGeneric" ) == 0 ) {
				// sampler 11 is the self-illumination mask. preserving it keeps the
				// emissive shape while the flattened base keeps its exact alpha too.
				constexpr std::uint32_t flat   = 1u << 0u;
				constexpr std::uint32_t normal = 1u << 3u;
				return { flat, normal, false };
			}

			if( _stricmp( shader, "WorldTwoTextureBlend" ) == 0 )
				return { ( 1u << 0u ) | ( 1u << 3u ), 1u << 4u, false };

			// unknown shaders may carry video frames, particle masks, or lookup tables.
			// preserving them is safer than guessing that every sampler is albedo.
			return { };
		}

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
			const unsigned int red	 = ( static_cast< unsigned int >( color.m_red ) * 31u + 127u ) / 255u;
			const unsigned int green = ( static_cast< unsigned int >( color.m_green ) * 63u + 127u ) / 255u;
			const unsigned int blue	 = ( static_cast< unsigned int >( color.m_blue ) * 31u + 127u ) / 255u;
			return static_cast< std::uint16_t >( ( red << 11 ) | ( green << 5 ) | blue );
		}

		// decode the shared RGB block used by DXT1, DXT3, and DXT5.
		std::array< rgb_t, 4 > dxt_palette( const std::uint8_t* color_block, bool dxt1 ) noexcept {
			const std::uint16_t	   first_value	= read_value< std::uint16_t >( color_block );
			const std::uint16_t	   second_value = read_value< std::uint16_t >( color_block + 2 );
			const rgb_t			   first		= decode_565( first_value );
			const rgb_t			   second		= decode_565( second_value );
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
				const std::uint16_t first	= read_value< std::uint16_t >( block );
				const std::uint16_t second	= read_value< std::uint16_t >( block + 2 );
				const std::uint32_t indices = read_value< std::uint32_t >( block + 4 );
				const unsigned int	index	= ( indices >> ( pixel * 2u ) ) & 3u;
				return first <= second && index == 3u ? 0u : 255u;
			}

			if( format == D3DFMT_DXT3 ) {
				const std::uint64_t alpha = read_value< std::uint64_t >( block );
				return static_cast< unsigned int >( ( alpha >> ( pixel * 4u ) ) & 15u ) * 17u;
			}

			const std::array< std::uint8_t, 8 > palette = dxt5_alpha_palette( block );
			std::uint64_t						indices{ };
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
							 D3DFORMAT			  format,
							 const void*		  bits,
							 int				  pitch,
							 unsigned int		  width,
							 unsigned int		  height ) noexcept {
			const auto*		   source	  = static_cast< const std::uint8_t* >( bits );
			const unsigned int block_size = format == D3DFMT_DXT1 ? 8u : 16u;
			const unsigned int blocks_x	  = std::max( 1u, ( width + 3u ) / 4u );
			const unsigned int blocks_y	  = std::max( 1u, ( height + 3u ) / 4u );

			for( unsigned int block_y = 0; block_y < blocks_y; ++block_y ) {
				const auto* row = source + static_cast< size_t >( block_y ) * static_cast< size_t >( pitch );
				for( unsigned int block_x = 0; block_x < blocks_x; ++block_x ) {
					const auto*					 block		 = row + static_cast< size_t >( block_x ) * block_size;
					const auto*					 color_block = block + ( format == D3DFMT_DXT1 ? 0u : 8u );
					const std::array< rgb_t, 4 > palette	 = dxt_palette( color_block, format == D3DFMT_DXT1 );
					const std::uint32_t			 indices	 = read_value< std::uint32_t >( color_block + 4 );

					for( unsigned int local_y = 0; local_y < 4u; ++local_y ) {
						for( unsigned int local_x = 0; local_x < 4u; ++local_x ) {
							const unsigned int x = block_x * 4u + local_x;
							const unsigned int y = block_y * 4u + local_y;
							if( x >= width || y >= height )
								continue;
							const unsigned int pixel	   = local_y * 4u + local_x;
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
			const std::uint16_t target			 = encode_565( color );
			const std::uint16_t source_first	 = read_value< std::uint16_t >( source );
			const std::uint16_t source_second	 = read_value< std::uint16_t >( source + 2 );
			const std::uint32_t source_indices	 = read_value< std::uint32_t >( source + 4 );
			const bool			transparent_mode = dxt1 && source_first <= source_second;

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

		unsigned int mip_dimension( unsigned int base, unsigned int level ) noexcept {
			return std::max( 1u, level >= 31u ? 1u : base >> level );
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
					color					  = { expand_bits( ( value >> 10 ) & 31u, 31u ),
												  expand_bits( ( value >> 5 ) & 31u, 31u ),
												  expand_bits( value & 31u, 31u ) };
					break;
				}
				case D3DFMT_A4R4G4B4:
				case D3DFMT_X4R4G4B4: {
					const std::uint16_t value = read_value< std::uint16_t >( row + x * 2u );
					color					  = { expand_bits( ( value >> 8 ) & 15u, 15u ),
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
	bool write_flat_texture( D3DFORMAT	  format,
							 void*		  destination,
							 int		  destination_pitch,
							 const void*  source,
							 int		  source_pitch,
							 unsigned int width,
							 unsigned int height,
							 flat_color_t color ) noexcept {
		if( !destination || !source || destination_pitch <= 0 || source_pitch <= 0 || !width || !height ||
			!flat_texture_format_supported( format ) )
			return false;

		auto*		output = static_cast< std::uint8_t* >( destination );
		const auto* input  = static_cast< const std::uint8_t* >( source );
		if( format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5 ) {
			const unsigned int block_size = format == D3DFMT_DXT1 ? 8u : 16u;
			const unsigned int blocks_x	  = std::max( 1u, ( width + 3u ) / 4u );
			const unsigned int blocks_y	  = std::max( 1u, ( height + 3u ) / 4u );
			for( unsigned int y = 0; y < blocks_y; ++y ) {
				auto*		destination_row = output + static_cast< size_t >( y ) * static_cast< size_t >( destination_pitch );
				const auto* source_row		= input + static_cast< size_t >( y ) * static_cast< size_t >( source_pitch );
				for( unsigned int x = 0; x < blocks_x; ++x ) {
					auto*		destination_block = destination_row + static_cast< size_t >( x ) * block_size;
					const auto* source_block	  = source_row + static_cast< size_t >( x ) * block_size;
					if( format != D3DFMT_DXT1 )
						std::memcpy( destination_block, source_block, 8 );
					const unsigned int color_offset = format == D3DFMT_DXT1 ? 0u : 8u;
					flatten_dxt_color_block( destination_block + color_offset, source_block + color_offset, color, format == D3DFMT_DXT1 );
				}
			}
			return true;
		}

		for( unsigned int y = 0; y < height; ++y ) {
			auto*		destination_row = output + static_cast< size_t >( y ) * static_cast< size_t >( destination_pitch );
			const auto* source_row		= input + static_cast< size_t >( y ) * static_cast< size_t >( source_pitch );
			size_t		row_size{ };
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
					destination_row[ x * 3u ]	   = color.m_blue;
					destination_row[ x * 3u + 1u ] = color.m_green;
					destination_row[ x * 3u + 2u ] = color.m_red;
					break;
				case D3DFMT_A8R8G8B8:
				case D3DFMT_X8R8G8B8:
					destination_row[ x * 4u ]	   = color.m_blue;
					destination_row[ x * 4u + 1u ] = color.m_green;
					destination_row[ x * 4u + 2u ] = color.m_red;
					break;
				case D3DFMT_A8B8G8R8:
				case D3DFMT_X8B8G8R8:
					destination_row[ x * 4u ]	   = color.m_red;
					destination_row[ x * 4u + 1u ] = color.m_green;
					destination_row[ x * 4u + 2u ] = color.m_blue;
					break;
				case D3DFMT_R5G6B5:
					write_value( destination_row + x * 2u, encode_565( color ) );
					break;
				case D3DFMT_X1R5G5B5:
				case D3DFMT_A1R5G5B5: {
					const std::uint16_t alpha  = read_value< std::uint16_t >( source_row + x * 2u ) & 0x8000u;
					const std::uint16_t red	   = static_cast< std::uint16_t >( ( color.m_red * 31u + 127u ) / 255u );
					const std::uint16_t green  = static_cast< std::uint16_t >( ( color.m_green * 31u + 127u ) / 255u );
					const std::uint16_t blue   = static_cast< std::uint16_t >( ( color.m_blue * 31u + 127u ) / 255u );
					const std::uint16_t prefix = format == D3DFMT_A1R5G5B5 ? alpha : 0x8000u;
					write_value( destination_row + x * 2u, static_cast< std::uint16_t >( prefix | ( red << 10 ) | ( green << 5 ) | blue ) );
					break;
				}
				case D3DFMT_A4R4G4B4:
				case D3DFMT_X4R4G4B4: {
					const std::uint16_t source_value = read_value< std::uint16_t >( source_row + x * 2u );
					const std::uint16_t alpha		 = format == D3DFMT_A4R4G4B4 ? source_value & 0xf000u : 0xf000u;
					const std::uint16_t red			 = static_cast< std::uint16_t >( ( color.m_red * 15u + 127u ) / 255u );
					const std::uint16_t green		 = static_cast< std::uint16_t >( ( color.m_green * 15u + 127u ) / 255u );
					const std::uint16_t blue		 = static_cast< std::uint16_t >( ( color.m_blue * 15u + 127u ) / 255u );
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

	FlatTextures* FlatTextures::m_instance{ };

	FlatTextures::~FlatTextures( ) {
		remove( );
	}

	// discover live DXVK objects, validate Source methods, and install hooks.
	bool FlatTextures::install( ) {
		if( m_instance )
			return false;

		const Address shader{ GetModuleHandleW( L"shaderapivk.dll" ) };
		const Address materials{ GetModuleHandleW( L"materialsystem.dll" ) };
		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		if( !shader || !materials || !engine )
			return false;

		// the unique CreateDevice-success sequence stores DXVK's live D3D9
		// device in this relocated global. no preferred RVA is used at runtime.
		const Address device_xref	= pattern::find_unique( shader, portal2::signature::flat_texture_device );
		const Address device_global = device_xref.add( portal2::xref::flat_texture_device_global ).at< Address >( );
		m_device					= device_global.to< IDirect3DDevice9* >( );
		if( !m_device )
			return false;

		using CreateInterfaceFn = sdk::CreateInterfaceFn;
		auto material_factory	= reinterpret_cast< CreateInterfaceFn >( GetProcAddress( materials.as< HMODULE >( ), "CreateInterface" ) );
		auto engine_factory		= reinterpret_cast< CreateInterfaceFn >( GetProcAddress( engine.as< HMODULE >( ), "CreateInterface" ) );
		m_material_system		= material_factory ? static_cast< sdk::MaterialSystem* >( material_factory( "VMaterialSystem080", nullptr ) ) : nullptr;
		m_render_view			= engine_factory ? static_cast< sdk::RenderView* >( engine_factory( "VEngineRenderView013", nullptr ) ) : nullptr;
		if( !m_material_system || !m_render_view ) {
			m_render_view	  = nullptr;
			m_material_system = nullptr;
			m_device		  = nullptr;
			return false;
		}

		// methods 79 and 80 are ReloadTextures and ReloadMaterials in Valve's
		// VMaterialSystem080. validate them before either can be called.
		const Address reload_textures{ m_material_system->method( portal2::vmt::reload_textures ) };
		const Address reload{ m_material_system->method( portal2::vmt::reload_materials ) };
		const Address get_render_context{ m_material_system->method( portal2::vmt::get_render_context ) };
		const Address material_end = materials.add( static_cast< std::ptrdiff_t >( pattern::image_size( materials ) ) );
		const Address vgui_paint{ m_render_view->method( portal2::vmt::render_view_vgui_paint ) };
		const Address engine_end = engine.add( static_cast< std::ptrdiff_t >( pattern::image_size( engine ) ) );
		if( reload_textures.as< std::uintptr_t >( ) < materials.as< std::uintptr_t >( ) ||
			reload_textures.as< std::uintptr_t >( ) >= material_end.as< std::uintptr_t >( ) ||
			reload.as< std::uintptr_t >( ) < materials.as< std::uintptr_t >( ) ||
			reload.as< std::uintptr_t >( ) >= material_end.as< std::uintptr_t >( ) ||
			get_render_context.as< std::uintptr_t >( ) < materials.as< std::uintptr_t >( ) ||
			get_render_context.as< std::uintptr_t >( ) >= material_end.as< std::uintptr_t >( ) ||
			vgui_paint.as< std::uintptr_t >( ) < engine.as< std::uintptr_t >( ) ||
			vgui_paint.as< std::uintptr_t >( ) >= engine_end.as< std::uintptr_t >( ) ) {
			m_render_view	  = nullptr;
			m_material_system = nullptr;
			m_device		  = nullptr;
			return false;
		}

		sdk::RenderContext* render_context = m_material_system->GetRenderContext( );
		if( !render_context ) {
			m_render_view	  = nullptr;
			m_material_system = nullptr;
			m_device		  = nullptr;
			return false;
		}
		m_render_context_vmt = render_context->methods( );
		const Address bind{ m_render_context_vmt[ portal2::vmt::render_context_bind ] };
		if( bind.as< std::uintptr_t >( ) < materials.as< std::uintptr_t >( ) ||
			bind.as< std::uintptr_t >( ) >= material_end.as< std::uintptr_t >( ) ) {
			render_context->Release( );
			m_render_context_vmt = nullptr;
			m_render_view		 = nullptr;
			m_material_system	 = nullptr;
			m_device			 = nullptr;
			return false;
		}

		auto** device_vmt	 = *reinterpret_cast< void*** >( m_device );
		m_instance			 = this;
		const bool installed = m_bind_material.install( m_render_context_vmt + portal2::vmt::render_context_bind, reinterpret_cast< void* >( &BindMaterial ) ) &&
							   m_vgui_paint.install( m_render_view->methods( ) + portal2::vmt::render_view_vgui_paint, reinterpret_cast< void* >( &VguiPaint ) ) &&
							   m_create_texture.install( device_vmt + portal2::vmt::d3d9_create_texture, reinterpret_cast< void* >( &CreateTexture ) ) &&
							   m_update_surface.install( device_vmt + portal2::vmt::d3d9_update_surface, reinterpret_cast< void* >( &UpdateSurface ) ) &&
							   m_update_texture.install( device_vmt + portal2::vmt::d3d9_update_texture, reinterpret_cast< void* >( &UpdateTexture ) ) &&
							   m_set_texture.install( device_vmt + portal2::vmt::d3d9_set_texture, reinterpret_cast< void* >( &SetTexture ) );
		render_context->Release( );
		if( !installed ) {
			m_set_texture.remove( );
			m_update_texture.remove( );
			m_update_surface.remove( );
			m_create_texture.remove( );
			m_vgui_paint.remove( );
			m_bind_material.remove( );
			m_render_context_vmt = nullptr;
			m_instance			 = nullptr;
			m_render_view		 = nullptr;
			m_material_system	 = nullptr;
			m_device			 = nullptr;
			return false;
		}

		return true;
	}

	void FlatTextures::remove( ) {
		// disable selection before any companion object can be released.
		m_enabled.store( false, std::memory_order_relaxed );
		m_set_texture.remove( );
		m_update_texture.remove( );
		m_update_surface.remove( );
		m_create_texture.remove( );
		m_vgui_paint.remove( );
		m_bind_material.remove( );
		m_render_context_vmt = nullptr;
		release_all_companions( );
		m_unlock_surface.remove( );
		m_lock_surface.remove( );
		m_release_surface.remove( );
		m_surface_vmt = nullptr;
		m_unlock_texture.remove( );
		m_lock_texture.remove( );
		m_get_surface_level.remove( );
		m_release_texture.remove( );
		m_texture_vmt	  = nullptr;
		m_render_view	  = nullptr;
		m_material_system = nullptr;
		m_device		  = nullptr;
		m_substitutions.store( 0, std::memory_order_relaxed );
		m_texture_uploads.store( 0, std::memory_order_relaxed );
		m_surface_uploads.store( 0, std::memory_order_relaxed );
		m_device_uploads.store( 0, std::memory_order_relaxed );
		if( m_instance == this )
			m_instance = nullptr;
	}

	bool FlatTextures::set_enabled( bool enabled ) {
		// a toggle is valid only after the full hook set was installed.
		if( !available( ) )
			return false;
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) == enabled )
			return true;

		// existing map textures predate the toggle. allocate their companions,
		// then ask Source to resend the actual texture pixels through our hooks.
		if( enabled && ( !create_flat_normal( ) || !create_missing_companions( ) || !reload_textures( ) ) ) {
			m_enabled.store( !enabled, std::memory_order_relaxed );
			release_companions( );
			return false;
		}

		// refresh cached material bindings after the pixels have been rebuilt.
		if( !reload_materials( ) ) {
			m_enabled.store( !enabled, std::memory_order_relaxed );
			if( enabled )
				release_companions( );
			return false;
		}
		if( !enabled )
			release_companions( );
		return true;
	}

	void FlatTextures::release_level_textures( ) {
		// Source retains many textures between maps. their COM Release hooks remove
		// genuinely destroyed resources; clearing the whole map here would orphan
		// cached textures and make the toggle stop working after a transition.
		std::unique_lock lock{ m_mutex };
		m_locks.clear( );
		m_surface_locks.clear( );
	}

	size_t FlatTextures::texture_count( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return m_textures.size( );
	}

	size_t FlatTextures::ready_texture_count( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return static_cast< size_t >( std::count_if( m_textures.begin( ), m_textures.end( ), []( const auto& pair ) {
			return pair.second.m_flat && pair.second.m_color_valid;
		} ) );
	}

	bool FlatTextures::normal_ready( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return m_flat_normal != nullptr;
	}

	bool FlatTextures::install_texture_hooks( IDirect3DTexture9* texture ) {
		// DXVK uses one shared implementation VMT for ordinary 2D textures.
		if( !texture )
			return false;

		auto** vmt = *reinterpret_cast< void*** >( texture );
		if( m_texture_vmt )
			return m_texture_vmt == vmt;

		m_texture_vmt = vmt;
		if( !m_release_texture.install( vmt + portal2::vmt::d3d9_texture_release, reinterpret_cast< void* >( &ReleaseTexture ) ) ||
			!m_get_surface_level.install( vmt + portal2::vmt::d3d9_texture_get_surface_level, reinterpret_cast< void* >( &GetSurfaceLevel ) ) ||
			!m_lock_texture.install( vmt + portal2::vmt::d3d9_texture_lock, reinterpret_cast< void* >( &LockTexture ) ) ||
			!m_unlock_texture.install( vmt + portal2::vmt::d3d9_texture_unlock, reinterpret_cast< void* >( &UnlockTexture ) ) ) {
			m_unlock_texture.remove( );
			m_lock_texture.remove( );
			m_get_surface_level.remove( );
			m_release_texture.remove( );
			m_texture_vmt = nullptr;
			return false;
		}

		return true;
	}

	bool FlatTextures::install_surface_hooks( IDirect3DSurface9* surface ) {
		// surface objects likewise share one VMT in the supported DXVK build.
		if( !surface )
			return false;

		auto** vmt = *reinterpret_cast< void*** >( surface );
		if( m_surface_vmt )
			return m_surface_vmt == vmt;

		m_surface_vmt = vmt;
		if( !m_release_surface.install( vmt + portal2::vmt::d3d9_surface_release, reinterpret_cast< void* >( &ReleaseSurface ) ) ||
			!m_lock_surface.install( vmt + portal2::vmt::d3d9_surface_lock, reinterpret_cast< void* >( &LockSurface ) ) ||
			!m_unlock_surface.install( vmt + portal2::vmt::d3d9_surface_unlock, reinterpret_cast< void* >( &UnlockSurface ) ) ) {
			m_unlock_surface.remove( );
			m_lock_surface.remove( );
			m_release_surface.remove( );
			m_surface_vmt = nullptr;
			return false;
		}

		return true;
	}

	bool FlatTextures::create_flat_normal( ) {
		{
			std::shared_lock lock{ m_mutex };
			if( m_flat_normal )
				return true;
		}

		auto create = reinterpret_cast< CreateTextureFn >( m_create_texture.previous( ) );
		if( !create || !m_device )
			return false;

		// one RGBA texel is neutral for both ordinary RGB normals and Source's
		// DXT5-style AG unpacking. every normal sampler can safely share it.
		IDirect3DTexture9* normal{ };
		if( FAILED( create( m_device, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &normal, nullptr ) ) || !normal )
			return false;

		D3DLOCKED_RECT locked{ };
		if( FAILED( normal->LockRect( 0, &locked, nullptr, 0 ) ) || !locked.pBits ) {
			normal->Release( );
			return false;
		}

		// little-endian A8R8G8B8 memory stores this value as B=255,G=128,R=128,A=128.
		write_value( locked.pBits, std::uint32_t{ 0x808080ffu } );
		normal->UnlockRect( 0 );
		{
			std::unique_lock lock{ m_mutex };
			if( !m_flat_normal ) {
				m_flat_normal = normal;
				return true;
			}
		}

		// another caller won the publication race while the driver allocated.
		normal->Release( );
		return true;
	}

	bool FlatTextures::create_missing_companions( ) {
		auto create = reinterpret_cast< CreateTextureFn >( m_create_texture.previous( ) );
		if( !create || !m_device )
			return false;

		// snapshot records before entering DXVK. resource creation can invoke
		// callbacks, so the map lock must never span the driver call.
		std::vector< std::pair< IDirect3DTexture9*, texture_record_t > > pending;
		{
			std::shared_lock lock{ m_mutex };
			pending.reserve( m_textures.size( ) );
			for( const auto& pair : m_textures ) {
				if( !pair.second.m_flat )
					pending.push_back( pair );
			}
		}

		bool complete = true;
		for( const auto& pair : pending ) {
			const texture_record_t& record = pair.second;
			IDirect3DTexture9*		flat{ };
			if( FAILED( create( m_device,
								record.m_width,
								record.m_height,
								record.m_levels,
								record.m_usage,
								record.m_format,
								record.m_pool,
								&flat,
								nullptr ) ) ||
				!flat ) {
				complete = false;
				continue;
			}

			std::unique_lock lock{ m_mutex };
			auto			 current = m_textures.find( pair.first );
			if( current != m_textures.end( ) && !current->second.m_flat )
				current->second.m_flat = flat;
			else
				flat->Release( );
		}
		return complete;
	}

	bool FlatTextures::reload_textures( ) const {
		if( !m_material_system )
			return false;

		m_material_system->ReloadTextures( );
		return true;
	}

	bool FlatTextures::reload_materials( ) const {
		if( !m_material_system )
			return false;

		m_material_system->ReloadMaterials( );
		return true;
	}

	void FlatTextures::release_companions( ) {
		// detach under the lock and perform COM releases after unlocking.
		auto							  release = reinterpret_cast< ReleaseTextureFn >( m_release_texture.previous( ) );
		std::vector< IDirect3DTexture9* > companions;
		IDirect3DTexture9*				  flat_normal{ };
		{
			std::unique_lock lock{ m_mutex };
			flat_normal	  = m_flat_normal;
			m_flat_normal = nullptr;
			companions.reserve( m_textures.size( ) );
			for( auto& pair : m_textures ) {
				if( pair.second.m_flat )
					companions.push_back( pair.second.m_flat );
				pair.second.m_flat		  = nullptr;
				pair.second.m_color_valid = false;
			}
			m_locks.clear( );
			m_surface_locks.clear( );
		}

		if( release ) {
			for( IDirect3DTexture9* texture : companions )
				release( texture );
		}
		if( flat_normal )
			release ? release( flat_normal ) : flat_normal->Release( );
	}

	void FlatTextures::release_all_companions( ) {
		// teardown also forgets original resources and outstanding locks.
		auto							  release = reinterpret_cast< ReleaseTextureFn >( m_release_texture.previous( ) );
		std::vector< IDirect3DTexture9* > companions;
		IDirect3DTexture9*				  flat_normal{ };
		{
			std::unique_lock lock{ m_mutex };
			flat_normal	  = m_flat_normal;
			m_flat_normal = nullptr;
			companions.reserve( m_textures.size( ) );
			for( const auto& pair : m_textures ) {
				if( pair.second.m_flat )
					companions.push_back( pair.second.m_flat );
			}
			m_textures.clear( );
			m_locks.clear( );
			m_surfaces.clear( );
			m_surface_locks.clear( );
		}

		if( release ) {
			for( IDirect3DTexture9* texture : companions )
				release( texture );
		}
		if( flat_normal )
			release ? release( flat_normal ) : flat_normal->Release( );
	}

	HRESULT __stdcall FlatTextures::CreateTexture( IDirect3DDevice9* device, unsigned int width, unsigned int height, unsigned int levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* shared_handle ) {
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< CreateTextureFn >( instance->m_create_texture.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// create the original first so the optional feature cannot break allocation.
		const HRESULT result = original( device, width, height, levels, usage, format, pool, texture, shared_handle );
		if( FAILED( result ) || !texture || !*texture )
			return result;

		// only sampled, lockable color textures can receive safe companions.
		const DWORD excluded_usage = D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP;
		const bool	sampled_pool   = pool == D3DPOOL_DEFAULT || pool == D3DPOOL_MANAGED;
		if( usage & excluded_usage || !sampled_pool || !flat_texture_format_supported( format ) ||
			!instance->install_texture_hooks( *texture ) )
			return result;

		IDirect3DTexture9* flat{ };
		if( instance->enabled( ) ) {
			const HRESULT flat_result = original( device, width, height, levels, usage, format, pool, &flat, nullptr );
			if( FAILED( flat_result ) )
				flat = nullptr;
		}

		std::unique_lock lock{ instance->m_mutex };
		instance->m_textures.emplace( *texture, texture_record_t{ flat, format, width, height, levels, usage, pool } );
		return result;
	}

	HRESULT __stdcall FlatTextures::UpdateSurface( IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* source_rect, IDirect3DSurface9* destination, const POINT* destination_point ) {
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< UpdateSurfaceFn >( instance->m_update_surface.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// mirror successful Source uploads into the matching flat companion.
		const HRESULT result = original( device, source, source_rect, destination, destination_point );
		if( FAILED( result ) || !source || !destination || !instance->enabled( ) )
			return result;

		IDirect3DTexture9* destination_texture{ };
		if( FAILED( destination->GetContainer( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void** >( &destination_texture ) ) ) ||
			!destination_texture )
			return result;

		texture_record_t record;
		{
			std::shared_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( destination_texture );
			if( found == instance->m_textures.end( ) || !found->second.m_flat ) {
				destination_texture->Release( );
				return result;
			}
			record = found->second;
		}

		D3DSURFACE_DESC source_description{ };
		D3DSURFACE_DESC destination_description{ };
		if( FAILED( source->GetDesc( &source_description ) ) || FAILED( destination->GetDesc( &destination_description ) ) ||
			source_description.Format != record.m_format || destination_description.Format != record.m_format ) {
			destination_texture->Release( );
			return result;
		}

		unsigned int	   level  = std::numeric_limits< unsigned int >::max( );
		const unsigned int levels = destination_texture->GetLevelCount( );
		for( unsigned int candidate = 0; candidate < levels; ++candidate ) {
			D3DSURFACE_DESC candidate_description{ };
			if( SUCCEEDED( destination_texture->GetLevelDesc( candidate, &candidate_description ) ) &&
				candidate_description.Width == destination_description.Width &&
				candidate_description.Height == destination_description.Height ) {
				level = candidate;
				break;
			}
		}
		if( level == std::numeric_limits< unsigned int >::max( ) ) {
			destination_texture->Release( );
			return result;
		}

		const RECT		   upload_source	  = source_rect ? *source_rect : RECT{ 0, 0, static_cast< LONG >( source_description.Width ), static_cast< LONG >( source_description.Height ) };
		const POINT		   upload_destination = destination_point ? *destination_point : POINT{ 0, 0 };
		const unsigned int width			  = static_cast< unsigned int >( upload_source.right - upload_source.left );
		const unsigned int height			  = static_cast< unsigned int >( upload_source.bottom - upload_source.top );
		if( !width || !height || upload_destination.x < 0 || upload_destination.y < 0 ||
			static_cast< unsigned int >( upload_destination.x ) + width > destination_description.Width ||
			static_cast< unsigned int >( upload_destination.y ) + height > destination_description.Height ) {
			destination_texture->Release( );
			return result;
		}

		const RECT		   flat_rect{ upload_destination.x,
							  upload_destination.y,
							  upload_destination.x + static_cast< LONG >( width ),
							  upload_destination.y + static_cast< LONG >( height ) };
		IDirect3DSurface9* flat_surface{ };
		D3DLOCKED_RECT	   source_bits{ };
		D3DLOCKED_RECT	   destination_bits{ };
		if( FAILED( record.m_flat->GetSurfaceLevel( level, &flat_surface ) ) || !flat_surface ||
			FAILED( source->LockRect( &source_bits, &upload_source, D3DLOCK_READONLY ) ) ) {
			if( flat_surface )
				flat_surface->Release( );
			destination_texture->Release( );
			return result;
		}

		if( FAILED( flat_surface->LockRect( &destination_bits, &flat_rect, 0 ) ) ) {
			source->UnlockRect( );
			flat_surface->Release( );
			destination_texture->Release( );
			return result;
		}

		const bool complete_base = level == 0 && upload_source.left == 0 && upload_source.top == 0 && width == source_description.Width &&
								   height == source_description.Height && upload_destination.x == 0 && upload_destination.y == 0 &&
								   width == destination_description.Width && height == destination_description.Height;
		const flat_color_t color   = record.m_color_valid
										 ? record.m_color
										 : classify_flat_texture( record.m_format, source_bits.pBits, source_bits.Pitch, width, height );
		const bool		   written = write_flat_texture( record.m_format,
														 destination_bits.pBits,
														 destination_bits.Pitch,
														 source_bits.pBits,
														 source_bits.Pitch,
														 width,
														 height,
														 color );
		if( written )
			instance->m_device_uploads.fetch_add( 1, std::memory_order_relaxed );
		flat_surface->UnlockRect( );
		source->UnlockRect( );
		flat_surface->Release( );

		if( written && complete_base ) {
			std::unique_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( destination_texture );
			if( found != instance->m_textures.end( ) ) {
				found->second.m_color		= color;
				found->second.m_color_valid = true;
			}
		}
		destination_texture->Release( );
		return result;
	}

	HRESULT __stdcall FlatTextures::UpdateTexture( IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination ) {
		// mirror complete managed-to-default uploads across every mip level.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< UpdateTextureFn >( instance->m_update_texture.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( device, source, destination );
		if( FAILED( result ) || !source || !destination || !instance->enabled( ) || source->GetType( ) != D3DRTYPE_TEXTURE ||
			destination->GetType( ) != D3DRTYPE_TEXTURE )
			return result;

		texture_record_t record;
		{
			std::shared_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( destination ) );
			if( found == instance->m_textures.end( ) || !found->second.m_flat )
				return result;
			record = found->second;
		}

		auto*			   source_texture	  = static_cast< IDirect3DTexture9* >( source );
		const unsigned int source_levels	  = source_texture->GetLevelCount( );
		const unsigned int destination_levels = record.m_flat->GetLevelCount( );
		if( !source_levels || source_levels != destination_levels )
			return result;

		D3DSURFACE_DESC base_description{ };
		D3DLOCKED_RECT	base_source{ };
		if( FAILED( source_texture->GetLevelDesc( 0, &base_description ) ) ||
			FAILED( source_texture->LockRect( 0, &base_source, nullptr, D3DLOCK_READONLY ) ) )
			return result;

		// Source uploads carry the complete texture, so derive one color from
		// the full-resolution visible pixels and reuse it for every mip level.
		const flat_color_t color = classify_flat_texture( record.m_format, base_source.pBits, base_source.Pitch, base_description.Width, base_description.Height );
		source_texture->UnlockRect( 0 );

		bool complete = true;
		for( unsigned int level = 0; level < source_levels; ++level ) {
			D3DSURFACE_DESC description{ };
			D3DLOCKED_RECT	source_bits{ };
			D3DLOCKED_RECT	destination_bits{ };
			if( FAILED( source_texture->GetLevelDesc( level, &description ) ) ||
				FAILED( source_texture->LockRect( level, &source_bits, nullptr, D3DLOCK_READONLY ) ) ) {
				complete = false;
				break;
			}

			if( FAILED( record.m_flat->LockRect( level, &destination_bits, nullptr, 0 ) ) ) {
				source_texture->UnlockRect( level );
				complete = false;
				break;
			}

			complete = write_flat_texture( record.m_format,
										   destination_bits.pBits,
										   destination_bits.Pitch,
										   source_bits.pBits,
										   source_bits.Pitch,
										   description.Width,
										   description.Height,
										   color );
			record.m_flat->UnlockRect( level );
			source_texture->UnlockRect( level );
			if( !complete )
				break;
		}

		if( complete ) {
			instance->m_device_uploads.fetch_add( 1, std::memory_order_relaxed );
			std::unique_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( destination ) );
			if( found != instance->m_textures.end( ) ) {
				found->second.m_color		= color;
				found->second.m_color_valid = true;
			}
		}

		return result;
	}

	HRESULT __stdcall FlatTextures::SetTexture( IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture ) {
		// learn each texture's role during ordinary draws. later override passes can
		// then reuse the correct solid color or neutral-normal replacement.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< SetTextureFn >( instance->m_set_texture.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;
		if( g_vgui_paint_depth )
			return original( device, stage, texture );

		IDirect3DBaseTexture9* selected = texture;
		if( texture && stage < 32u && !g_material_excluded && instance->enabled( ) ) {
			const bool		 current_flat	= ( g_flat_sampler_mask & ( 1u << stage ) ) != 0;
			const bool		 current_normal = ( g_normal_sampler_mask & ( 1u << stage ) ) != 0;
			std::unique_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( texture ) );
			if( found != instance->m_textures.end( ) ) {
				// remember both roles so shadow and depth materials do not restore detail.
				found->second.m_flat_texture   = found->second.m_flat_texture || current_flat;
				found->second.m_normal_texture = found->second.m_normal_texture || current_normal;
			}

			const bool learned_normal = found != instance->m_textures.end( ) && found->second.m_normal_texture;
			const bool learned_flat	  = found != instance->m_textures.end( ) && found->second.m_flat_texture;
			if( ( current_normal || ( learned_normal && !learned_flat ) ) && instance->m_flat_normal ) {
				selected = instance->m_flat_normal;
				instance->m_substitutions.fetch_add( 1, std::memory_order_relaxed );
			} else if( found != instance->m_textures.end( ) && ( current_flat || learned_flat ) && found->second.m_flat &&
					   found->second.m_color_valid ) {
				selected = found->second.m_flat;
				instance->m_substitutions.fetch_add( 1, std::memory_order_relaxed );
			}
		}

		return original( device, stage, selected );
	}

	ULONG __stdcall FlatTextures::ReleaseTexture( IDirect3DTexture9* texture ) {
		// release bookkeeping only when DXVK destroys the original object.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< ReleaseTextureFn >( instance->m_release_texture.previous( ) ) : nullptr;
		if( !original )
			return 0;

		const ULONG references = original( texture );
		if( references )
			return references;

		IDirect3DTexture9* companion{ };
		{
			std::unique_lock lock{ instance->m_mutex };
			auto			 found = instance->m_textures.find( texture );
			if( found != instance->m_textures.end( ) ) {
				companion = found->second.m_flat;
				instance->m_textures.erase( found );
			}
			for( auto region = instance->m_locks.begin( ); region != instance->m_locks.end( ); ) {
				if( region->first.m_texture == texture )
					region = instance->m_locks.erase( region );
				else
					++region;
			}
			for( auto surface = instance->m_surfaces.begin( ); surface != instance->m_surfaces.end( ); ) {
				if( surface->second.m_texture == texture ) {
					instance->m_surface_locks.erase( surface->first );
					surface = instance->m_surfaces.erase( surface );
				} else {
					++surface;
				}
			}
		}

		if( companion )
			original( companion );
		return references;
	}

	HRESULT __stdcall FlatTextures::GetSurfaceLevel( IDirect3DTexture9* texture, unsigned int level, IDirect3DSurface9** surface ) {
		// remember which parent texture owns each surfaced mip level.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< GetSurfaceLevelFn >( instance->m_get_surface_level.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( texture, level, surface );
		if( FAILED( result ) || !surface || !*surface )
			return result;

		{
			std::shared_lock lock{ instance->m_mutex };
			if( instance->m_textures.find( texture ) == instance->m_textures.end( ) )
				return result;
		}

		if( !instance->install_surface_hooks( *surface ) )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		if( instance->m_textures.find( texture ) != instance->m_textures.end( ) )
			instance->m_surfaces[ *surface ] = { texture, level };
		return result;
	}

	HRESULT __stdcall FlatTextures::LockTexture( IDirect3DTexture9* texture, unsigned int level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags ) {
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< LockTextureFn >( instance->m_lock_texture.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// save writable upload memory until Source completes it in UnlockRect.
		const HRESULT result = original( texture, level, locked, rect, flags );
		if( FAILED( result ) || !locked || !locked->pBits || flags & D3DLOCK_READONLY )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		if( instance->m_textures.find( texture ) == instance->m_textures.end( ) )
			return result;

		locked_region_t region;
		region.m_source	  = *locked;
		region.m_flags	  = flags;
		region.m_has_rect = rect != nullptr;
		if( rect )
			region.m_rect = *rect;
		instance->m_locks[ { texture, level } ] = region;
		return result;
	}

	HRESULT __stdcall FlatTextures::UnlockTexture( IDirect3DTexture9* texture, unsigned int level ) {
		// copy the completed upload before the original texture is unlocked.
		FlatTextures* instance		  = m_instance;
		auto		  original_unlock = instance ? reinterpret_cast< UnlockTextureFn >( instance->m_unlock_texture.previous( ) ) : nullptr;
		auto		  original_lock	  = instance ? reinterpret_cast< LockTextureFn >( instance->m_lock_texture.previous( ) ) : nullptr;
		if( !original_unlock )
			return D3DERR_INVALIDCALL;

		texture_record_t record;
		locked_region_t	 region;
		bool			 found_upload = false;
		{
			std::unique_lock lock{ instance->m_mutex };
			auto			 texture_record = instance->m_textures.find( texture );
			auto			 locked_region	= instance->m_locks.find( { texture, level } );
			if( texture_record != instance->m_textures.end( ) && locked_region != instance->m_locks.end( ) ) {
				record = texture_record->second;
				region = locked_region->second;
				instance->m_locks.erase( locked_region );
				found_upload = true;
			}
		}

		if( found_upload && record.m_flat && original_lock ) {
			const unsigned int level_width	= mip_dimension( record.m_width, level );
			const unsigned int level_height = mip_dimension( record.m_height, level );
			const unsigned int width		= region.m_has_rect ? static_cast< unsigned int >( region.m_rect.right - region.m_rect.left ) : level_width;
			const unsigned int height		= region.m_has_rect ? static_cast< unsigned int >( region.m_rect.bottom - region.m_rect.top ) : level_height;
			flat_color_t	   color		= record.m_color_valid
												  ? record.m_color
												  : classify_flat_texture( record.m_format, region.m_source.pBits, region.m_source.Pitch, width, height );

			D3DLOCKED_RECT destination{ };
			const RECT*	   rect = region.m_has_rect ? &region.m_rect : nullptr;
			if( SUCCEEDED( original_lock( record.m_flat, level, &destination, rect, 0 ) ) ) {
				const bool written = write_flat_texture( record.m_format,
														 destination.pBits,
														 destination.Pitch,
														 region.m_source.pBits,
														 region.m_source.Pitch,
														 width,
														 height,
														 color );
				original_unlock( record.m_flat, level );
				if( written ) {
					instance->m_texture_uploads.fetch_add( 1, std::memory_order_relaxed );
					const bool complete_base =
						level == 0 && ( !region.m_has_rect || ( region.m_rect.left == 0 && region.m_rect.top == 0 &&
																region.m_rect.right == static_cast< LONG >( level_width ) &&
																region.m_rect.bottom == static_cast< LONG >( level_height ) ) );
					if( complete_base ) {
						std::unique_lock lock{ instance->m_mutex };
						auto			 current = instance->m_textures.find( texture );
						if( current != instance->m_textures.end( ) ) {
							current->second.m_color		  = color;
							current->second.m_color_valid = true;
						}
					}
				}
			}
		}

		return original_unlock( texture, level );
	}

	ULONG __stdcall FlatTextures::ReleaseSurface( IDirect3DSurface9* surface ) {
		// discard mappings only after the original surface reaches refcount zero.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< ReleaseSurfaceFn >( instance->m_release_surface.previous( ) ) : nullptr;
		if( !original )
			return 0;

		const ULONG references = original( surface );
		if( !references ) {
			std::unique_lock lock{ instance->m_mutex };
			instance->m_surfaces.erase( surface );
			instance->m_surface_locks.erase( surface );
		}
		return references;
	}

	HRESULT __stdcall FlatTextures::LockSurface( IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags ) {
		// surface uploads follow the same deferred copy rule as texture uploads.
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< LockSurfaceFn >( instance->m_lock_surface.previous( ) ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( surface, locked, rect, flags );
		if( FAILED( result ) || !locked || !locked->pBits || flags & D3DLOCK_READONLY || !instance->enabled( ) )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		auto			 mapped = instance->m_surfaces.find( surface );
		if( mapped == instance->m_surfaces.end( ) )
			return result;
		auto texture = instance->m_textures.find( mapped->second.m_texture );
		if( texture == instance->m_textures.end( ) || !texture->second.m_flat )
			return result;

		locked_region_t region;
		region.m_source	  = *locked;
		region.m_flags	  = flags;
		region.m_has_rect = rect != nullptr;
		if( rect )
			region.m_rect = *rect;
		instance->m_surface_locks[ surface ] = region;
		return result;
	}

	HRESULT __stdcall FlatTextures::UnlockSurface( IDirect3DSurface9* surface ) {
		// resolve the surface back to its companion texture and mip level.
		FlatTextures* instance		  = m_instance;
		auto		  original_unlock = instance ? reinterpret_cast< UnlockSurfaceFn >( instance->m_unlock_surface.previous( ) ) : nullptr;
		auto		  original_lock	  = instance ? reinterpret_cast< LockSurfaceFn >( instance->m_lock_surface.previous( ) ) : nullptr;
		if( !original_unlock )
			return D3DERR_INVALIDCALL;

		surface_record_t surface_record;
		texture_record_t texture_record;
		locked_region_t	 region;
		bool			 found_upload = false;
		{
			std::unique_lock lock{ instance->m_mutex };
			auto			 mapped = instance->m_surfaces.find( surface );
			auto			 locked = instance->m_surface_locks.find( surface );
			if( mapped != instance->m_surfaces.end( ) && locked != instance->m_surface_locks.end( ) ) {
				auto texture = instance->m_textures.find( mapped->second.m_texture );
				if( texture != instance->m_textures.end( ) && texture->second.m_flat ) {
					surface_record = mapped->second;
					texture_record = texture->second;
					region		   = locked->second;
					found_upload   = true;
				}
				instance->m_surface_locks.erase( locked );
			}
		}

		if( found_upload && original_lock ) {
			const unsigned int level_width	= mip_dimension( texture_record.m_width, surface_record.m_level );
			const unsigned int level_height = mip_dimension( texture_record.m_height, surface_record.m_level );
			const unsigned int width		= region.m_has_rect ? static_cast< unsigned int >( region.m_rect.right - region.m_rect.left ) : level_width;
			const unsigned int height		= region.m_has_rect ? static_cast< unsigned int >( region.m_rect.bottom - region.m_rect.top ) : level_height;
			const flat_color_t color		= texture_record.m_color_valid ? texture_record.m_color : classify_flat_texture( texture_record.m_format, region.m_source.pBits, region.m_source.Pitch, width, height );

			IDirect3DSurface9* flat_surface{ };
			if( SUCCEEDED( texture_record.m_flat->GetSurfaceLevel( surface_record.m_level, &flat_surface ) ) && flat_surface ) {
				D3DLOCKED_RECT destination{ };
				const RECT*	   rect = region.m_has_rect ? &region.m_rect : nullptr;
				if( SUCCEEDED( original_lock( flat_surface, &destination, rect, 0 ) ) ) {
					const bool written = write_flat_texture( texture_record.m_format,
															 destination.pBits,
															 destination.Pitch,
															 region.m_source.pBits,
															 region.m_source.Pitch,
															 width,
															 height,
															 color );
					original_unlock( flat_surface );
					if( written )
						instance->m_surface_uploads.fetch_add( 1, std::memory_order_relaxed );

					const bool complete_base = surface_record.m_level == 0 &&
											   ( !region.m_has_rect || ( region.m_rect.left == 0 && region.m_rect.top == 0 &&
																		 region.m_rect.right == static_cast< LONG >( level_width ) &&
																		 region.m_rect.bottom == static_cast< LONG >( level_height ) ) );
					if( written && complete_base ) {
						std::unique_lock lock{ instance->m_mutex };
						auto			 current = instance->m_textures.find( surface_record.m_texture );
						if( current != instance->m_textures.end( ) ) {
							current->second.m_color		  = color;
							current->second.m_color_valid = true;
						}
					}
				}
				flat_surface->Release( );
			}
		}

		return original_unlock( surface );
	}

	void __fastcall FlatTextures::BindMaterial( void* context, void*, sdk::Material* material, void* proxy_data ) {
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< BindMaterialFn >( instance->m_bind_material.previous( ) ) : nullptr;
		if( !original )
			return;

		// preserve the outer pass when the material system performs a nested Bind.
		const bool			previous_excluded	 = g_material_excluded;
		const std::uint32_t previous_flat_mask	 = g_flat_sampler_mask;
		const std::uint32_t previous_normal_mask = g_normal_sampler_mask;
		const bool			previous_active		 = g_material_pass_active;
		const bool			nested_bind			 = g_material_bind_depth++ != 0;

		// classification remains live through the deferred D3D texture binds.
		const material_pass_t pass = classify_material_pass( material );
		g_material_excluded		   = pass.m_excluded;
		g_flat_sampler_mask		   = pass.m_flat_samplers;
		g_normal_sampler_mask	   = pass.m_normal_samplers;
		g_material_pass_active	   = true;
		original( context, material, proxy_data );
		--g_material_bind_depth;

		// an inner Bind returns control to its outer material. a top-level Bind stays
		// active until the next material or the dedicated VGui_Paint boundary.
		if( nested_bind ) {
			g_material_excluded	   = previous_excluded;
			g_flat_sampler_mask	   = previous_flat_mask;
			g_normal_sampler_mask  = previous_normal_mask;
			g_material_pass_active = previous_active;
		}
	}

	void __fastcall FlatTextures::VguiPaint( void* render_view, void*, int mode ) {
		FlatTextures* instance = m_instance;
		auto		  original = instance ? reinterpret_cast< VguiPaintFn >( instance->m_vgui_paint.previous( ) ) : nullptr;
		if( !original )
			return;

		// VGui_Paint owns menus, console, HUD, cursor, and third-party overlays.
		// bypassing substitution for the whole boundary requires no material names.
		++g_vgui_paint_depth;
		clear_material_pass( );
		original( render_view, mode );
		clear_material_pass( );
		--g_vgui_paint_depth;
	}

}
