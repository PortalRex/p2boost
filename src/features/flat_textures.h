#pragma once

#include "hook.h"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace p2boost {

	// One solid sRGB color chosen from a texture's visible pixels.
	struct FlatColor {
		std::uint8_t m_red{ 255 };
		std::uint8_t m_green{ 255 };
		std::uint8_t m_blue{ 255 };
	};

	// These helpers are public only so alpha preservation can be unit-tested
	// without loading Portal 2 or creating a D3D9 device.
	bool flat_texture_format_supported( D3DFORMAT format ) noexcept;
	FlatColor classify_flat_texture( D3DFORMAT format, const void* bits, int pitch, unsigned int width, unsigned int height ) noexcept;
	bool write_flat_texture( D3DFORMAT format,
		void* destination,
		int destination_pitch,
		const void* source,
		int source_pitch,
		unsigned int width,
		unsigned int height,
		FlatColor color ) noexcept;

	// Creates companion textures during Source uploads, then substitutes only
	// diffuse sampler zero. The original texture is always retained, making the
	// feature reversible without a game restart or lossy restoration.
	class FlatTextures {
	public:
		~FlatTextures( );

		FlatTextures( ) = default;
		FlatTextures( const FlatTextures& ) = delete;
		FlatTextures& operator=( const FlatTextures& ) = delete;

		bool install( );
		void remove( );

		// Reload Source materials so already-resident textures receive or discard
		// companions immediately when the user changes the toggle.
		bool set_enabled( bool enabled );
		void set_rendering_active( bool active ) noexcept;
		void release_level_textures( );

		bool available( ) const noexcept {
			return m_create_texture.installed( ) && m_set_texture.installed( ) && m_bind_material.installed( ) && m_material_system;
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		size_t texture_count( ) const noexcept;
		size_t ready_texture_count( ) const noexcept;
		std::uint64_t substitution_count( ) const noexcept {
			return m_substitutions.load( std::memory_order_relaxed );
		}
		std::uint64_t texture_upload_count( ) const noexcept {
			return m_texture_uploads.load( std::memory_order_relaxed );
		}
		std::uint64_t surface_upload_count( ) const noexcept {
			return m_surface_uploads.load( std::memory_order_relaxed );
		}
		std::uint64_t device_upload_count( ) const noexcept {
			return m_device_uploads.load( std::memory_order_relaxed );
		}

	private:
		struct TextureRecord {
			IDirect3DTexture9* m_flat{ };
			D3DFORMAT m_format{ D3DFMT_UNKNOWN };
			unsigned int m_width{ };
			unsigned int m_height{ };
			unsigned int m_levels{ };
			DWORD m_usage{ };
			D3DPOOL m_pool{ D3DPOOL_DEFAULT };
			FlatColor m_color;
			bool m_color_valid{ };
		};

		struct LockKey {
			IDirect3DTexture9* m_texture{ };
			unsigned int m_level{ };

			bool operator==( const LockKey& other ) const noexcept {
				return m_texture == other.m_texture && m_level == other.m_level;
			}
		};

		struct LockKeyHash {
			size_t operator( )( const LockKey& key ) const noexcept {
				const size_t texture = reinterpret_cast< size_t >( key.m_texture );
				return ( texture >> 4 ) ^ ( static_cast< size_t >( key.m_level ) * 0x9e3779b9u );
			}
		};

		struct LockedRegion {
			D3DLOCKED_RECT m_source{ };
			RECT m_rect{ };
			DWORD m_flags{ };
			bool m_has_rect{ };
		};

		struct SurfaceRecord {
			IDirect3DTexture9* m_texture{ };
			unsigned int m_level{ };
		};

		using CreateTextureFn = HRESULT( __stdcall* )(
			IDirect3DDevice9*, unsigned int, unsigned int, unsigned int, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE* );
		using UpdateSurfaceFn = HRESULT(
			__stdcall* )( IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT* );
		using UpdateTextureFn = HRESULT( __stdcall* )( IDirect3DDevice9*, IDirect3DBaseTexture9*, IDirect3DBaseTexture9* );
		using SetTextureFn = HRESULT( __stdcall* )( IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9* );
		using ReleaseTextureFn = ULONG( __stdcall* )( IDirect3DTexture9* );
		using GetSurfaceLevelFn = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int, IDirect3DSurface9** );
		using LockTextureFn = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int, D3DLOCKED_RECT*, const RECT*, DWORD );
		using UnlockTextureFn = HRESULT( __stdcall* )( IDirect3DTexture9*, unsigned int );
		using ReleaseSurfaceFn = ULONG( __stdcall* )( IDirect3DSurface9* );
		using LockSurfaceFn = HRESULT( __stdcall* )( IDirect3DSurface9*, D3DLOCKED_RECT*, const RECT*, DWORD );
		using UnlockSurfaceFn = HRESULT( __stdcall* )( IDirect3DSurface9* );
		using ReloadTexturesFn = void( __thiscall* )( void* );
		using ReloadMaterialsFn = void( __thiscall* )( void*, const char* );
		using GetRenderContextFn = void*(__thiscall*)( void* );
		using BindMaterialFn = void( __thiscall* )( void*, void*, void* );

		bool install_texture_hooks( IDirect3DTexture9* texture );
		bool install_surface_hooks( IDirect3DSurface9* surface );
		bool create_missing_companions( );
		bool reload_textures( ) const;
		bool reload_materials( ) const;
		void release_companions( );
		void release_all_companions( );

		static HRESULT __stdcall CreateTexture( IDirect3DDevice9* device,
			unsigned int width,
			unsigned int height,
			unsigned int levels,
			DWORD usage,
			D3DFORMAT format,
			D3DPOOL pool,
			IDirect3DTexture9** texture,
			HANDLE* shared_handle );
		static HRESULT __stdcall UpdateSurface( IDirect3DDevice9* device,
			IDirect3DSurface9* source,
			const RECT* source_rect,
			IDirect3DSurface9* destination,
			const POINT* destination_point );
		static HRESULT __stdcall UpdateTexture(
			IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination );
		static HRESULT __stdcall SetTexture( IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture );
		static ULONG __stdcall ReleaseTexture( IDirect3DTexture9* texture );
		static HRESULT __stdcall GetSurfaceLevel( IDirect3DTexture9* texture, unsigned int level, IDirect3DSurface9** surface );
		static HRESULT __stdcall LockTexture(
			IDirect3DTexture9* texture, unsigned int level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags );
		static HRESULT __stdcall UnlockTexture( IDirect3DTexture9* texture, unsigned int level );
		static ULONG __stdcall ReleaseSurface( IDirect3DSurface9* surface );
		static HRESULT __stdcall LockSurface( IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags );
		static HRESULT __stdcall UnlockSurface( IDirect3DSurface9* surface );
		static void __fastcall BindMaterial( void* context, void*, void* material, void* proxy_data );

		static FlatTextures* m_instance;

		void* m_material_system{ };
		void** m_render_context_vtable{ };
		IDirect3DDevice9* m_device{ };
		void** m_texture_vtable{ };
		void** m_surface_vtable{ };
		VtableSlotHook m_create_texture;
		VtableSlotHook m_update_surface;
		VtableSlotHook m_update_texture;
		VtableSlotHook m_set_texture;
		VtableSlotHook m_release_texture;
		VtableSlotHook m_get_surface_level;
		VtableSlotHook m_lock_texture;
		VtableSlotHook m_unlock_texture;
		VtableSlotHook m_release_surface;
		VtableSlotHook m_lock_surface;
		VtableSlotHook m_unlock_surface;
		VtableSlotHook m_bind_material;
		mutable std::shared_mutex m_mutex;
		std::unordered_map< IDirect3DTexture9*, TextureRecord > m_textures;
		std::unordered_map< LockKey, LockedRegion, LockKeyHash > m_locks;
		std::unordered_map< IDirect3DSurface9*, SurfaceRecord > m_surfaces;
		std::unordered_map< IDirect3DSurface9*, LockedRegion > m_surface_locks;
		std::atomic_bool m_enabled{ };
		std::atomic_bool m_rendering_active{ };
		std::atomic_uint64_t m_substitutions{ };
		std::atomic_uint64_t m_texture_uploads{ };
		std::atomic_uint64_t m_surface_uploads{ };
		std::atomic_uint64_t m_device_uploads{ };
	};

}
