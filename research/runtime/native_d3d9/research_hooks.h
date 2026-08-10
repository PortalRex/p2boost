#pragma once

#include "hook.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2boost {

	// patches one embedded x86 function pointer for an archived probe.
	class PointerImmediatePatch {
	private:
		std::uint8_t*               m_address{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		void*                       m_previous{ };
		bool                        m_installed{ };

	public:
		PointerImmediatePatch( ) = default;
		~PointerImmediatePatch( );

		PointerImmediatePatch( const PointerImmediatePatch& )            = delete;
		PointerImmediatePatch& operator=( const PointerImmediatePatch& ) = delete;

		bool install( void* address, const std::vector< std::uint8_t >& expected, size_t pointer_offset, void* replacement );
		bool remove( );

		void* previous( ) const noexcept {
			return m_previous;
		}

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

	// owns a five-byte x86 detour and its displaced-byte trampoline.
	class InlineDetour {
	private:
		std::uint8_t*               m_address{ };
		std::uint8_t*               m_trampoline{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		bool                        m_installed{ };

	public:
		InlineDetour( ) = default;
		~InlineDetour( );

		InlineDetour( const InlineDetour& )            = delete;
		InlineDetour& operator=( const InlineDetour& ) = delete;

		bool install( void* address, const std::vector< std::uint8_t >& expected, void* replacement );
		bool remove( );

		void* previous( ) const noexcept {
			return m_trampoline;
		}

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

}
