#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2boost {

	// replaces one vtable slot while preserving later hook owners.
	class VtableSlotHook {
	private:
		void** m_slot{ };
		void*  m_previous{ };
		void*  m_replacement{ };
		bool   m_installed{ };

	public:
		// default ctor / dtor.
		VtableSlotHook( ) = default;
		~VtableSlotHook( );

		VtableSlotHook( const VtableSlotHook& )			   = delete;
		VtableSlotHook& operator=( const VtableSlotHook& ) = delete;

		// install or remove this slot replacement.
		bool install( void** slot, void* replacement );
		bool remove( );

		// expose the preserved chain target.
		void* previous( ) const noexcept {
			return m_previous;
		}

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

	// rewrites one verified x86 call site.
	class CallSitePatch {
	private:
		std::uint8_t*				m_address{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		bool						m_installed{ };

	public:
		// default ctor / dtor.
		CallSitePatch( ) = default;
		~CallSitePatch( );

		CallSitePatch( const CallSitePatch& )			 = delete;
		CallSitePatch& operator=( const CallSitePatch& ) = delete;

		// install or remove this call patch.
		bool install( void* address, const std::vector< std::uint8_t >& expected, void* replacement );
		bool remove( );

		bool installed( ) const noexcept {
			return m_installed;
		}
	};

	// replaces one verified imm32 function pointer.
	class PointerImmediatePatch {
	private:
		std::uint8_t*				m_address{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		void*						m_previous{ };
		bool						m_installed{ };

	public:
		// default ctor / dtor.
		PointerImmediatePatch( ) = default;
		~PointerImmediatePatch( );

		PointerImmediatePatch( const PointerImmediatePatch& )			 = delete;
		PointerImmediatePatch& operator=( const PointerImmediatePatch& ) = delete;

		// install or remove this immediate patch.
		bool install(
			void* address, const std::vector< std::uint8_t >& expected, size_t pointer_offset, void* replacement );
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
		std::uint8_t*				m_address{ };
		std::uint8_t*				m_trampoline{ };
		std::vector< std::uint8_t > m_original;
		std::vector< std::uint8_t > m_patched;
		bool						m_installed{ };

	public:
		// default ctor / dtor.
		InlineDetour( ) = default;
		~InlineDetour( );

		InlineDetour( const InlineDetour& )			   = delete;
		InlineDetour& operator=( const InlineDetour& ) = delete;

		// install or remove this entry detour.
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
