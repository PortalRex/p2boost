#pragma once

#include "flat_texture_codec.h"
#include "hook.h"
#include "interfaces.h"
#include "portal2_context.h"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace p2boost {

	// creates solid color companions during Source uploads and supplies one shared
	// neutral normal map. originals remain resident for instant, lossless reversal.
	class FlatTextures {
	public:
		~FlatTextures( );

		FlatTextures( )                                = default;
		FlatTextures( const FlatTextures& )            = delete;
		FlatTextures& operator=( const FlatTextures& ) = delete;

		bool install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces, IDirect3DDevice9* device );
		void remove( );

		// reload Source materials so already-resident textures receive or discard
		// companions immediately when the user changes the toggle.
		bool set_enabled( bool enabled );

		// discard uploads interrupted by a level transition; Source still owns the
		// original textures and their reusable flat companions.
		void discard_pending_uploads( );

		bool available( ) const noexcept {
			return m_create_texture.installed( ) && m_update_surface.installed( ) && m_update_texture.installed( ) &&
			       m_set_texture.installed( ) && m_bind_material.installed( ) && m_vgui_paint.installed( ) &&
			       m_material_system && m_engine_vgui;
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		size_t texture_count( ) const noexcept;
		size_t ready_texture_count( ) const noexcept;
		bool   normal_ready( ) const noexcept;

	private:
		enum class companion_policy_t {
			retain_originals,
			forget_originals
		};

		struct texture_record_t {
			IDirect3DTexture9* m_flat{ };
			D3DFORMAT          m_format{ D3DFMT_UNKNOWN };
			unsigned int       m_width{ };
			unsigned int       m_height{ };
			unsigned int       m_levels{ };
			DWORD              m_usage{ };
			D3DPOOL            m_pool{ D3DPOOL_DEFAULT };
			flat_color_t       m_color;
			bool               m_color_valid{ };
			bool               m_flat_texture{ };
			bool               m_normal_texture{ };
		};

		struct lock_key_t {
			IDirect3DTexture9* m_texture{ };
			unsigned int       m_level{ };

			bool operator==( const lock_key_t& other ) const noexcept {
				return m_texture == other.m_texture && m_level == other.m_level;
			}
		};

		struct lock_key_hash_t {
			size_t operator( )( const lock_key_t& key ) const noexcept {
				const size_t texture = reinterpret_cast< size_t >( key.m_texture );
				return ( texture >> 4 ) ^ ( static_cast< size_t >( key.m_level ) * 0x9e3779b9u );
			}
		};

		struct locked_region_t {
			D3DLOCKED_RECT m_source{ };
			RECT           m_rect{ };
			DWORD          m_flags{ };
			bool           m_has_rect{ };
		};

		struct surface_record_t {
			IDirect3DTexture9* m_texture{ };
			unsigned int       m_level{ };
		};

		// short aliases keep the synchronized resource state readable.
		using texture_map_t   = std::unordered_map< IDirect3DTexture9*, texture_record_t >;
		using texture_locks_t = std::unordered_map< lock_key_t, locked_region_t, lock_key_hash_t >;
		using surface_map_t   = std::unordered_map< IDirect3DSurface9*, surface_record_t >;
		using surface_locks_t = std::unordered_map< IDirect3DSurface9*, locked_region_t >;

		// exact COM signatures for the D3D9 methods intercepted below.
		using create_texture_fn_t = HRESULT( __stdcall* )(
		    IDirect3DDevice9*, unsigned int, unsigned int, unsigned int, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE* );
		using update_surface_fn_t = HRESULT( __stdcall* )(
		    IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT* );
		using update_texture_fn_t = HRESULT( __stdcall* )(
		    IDirect3DDevice9*, IDirect3DBaseTexture9*, IDirect3DBaseTexture9* );
		using set_texture_fn_t       = HRESULT( __stdcall* )( IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9* );
		using release_texture_fn_t   = ULONG( __stdcall* )( IDirect3DTexture9* );
		using get_surface_level_fn_t = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int, IDirect3DSurface9** );
		using lock_texture_fn_t      = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int, D3DLOCKED_RECT*, const RECT*, DWORD );
		using unlock_texture_fn_t    = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int );
		using release_surface_fn_t   = ULONG( __stdcall* )( IDirect3DSurface9* );
		using lock_surface_fn_t      = HRESULT( __stdcall* )( IDirect3DSurface9*, D3DLOCKED_RECT*, const RECT*, DWORD );
		using unlock_surface_fn_t    = HRESULT( __stdcall* )( IDirect3DSurface9* );
		using bind_material_fn_t     = void( __thiscall* )( void*, sdk::IMaterial*, void* );
		using vgui_paint_fn_t        = int( __thiscall* )( void*, int );

		// install resource hooks lazily when the first object appears.
		bool install_texture_hooks( IDirect3DTexture9* texture );
		bool install_surface_hooks( IDirect3DSurface9* surface );
		bool create_flat_normal( );
		bool create_missing_companions( );
		bool reload_textures( ) const;
		bool reload_materials( ) const;
		void release_companions( companion_policy_t policy );

		static HRESULT __stdcall CreateTexture( IDirect3DDevice9* device, unsigned int width, unsigned int height, unsigned int levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* shared_handle );
		static HRESULT __stdcall UpdateSurface( IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* source_rect, IDirect3DSurface9* destination, const POINT* destination_point );
		static HRESULT __stdcall UpdateTexture( IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination );
		static HRESULT __stdcall SetTexture( IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture );
		static ULONG __stdcall ReleaseTexture( IDirect3DTexture9* texture );
		static HRESULT __stdcall GetSurfaceLevel( IDirect3DTexture9* texture, unsigned int level, IDirect3DSurface9** surface );
		static HRESULT __stdcall LockTexture( IDirect3DTexture9* texture, unsigned int level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags );
		static HRESULT __stdcall UnlockTexture( IDirect3DTexture9* texture, unsigned int level );
		static ULONG __stdcall ReleaseSurface( IDirect3DSurface9* surface );
		static HRESULT __stdcall LockSurface( IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags );
		static HRESULT __stdcall UnlockSurface( IDirect3DSurface9* surface );
		static void __fastcall BindMaterial( void* context, void*, sdk::IMaterial* material, void* proxy_data );
		static int __fastcall VguiPaint( void* engine_vgui, void*, int mode );

		static FlatTextures* m_instance;

		// engine objects and chain-aware hooks installed on their VMTs.
		sdk::IMaterialSystem* m_material_system{ };
		sdk::IEngineVGui*     m_engine_vgui{ };
		IDirect3DDevice9*     m_device{ };
		IDirect3DTexture9*    m_flat_normal{ };
		void**                m_texture_vmt{ };
		void**                m_surface_vmt{ };
		VmtHook               m_create_texture;
		VmtHook               m_update_surface;
		VmtHook               m_update_texture;
		VmtHook               m_set_texture;
		VmtHook               m_release_texture;
		VmtHook               m_get_surface_level;
		VmtHook               m_lock_texture;
		VmtHook               m_unlock_texture;
		VmtHook               m_release_surface;
		VmtHook               m_lock_surface;
		VmtHook               m_unlock_surface;
		VmtHook               m_bind_material;
		VmtHook               m_vgui_paint;

		// all resource maps share one lock because their lifetimes are coupled.
		std::mutex                m_hook_mutex;
		mutable std::shared_mutex m_mutex;
		texture_map_t             m_textures;
		texture_locks_t           m_locks;
		surface_map_t             m_surfaces;
		surface_locks_t           m_surface_locks;

		// the hot-path flag remains lock-free.
		std::atomic_bool m_enabled{ };
	};

}
