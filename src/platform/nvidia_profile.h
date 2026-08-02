#pragma once

namespace p2boost {

	// describe whether the two Portal 2 application values need attention.
	enum class nvidia_profile_state_t {
		unknown,
		configured,
		applied,
		unavailable,
		failed
	};

	// retain the exact failing call and code for useful in-game diagnostics.
	struct nvidia_profile_result_t {
		nvidia_profile_state_t m_state{ nvidia_profile_state_t::unknown };
		const char*			   m_operation{ "none" };
		int					   m_status{ };
	};

	// check or update the profile using Portal 2's current process token.
	nvidia_profile_result_t configure_nvidia_profile( ) noexcept;

	// retry by loading this same DLL through an elevated system rundll32.
	nvidia_profile_result_t configure_nvidia_profile_elevated( ) noexcept;

	// expose a stable text value for logs and p2boost_status.
	const char* nvidia_profile_state_name( nvidia_profile_state_t state ) noexcept;

}
