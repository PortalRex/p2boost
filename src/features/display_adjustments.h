#pragma once

#include "console_commands.h"
#include "d3d9_util.h"
#include "frame_metrics.h"
#include "hook.h"
#include "interfaces.h"
#include "portal2_context.h"

#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace p2boost {
	// r_intensity bounds, matching Quake Live's 0.0..2.0 range with 1.0 neutral.
	constexpr float min_intensity{ 0.0f };
	constexpr float max_intensity{ 2.0f };

	// return the final-frame exponent relative to Source's neutral 2.2 curve.
	float monitor_gamma_curve( float monitor_gamma ) noexcept;

	// describe one low-resolution view and the slot it must fill. Portal 2
	// preserves split-screen origins while scaling only each slot's dimensions.
	struct viewport_scale_t {
		RECT m_source{ };
		RECT m_destination{ };
	};

	bool scaled_viewport_rect( const D3DVIEWPORT9&    viewport,
	                           const D3DSURFACE_DESC& target,
	                           float                  scale,
	                           viewport_scale_t&      result ) noexcept;

	// clamp a degenerate 0-size viewport to one pixel so D3D9 does not silently
	// keep the previous (possibly fullscreen) viewport for a hidden split-slot.
	D3DVIEWPORT9 clamp_degenerate_viewport( const D3DVIEWPORT9& viewport ) noexcept;

	// restores two display controls that Portal 2's DXVK path does not finish:
	// viewport scaling is expanded before VGUI, and monitor gamma is applied
	// after every final UI draw.
	class DisplayAdjustments {
	public:
		~DisplayAdjustments( );

		DisplayAdjustments( )                                      = default;
		DisplayAdjustments( const DisplayAdjustments& )            = delete;
		DisplayAdjustments& operator=( const DisplayAdjustments& ) = delete;

		bool install( const ConsoleCommandRegistry& commands, IDirect3DDevice9* device, FrameMetrics* metrics );
		void remove( );

		// the shader API's CommitSetViewports skips zero-size viewports, which
		// leaves the previous (possibly fullscreen) viewport active when a hidden
		// split-screen slot renders. this is the engine-side companion to the
		// degenerate-viewport clamp in SetViewport.
		bool install_viewport_commit_patch( const portal2_modules_t& modules );

		bool available( ) const noexcept {
			return m_present.installed( ) && m_reset.installed( ) && m_set_viewport.installed( );
		}

		std::uint64_t gamma_passes( ) const noexcept {
			return m_gamma_passes.load( std::memory_order_relaxed );
		}

		std::uint64_t viewport_passes( ) const noexcept {
			return m_viewport_passes.load( std::memory_order_relaxed );
		}

		// Quake-style r_intensity: a linear RGB multiplier applied to the final
		// frame before the monitor-gamma curve. 1.0 is neutral.
		float intensity( ) const noexcept {
			return m_intensity.load( std::memory_order_relaxed );
		}

		void set_intensity( float intensity ) noexcept {
			m_intensity.store( std::clamp( intensity, min_intensity, max_intensity ), std::memory_order_relaxed );
		}

		// render thread: finish the pending expansion of the scaled slot whose
		// final rect is destination now, instead of at its next unscaled
		// viewport. a feature that copies the slot's final picture calls this
		// first, since a slot without HUD draws may not set that viewport.
		void finish_scaled_view( IDirect3DDevice9* device, const RECT& destination );

		// other features' default-pool resources and render-target contents
		// must also be dropped at IDirect3DDevice9::Reset; this one hook already
		// owns that boundary. a context registers at most one listener.
		using reset_listener_fn_t = void ( * )( void* context );
		bool add_reset_listener( reset_listener_fn_t listener, void* context ) noexcept;
		void remove_reset_listener( void* context ) noexcept;

	private:
		using present_fn_t      = HRESULT( __stdcall* )( IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA* );
		using reset_fn_t        = HRESULT( __stdcall* )( IDirect3DDevice9*, D3DPRESENT_PARAMETERS* );
		using set_viewport_fn_t = HRESULT( __stdcall* )( IDirect3DDevice9*, const D3DVIEWPORT9* );

		bool apply_gamma( IDirect3DDevice9* device );
		bool apply_viewport_scale( IDirect3DDevice9* device, const viewport_scale_t& view );
		bool ensure_upscale_target( IDirect3DDevice9* device, LONG width, LONG height );
		void clear_back_buffer_rects( IDirect3DDevice9* device, const D3DRECT* rects, DWORD count );
		bool copy_back_buffer( IDirect3DDevice9*    device,
		                       const RECT*          source,
		                       const RECT*          destination,
		                       D3DTEXTUREFILTERTYPE filter );
		bool restore_back_buffer( IDirect3DDevice9* device );
		bool draw_fullscreen_copy( IDirect3DDevice9* device, float curve, float intensity );
		bool ensure_copy_target( IDirect3DDevice9* device, const D3DSURFACE_DESC& description );
		bool ensure_gamma_resources( IDirect3DDevice9* device );
		void release_resources( );

		static HRESULT __stdcall Present( IDirect3DDevice9* device, const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty_region );
		static HRESULT __stdcall Reset( IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters );
		static HRESULT __stdcall SetViewport( IDirect3DDevice9* device, const D3DVIEWPORT9* viewport );

		static DisplayAdjustments* m_instance;
		FrameMetrics*              m_metrics{ };
		con_var_t*                 m_monitor_gamma{ };
		con_var_t*                 m_viewport_scale{ };
		con_var_t*                 m_viewport_upscale{ };
		IDirect3DDevice9*          m_device{ };
		com_ptr< IDirect3DTexture9 >     m_copy_texture;
		com_ptr< IDirect3DTexture9 >     m_upscale_texture;
		com_ptr< IDirect3DPixelShader9 > m_gamma_shader;
		com_ptr< IDirect3DStateBlock9 >  m_state_block;
		unsigned int               m_copy_width{ };
		unsigned int               m_copy_height{ };
		unsigned int               m_upscale_width{ };
		unsigned int               m_upscale_height{ };
		D3DFORMAT                  m_copy_format{ D3DFMT_UNKNOWN };
		viewport_scale_t           m_scaled_view{ };
		bool                       m_scaled_view_pending{ };
		bool                       m_internal_viewport{ };
		VmtHook                    m_present;
		VmtHook                    m_reset;
		VmtHook                    m_set_viewport;
		BytePatch                  m_viewport_commit;
		std::atomic< float >       m_intensity{ 1.0f };
		std::atomic_uint64_t       m_gamma_passes{ };
		std::atomic_uint64_t       m_viewport_passes{ };

		struct reset_listener_t {
			std::atomic< reset_listener_fn_t > m_listener{ };
			void*                              m_context{ };
		};
		std::array< reset_listener_t, 2 > m_reset_listeners{ };
	};

}