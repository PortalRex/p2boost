#pragma once

#include "interfaces.h"

namespace p2boost {

	// writes one human-readable build marker through the same command-buffer
	// path SAR uses, making the marker visible during real demo playback.
	class DemoProvenance {
	public:
		bool connect( sdk::EngineClient* engine_client );
		void disconnect( );
		void reset( ) noexcept;
		void update( const char* version, bool fixes_enabled, bool flat_textures_enabled );

		bool available( ) const noexcept {
			return m_engine_client && m_cbuf_add_text;
		}

	private:
		using CbufAddTextFn = void( __cdecl* )( int, const char*, int );

		sdk::EngineClient* m_engine_client{ };
		CbufAddTextFn	   m_cbuf_add_text{ };
		unsigned int	   m_recording_frames{ };
		bool			   m_marked{ };
	};

}
