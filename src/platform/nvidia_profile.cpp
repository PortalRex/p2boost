#include "nvidia_profile.h"

#include <Windows.h>

#include <nvapi.h>

#include <cwchar>

namespace p2boost {
	namespace {

		constexpr NvU32	  present_method_id{ 0x20d690f8 };
		constexpr NvU32	  present_method_layered{ 0x00000001 };
		constexpr NvU32	  present_flags_id{ 0x20324987 };
		constexpr NvU32	  present_flags_dxvk_native{ 0x000802a5 };
		constexpr wchar_t application_name[]{ L"portal2.exe" };
		constexpr wchar_t profile_name[]{ L"p2boost Portal 2" };

		// own the NVAPI and DRS lifetime for one startup check.
		class DrsSession {
		public:
			~DrsSession( ) {
				if( m_session )
					NvAPI_DRS_DestroySession( m_session );

				if( m_initialized )
					NvAPI_Unload( );
			}

			bool open( ) noexcept {
				if( NvAPI_Initialize( ) != NVAPI_OK )
					return false;

				m_initialized = true;
				if( NvAPI_DRS_CreateSession( &m_session ) != NVAPI_OK )
					return false;

				return NvAPI_DRS_LoadSettings( m_session ) == NVAPI_OK;
			}

			NvDRSSessionHandle get( ) const noexcept {
				return m_session;
			}

		private:
			NvDRSSessionHandle m_session{ };
			bool			   m_initialized{ };
		};

		// find the profile currently associated with portal2.exe.
		bool find_profile( NvDRSSessionHandle session, NvDRSProfileHandle& profile ) noexcept {
			NVDRS_APPLICATION application{ };
			application.version = NVDRS_APPLICATION_VER;
			auto* name			= reinterpret_cast< NvU16* >( const_cast< wchar_t* >( application_name ) );
			return NvAPI_DRS_FindApplicationByName( session, name, &profile, &application ) == NVAPI_OK;
		}

		// create a private association only when NVIDIA has no Portal 2 profile.
		bool create_profile( NvDRSSessionHandle session, NvDRSProfileHandle& profile ) noexcept {
			NVDRS_PROFILE information{ };
			information.version = NVDRS_PROFILE_VER;
			wcsncpy_s( reinterpret_cast< wchar_t* >( information.profileName ), NVAPI_UNICODE_STRING_MAX, profile_name, _TRUNCATE );
			if( NvAPI_DRS_CreateProfile( session, &information, &profile ) != NVAPI_OK )
				return false;

			NVDRS_APPLICATION application{ };
			application.version = NVDRS_APPLICATION_VER;
			wcsncpy_s( reinterpret_cast< wchar_t* >( application.appName ), NVAPI_UNICODE_STRING_MAX, application_name, _TRUNCATE );
			wcsncpy_s( reinterpret_cast< wchar_t* >( application.userFriendlyName ), NVAPI_UNICODE_STRING_MAX, L"Portal 2", _TRUNCATE );
			if( NvAPI_DRS_CreateApplication( session, profile, &application ) == NVAPI_OK )
				return true;

			NvAPI_DRS_DeleteProfile( session, profile );
			profile = nullptr;
			return false;
		}

		// read the effective value, including inherited driver defaults.
		bool read_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 default_value, NvU32& value ) noexcept {
			NVDRS_SETTING setting{ };
			setting.version			  = NVDRS_SETTING_VER;
			const NvAPI_Status status = NvAPI_DRS_GetSetting( session, profile, id, &setting );
			if( status == NVAPI_SETTING_NOT_FOUND ) {
				value = default_value;
				return true;
			}

			if( status != NVAPI_OK )
				return false;

			value = setting.u32CurrentValue;
			return true;
		}

		// write one DWORD value into the current application profile.
		bool set_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 value ) noexcept {
			NVDRS_SETTING setting{ };
			setting.version			= NVDRS_SETTING_VER;
			setting.settingId		= id;
			setting.settingType		= NVDRS_DWORD_TYPE;
			setting.u32CurrentValue = value;
			return NvAPI_DRS_SetSetting( session, profile, &setting ) == NVAPI_OK;
		}

	}

	NvidiaProfileState configure_nvidia_profile( ) noexcept {
		DrsSession drs;
		if( !drs.open( ) )
			return NvidiaProfileState::unavailable;

		NvDRSProfileHandle profile{ };
		if( !find_profile( drs.get( ), profile ) && !create_profile( drs.get( ), profile ) )
			return NvidiaProfileState::failed;

		NvU32 method{ };
		NvU32 flags{ };
		if( !read_setting( drs.get( ), profile, present_method_id, 0x00000002, method ) || !read_setting( drs.get( ), profile, present_flags_id, 0x00000000, flags ) )
			return NvidiaProfileState::failed;

		if( method == present_method_layered && flags == present_flags_dxvk_native )
			return NvidiaProfileState::configured;

		// change only missing values and commit them as one DRS transaction.
		if( ( method != present_method_layered && !set_setting( drs.get( ), profile, present_method_id, present_method_layered ) ) ||
			( flags != present_flags_dxvk_native && !set_setting( drs.get( ), profile, present_flags_id, present_flags_dxvk_native ) ) ||
			NvAPI_DRS_SaveSettings( drs.get( ) ) != NVAPI_OK )
			return NvidiaProfileState::failed;

		return NvidiaProfileState::applied;
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

}
