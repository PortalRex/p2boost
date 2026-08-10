#pragma once

#include <cstddef>

namespace p2boost::d3d9_slot {

	// public IDirect3DDevice9 layout used by DXVK's native D3D9 implementation.
	constexpr std::size_t reset{ 16 };
	constexpr std::size_t present{ 17 };
	constexpr std::size_t create_texture{ 23 };
	constexpr std::size_t update_surface{ 30 };
	constexpr std::size_t update_texture{ 31 };
	constexpr std::size_t set_viewport{ 47 };
	constexpr std::size_t set_texture{ 65 };

	// public IDirect3DTexture9 layout.
	constexpr std::size_t texture_release{ 2 };
	constexpr std::size_t texture_get_surface_level{ 18 };
	constexpr std::size_t texture_lock{ 19 };
	constexpr std::size_t texture_unlock{ 20 };

	// public IDirect3DSurface9 layout.
	constexpr std::size_t surface_release{ 2 };
	constexpr std::size_t surface_lock{ 13 };
	constexpr std::size_t surface_unlock{ 14 };

}
