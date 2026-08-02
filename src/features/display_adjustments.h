#pragma once

#include "console_commands.h"
#include "hook.h"
#include "interfaces.h"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdint>

namespace p2boost {
	// return the final-frame exponent relative to Source's neutral 2.2 curve.
	float monitor_gamma_curve( float monitor_gamma ) noexcept;

	// restores two display controls that Portal 2's DXVK path does not finish:
	// viewport scaling is expanded before VGUI, and monitor gamma is applied
	// after every final UI draw.
	class DisplayAdjustments {
	public:
		~DisplayAdjustments( );

		DisplayAdjustments( )									   = default;
		DisplayAdjustments( const DisplayAdjustments& )			   = delete;
		DisplayAdjustments& operator=( const DisplayAdjustments& ) = delete;

		bool install( const ConsoleCommandRegistry& commands );
		void remove( );

		bool available( ) const noexcept {
			return m_present.installed( ) && m_reset.installed( ) && m_set_viewport.installed( );
		}

		std::uint64_t gamma_passes( ) const noexcept {
			return m_gamma_passes.load( std::memory_order_relaxed );
		}

		std::uint64_t viewport_passes( ) const noexcept {
			return m_viewport_passes.load( std::memory_order_relaxed );
		}

	private:
		using PresentFn		= HRESULT( __stdcall* )( IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA* );
		using ResetFn		= HRESULT( __stdcall* )( IDirect3DDevice9*, D3DPRESENT_PARAMETERS* );
		using SetViewportFn = HRESULT( __stdcall* )( IDirect3DDevice9*, const D3DVIEWPORT9* );

		bool apply_gamma( IDirect3DDevice9* device );
		bool apply_viewport_scale( IDirect3DDevice9* device );
		bool rendering_to_back_buffer( IDirect3DDevice9* device, D3DSURFACE_DESC& description ) const;
		bool copy_back_buffer( IDirect3DDevice9* device, const RECT* source, D3DTEXTUREFILTERTYPE filter );
		bool ensure_copy_target( IDirect3DDevice9* device, const D3DSURFACE_DESC& description );
		bool ensure_gamma_resources( IDirect3DDevice9* device );
		void release_resources( );

		static HRESULT __stdcall Present( IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty_region );
		static HRESULT __stdcall Reset( IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters );
		static HRESULT __stdcall SetViewport( IDirect3DDevice9* device, const D3DVIEWPORT9* viewport );

		static DisplayAdjustments* m_instance;
		static PresentFn		   m_next_present;
		static ResetFn			   m_next_reset;
		static SetViewportFn	   m_next_set_viewport;

		ConsoleVariable*	   m_monitor_gamma{ };
		ConsoleVariable*	   m_viewport_scale{ };
		ConsoleVariable*	   m_viewport_upscale{ };
		IDirect3DDevice9*	   m_device{ };
		IDirect3DTexture9*	   m_copy_texture{ };
		IDirect3DPixelShader9* m_gamma_shader{ };
		IDirect3DStateBlock9*  m_state_block{ };
		unsigned int		   m_copy_width{ };
		unsigned int		   m_copy_height{ };
		D3DFORMAT			   m_copy_format{ D3DFMT_UNKNOWN };
		bool				   m_viewport_applied{ };
		bool				   m_scaled_view_seen{ };
		bool				   m_internal_viewport{ };
		VmtHook				   m_present;
		VmtHook				   m_reset;
		VmtHook				   m_set_viewport;
		std::atomic_uint64_t   m_gamma_passes{ };
		std::atomic_uint64_t   m_viewport_passes{ };
	};

}
