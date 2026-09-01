#pragma once

#include <atomic>
#include <thread>

namespace p2boost {

	// downloads and installs the latest p2boost release from GitHub, mirroring
	// SAR's in-game updater. the release carries a raw p2boost.dll asset beside
	// the drop-in zip, so the swap needs no archive handling in the plugin.
	class AutoUpdate {
	public:
		~AutoUpdate( );

		AutoUpdate( )                              = default;
		AutoUpdate( const AutoUpdate& )            = delete;
		AutoUpdate& operator=( const AutoUpdate& ) = delete;

		// start a background check/install. force reinstalls even when the
		// running version already matches the latest release.
		void update( bool force );

		bool busy( ) const noexcept {
			return m_busy.load( std::memory_order_relaxed );
		}

	private:
		friend void AutoUpdateDispatch( AutoUpdate* updater, bool force );

		std::thread      m_worker;
		std::atomic_bool m_busy{ };
	};

	void AutoUpdateDispatch( AutoUpdate* updater, bool force );

}