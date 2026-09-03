#include "activation_trace.h"

#include "address.h"
#include "metrics.h"
#include "pattern.h"
#include "portal2_research.h"
#include "qpc.h"

#include <Windows.h>
#include <cstdint>
#include <vector>

namespace p2boost {

	ActivationTrace* ActivationTrace::m_instance{ };

	ActivationTrace::~ActivationTrace( ) {
		remove( );
	}

	bool ActivationTrace::install( Metrics* metrics ) {
#ifndef P2BOOST_RESEARCH_WORLD_TRACE
		(void)metrics;
		return true;
#else
		if( m_instance || !metrics )
			return false;

		const Address client{ GetModuleHandleW( L"client.dll" ) };
		const Address sound{ GetModuleHandleW( L"soundemittersystem.dll" ) };
		const Address engine{ GetModuleHandleW( L"engine.dll" ) };
		const Address animation_events = pattern::find_unique( client, portal2::signature::animation_events );
		const Address sound_lookup	   = pattern::find_unique( sound, portal2::signature::sound_lookup );
		const Address read_ubit_var	   = pattern::find_unique( engine, portal2::signature::read_ubit_var );
		if( !animation_events || !sound_lookup || !read_ubit_var )
			return false;

		m_metrics  = metrics;
		m_instance = this;
		if( !m_animation_events.install( animation_events.as< void* >( ),
				{ 0x55, 0x8b, 0xec, 0x83, 0xec, 0x20 },
				reinterpret_cast< void* >( &AnimationEvents ) ) ||
			!m_sound_lookup.install( sound_lookup.as< void* >( ),
				{ 0x55, 0x8b, 0xec, 0x56, 0x57 },
				reinterpret_cast< void* >( &SoundLookup ) ) ||
			!m_read_ubit_var.install( read_ubit_var.as< void* >( ),
				{ 0x55, 0x8b, 0xec, 0x83, 0xec, 0x08 },
				reinterpret_cast< void* >( &ReadUbitVar ) ) ) {
			remove( );
			return false;
		}

		m_animation_events_original = reinterpret_cast< AnimationEvents_t >( m_animation_events.previous( ) );
		m_sound_lookup_original		= reinterpret_cast< SoundLookup_t >( m_sound_lookup.previous( ) );
		m_read_ubit_var_original	= reinterpret_cast< ReadUbitVar_t >( m_read_ubit_var.previous( ) );
		return m_animation_events_original && m_sound_lookup_original && m_read_ubit_var_original;
#endif
	}

	void ActivationTrace::remove( ) {
		m_read_ubit_var.remove( );
		m_sound_lookup.remove( );
		m_animation_events.remove( );
		m_animation_events_original = nullptr;
		m_sound_lookup_original		= nullptr;
		m_read_ubit_var_original	= nullptr;
		m_metrics					= nullptr;
		if( m_instance == this )
			m_instance = nullptr;
	}

	void __fastcall ActivationTrace::AnimationEvents( void* self, void*, void* entity ) {
		ActivationTrace* instance = m_instance;
		if( !instance || !instance->m_animation_events_original )
			return;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) ) {
			instance->m_animation_events_original( self, entity );
			return;
		}
		Stopwatch timer;
		instance->m_animation_events_original( self, entity );
		instance->m_metrics->record_animation_events( timer.elapsed_ms( ) );
	}

	const char* __fastcall ActivationTrace::SoundLookup( void* self, void*, unsigned short index ) {
		ActivationTrace* instance = m_instance;
		if( !instance || !instance->m_sound_lookup_original )
			return nullptr;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return instance->m_sound_lookup_original( self, index );
		Stopwatch	timer;
		const char* result = instance->m_sound_lookup_original( self, index );
		instance->m_metrics->record_sound_lookup( timer.elapsed_ms( ) );
		return result;
	}

	int __fastcall ActivationTrace::ReadUbitVar( void* self, void* ) {
		ActivationTrace* instance = m_instance;
		if( !instance || !instance->m_read_ubit_var_original )
			return 0;
		if( !instance->m_metrics || !instance->m_metrics->tracing( ) )
			return instance->m_read_ubit_var_original( self );
		Stopwatch timer;
		const int result = instance->m_read_ubit_var_original( self );
		instance->m_metrics->record_read_ubit_var( timer.elapsed_ms( ) );
		return result;
	}

}
