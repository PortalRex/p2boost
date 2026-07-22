#pragma once

#include "hook.h"

namespace p2boost {

	class Metrics;

	// times the two portal-render call paths without changing their work.
	class PortalTrace {
	public:
		// dtor restores only this instance's call patches.
		~PortalTrace( );

		// install or remove the verified portal call hooks.
		bool install( Metrics* metrics );
		void remove( );

		bool installed( ) const noexcept {
			return m_primary_view.installed( ) && m_secondary_view.installed( );
		}

	private:
		using PortalRender_t = void( __thiscall* )( void*, void*, unsigned int );

		static void __fastcall PortalRender( void* self, void*, void* view, unsigned int clear_flags );

		static PortalTrace* m_instance;
		CallSitePatch		m_primary_view;
		CallSitePatch		m_secondary_view;
		PortalRender_t		m_original{ };
		Metrics*			m_metrics{ };
	};

}
