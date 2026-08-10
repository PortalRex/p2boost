#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// VEngineClient015 methods used by demo provenance and the profiler.
	class IVEngineClient final : public Interface {
	public:
		static constexpr const char* version{ "VEngineClient015" };
		static constexpr std::size_t client_cmd_slot{ 7 };
		static constexpr std::size_t get_screen_size_slot{ 5 };
		static constexpr std::size_t get_max_clients_slot{ 20 };
		static constexpr std::size_t is_recording_demo_slot{ 79 };
		static constexpr std::size_t execute_client_cmd_slot{ 104 };
		static constexpr std::size_t active_split_screen_slot{ 127 };

		void GetScreenSize( int& width, int& height ) {
			using function_t = void( __thiscall* )( void*, int&, int& );
			invoke< get_screen_size_slot, function_t >( width, height );
		}

		int GetMaxClients( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< get_max_clients_slot, function_t >( );
		}

		bool IsRecordingDemo( ) {
			using function_t = bool( __thiscall* )( void* );
			return invoke< is_recording_demo_slot, function_t >( );
		}

		void ExecuteClientCmd( const char* command ) {
			using function_t = void( __thiscall* )( void*, const char* );
			invoke< execute_client_cmd_slot, function_t >( command );
		}

		int GetActiveSplitScreenPlayerSlot( ) {
			using function_t = int( __thiscall* )( void* );
			return invoke< active_split_screen_slot, function_t >( );
		}

		Address GetScreenSizeAddress( ) const noexcept {
			return method( get_screen_size_slot );
		}

		Address GetMaxClientsAddress( ) const noexcept {
			return method( get_max_clients_slot );
		}

		Address ClientCmdAddress( ) const noexcept {
			return method( client_cmd_slot );
		}
	};

	// VEngineModel016 owns model-instance transfers during ragdoll creation.
	class IVModelRender final : public Interface {
	public:
		static constexpr const char* version{ "VEngineModel016" };
		static constexpr std::size_t change_instance_slot{ 7 };

		Address ChangeInstanceAddress( ) const noexcept {
			return method( change_instance_slot );
		}

		void** ChangeInstanceSlot( ) noexcept {
			return method_slot( change_instance_slot );
		}
	};

	// VEngineVGui001 owns the final UI paint callback used by SAR and p2boost.
	class IEngineVGui final : public Interface {
	public:
		static constexpr const char* version{ "VEngineVGui001" };
		static constexpr std::size_t paint_slot{ 14 };

		Address PaintAddress( ) const noexcept {
			return method( paint_slot );
		}

		void** PaintSlot( ) noexcept {
			return method_slot( paint_slot );
		}
	};

}
