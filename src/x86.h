#pragma once

#include "address.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace p2boost::x86 {

	// encode one x86 rel32 branch and pad the displaced span.
	__forceinline std::vector< std::uint8_t > relative_branch( Address instruction, Address target, std::uint8_t opcode, size_t size = 5 ) {
		if( !instruction || !target )
			return { };
		if( size < 5 )
			return { };

		std::int64_t displacement = static_cast< std::int64_t >( static_cast< std::uintptr_t >( target ) ) - static_cast< std::int64_t >( static_cast< std::uintptr_t >( instruction.add( 5 ) ) );
		if( displacement < ( std::numeric_limits< std::int32_t >::min )( ) || displacement > ( std::numeric_limits< std::int32_t >::max )( ) )
			return { };

		std::vector< std::uint8_t > bytes( size, 0x90 );
		bytes[ 0 ] = opcode;

		std::int32_t relative = static_cast< std::int32_t >( displacement );
		std::memcpy( bytes.data( ) + 1, &relative, sizeof( relative ) );
		return bytes;
	}

	// encode one x86 near call.
	__forceinline std::vector< std::uint8_t > relative_call( Address instruction, Address target, size_t size = 5 ) {
		return relative_branch( instruction, target, 0xe8, size );
	}

	// encode one x86 near jump.
	__forceinline std::vector< std::uint8_t > relative_jump( Address instruction, Address target, size_t size = 5 ) {
		return relative_branch( instruction, target, 0xe9, size );
	}

}
