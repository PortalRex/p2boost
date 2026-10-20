#include "demo_provenance.h"

#include "address.h"
#include "flat_textures.h"
#include "game_profile.h"
#include "vmt.h"

#include <cstdio>

namespace p2boost {
	namespace {

		constexpr int signon_state_new{ 3 };
		constexpr int signon_state_full{ 6 };

	}

	DemoProvenance* DemoProvenance::m_instance{ };

	bool DemoProvenance::connect( const portal2_modules_t& modules, sdk::IVEngineClient* engine_client, const bool* fixes_enabled,
	                              const FlatTextures* flat_textures ) {
		disconnect( );
		if( m_instance || !engine_client || !fixes_enabled || !flat_textures )
			return false;

		// follow ClientCmd's verified call to Cbuf_AddText, which queues the
		// marker, instead of storing its RVA.
		const game::profile_t* game          = game::active( );
		const Address          cbuf_add_text = engine_client->ClientCmdAddress( ).add( game->xref_engine_client_cmd_cbuf_add_text_operand - 1 ).call_target( );
		if( !modules.m_engine.contains( cbuf_add_text ) )
			return false;

		// the demo recorder, read the way SAR reads it: the operand of
		// `mov ecx, [g_pClientDemoRecorder]` inside CClientState::Disconnect.
		const Address recorder_load = modules.m_engine.find( game->client_state_disconnect ).add( game->xref_client_state_disconnect_demo_recorder - 2 );
		if( recorder_load.at< std::uint16_t >( ) != 0x0d8b )
			return false;

		// SAR swaps the recorder's vtable for a copy, so its slots may point into
		// sar.dll. the copy keeps the RTTI locator in front of slot 0, which
		// still identifies the engine's class.
		void*  recorder = recorder_load.add( 2 ).to< Address >( ).to< void* >( );
		void** methods  = vmt_of( recorder );
		if( !methods || !modules.m_engine.contains( Address{ methods[ -1 ] } ) || !methods[ game->demo_recorder_set_signon_state_slot ] ||
		    !methods[ game->demo_recorder_start_recording_slot ] )
			return false;

		m_engine_client = engine_client;
		m_cbuf_add_text = cbuf_add_text.as< cbuf_add_text_fn_t >( );
		m_fixes_enabled = fixes_enabled;
		m_flat_textures = flat_textures;
		m_instance      = this;
		if( m_set_signon_state.install( vmt_slot( recorder, game->demo_recorder_set_signon_state_slot ), reinterpret_cast< void* >( &SetSignonState ) ) &&
		    m_start_recording.install( vmt_slot( recorder, game->demo_recorder_start_recording_slot ), reinterpret_cast< void* >( &StartRecording ) ) )
			return true;

		disconnect( );
		return false;
	}

	void DemoProvenance::disconnect( ) {
		m_start_recording.remove( );
		m_set_signon_state.remove( );
		if( m_instance == this )
			m_instance = nullptr;

		m_engine_client = nullptr;
		m_cbuf_add_text = nullptr;
		m_fixes_enabled = nullptr;
		m_flat_textures = nullptr;
		m_pending       = false;
	}

	// SAR's EngineDemoRecorder::SetSignonState: a level change while recording
	// (SIGNONSTATE_NEW) starts a new demo segment, and the segment is marked once
	// the client reaches SIGNONSTATE_FULL.
	int __fastcall DemoProvenance::SetSignonState( void* self, void*, int state ) {
		DemoProvenance* provenance = m_instance;
		if( !provenance )
			return 0;

		const bool was_recording = provenance->m_engine_client->IsRecordingDemo( );
		const int  result        = provenance->m_set_signon_state.previous< set_signon_state_fn_t >( )( self, state );

		if( was_recording && state == signon_state_new )
			provenance->m_pending = true;
		if( state == signon_state_full && provenance->m_pending ) {
			provenance->m_pending = false;
			provenance->mark( );
		}
		return result;
	}

	// SAR's EngineDemoRecorder::StartRecording: a new recording is marked at
	// its first SIGNONSTATE_FULL.
	int __fastcall DemoProvenance::StartRecording( void* self, void*, const char* filename, bool continuously ) {
		DemoProvenance* provenance = m_instance;
		if( !provenance )
			return 0;

		const int result      = provenance->m_start_recording.previous< start_recording_fn_t >( )( self, filename, continuously );
		provenance->m_pending = true;
		return result;
	}

	void DemoProvenance::mark( ) {
		char command[ 192 ]{ };
		std::snprintf( command,
		               sizeof( command ),
		               "echo \"p2boost %s (enabled=%d flat=%d backend=DXVK/Vulkan)\"",
		               P2BOOST_VERSION,
		               *m_fixes_enabled,
		               m_flat_textures->enabled( ) );

		// SAR's Engine::ExecuteCommand( command, true ): queue through
		// Cbuf_AddText, then an empty immediate command flushes the buffer.
		m_cbuf_add_text( m_engine_client->GetActiveSplitScreenPlayerSlot( ), command, 0 );
		m_engine_client->ExecuteClientCmd( "" );
	}

}
