#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// IShadowMgr exposes engine state for an active shadow handle.
	class IShadowMgr final : public Interface {
	public:
		static constexpr std::size_t set_flashlight_depth_texture_slot{ 19 };
		static constexpr std::size_t get_flashlight_state_slot{ 20 };

		// the stock depth selector passes (handle, null, 0) for every light it
		// culls, so a culled light never samples a depth slot reused this frame.
		void SetFlashlightDepthTexture( unsigned short handle, void* texture, unsigned char stencil_bit ) {
			using function_t = void( __thiscall* )( void*, unsigned short, void*, unsigned char );
			invoke< set_flashlight_depth_texture_slot, function_t >( handle, texture, stencil_bit );
		}

		const void* GetFlashlightState( unsigned short handle ) {
			using function_t = const void*(__thiscall*)( void*, unsigned short );
			return invoke< get_flashlight_state_slot, function_t >( handle );
		}

		// client BuildFlashlight tests this before any leaf enumeration and, when
		// set (with a render-context condition), projects the light without
		// receivers. a projection built in either mode is only valid in that mode.
		static constexpr std::size_t leafless_flashlight_mode_slot{ 29 };

		bool LeaflessFlashlightMode( ) {
			using function_t = bool( __thiscall* )( void* );
			return invoke< leafless_flashlight_mode_slot, function_t >( );
		}
	};

}
