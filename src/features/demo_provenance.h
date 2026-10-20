#pragma once

#include "hook.h"
#include "interfaces.h"
#include "portal2_context.h"

namespace p2boost {

	class FlatTextures;

	// writes one human-readable build marker into every demo segment, exactly
	// the way SAR writes its own: when the client demo recorder reaches
	// SIGNONSTATE_FULL after a recording started or a level changed, an echo
	// goes through Cbuf_AddText and is flushed at once. both hooks sit on the
	// client's CDemoRecorder, so a co-op guest's demos are marked too.
	class DemoProvenance {
	public:
		bool connect( const portal2_modules_t& modules, sdk::IVEngineClient* engine_client, const bool* fixes_enabled,
		              const FlatTextures* flat_textures );
		void disconnect( );

		bool available( ) const noexcept {
			return m_set_signon_state.installed( ) && m_start_recording.installed( );
		}

	private:
		using cbuf_add_text_fn_t    = void( __cdecl* )( int, const char*, int );
		using set_signon_state_fn_t = int( __thiscall* )( void*, int );
		using start_recording_fn_t  = int( __thiscall* )( void*, const char*, bool );

		static int __fastcall SetSignonState( void* self, void*, int state );
		static int __fastcall StartRecording( void* self, void*, const char* filename, bool continuously );
		void mark( );

		static DemoProvenance* m_instance;

		sdk::IVEngineClient* m_engine_client{ };
		cbuf_add_text_fn_t   m_cbuf_add_text{ };
		const bool*          m_fixes_enabled{ };
		const FlatTextures*  m_flat_textures{ };
		VmtHook              m_set_signon_state;
		VmtHook              m_start_recording;
		bool                 m_pending{ };
	};

}
