#include "game_profile.h"

namespace p2boost::game {

	// Portal Stories Mel ships a recompiled fork of the same Portal 2 source
	// era. Its DLLs have different hashes and sizes, but only the active-list
	// sort workspace (0x1198 vs 0x1188) and the mod's client bin directory
	// differ from the base game. Everything else is inherited from portal2_t.
	portal_stories_mel_t::portal_stories_mel_t( ) {
		name                     = "Portal Stories: Mel";
		client_path              = "portal_stories\\bin\\client.dll";
		active_shadow_depth_list = "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC B8 98 11 00 00";
	}

}