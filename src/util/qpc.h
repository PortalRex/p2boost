#pragma once

#include <Windows.h>

namespace p2boost {

	// wrap the invariant Windows performance counter used by all traces.
	class Qpc {
	public:
		using tick_t = long long;

		// read the current counter without converting units.
		__forceinline static tick_t now( ) noexcept {
			LARGE_INTEGER value{ };
			QueryPerformanceCounter( &value );
			return value.QuadPart;
		}

		// return the process-wide counter frequency.
		__forceinline static tick_t frequency( ) noexcept {
			static const tick_t value = [] {
				LARGE_INTEGER frequency{ };
				QueryPerformanceFrequency( &frequency );
				return frequency.QuadPart;
			}( );

			return value;
		}

		// convert counter ticks to milliseconds.
		__forceinline static double milliseconds( tick_t ticks ) noexcept {
			return static_cast< double >( ticks ) * 1000.0 / static_cast< double >( frequency( ) );
		}

		// convert milliseconds to counter ticks.
		__forceinline static tick_t ticks( double milliseconds ) noexcept {
			return static_cast< tick_t >( milliseconds * static_cast< double >( frequency( ) ) / 1000.0 );
		}
	};

	// measure one already-selected code path.
	class Stopwatch {
	private:
		Qpc::tick_t m_begin;

	public:
		__forceinline explicit Stopwatch( bool enabled = true ) noexcept
		    : m_begin{ enabled ? Qpc::now( ) : 0 } {
		}

		// return elapsed counter ticks.
		__forceinline Qpc::tick_t elapsed( ) const noexcept {
			return m_begin ? Qpc::now( ) - m_begin : 0;
		}

		// return elapsed milliseconds.
		__forceinline double elapsed_ms( ) const noexcept {
			return Qpc::milliseconds( elapsed( ) );
		}
	};

}
