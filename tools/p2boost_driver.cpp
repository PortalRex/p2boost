#include <Windows.h>

#include <nvapi.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

	constexpr NvU32	  present_method_id{ 0x20d690f8 };
	constexpr NvU32	  present_method_layered{ 0x00000001 };
	constexpr NvU32	  present_flags_id{ 0x20324987 };
	constexpr NvU32	  present_flags_dxvk_native{ 0x000802a5 };
	constexpr wchar_t application_name[]{ L"portal2.exe" };
	constexpr wchar_t created_profile_name[]{ L"p2boost Portal 2" };

	struct PreviousSetting {
		bool  m_had_override{ };
		NvU32 m_value{ };
	};

	struct Backup {
		bool			m_created_profile{ };
		PreviousSetting m_present_method;
		PreviousSetting m_present_flags;
	};

	// resolve NVAPI errors before unloading the library.
	void print_error( const char* operation, NvAPI_Status status ) {
		NvAPI_ShortString message{ };
		NvAPI_GetErrorMessage( status, message );
		std::fprintf( stderr, "p2boost_driver: %s failed (%d: %s)\n", operation, status, message );
	}

	// keep the reversible state beside the helper executable.
	std::filesystem::path backup_path( ) {
		std::wstring executable( 32768, L'\0' );
		const DWORD	 length = GetModuleFileNameW( nullptr, executable.data( ), static_cast< DWORD >( executable.size( ) ) );
		executable.resize( length );
		return std::filesystem::path( executable ).parent_path( ) / L"p2boost_driver_profile.state";
	}

	// use a small versioned text format that can be inspected and recovered.
	bool write_backup( const std::filesystem::path& path, const Backup& backup ) {
		std::ofstream output( path, std::ios::trunc );
		if( !output )
			return false;

		output << "p2boost_driver_profile_v1\n"
			   << "created_profile=" << backup.m_created_profile << '\n'
			   << "present_method_override=" << backup.m_present_method.m_had_override << '\n'
			   << "present_method_value=" << backup.m_present_method.m_value << '\n'
			   << "present_flags_override=" << backup.m_present_flags.m_had_override << '\n'
			   << "present_flags_value=" << backup.m_present_flags.m_value << '\n';
		return output.good( );
	}

	bool read_backup( const std::filesystem::path& path, Backup& backup ) {
		std::ifstream input( path );
		std::string	  line;
		if( !input || !std::getline( input, line ) || line != "p2boost_driver_profile_v1" )
			return false;

		// accept fields only in the exact expected order and syntax.
		auto read_value = [ &input ]( const char* key, NvU32& value ) {
			std::string entry;
			if( !std::getline( input, entry ) )
				return false;
			const std::string prefix = std::string( key ) + '=';
			if( entry.rfind( prefix, 0 ) != 0 )
				return false;
			try {
				value = static_cast< unsigned int >( std::stoul( entry.substr( prefix.size( ) ) ) );
				return true;
			} catch( ... ) {
				return false;
			}
		};

		NvU32 created{ };
		NvU32 method_override{ };
		NvU32 flags_override{ };
		if( !read_value( "created_profile", created ) || !read_value( "present_method_override", method_override ) ||
			!read_value( "present_method_value", backup.m_present_method.m_value ) ||
			!read_value( "present_flags_override", flags_override ) ||
			!read_value( "present_flags_value", backup.m_present_flags.m_value ) )
			return false;

		backup.m_created_profile			   = created != 0;
		backup.m_present_method.m_had_override = method_override != 0;
		backup.m_present_flags.m_had_override  = flags_override != 0;
		return true;
	}

	bool find_application( NvDRSSessionHandle session, NvDRSProfileHandle& profile ) {
		// find whichever current profile owns portal2.exe.
		NVDRS_APPLICATION application{ };
		application.version = NVDRS_APPLICATION_VER;
		auto* name			= reinterpret_cast< NvU16* >( const_cast< wchar_t* >( application_name ) );
		return NvAPI_DRS_FindApplicationByName( session, name, &profile, &application ) == NVAPI_OK;
	}

	bool create_application_profile( NvDRSSessionHandle session, NvDRSProfileHandle& profile ) {
		// create a dedicated profile only when NVIDIA has no existing association.
		NVDRS_PROFILE profile_info{ };
		profile_info.version = NVDRS_PROFILE_VER;
		wcsncpy_s( reinterpret_cast< wchar_t* >( profile_info.profileName ), NVAPI_UNICODE_STRING_MAX, created_profile_name, _TRUNCATE );

		NvAPI_Status status = NvAPI_DRS_CreateProfile( session, &profile_info, &profile );
		if( status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_CreateProfile", status );
			return false;
		}

		NVDRS_APPLICATION application{ };
		application.version = NVDRS_APPLICATION_VER;
		wcsncpy_s( reinterpret_cast< wchar_t* >( application.appName ), NVAPI_UNICODE_STRING_MAX, application_name, _TRUNCATE );
		wcsncpy_s( reinterpret_cast< wchar_t* >( application.userFriendlyName ), NVAPI_UNICODE_STRING_MAX, L"Portal 2", _TRUNCATE );
		status = NvAPI_DRS_CreateApplication( session, profile, &application );
		if( status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_CreateApplication", status );
			NvAPI_DRS_DeleteProfile( session, profile );
			return false;
		}
		return true;
	}

	bool read_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 default_value, PreviousSetting& previous ) {
		// distinguish an inherited value from a user-owned profile override.
		NVDRS_SETTING setting{ };
		setting.version			  = NVDRS_SETTING_VER;
		const NvAPI_Status status = NvAPI_DRS_GetSetting( session, profile, id, &setting );
		if( status == NVAPI_SETTING_NOT_FOUND ) {
			// hidden flags are absent until a profile overrides them. preserve that
			// absence so restore can delete our value instead of inventing one.
			previous.m_had_override = false;
			previous.m_value		= default_value;
			return true;
		}
		if( status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_GetSetting", status );
			return false;
		}

		previous.m_had_override = setting.settingLocation == NVDRS_CURRENT_PROFILE_LOCATION && setting.isCurrentPredefined == 0;
		previous.m_value		= setting.u32CurrentValue;
		return true;
	}

	bool set_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 value ) {
		NVDRS_SETTING setting{ };
		setting.version			  = NVDRS_SETTING_VER;
		setting.settingId		  = id;
		setting.settingType		  = NVDRS_DWORD_TYPE;
		setting.u32CurrentValue	  = value;
		const NvAPI_Status status = NvAPI_DRS_SetSetting( session, profile, &setting );
		if( status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_SetSetting", status );
			return false;
		}
		return true;
	}

	bool restore_setting( NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, const PreviousSetting& previous ) {
		// restore an override or delete ours to reveal the inherited default.
		if( previous.m_had_override )
			return set_setting( session, profile, id, previous.m_value );

		const NvAPI_Status status = NvAPI_DRS_DeleteProfileSetting( session, profile, id );
		if( status != NVAPI_OK && status != NVAPI_SETTING_NOT_FOUND ) {
			print_error( "NvAPI_DRS_DeleteProfileSetting", status );
			return false;
		}
		return true;
	}

	bool print_status( NvDRSSessionHandle session, NvDRSProfileHandle profile ) {
		PreviousSetting method;
		PreviousSetting flags;
		if( !read_setting( session, profile, present_method_id, 0x00000002, method ) ||
			!read_setting( session, profile, present_flags_id, 0x00000000, flags ) )
			return false;

		std::printf( "present_method=0x%08x expected=0x%08x\n", method.m_value, present_method_layered );
		std::printf( "present_flags=0x%08x expected=0x%08x\n", flags.m_value, present_flags_dxvk_native );
		const bool configured = method.m_value == present_method_layered && flags.m_value == present_flags_dxvk_native;
		std::printf( "configured=%d\n", configured ? 1 : 0 );
		return configured;
	}

	class DrsSession {
	public:
		bool open( ) {
			// initialize, create, and load the DRS database in strict order.
			NvAPI_Status status = NvAPI_Initialize( );
			if( status != NVAPI_OK ) {
				print_error( "NvAPI_Initialize", status );
				return false;
			}

			status = NvAPI_DRS_CreateSession( &m_session );
			if( status != NVAPI_OK ) {
				print_error( "NvAPI_DRS_CreateSession", status );
				NvAPI_Unload( );
				return false;
			}

			status = NvAPI_DRS_LoadSettings( m_session );
			if( status != NVAPI_OK ) {
				print_error( "NvAPI_DRS_LoadSettings", status );
				return false;
			}
			return true;
		}

		~DrsSession( ) {
			if( m_session )
				NvAPI_DRS_DestroySession( m_session );
			NvAPI_Unload( );
		}

		NvDRSSessionHandle get( ) const noexcept {
			return m_session;
		}

	private:
		NvDRSSessionHandle m_session{ };
	};

	int apply_profile( NvDRSSessionHandle session, const std::filesystem::path& state_path ) {
		// preserve the first backup so repeated apply calls remain reversible.
		NvDRSProfileHandle profile{ };
		Backup			   backup;
		if( !find_application( session, profile ) ) {
			if( !create_application_profile( session, profile ) )
				return 1;
			backup.m_created_profile = true;
		}

		if( std::filesystem::exists( state_path ) ) {
			std::puts( "p2boost_driver: profile is already backed up; preserving the original backup" );
		} else {
			if( !read_setting( session, profile, present_method_id, 0x00000002, backup.m_present_method ) ||
				!read_setting( session, profile, present_flags_id, 0x00000000, backup.m_present_flags ) ||
				!write_backup( state_path, backup ) ) {
				std::fputs( "p2boost_driver: could not create a reversible profile backup\n", stderr );
				return 1;
			}
		}

		if( !set_setting( session, profile, present_method_id, present_method_layered ) ||
			!set_setting( session, profile, present_flags_id, present_flags_dxvk_native ) )
			return 1;

		const NvAPI_Status save_status = NvAPI_DRS_SaveSettings( session );
		if( save_status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_SaveSettings", save_status );
			return 1;
		}

		std::puts( "p2boost_driver: applied the Portal 2 DXVK presentation profile; restart the game" );
		return 0;
	}

	int restore_profile( NvDRSSessionHandle session, const std::filesystem::path& state_path ) {
		// never infer prior values when the durable backup is missing or corrupt.
		Backup backup;
		if( !read_backup( state_path, backup ) ) {
			std::fputs( "p2boost_driver: no valid backup exists; refusing to guess previous settings\n", stderr );
			return 1;
		}

		NvDRSProfileHandle profile{ };
		if( !find_application( session, profile ) ) {
			std::fputs( "p2boost_driver: Portal 2's NVIDIA profile no longer exists\n", stderr );
			return 1;
		}

		if( backup.m_created_profile ) {
			NVDRS_PROFILE profile_info{ };
			profile_info.version		   = NVDRS_PROFILE_VER;
			const NvAPI_Status info_status = NvAPI_DRS_GetProfileInfo( session, profile, &profile_info );
			if( info_status != NVAPI_OK || profile_info.isPredefined != 0 ||
				std::wcscmp( reinterpret_cast< const wchar_t* >( profile_info.profileName ), created_profile_name ) != 0 ) {
				std::fputs( "p2boost_driver: created profile changed ownership; refusing to delete it\n", stderr );
				return 1;
			}
			if( NvAPI_DRS_DeleteProfile( session, profile ) != NVAPI_OK )
				return 1;
		} else if( !restore_setting( session, profile, present_method_id, backup.m_present_method ) ||
				   !restore_setting( session, profile, present_flags_id, backup.m_present_flags ) ) {
			return 1;
		}

		const NvAPI_Status save_status = NvAPI_DRS_SaveSettings( session );
		if( save_status != NVAPI_OK ) {
			print_error( "NvAPI_DRS_SaveSettings", save_status );
			return 1;
		}

		std::error_code error;
		std::filesystem::remove( state_path, error );
		std::puts( "p2boost_driver: restored the previous Portal 2 NVIDIA profile" );
		return 0;
	}

}

int main( int argc, char** argv ) {
	if( argc != 2 ||
		( _stricmp( argv[ 1 ], "status" ) != 0 && _stricmp( argv[ 1 ], "apply" ) != 0 && _stricmp( argv[ 1 ], "restore" ) != 0 ) ) {
		std::fputs( "usage: p2boost_driver.exe status|apply|restore\n", stderr );
		return 2;
	}

	DrsSession drs;
	if( !drs.open( ) )
		return 1;

	if( _stricmp( argv[ 1 ], "apply" ) == 0 )
		return apply_profile( drs.get( ), backup_path( ) );
	if( _stricmp( argv[ 1 ], "restore" ) == 0 )
		return restore_profile( drs.get( ), backup_path( ) );

	NvDRSProfileHandle profile{ };
	if( !find_application( drs.get( ), profile ) ) {
		std::fputs( "p2boost_driver: Portal 2 has no NVIDIA application profile\n", stderr );
		return 1;
	}
	return print_status( drs.get( ), profile ) ? 0 : 1;
}
