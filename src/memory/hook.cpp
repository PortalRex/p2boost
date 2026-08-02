#include "hook.h"

#include "address.h"
#include "x86.h"

#include <Windows.h>
#include <cstring>

namespace p2boost {
	namespace {

		enum class write_result_t {
			written,
			mismatch,
			protection_failed
		};

		// write only while the caller still owns the expected bytes.
		write_result_t write_bytes( void* address, const void* expected, const void* replacement, size_t size, DWORD protection ) {
			DWORD old_protection{ };
			if( !VirtualProtect( address, size, protection, &old_protection ) )
				return write_result_t::protection_failed;

			write_result_t result{ write_result_t::mismatch };
			if( std::memcmp( address, expected, size ) == 0 ) {
				std::memcpy( address, replacement, size );
				FlushInstructionCache( GetCurrentProcess( ), address, size );
				result = write_result_t::written;
			}

			DWORD ignored{ };
			VirtualProtect( address, size, old_protection, &ignored );
			return result;
		}

		// write one VMT entry with the same ownership rule.
		write_result_t write_method( void** method, void* expected, void* replacement ) {
			return write_bytes( method, &expected, &replacement, sizeof( replacement ), PAGE_READWRITE );
		}

	}

	VmtHook::~VmtHook( ) {
		remove( );
	}

	bool VmtHook::install( void** method, void* replacement ) {
		if( m_installed || !method || !replacement )
			return false;

		// preserve the current chain target before replacing the VMT entry.
		m_method	  = method;
		m_previous	  = *method;
		m_replacement = replacement;
		m_installed	  = write_method( m_method, m_previous, m_replacement ) == write_result_t::written;

		if( !m_installed ) {
			m_method	  = nullptr;
			m_previous	  = nullptr;
			m_replacement = nullptr;
		}

		return m_installed;
	}

	bool VmtHook::remove( ) {
		if( !m_installed )
			return true;

		// restore only while our replacement still owns the entry.
		write_result_t result{ write_method( m_method, m_replacement, m_previous ) };
		m_installed	  = false;
		m_method	  = nullptr;
		m_previous	  = nullptr;
		m_replacement = nullptr;
		return result == write_result_t::written;
	}

	CallSitePatch::~CallSitePatch( ) {
		remove( );
	}

	bool CallSitePatch::install( void* address, const std::vector< std::uint8_t >& expected, void* replacement ) {
		if( m_installed || !address || !replacement || expected.size( ) < 5 )
			return false;

		// replace the full verified instruction sequence with CALL and NOP padding.
		m_address  = static_cast< std::uint8_t* >( address );
		m_original = expected;
		m_patched  = x86::relative_call( Address{ m_address }, Address{ replacement }, expected.size( ) );
		if( m_patched.empty( ) )
			return false;

		m_installed = write_bytes( m_address, m_original.data( ), m_patched.data( ), m_patched.size( ), PAGE_EXECUTE_READWRITE ) ==
					  write_result_t::written;
		if( !m_installed )
			m_address = nullptr;

		return m_installed;
	}

	bool CallSitePatch::remove( ) {
		if( !m_installed )
			return true;

		// a later owner wins; never overwrite bytes changed after installation.
		write_result_t result{ write_bytes( m_address, m_patched.data( ), m_original.data( ), m_original.size( ), PAGE_EXECUTE_READWRITE ) };
		if( result == write_result_t::protection_failed )
			return false;

		m_installed = false;
		m_address	= nullptr;
		return result == write_result_t::written;
	}

}
