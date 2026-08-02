#include "nvidia_profile.h"

#include <Windows.h>
#include <shellapi.h>

#include <nvapi.h>

#include <cwchar>
#include <string>

namespace p2boost {
	namespace {

		constexpr NvU32	  present_method_id{ 0x20d690f8 };
		constexpr NvU32	  present_method_layered{ 0x00000001 };
		constexpr NvU32	  present_flags_id{ 0x20324987 };
		constexpr NvU32	  present_flags_dxvk_native{ 0x000802a5 };
		constexpr wchar_t profile_name[]{ L"p2boost Portal 2" };

		constexpr DWORD elevated_configured{ 0 };
		constexpr DWORD elevated_applied{ 10 };
		constexpr DWORD elevated_failed{ 20 };

		int module_anchor{ };

		NvidiaProfileResult failure( const char* operation, int status ) noexcept {
			return { NvidiaProfileState::failed, operation, status };
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

			bool open( NvidiaProfileResult& result ) noexcept {
				NvAPI_Status status = NvAPI_Initialize( );
				if( status != NVAPI_OK ) {
					result = { NvidiaProfileState::unavailable, "NvAPI_Initialize", status };
					return false;
				}

				m_initialized = true;
				status		  = NvAPI_DRS_CreateSession( &m_session );
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
			bool			   m_initialized{ };
		};

		// obtain the exact executable path to avoid ambiguous same-name profiles.
		std::wstring current_application_path( ) {
			std::wstring path( 32768, L'\0' );
			const DWORD	 length = GetModuleFileNameW( nullptr, path.data( ), static_cast< DWORD >( path.size( ) ) );
			if( !length || length >= path.size( ) )
				return { };

			path.resize( length );
			return path;
		}

		// find the loaded DLL without relying on its filename.
		std::wstring current_module_path( ) {
			HMODULE module{ };
			if( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
									 reinterpret_cast< const wchar_t* >( &module_anchor ),
									 &module ) )
				return { };

			std::wstring path( 32768, L'\0' );
			const DWORD	 length = GetModuleFileNameW( module, path.data( ), static_cast< DWORD >( path.size( ) ) );
			if( !length || length >= path.size( ) )
				return { };

			path.resize( length );
			return path;
		}

		// derive Portal 2's executable from portal2/p2boost.dll in the install.
		std::wstring installed_application_path( ) {
			std::wstring path = current_module_path( );
			for( int index = 0; index < 2; ++index ) {
				const size_t separator = path.find_last_of( L"\\/" );
				if( separator == std::wstring::npos )
					return { };
				path.resize( separator );
			}

			return path + L"\\portal2.exe";
		}

		// find the profile the driver will actually apply to this installation.
		NvAPI_Status find_profile( NvDRSSessionHandle session, const std::wstring& application_path, NvDRSProfileHandle& profile ) noexcept {
			NVDRS_APPLICATION application{ };
			application.version = NVDRS_APPLICATION_VER;
			auto* name			= reinterpret_cast< NvU16* >( const_cast< wchar_t* >( application_path.c_str( ) ) );
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
								   NvU32			  id,
								   NvU32			  default_value,
								   NvU32&			  value ) noexcept {
			NVDRS_SETTING setting{ };
			setting.version			  = NVDRS_SETTING_VER;
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
			setting.version			= NVDRS_SETTING_VER;
			setting.settingId		= id;
			setting.settingType		= NVDRS_DWORD_TYPE;
			setting.u32CurrentValue = value;
			return NvAPI_DRS_SetSetting( session, profile, &setting );
		}

		NvidiaProfileResult configure_profile( const std::wstring& application_path ) noexcept {
			if( application_path.empty( ) )
				return failure( "GetModuleFileNameW", static_cast< int >( GetLastError( ) ) );

			NvidiaProfileResult result;
			DrsSession			drs;
			if( !drs.open( result ) )
				return result;

			NvDRSProfileHandle profile{ };
			NvAPI_Status	   status = find_profile( drs.get( ), application_path, profile );
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
				return { NvidiaProfileState::configured, "none", NVAPI_OK };

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

			return { NvidiaProfileState::applied, "none", NVAPI_OK };
		}

	}

	NvidiaProfileResult configure_nvidia_profile( ) noexcept {
		return configure_profile( current_application_path( ) );
	}

	NvidiaProfileResult configure_nvidia_profile_elevated( ) noexcept {
		wchar_t system_directory[ MAX_PATH ]{ };
		if( !GetSystemDirectoryW( system_directory, MAX_PATH ) )
			return failure( "GetSystemDirectoryW", static_cast< int >( GetLastError( ) ) );

		const std::wstring module_path = current_module_path( );
		if( module_path.empty( ) )
			return failure( "GetModuleFileNameW", static_cast< int >( GetLastError( ) ) );

		const std::wstring executable = std::wstring( system_directory ) + L"\\rundll32.exe";
		const std::wstring parameters = L"\"" + module_path + L"\",ApplyNvidiaProfile";

		SHELLEXECUTEINFOW launch{ };
		launch.cbSize		= sizeof( launch );
		launch.fMask		= SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		launch.hwnd			= GetForegroundWindow( );
		launch.lpVerb		= L"runas";
		launch.lpFile		= executable.c_str( );
		launch.lpParameters = parameters.c_str( );
		launch.nShow		= SW_HIDE;
		if( !ShellExecuteExW( &launch ) || !launch.hProcess )
			return failure( "ShellExecuteExW(runas)", static_cast< int >( GetLastError( ) ) );

		const DWORD wait	  = WaitForSingleObject( launch.hProcess, 120000 );
		DWORD		exit_code = elevated_failed;
		if( wait == WAIT_OBJECT_0 )
			GetExitCodeProcess( launch.hProcess, &exit_code );
		CloseHandle( launch.hProcess );

		if( wait != WAIT_OBJECT_0 )
			return failure( "WaitForSingleObject(rundll32)", static_cast< int >( wait ) );
		if( exit_code == elevated_configured )
			return { NvidiaProfileState::configured, "none", 0 };
		if( exit_code == elevated_applied )
			return { NvidiaProfileState::applied, "none", 0 };
		return failure( "ApplyNvidiaProfile", static_cast< int >( exit_code ) );
	}

	const char* nvidia_profile_state_name( NvidiaProfileState state ) noexcept {
		switch( state ) {
		case NvidiaProfileState::configured:
			return "configured";
		case NvidiaProfileState::applied:
			return "applied";
		case NvidiaProfileState::unavailable:
			return "unavailable";
		case NvidiaProfileState::failed:
			return "failed";
		default:
			return "unknown";
		}
	}

	// rundll32 calls this export in the elevated process and returns one result.
	extern "C" void CALLBACK ApplyNvidiaProfile( HWND, HINSTANCE, char*, int ) {
		const NvidiaProfileResult result = configure_profile( installed_application_path( ) );
		if( result.m_state == NvidiaProfileState::configured )
			ExitProcess( elevated_configured );
		if( result.m_state == NvidiaProfileState::applied )
			ExitProcess( elevated_applied );
		ExitProcess( static_cast< DWORD >( result.m_status ) );
	}

}
