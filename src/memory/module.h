#pragma once

#include "address.h"
#include "pattern.h"

#include <Windows.h>

#include <cstddef>
#include <string>

namespace p2boost {

	// describes one image that is already mapped into the current process.
	// Module never loads or unloads a DLL; Portal 2 remains its sole owner.
	class Module {
	public:
		Module( ) noexcept = default;

		explicit Module( const wchar_t* name ) noexcept
		    : Module{ name ? GetModuleHandleW( name ) : nullptr } {
		}

		explicit Module( HMODULE handle ) noexcept
		    : m_base{ handle }, m_size{ pattern::image_size( m_base ) } {
		}

		// find the image that owns one address without changing its reference count.
		static Module containing( const void* address ) noexcept {
			HMODULE handle{ };
			if( !address || !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast< const wchar_t* >( address ), &handle ) )
				return { };

			return Module{ handle };
		}

		// a valid image has both a mapped base and a readable PE image size.
		explicit operator bool( ) const noexcept {
			return m_base && m_size;
		}

		Address base( ) const noexcept {
			return m_base;
		}

		Address end( ) const noexcept {
			return m_base.add( static_cast< std::ptrdiff_t >( m_size ) );
		}

		std::size_t size( ) const noexcept {
			return m_size;
		}

		HMODULE handle( ) const noexcept {
			return m_base.as< HMODULE >( );
		}

		// return the loader's canonical path for fingerprint and identity checks.
		std::wstring path( ) const {
			if( !( *this ) )
				return { };

			std::wstring value( 32768, L'\0' );
			const DWORD  length = GetModuleFileNameW( handle( ), value.data( ), static_cast< DWORD >( value.size( ) ) );
			if( !length || length >= value.size( ) )
				return { };

			value.resize( length );
			return value;
		}

		// prove that a complete byte range belongs to this mapped image.
		bool contains( Address address, std::size_t length = 1 ) const noexcept {
			if( !( *this ) || !address || !length || length > m_size )
				return false;

			const std::uintptr_t first = address.as< std::uintptr_t >( );
			const std::uintptr_t begin = m_base.as< std::uintptr_t >( );
			return first >= begin && first - begin <= m_size - length;
		}

		// validate a recovered function at its exact address.
		bool matches( Address address, const char* signature ) const {
			if( !signature )
				return false;

			const pattern::bytes_t bytes = pattern::parse( signature );
			return !bytes.empty( ) && contains( address, bytes.size( ) ) &&
			       pattern::find( address, bytes.size( ), bytes ) == address;
		}

		// scan only this image and reject ambiguous signatures.
		Address find( const char* signature ) const {
			return signature && *this ? pattern::find_unique( m_base, signature ) : Address{ };
		}

		// resolve one exported symbol without repeating Win32 casts at call sites.
		template < typename function_t >
		function_t export_function( const char* name ) const noexcept {
			if( !( *this ) || !name )
				return nullptr;

			return reinterpret_cast< function_t >( GetProcAddress( handle( ), name ) );
		}

	private:
		Address     m_base{ };
		std::size_t m_size{ };
	};

}
