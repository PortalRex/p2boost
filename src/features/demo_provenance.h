#pragma once

#include "interfaces.h"
#include "portal2_context.h"

namespace p2boost {

	// writes one human-readable build marker through the same command-buffer
	// path SAR uses, making the marker visible during real demo playback.
	class DemoProvenance {
	public:
		bool connect( const portal2_modules_t& modules, sdk::IVEngineClient* engine_client );
		void disconnect( );
		void reset( ) noexcept;
		void update( const char* version, bool fixes_enabled, bool flat_textures_enabled );

		bool available( ) const noexcept {
			return m_engine_client && m_cbuf_add_text;
		}

	private:
		using cbuf_add_text_fn_t = void( __cdecl* )( int, const char*, int );

		sdk::IVEngineClient* m_engine_client{ };
		cbuf_add_text_fn_t m_cbuf_add_text{ };
		unsigned int m_recording_frames{ };
		bool m_marked{ };
	};

}
