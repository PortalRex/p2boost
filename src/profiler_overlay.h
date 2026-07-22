#pragma once

#include "qpc.h"
#include "sdk.h"

namespace p2boost {

	class Metrics;

	// Draws a low-frequency metrics summary through Valve's public engine ABI.
	class ProfilerOverlay {
	public:
		bool connect( sdk::CreateInterfaceFn interface_factory ) noexcept;
		void disconnect( ) noexcept;
		void set_enabled( bool enabled, Metrics& metrics ) noexcept;
		void draw( const Metrics& metrics, bool fixes_enabled, bool sar_loaded ) noexcept;

		bool available( ) const noexcept {
			return m_engine != nullptr;
		}

		bool enabled( ) const noexcept {
			return m_enabled;
		}

	private:
		void clear( ) const noexcept;

		void*		m_engine{ };
		Qpc::tick_t m_next_update{ };
		bool		m_enabled{ };
	};

}
