#pragma once

#include "hook.h"

namespace p2boost {

	class Metrics;

	// Research-only timers for the chamber's network/sound/entity activation path.
	class ActivationTrace {
	public:
		~ActivationTrace( );

		bool install( Metrics* metrics );
		void remove( );

	private:
		using AnimationEvents_t = void( __thiscall* )( void*, void* );
		using SoundLookup_t		= const char*(__thiscall*)( void*, unsigned short );
		using ReadUbitVar_t		= int( __thiscall* )( void* );

		static void __fastcall AnimationEvents( void* self, void*, void* entity );
		static const char* __fastcall SoundLookup( void* self, void*, unsigned short index );
		static int __fastcall ReadUbitVar( void* self, void* );

		static ActivationTrace* m_instance;
		InlineDetour			m_animation_events;
		InlineDetour			m_sound_lookup;
		InlineDetour			m_read_ubit_var;
		AnimationEvents_t		m_animation_events_original{ };
		SoundLookup_t			m_sound_lookup_original{ };
		ReadUbitVar_t			m_read_ubit_var_original{ };
		Metrics*				m_metrics{ };
	};

}
