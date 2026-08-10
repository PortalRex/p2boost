#include "flat_material_pass.h"

#include <cstring>

namespace p2boost {
	namespace {

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

	}

	material_pass_t classify_material_pass( sdk::IMaterial* material ) noexcept {
		if( !material )
			return { };

		const char* group  = material->GetTextureGroupName( );
		const char* shader = material->GetShaderName( );

		// generated resources are render inputs or outputs, not authored colors.
		if( contains_text( group, "render target" ) || contains_text( group, "lightmap" ) ||
		    contains_text( group, "cubemap" ) )
			return { 0, 0, true };

		if( !shader )
			return { };

		// preserve control textures, lightmaps, framebuffer copies, and flashlight data.
		if( _stricmp( shader, "LightmappedGeneric" ) == 0 || _stricmp( shader, "WorldVertexTransition" ) == 0 ) {
			constexpr std::uint32_t flat   = ( 1u << 0u ) | ( 1u << 7u );
			constexpr std::uint32_t normal = ( 1u << 4u ) | ( 1u << 5u ) | ( 1u << 8u );
			return { flat, normal, false };
		}

		if( _stricmp( shader, "VertexLitGeneric" ) == 0 ) {
			// sampler 11 carries self-illumination and must keep its original mask.
			constexpr std::uint32_t flat   = 1u << 0u;
			constexpr std::uint32_t normal = 1u << 3u;
			return { flat, normal, false };
		}

		if( _stricmp( shader, "WorldTwoTextureBlend" ) == 0 )
			return { ( 1u << 0u ) | ( 1u << 3u ), 1u << 4u, false };

		// unknown shaders may carry video, particle masks, or lookup tables.
		return { };
	}

}
