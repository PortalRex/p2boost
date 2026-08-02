#include "demo_provenance.h"

#include "address.h"
#include "pattern.h"
#include "portal2.h"

#include <Windows.h>

#include <cstdio>

namespace p2boost {

	bool DemoProvenance::connect( sdk::EngineClient* engine_client ) {
		disconnect( );
		if( !engine_client )
			return false;

		// follow ClientCmd's verified rel32 instead of storing Cbuf_AddText's RVA.
		// the opcode check fails closed if a future build changes this call site.
		const Address client_cmd{ engine_client->method( portal2::vmt::engine_client_cmd ) };
		const Address operand = client_cmd.add( portal2::xref::engine_client_cmd_cbuf_add_text_operand );
		if( operand.sub( 1 ).at< unsigned char >( ) != 0xe8 )
			return false;

		const Address target = operand.sub( 1 ).rel32( 1 );
		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		const Address engine_end = engine.add( static_cast< std::ptrdiff_t >( pattern::image_size( engine ) ) );
		if( !target || target.as< std::uintptr_t >( ) < engine.as< std::uintptr_t >( ) ||
			target.as< std::uintptr_t >( ) >= engine_end.as< std::uintptr_t >( ) )
			return false;

		m_engine_client = engine_client;
		m_cbuf_add_text = target.as< CbufAddTextFn >( );
		return true;
	}

	void DemoProvenance::disconnect( ) {
		m_engine_client = nullptr;
		m_cbuf_add_text = nullptr;
		reset( );
	}

	void DemoProvenance::reset( ) noexcept {
		m_recording_frames = 0;
		m_marked		   = false;
	}

	void DemoProvenance::update( const char* version, bool fixes_enabled, bool flat_textures_enabled ) {
		if( !available( ) || !version )
			return;

		if( !m_engine_client->IsRecordingDemo( ) ) {
			reset( );
			return;
		}
		if( m_marked )
			return;

		// SAR waits for SIGNONSTATE_FULL before its echo. a stable frame window
		// ensures p2boost follows it and the demo command stream is ready.
		constexpr unsigned int ready_frames{ 16 };
		if( ++m_recording_frames < ready_frames )
			return;

		char command[ 192 ]{ };
		std::snprintf( command,
					   sizeof( command ),
					   "echo \"p2boost %s (enabled=%d flat=%d backend=DXVK/Vulkan)\"",
					   version,
					   fixes_enabled,
					   flat_textures_enabled );

		// Cbuf_AddText is the path SAR uses for its recorded echo. executing an
		// empty immediate command flushes the queued marker before this frame ends.
		const int slot = m_engine_client->GetActiveSplitScreenPlayerSlot( );
		m_cbuf_add_text( slot, command, 0 );
		m_engine_client->ExecuteClientCmd( "" );
		m_marked = true;
	}

}
