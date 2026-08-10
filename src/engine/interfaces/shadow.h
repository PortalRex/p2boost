#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// IShadowMgr exposes immutable state for an active engine shadow handle.
	class IShadowMgr final : public Interface {
	public:
		static constexpr std::size_t get_flashlight_state_slot{ 20 };

		const void* GetFlashlightState( unsigned short handle ) {
			using function_t = const void*(__thiscall*)( void*, unsigned short );
			return invoke< get_flashlight_state_slot, function_t >( handle );
		}
	};

}
