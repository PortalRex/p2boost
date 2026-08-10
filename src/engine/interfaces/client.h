#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// VClient016 receives entity lifecycle stages on host and remote clients.
	class IBaseClientDLL final : public Interface {
	public:
		static constexpr const char* version{ "VClient016" };
		static constexpr std::size_t frame_stage_notify_slot{ 35 };

		Address FrameStageNotifyAddress( ) const noexcept {
			return method( frame_stage_notify_slot );
		}

		void** FrameStageNotifySlot( ) noexcept {
			return method_slot( frame_stage_notify_slot );
		}
	};

}
