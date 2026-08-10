#pragma once

#include "math_types.h"

#include <array>

namespace p2boost::frustum {

	using frustum_t = std::array< vec3_t, 8 >;

	// renderer-independent camera data needed to reconstruct one finite frustum.
	struct camera_view_t {
		vec3_t origin;
		vec3_t angles;
		float near_z{ };
		float far_z{ };
		float fov{ };
		float aspect_ratio{ };
	};

	// reconstruct the projected texture's convex world-space volume.
	bool projected_frustum( const matrix4x4_t& world_to_volume, frustum_t& output ) noexcept;

	// reconstruct one decoded camera as a convex world-space volume.
	bool camera_frustum( const camera_view_t& view, frustum_t& output ) noexcept;

	// test two convex frusta without weakening Source's visibility rules.
	bool frusta_intersect( const frustum_t& left, const frustum_t& right ) noexcept;

}
