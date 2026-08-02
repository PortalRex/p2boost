#pragma once

#include "address.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace p2boost::pattern {

	// stores one parsed byte or wildcard from a signature.
	using byte_t  = std::pair< std::uint8_t, bool >;
	using bytes_t = std::vector< byte_t >;

	// convert a readable ida-style signature once before scanning.
	inline bytes_t parse( const std::string& signature ) {
		bytes_t			  out;
		std::stringstream stream{ signature };
		std::string		  token;

		while( stream >> token ) {
			// a question mark accepts any byte, including relocated operands.
			if( token == "?" || token == "??" ) {
				out.emplace_back( std::uint8_t{ }, true );
				continue;
			}

			// every fixed token must contain two hexadecimal digits.
			if( token.size( ) != 2 || !std::isxdigit( static_cast< unsigned char >( token[ 0 ] ) ) ||
				!std::isxdigit( static_cast< unsigned char >( token[ 1 ] ) ) )
				return { };

			out.emplace_back( static_cast< std::uint8_t >( std::strtoul( token.c_str( ), nullptr, 16 ) ), false );
		}

		return out;
	}

	// return the mapped size recorded by a loaded pe image.
	inline size_t image_size( Address module ) noexcept {
		if( !module )
			return 0;

		const auto* dos = module.as< const IMAGE_DOS_HEADER* >( );
		if( dos->e_magic != IMAGE_DOS_SIGNATURE )
			return 0;

		const auto* nt = module.add( dos->e_lfanew ).as< const IMAGE_NT_HEADERS32* >( );
		if( nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC )
			return 0;

		return nt->OptionalHeader.SizeOfImage;
	}

	// find the first signature inside a validated memory range.
	inline Address find( Address start, size_t length, const bytes_t& signature ) noexcept {
		if( !start || !length || signature.empty( ) || signature.size( ) > length )
			return { };

		auto* begin = start.as< std::uint8_t* >( );
		auto* end	= begin + length;
		auto  found = std::search( begin, end, signature.begin( ), signature.end( ), []( std::uint8_t value, const byte_t& expected ) {
			 return expected.second || value == expected.first;
		 } );

		return found == end ? Address{ } : Address{ found };
	}

	// require exactly one match so a weak signature can never select a hook.
	inline Address find_unique( Address module, const std::string& text ) {
		// scanning the mapped image accounts for loader relocations wildcarded above.
		const size_t  length	= image_size( module );
		const bytes_t signature = parse( text );
		const Address first		= find( module, length, signature );
		if( !first )
			return { };

		// a second match makes the signature ambiguous and therefore unusable.
		const Address next		= first.add( 1 );
		const size_t  remaining = length - static_cast< size_t >( next.as< std::uintptr_t >( ) - module.as< std::uintptr_t >( ) );
		return find( next, remaining, signature ) ? Address{ } : first;
	}

	// locate one relocated function pointer without relying on a VMT RVA.
	inline Address find_pointer_unique( Address module, Address target ) noexcept {
		const size_t length = image_size( module );
		if( !module || !target || length < sizeof( std::uintptr_t ) )
			return { };

		Address match;
		for( size_t offset = 0; offset + sizeof( std::uintptr_t ) <= length; ++offset ) {
			const Address candidate = module.add( static_cast< std::ptrdiff_t >( offset ) );
			if( candidate.at< std::uintptr_t >( ) != target.as< std::uintptr_t >( ) )
				continue;
			if( match )
				return { };
			match = candidate;
		}

		return match;
	}

}
