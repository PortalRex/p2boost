#include "hook.h"

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

		// write only while the caller still owns the expected bytes.
		WriteResult write_bytes(
			void* address, const void* expected, const void* replacement, size_t size, DWORD protection ) {
			DWORD old_protection{ };
			if( !VirtualProtect( address, size, protection, &old_protection ) )
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

		// write one vtable slot with the same ownership rule.
		WriteResult write_slot( void** slot, void* expected, void* replacement ) {
			return write_bytes( slot, &expected, &replacement, sizeof( replacement ), PAGE_READWRITE );
		}

	}

	VtableSlotHook::~VtableSlotHook( ) {
		remove( );
	}

	bool VtableSlotHook::install( void** slot, void* replacement ) {
		if( m_installed || !slot || !replacement )
			return false;

		m_slot		  = slot;
		m_previous	  = *slot;
		m_replacement = replacement;
		m_installed	  = write_slot( m_slot, m_previous, m_replacement ) == WriteResult::written;

		if( !m_installed ) {
			m_slot		  = nullptr;
			m_previous	  = nullptr;
			m_replacement = nullptr;
		}

		return m_installed;
	}

	bool VtableSlotHook::remove( ) {
		if( !m_installed )
			return true;

		WriteResult result{ write_slot( m_slot, m_replacement, m_previous ) };
		m_installed	  = false;
		m_slot		  = nullptr;
		m_previous	  = nullptr;
		m_replacement = nullptr;
		return result == WriteResult::written;
	}

	CallSitePatch::~CallSitePatch( ) {
		remove( );
	}

	bool CallSitePatch::install( void* address, const std::vector< std::uint8_t >& expected, void* replacement ) {
		if( m_installed || !address || !replacement || expected.size( ) < 5 )
			return false;

		m_address  = static_cast< std::uint8_t* >( address );
		m_original = expected;
		m_patched  = x86::relative_call( Address{ m_address }, Address{ replacement }, expected.size( ) );
		if( m_patched.empty( ) )
			return false;

		m_installed = write_bytes( m_address,
						  m_original.data( ),
						  m_patched.data( ),
						  m_patched.size( ),
						  PAGE_EXECUTE_READWRITE ) == WriteResult::written;
		if( !m_installed )
			m_address = nullptr;

		return m_installed;
	}

	bool CallSitePatch::remove( ) {
		if( !m_installed )
			return true;

		WriteResult result{ write_bytes( m_address,
			m_patched.data( ),
			m_original.data( ),
			m_original.size( ),
			PAGE_EXECUTE_READWRITE ) };
		if( result == WriteResult::protection_failed )
			return false;

		m_installed = false;
		m_address	= nullptr;
		return result == WriteResult::written;
	}

	PointerImmediatePatch::~PointerImmediatePatch( ) {
		remove( );
	}

	bool PointerImmediatePatch::install(
		void* address, const std::vector< std::uint8_t >& expected, size_t pointer_offset, void* replacement ) {
		if( m_installed || !address || !replacement || pointer_offset + sizeof( std::uint32_t ) > expected.size( ) )
			return false;

		std::uintptr_t replacement_value{ reinterpret_cast< std::uintptr_t >( replacement ) };
		if( replacement_value > UINT32_MAX )
			return false;

		m_address  = static_cast< std::uint8_t* >( address );
		m_original = expected;
		m_patched  = expected;

		std::uint32_t previous{ };
		std::memcpy( &previous, m_original.data( ) + pointer_offset, sizeof( previous ) );
		m_previous = reinterpret_cast< void* >( static_cast< std::uintptr_t >( previous ) );

		std::uint32_t replacement32{ static_cast< std::uint32_t >( replacement_value ) };
		std::memcpy( m_patched.data( ) + pointer_offset, &replacement32, sizeof( replacement32 ) );

		m_installed = write_bytes( m_address,
						  m_original.data( ),
						  m_patched.data( ),
						  m_patched.size( ),
						  PAGE_EXECUTE_READWRITE ) == WriteResult::written;
		if( !m_installed ) {
			m_address  = nullptr;
			m_previous = nullptr;
		}

		return m_installed;
	}

	bool PointerImmediatePatch::remove( ) {
		if( !m_installed )
			return true;

		WriteResult result{ write_bytes( m_address,
			m_patched.data( ),
			m_original.data( ),
			m_original.size( ),
			PAGE_EXECUTE_READWRITE ) };
		if( result == WriteResult::protection_failed )
			return false;

		m_installed = false;
		m_address	= nullptr;
		m_previous	= nullptr;
		return result == WriteResult::written;
	}

	InlineDetour::~InlineDetour( ) {
		remove( );
	}

	bool InlineDetour::install( void* address, const std::vector< std::uint8_t >& expected, void* replacement ) {
		if( m_installed || !address || !replacement || expected.size( ) != 5 )
			return false;

		m_address = static_cast< std::uint8_t* >( address );
		if( std::memcmp( m_address, expected.data( ), expected.size( ) ) != 0 ) {
			m_address = nullptr;
			return false;
		}

		// copy the displaced bytes and jump back after them.
		m_trampoline = static_cast< std::uint8_t* >(
			VirtualAlloc( nullptr, expected.size( ) + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
		if( !m_trampoline ) {
			m_address = nullptr;
			return false;
		}

		m_original = expected;
		std::memcpy( m_trampoline, m_original.data( ), m_original.size( ) );

		std::vector< std::uint8_t > return_jump{ x86::relative_jump( Address{ m_trampoline + m_original.size( ) },
			Address{ m_address + m_original.size( ) } ) };
		m_patched = x86::relative_jump( Address{ m_address }, Address{ replacement } );
		if( return_jump.empty( ) || m_patched.empty( ) ) {
			VirtualFree( m_trampoline, 0, MEM_RELEASE );
			m_trampoline = nullptr;
			m_address	 = nullptr;
			return false;
		}

		std::memcpy( m_trampoline + m_original.size( ), return_jump.data( ), return_jump.size( ) );
		FlushInstructionCache( GetCurrentProcess( ), m_trampoline, m_original.size( ) + return_jump.size( ) );

		m_installed = write_bytes( m_address,
						  m_original.data( ),
						  m_patched.data( ),
						  m_patched.size( ),
						  PAGE_EXECUTE_READWRITE ) == WriteResult::written;
		if( !m_installed ) {
			VirtualFree( m_trampoline, 0, MEM_RELEASE );
			m_trampoline = nullptr;
			m_address	 = nullptr;
		}

		return m_installed;
	}

	bool InlineDetour::remove( ) {
		if( !m_installed )
			return true;

		WriteResult result{ write_bytes( m_address,
			m_patched.data( ),
			m_original.data( ),
			m_original.size( ),
			PAGE_EXECUTE_READWRITE ) };
		if( result == WriteResult::protection_failed )
			return false;

		// preserve the trampoline when a later hook still chains through it.
		if( result == WriteResult::written )
			VirtualFree( m_trampoline, 0, MEM_RELEASE );

		m_installed	 = false;
		m_address	 = nullptr;
		m_trampoline = nullptr;
		return result == WriteResult::written;
	}

}
