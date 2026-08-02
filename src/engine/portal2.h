#pragma once

#include <cstddef>

namespace p2boost::portal2 {

	// Ghidra-verified signatures for Windows build 23973718. The release scans
	// these exact instruction sequences instead of relying on preferred RVAs.
	namespace signature {
		constexpr auto frame_complete = "84 DB 5B 74 0C 8B 0D ? ? ? ? FF 15 ? ? ? ? 0F 57 C0 F3 0F 11 46 18 5E 8B E5 5D C3";

		constexpr auto shadow_manager = "50 57 53 B9 ? ? ? ? E8 ? ? ? ? 5F 5B 5E 5D C3";

		constexpr auto flashlight_models = "6A 00 68 ? ? ? ? FF 15 ? ? ? ? 8B 0D ? ? ? ? 83 79 30 00 8D 86 A0 00 00 00";
	}

	// Operand offsets inside the unique signatures above.
	namespace xref {
		constexpr std::ptrdiff_t frame_complete{ 17 };
		constexpr std::ptrdiff_t shadow_manager_object{ 4 };
		constexpr std::ptrdiff_t flashlight_models_global{ 15 };
	}

	// Recovered class and stack fields used by the two production hooks.
	namespace frame {
		constexpr std::ptrdiff_t accumulator{ 0x18 };
		constexpr size_t target_local{ 8 };
	}

	namespace shadow {
		constexpr std::ptrdiff_t entries{ 0x2c };
		constexpr std::ptrdiff_t flashlight_models{ 0x30 };
		constexpr size_t entry_size{ 0x114 };
		constexpr std::ptrdiff_t remove_dirty_call{ 0x75 };
	}

	// Vtable indices are ABI layout, not process addresses.
	namespace slot {
		constexpr size_t update_projected{ 24 };
	}

}
