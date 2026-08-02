#pragma once

namespace p2boost {

	// result of validating Portal 2's two DXVK presentation settings.
	enum class NvidiaProfileState {
		unknown,
		configured,
		applied,
		unavailable,
		failed
	};

	// apply missing per-application values directly through NVIDIA's DRS API.
	NvidiaProfileState configure_nvidia_profile( ) noexcept;

	const char* nvidia_profile_state_name( NvidiaProfileState state ) noexcept;

}
