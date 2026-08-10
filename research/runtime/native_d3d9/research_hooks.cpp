#include "research_hooks.h"

#include "address.h"
#include "x86.h"

#include <Windows.h>
#include <cstring>

namespace p2boost {
	namespace {

		enum class WriteResult {
			written,
			mismatch,
			protection_failed
		};

		// research probes obey the same chain-ownership rule as production hooks.
		WriteResult write_bytes( void* address, const void* expected, const void* replacement, size_t size ) {
			DWORD old_protection{ };
			if( !VirtualProtect( address, size, PAGE_EXECUTE_READWRITE, &old_protection ) )
				return WriteResult::protection_failed;

			WriteResult result{ WriteResult::mismatch };
			if( std::memcmp( address, expected, size ) == 0 ) {
				std::memcpy( address, replacement, size );
				FlushInstructionCache( GetCurrentProcess( ), address, size );
				result = WriteResult::written;
			}

			DWORD ignored{ };
			VirtualProtect( address, size, old_protection, &ignored );
			return result;
		}

	}

	PointerImmediatePatch::~PointerImmediatePatch( ) {
		remove( );
	}

	bool PointerImmediatePatch::install( void* address, const std::vector< std::uint8_t >& expected, size_t pointer_offset, void* replacement ) {
		if( m_installed || !address || !replacement || pointer_offset + sizeof( std::uint32_t ) > expected.size( ) )
			return false;

		const std::uintptr_t replacement_value{ reinterpret_cast< std::uintptr_t >( replacement ) };
		if( replacement_value > UINT32_MAX )
			return false;

		m_address = static_cast< std::uint8_t* >( address );
		m_original = expected;
		m_patched = expected;

		std::uint32_t previous{ };
		std::memcpy( &previous, m_original.data( ) + pointer_offset, sizeof( previous ) );
		m_previous = reinterpret_cast< void* >( static_cast< std::uintptr_t >( previous ) );

		const std::uint32_t replacement32{ static_cast< std::uint32_t >( replacement_value ) };
		std::memcpy( m_patched.data( ) + pointer_offset, &replacement32, sizeof( replacement32 ) );

		m_installed = write_bytes( m_address, m_original.data( ), m_patched.data( ), m_patched.size( ) ) == WriteResult::written;
		if( !m_installed ) {
			m_address = nullptr;
			m_previous = nullptr;
		}

		return m_installed;
	}

	bool PointerImmediatePatch::remove( ) {
		if( !m_installed )
			return true;

		const WriteResult result{ write_bytes( m_address, m_patched.data( ), m_original.data( ), m_original.size( ) ) };
		if( result == WriteResult::protection_failed )
			return false;

		m_installed = false;
		m_address = nullptr;
		m_previous = nullptr;
		return result == WriteResult::written;
	}

	InlineDetour::~InlineDetour( ) {
		remove( );
	}

	bool InlineDetour::install( void* address, const std::vector< std::uint8_t >& expected, void* replacement ) {
		if( m_installed || !address || !replacement || expected.size( ) < 5 )
			return false;

		m_address = static_cast< std::uint8_t* >( address );
		if( std::memcmp( m_address, expected.data( ), expected.size( ) ) != 0 ) {
			m_address = nullptr;
			return false;
		}

		m_trampoline = static_cast< std::uint8_t* >( VirtualAlloc( nullptr, expected.size( ) + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !m_trampoline ) {
			m_address = nullptr;
			return false;
		}

		m_original = expected;
		std::memcpy( m_trampoline, m_original.data( ), m_original.size( ) );

		const std::vector< std::uint8_t > return_jump{ x86::relative_jump( Address{ m_trampoline + m_original.size( ) }, Address{ m_address + m_original.size( ) } ) };
		m_patched = x86::relative_jump( Address{ m_address }, Address{ replacement }, expected.size( ) );
		if( return_jump.empty( ) || m_patched.empty( ) ) {
			VirtualFree( m_trampoline, 0, MEM_RELEASE );
			m_trampoline = nullptr;
			m_address = nullptr;
			return false;
		}

		std::memcpy( m_trampoline + m_original.size( ), return_jump.data( ), return_jump.size( ) );
		FlushInstructionCache( GetCurrentProcess( ), m_trampoline, m_original.size( ) + return_jump.size( ) );

		m_installed = write_bytes( m_address, m_original.data( ), m_patched.data( ), m_patched.size( ) ) == WriteResult::written;
		if( !m_installed ) {
			VirtualFree( m_trampoline, 0, MEM_RELEASE );
			m_trampoline = nullptr;
			m_address = nullptr;
		}

		return m_installed;
	}

	bool InlineDetour::remove( ) {
		if( !m_installed )
			return true;

		const WriteResult result{ write_bytes( m_address, m_patched.data( ), m_original.data( ), m_original.size( ) ) };
		if( result == WriteResult::protection_failed )
			return false;

		// preserve a trampoline that may still be referenced by a later hook.
		if( result == WriteResult::written )
			VirtualFree( m_trampoline, 0, MEM_RELEASE );

		m_installed = false;
		m_address = nullptr;
		m_trampoline = nullptr;
		return result == WriteResult::written;
	}

}
