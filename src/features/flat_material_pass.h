#pragma once

#include "interfaces.h"

#include <cstdint>

namespace p2boost {

	// identifies which samplers carry color, normals, or non-material data.
	struct material_pass_t {
		std::uint32_t m_flat_samplers{ };
		std::uint32_t m_normal_samplers{ };
		bool          m_excluded{ };
	};

	// classify sampler roles from Valve's public material shader contracts.
	material_pass_t classify_material_pass( sdk::IMaterial* material ) noexcept;

}
