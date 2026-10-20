#pragma once

#include "address.h"
#include "game_profile.h"
#include "module.h"

namespace p2boost {

	// CMatSystemSurface::StartDrawing and FinishDrawing open and close a direct
	// 2D draw section. no interface exposes them, so they are read from the two
	// calls inside PaintTraverseEx and verified against their signatures. both
	// are null when anything does not match.
	struct surface_drawing_t {
		Address m_start;
		Address m_finish;
	};

	inline surface_drawing_t find_surface_drawing( const Module& surface, Address paint_traverse ) {
		const game::profile_t* game = game::active( );
		if( !surface.matches( paint_traverse, game->overlay_surface_paint_traverse ) )
			return { };

		const Address start  = paint_traverse.add( game->surface_start_drawing_operand - 1 ).call_target( );
		const Address finish = paint_traverse.add( game->surface_finish_drawing_operand - 1 ).call_target( );
		if( !surface.matches( start, game->overlay_surface_start_drawing ) || !surface.matches( finish, game->overlay_surface_finish_drawing ) )
			return { };

		return { start, finish };
	}

}
