#include "shadow_trace.h"

#include "address.h"
#include "metrics.h"
#include "portal2.h"
#include "qpc.h"

#include <Windows.h>
#include <cstring>

namespace p2boost {

	ShadowTrace* ShadowTrace::m_instance{ };

	ShadowTrace::~ShadowTrace( ) {
		remove( );
	}

	bool ShadowTrace::install( Metrics* metrics ) {
		if( m_instance || !metrics )
			return false;

		const Address client{ GetModuleHandleW( L"client.dll" ) };
		const Address shadow_manager = client.add( portal2::client::shadow_manager );
		auto**		  vtable		 = shadow_manager.to< void** >( );
		if( !client || !vtable )
			return false;

		// slot 24 is CClientShadowMgr::UpdateProjectedTexture in build 23973718.
		const Address update_projected{ vtable[ portal2::client::update_projected_slot ] };
		const Address remove_dirty_call = update_projected.add( portal2::client::remove_dirty_call );
		if( remove_dirty_call.at< std::uint8_t >( ) != 0xe8 )
			return false;

		// the second direct call removes this handle from the dirty list.
		m_remove_dirty			   = remove_dirty_call.rel32( 1 ).as< RemoveDirty_t >( );
		m_flashlight_models_parent = client.add( portal2::client::flashlight_models ).as< int** >( );
		if( !m_remove_dirty || !m_flashlight_models_parent || !*m_flashlight_models_parent )
			return false;

		m_metrics = metrics;
		m_states.reserve( 16 );
		m_instance = this;
		if( !m_update_projected.install( vtable + portal2::client::update_projected_slot, reinterpret_cast< void* >( &UpdateProjected ) ) ) {
			m_instance = nullptr;
			m_metrics  = nullptr;
			return false;
		}

		return true;
	}

	void ShadowTrace::remove( ) {
		m_update_projected.remove( );
		invalidate( );
		m_metrics				   = nullptr;
		m_flashlight_models_parent = nullptr;
		m_remove_dirty			   = nullptr;
		m_enabled.store( false, std::memory_order_relaxed );
		m_skips.store( 0, std::memory_order_relaxed );
		if( m_instance == this )
			m_instance = nullptr;
	}

	void ShadowTrace::invalidate( ) noexcept {
		m_states.clear( );
	}

	void ShadowTrace::set_enabled( bool enabled ) noexcept {
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) != enabled )
			invalidate( );
	}

	void __fastcall ShadowTrace::UpdateProjected( void* self, void*, unsigned int handle, bool force ) {
		auto* instance = m_instance;
		if( !instance )
			return;

		auto previous = reinterpret_cast< UpdateProjected_t >( instance->m_update_projected.previous( ) );
		if( !previous )
			return;

		const bool tracing = instance->m_metrics && instance->m_metrics->tracing( );
		if( ( handle & 0xffffu ) == 0xffffu ) {
			previous( self, handle, force );
			return;
		}

		auto* entries = *reinterpret_cast< std::uint8_t** >( static_cast< std::uint8_t* >( self ) + portal2::shadow::entries );
		if( !entries ) {
			previous( self, handle, force );
			return;
		}

		const unsigned int index  = handle & 0xffffu;
		const auto*		   shadow = entries + index * portal2::shadow::entry_size;
		auto			   prior  = instance->m_states.end( );
		for( auto item = instance->m_states.begin( ); item != instance->m_states.end( ); ++item ) {
			if( item->m_handle == index ) {
				prior = item;
				break;
			}
		}

		auto*	   flashlight_models_parent = *instance->m_flashlight_models_parent;
		const int  flashlight_models		= flashlight_models_parent ? *reinterpret_cast< int* >( reinterpret_cast< std::uint8_t* >( flashlight_models_parent ) + portal2::shadow::flashlight_models ) : 0;
		const bool same						= prior != instance->m_states.end( ) && prior->m_flashlight_models == flashlight_models && std::memcmp( prior->m_bytes.data( ), shadow, prior->m_bytes.size( ) ) == 0;

		// preserve dirty-list cleanup while omitting only the identical rebuild.
		if( instance->m_enabled.load( std::memory_order_relaxed ) && same ) {
			instance->m_remove_dirty( self, handle );
			instance->m_skips.fetch_add( 1, std::memory_order_relaxed );
			if( tracing )
				instance->m_metrics->record_shadow_update( 0.0, true, true, true );
			return;
		}

		const Stopwatch timer{ tracing };
		previous( self, handle, force );
		if( tracing )
			instance->m_metrics->record_shadow_update( timer.elapsed_ms( ), same, same, false );

		if( prior == instance->m_states.end( ) ) {
			instance->m_states.push_back( { } );
			prior			= instance->m_states.end( ) - 1;
			prior->m_handle = static_cast< std::uint16_t >( index );
		}

		std::memcpy( prior->m_bytes.data( ), shadow, prior->m_bytes.size( ) );
		prior->m_flashlight_models = flashlight_models;
	}

}
