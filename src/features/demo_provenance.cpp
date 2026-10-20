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

		// follow ClientCmd's verified calls instead of storing RVAs: Cbuf_AddText
		// queues the marker, and GetClientState leads to the demo recorder the
		// way SAR finds it, through the global CClientState::Disconnect reads.
		const game::profile_t* game             = game::active( );
		const Address          client_cmd       = engine_client->ClientCmdAddress( );
		const Address          cbuf_add_text    = client_cmd.add( game->xref_engine_client_cmd_cbuf_add_text_operand - 1 ).call_target( );
		const Address          get_client_state = client_cmd.add( game->xref_engine_client_cmd_get_client_state_operand - 1 ).call_target( );
		if( !modules.m_engine.contains( cbuf_add_text ) || !modules.m_engine.contains( get_client_state ) )
			return false;

		void* client_state = get_client_state.as< get_client_state_fn_t >( )( );
		if( !client_state )
			return false;

		// the operand of `mov ecx, [g_pClientDemoRecorder]` inside Disconnect.
		const Address client_disconnect{ vmt_of( client_state )[ game->client_state_disconnect_slot ] };
		const Address recorder_load = client_disconnect.add( game->xref_client_state_disconnect_demo_recorder - 2 );
		if( !modules.m_engine.contains( client_disconnect ) || recorder_load.at< std::uint16_t >( ) != 0x0d8b )
			return false;

		void* recorder = recorder_load.add( 2 ).to< Address >( ).to< void* >( );
		if( !recorder || !modules.m_engine.contains( Address{ vmt_of( recorder )[ game->demo_recorder_set_signon_state_slot ] } ) ||
		    !modules.m_engine.contains( Address{ vmt_of( recorder )[ game->demo_recorder_start_recording_slot ] } ) )
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
