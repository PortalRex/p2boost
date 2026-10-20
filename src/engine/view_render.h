#pragma once

#include "game_profile.h"
#include "module.h"

#include <cstddef>

namespace p2boost {

	// one slot of client.dll's CViewRender vtable, found through
	// CViewRender::Render (slot game->vmt_view_render), or null unless the
	// vtable is unique and the slot points into client.dll.
	inline void** view_render_slot( const Module& client, std::ptrdiff_t slot ) noexcept {
		const game::profile_t* game   = game::active( );
		void** const           render = client.unique_slot( client.find( game->view_render ) );
		if( !render )
			return nullptr;

		void** const target = render + ( slot - game->vmt_view_render );
		return client.contains( Address{ *target } ) ? target : nullptr;
	}

}
