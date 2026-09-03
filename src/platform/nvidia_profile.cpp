#include "nvidia_profile.h"

#include "module.h"

#include <Windows.h>
#include <d3d9.h>
#include <shellapi.h>

#include <nvapi.h>

#include <cwchar>
#include <string>

namespace p2boost {
	namespace {

		constexpr NvU32   present_method_id{ 0x20d690f8 };
		constexpr NvU32   present_method_layered{ 0x00000001 };
		constexpr NvU32   present_flags_id{ 0x20324987 };
		constexpr NvU32   present_flags_dxvk_native{ 0x000802a5 };
		constexpr wchar_t profile_name[]{ L"p2boost Portal 2" };

		constexpr DWORD elevated_configured{ 0 };
		constexpr DWORD elevated_applied{ 10 };
		constexpr DWORD elevated_failed{ 20 };

		// read the adapter selected by Portal 2's live DXVK device. this avoids
		// probing NVIDIA APIs on AMD, Intel, and software-rendered systems.
		bool active_adapter_vendor( IDirect3DDevice9* device, unsigned int& vendor_id ) noexcept {
			if( !device )
				return false;

			D3DDEVICE_CREATION_PARAMETERS parameters{ };
			if( FAILED( device->GetCreationParameters( &parameters ) ) )
				return false;

			IDirect3D9* direct3d{ };
			if( FAILED( device->GetDirect3D( &direct3d ) ) || !direct3d )
				return false;

			D3DADAPTER_IDENTIFIER9 identifier{ };
			const bool             identified = SUCCEEDED( direct3d->GetAdapterIdentifier( parameters.AdapterOrdinal, 0, &identifier ) );
			direct3d->Release( );
			if( !identified )
				return false;

			vendor_id = identifier.VendorId;
			return true;
		}

		nvidia_profile_result_t failure( const char* operation, int status ) noexcept {
			return { nvidia_profile_state_t::failed, operation, status };
		}

		// own the NVAPI and DRS lifetime for one startup check.
		class DrsSession {
		public:
			~DrsSession( ) {
				if( m_session )
					NvAPI_DRS_DestroySession( m_session );

				if( m_initialized )
					NvAPI_Unload( );
			}

			bool open( nvidia_profile_result_t& result ) noexcept {
				NvAPI_Status status = NvAPI_Initialize( );
				if( status != NVAPI_OK ) {
					result = { nvidia_profile_state_t::unavailable, "NvAPI_Initialize", status };
					return false;
				}

				m_initialized = true;
				status        = NvAPI_DRS_CreateSession( &m_session );
				if( status != NVAPI_OK ) {
					result = failure( "NvAPI_DRS_CreateSession", status );
					return false;
				}

				status = NvAPI_DRS_LoadSettings( m_session );
				if( status != NVAPI_OK ) {
					result = failure( "NvAPI_DRS_LoadSettings", status );
					return false;
				}

				return true;
			}

			NvDRSSessionHandle get( ) const noexcept {
				return m_session;
			}

		private:
			NvDRSSessionHandle m_session{ };
			bool               m_initialized{ };
		};

		// obtain the exact executable path to avoid ambiguous same-name profiles.
		std::wstring current_application_path( ) {
			std::wstring path( 32768, L'\0' );
			const DWORD  length = GetModuleFileNameW( nullptr, path.data( ), static_cast< DWORD >( path.size( ) ) );
			if( !length || length >= path.size( ) )
				return { };

			path.resize( length );
			return path;
		}

		// find the loaded DLL without relying on its filename.
		std::wstring current_module_path( ) {
			return Module::containing( reinterpret_cast< const void* >( &current_module_path ) ).path( );
		}

		// derive the game's executable from p2boost.dll sitting in the game root.
		std::wstring installed_application_path( ) {
			std::wstring path = current_module_path( );
			const size_t separator = path.find_last_of( L"\\/" );
			if( separator == std::wstring::npos )
				return { };
			path.resize( separator );

			return path + L"\\portal2.exe";
		}

		// find the profile the driver will actually apply to this installation.
		NvAPI_Status find_profile( NvDRSSessionHandle session, const std::wstring& application_path, NvDRSProfileHandle& profile ) noexcept {
			NVDRS_APPLICATION application{ };
			application.version = NVDRS_APPLICATION_VER;
			auto* name          = reinterpret_cast< NvU16* >( const_cast< wchar_t* >( application_path.c_str( ) ) );
			return NvAPI_DRS_FindApplicationByName( session, name, &profile, &application );
		}

		// create a private association only when NVIDIA has no matching profile.
		NvAPI_Status create_profile( NvDRSSessionHandle session, const std::wstring& application_path, NvDRSProfileHandle& profile ) noexcept {
			NVDRS_PROFILE information{ };
			information.version = NVDRS_PROFILE_VER;
			wcsncpy_s( reinterpret_cast< wchar_t* >( information.profileName ), NVAPI_UNICODE_STRING_MAX, profile_name, _TRUNCATE );
			NvAPI_Status status = NvAPI_DRS_CreateProfile( session, &information, &profile );
			if( status != NVAPI_OK )
				return status;

			NVDRS_APPLICATION application{ };
			application.version = NVDRS_APPLICATION_VER;
			wcsncpy_s( reinterpret_cast< wchar_t* >( application.appName ), NVAPI_UNICODE_STRING_MAX, application_path.c_str( ), _TRUNCATE );
			wcsncpy_s( reinterpret_cast< wchar_t* >( application.userFriendlyName ), NVAPI_UNICODE_STRING_MAX, L"Portal 2", _TRUNCATE );
			status = NvAPI_DRS_CreateApplication( session, profile, &application );
			if( status == NVAPI_OK )
				return status;

			NvAPI_DRS_DeleteProfile( session, profile );
			profile = nullptr;
			return status;
		}

		NvAPI_Status read_setting( NvDRSSessionHandle session,
		                           NvDRSProfileHandle profile,
		                           NvU32              id,
		                           NvU32              default_value,
		                           NvU32&             value ) noexcept {
			NVDRS_SETTING setting{ };
			setting.version           = NVDRS_SETTING_VER;
			const NvAPI_Status status = NvAPI_DRS_GetSetting( session, profile, id, &setting );
			if( status == NVAPI_SETTING_NOT_FOUND ) {
				value = default_value;
				return NVAPI_OK;
			}

			if( status == NVAPI_OK )
				value = setting.u32CurrentValue;
			return status;
		}

		NvAPI_Status set_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 value ) noexcept {
			NVDRS_SETTING setting{ };
			setting.version         = NVDRS_SETTING_VER;
			setting.settingId       = id;
			setting.settingType     = NVDRS_DWORD_TYPE;
			setting.u32CurrentValue = value;
			return NvAPI_DRS_SetSetting( session, profile, &setting );
		}

		nvidia_profile_result_t configure_profile( const std::wstring& application_path ) noexcept {
			if( application_path.empty( ) )
				return failure( "GetModuleFileNameW", static_cast< int >( GetLastError( ) ) );

			nvidia_profile_result_t result;
			DrsSession              drs;
			if( !drs.open( result ) )
				return result;

			NvDRSProfileHandle profile{ };
			NvAPI_Status       status = find_profile( drs.get( ), application_path, profile );
			if( status == NVAPI_EXECUTABLE_NOT_FOUND ) {
				status = create_profile( drs.get( ), application_path, profile );
				if( status != NVAPI_OK )
					return failure( "NvAPI_DRS_CreateProfile/Application", status );
			}
			if( status != NVAPI_OK )
				return failure( "NvAPI_DRS_FindApplicationByName", status );

			NvU32 method{ };
			NvU32 flags{ };
			status = read_setting( drs.get( ), profile, present_method_id, 0x00000002, method );
			if( status != NVAPI_OK )
				return failure( "NvAPI_DRS_GetSetting(method)", status );

			status = read_setting( drs.get( ), profile, present_flags_id, 0x00000000, flags );
			if( status != NVAPI_OK )
				return failure( "NvAPI_DRS_GetSetting(flags)", status );

			if( method == present_method_layered && flags == present_flags_dxvk_native )
				return { nvidia_profile_state_t::configured, "none", NVAPI_OK };

			if( method != present_method_layered ) {
				status = set_setting( drs.get( ), profile, present_method_id, present_method_layered );
				if( status != NVAPI_OK )
					return failure( "NvAPI_DRS_SetSetting(method)", status );
			}

			if( flags != present_flags_dxvk_native ) {
				status = set_setting( drs.get( ), profile, present_flags_id, present_flags_dxvk_native );
				if( status != NVAPI_OK )
					return failure( "NvAPI_DRS_SetSetting(flags)", status );
			}

			status = NvAPI_DRS_SaveSettings( drs.get( ) );
			if( status != NVAPI_OK )
				return failure( "NvAPI_DRS_SaveSettings", status );

			return { nvidia_profile_state_t::applied, "none", NVAPI_OK };
		}

	}

	nvidia_profile_result_t configure_nvidia_profile( IDirect3DDevice9* device ) noexcept {
		unsigned int vendor_id{ };
		if( active_adapter_vendor( device, vendor_id ) && !needs_nvidia_profile( vendor_id ) )
			return { nvidia_profile_state_t::not_applicable, "non-NVIDIA adapter", static_cast< int >( vendor_id ) };

		// fall back to NVAPI when adapter discovery fails. this preserves support
		// for hybrid systems whose NVIDIA device is visible before DXVK settles.
		return configure_profile( current_application_path( ) );
	}

	nvidia_profile_result_t configure_nvidia_profile_elevated( ) noexcept {
		wchar_t system_directory[ MAX_PATH ]{ };
		if( !GetSystemDirectoryW( system_directory, MAX_PATH ) )
			return failure( "GetSystemDirectoryW", static_cast< int >( GetLastError( ) ) );

		const std::wstring module_path = current_module_path( );
		if( module_path.empty( ) )
			return failure( "GetModuleFileNameW", static_cast< int >( GetLastError( ) ) );

		const std::wstring executable = std::wstring( system_directory ) + L"\\rundll32.exe";
		const std::wstring parameters = L"\"" + module_path + L"\",ApplyNvidiaProfile";

		SHELLEXECUTEINFOW launch{ };
		launch.cbSize       = sizeof( launch );
		launch.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		launch.hwnd         = GetForegroundWindow( );
		launch.lpVerb       = L"runas";
		launch.lpFile       = executable.c_str( );
		launch.lpParameters = parameters.c_str( );
		launch.nShow        = SW_HIDE;
		if( !ShellExecuteExW( &launch ) || !launch.hProcess )
			return failure( "ShellExecuteExW(runas)", static_cast< int >( GetLastError( ) ) );

		const DWORD wait      = WaitForSingleObject( launch.hProcess, 120000 );
		DWORD       exit_code = elevated_failed;
		if( wait == WAIT_OBJECT_0 )
			GetExitCodeProcess( launch.hProcess, &exit_code );
		CloseHandle( launch.hProcess );

		if( wait != WAIT_OBJECT_0 )
			return failure( "WaitForSingleObject(rundll32)", static_cast< int >( wait ) );
		if( exit_code == elevated_configured )
			return { nvidia_profile_state_t::configured, "none", 0 };
		if( exit_code == elevated_applied )
			return { nvidia_profile_state_t::applied, "none", 0 };
		return failure( "ApplyNvidiaProfile", static_cast< int >( exit_code ) );
	}

	const char* nvidia_profile_state_name( nvidia_profile_state_t state ) noexcept {
		switch( state ) {
		case nvidia_profile_state_t::not_applicable:
			return "not_applicable";
		case nvidia_profile_state_t::configured:
			return "configured";
		case nvidia_profile_state_t::applied:
			return "applied";
		case nvidia_profile_state_t::unavailable:
			return "unavailable";
		case nvidia_profile_state_t::failed:
			return "failed";
		default:
			return "unknown";
		}
	}

	// rundll32 calls this export in the elevated process and returns one result.
	extern "C" void CALLBACK ApplyNvidiaProfile( HWND, HINSTANCE, char*, int ) {
		const nvidia_profile_result_t result = configure_profile( installed_application_path( ) );
		if( result.m_state == nvidia_profile_state_t::configured )
			ExitProcess( elevated_configured );
		if( result.m_state == nvidia_profile_state_t::applied )
			ExitProcess( elevated_applied );
		ExitProcess( static_cast< DWORD >( result.m_status ) );
	}

}
