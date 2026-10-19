#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2boost {

	// hooks one VMT entry while preserving the previous hook in the chain.
	class VmtHook {
	private:
		void** m_method{ };
		void*  m_previous{ };
		void*  m_replacement{ };
		bool   m_installed{ };

	public:
		// default ctor / dtor.
		VmtHook( ) = default;
		~VmtHook( );

		VmtHook( const VmtHook& )            = delete;
		VmtHook& operator=( const VmtHook& ) = delete;

		// install or remove this virtual method replacement.
		bool install( void** method, void* replacement );
		bool remove( );

		// expose the preserved chain target.
		void* previous( ) const noexcept {
			return m_previous;
		}

		template < typename function_t >
		function_t previous( ) const noexcept {
			return reinterpret_cast< function_t >( m_previous );
		}

		void** method( ) const noexcept {
			return m_method;
		}

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

	// rewrites one verified x86 call site.
	class CallSitePatch {
	private:
		std::uint8_t*               m_address{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		bool                        m_installed{ };

	public:
		// default ctor / dtor.
		CallSitePatch( ) = default;
		~CallSitePatch( );

		CallSitePatch( const CallSitePatch& )            = delete;
		CallSitePatch& operator=( const CallSitePatch& ) = delete;

		// install or remove this call patch.
		bool install( void* address, const std::vector< std::uint8_t >& expected, void* replacement );
		bool remove( );

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

	// rewrites one verified byte sequence in place, e.g. a conditional branch.
	class BytePatch {
	private:
		std::uint8_t*               m_address{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		bool                        m_installed{ };

	public:
		// default ctor / dtor.
		BytePatch( ) = default;
		~BytePatch( );

		BytePatch( const BytePatch& )            = delete;
		BytePatch& operator=( const BytePatch& ) = delete;

		// install or remove this in-place patch. expected must match exactly.
		bool install( void* address, const std::vector< std::uint8_t >& expected, const std::vector< std::uint8_t >& replacement );
		bool remove( );

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

}
