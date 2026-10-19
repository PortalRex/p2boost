#pragma once

#include "console_commands.h"
#include "display_adjustments.h"
#include "hook.h"
#include "portal2_context.h"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdint>

namespace p2boost {

	// a held +remote_view assigns the co-op partner to split-screen slot 1, and
	// CViewRender::Render then runs a complete second RenderView for that slot
	// every frame: visibility, render lists, portal recursion, shadow depth,
	// and water for a picture only ss_pipscale of the screen wide. local
	// split-screen renders the second player's half the same way. the
	// partner_view_fps fix renders the partner's slot at most N times per
	// second and redraws the last picture in between: the remote slot, or in
	// local split-screen the player in_forceuser is not sending input to. the
	// view being controlled always renders every frame. with mat_viewportscale
	// below 1 the picture is the scaled render and DisplayAdjustments expands
	// it as it would the engine's own.
	class RemoteView {
	public:
		~RemoteView( );

		RemoteView( )                              = default;
		RemoteView( const RemoteView& )            = delete;
		RemoteView& operator=( const RemoteView& ) = delete;

		bool install( const portal2_modules_t&      modules,
		              const Portal2Interfaces&      interfaces,
		              const ConsoleCommandRegistry& commands,
		              IDirect3DDevice9*             device,
		              DisplayAdjustments*           display );
		void remove( );

		bool available( ) const noexcept {
			return m_render_view.installed( );
		}

		// picture reuse also needs the call queue and the device copy path.
		bool reuse_available( ) const noexcept {
			return available( ) && m_device && m_reuse_supported;
		}

		void set_max_fps( int fps ) noexcept;
		int  max_fps( ) const noexcept {
			return m_max_fps.load( std::memory_order_relaxed );
		}

		// forget the last picture so the next remote frame renders for real.
		void invalidate( ) noexcept {
			m_picture_rect_valid = false;
		}

		std::uint64_t rendered_count( ) const noexcept {
			return m_rendered.load( std::memory_order_relaxed );
		}

		std::uint64_t reused_count( ) const noexcept {
			return m_reused.load( std::memory_order_relaxed );
		}

		// executed in render-thread order by a queued device call.
		void run_device_call( bool save, const RECT& rect );

	private:
		using render_view_fn_t = void( __thiscall* )( void*, const void*, const void*, int, int );

		static void __fastcall RenderView( void* self, void*, const void* view, const void* hud_view, int clear_flags, int what_to_draw );
		static void ResetListener( void* context );

		int  view_slot( const void* self, const void* view ) const noexcept;
		bool slot_is_remote( int slot ) const noexcept;
		int  partner_slot( ) const noexcept;
		bool queue_device_call( bool save, const RECT& rect );
		bool ensure_picture( IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format );
		bool draw_picture( IDirect3DDevice9* device, const RECT& rect );
		void release_device_resources( ) noexcept;

		static RemoteView* m_instance;

		VmtHook               m_render_view;
		IDirect3DDevice9*     m_device{ };
		DisplayAdjustments*   m_display{ };
		sdk::IMaterialSystem* m_material_system{ };
		con_var_t*            m_viewport_scale{ };
		con_var_t*            m_force_user{ };
		const std::uint32_t*  m_local_slot_mask{ };
		const std::uint32_t*  m_remote_players{ };
		bool                  m_reuse_supported{ };

		std::atomic_int m_max_fps{ };

		// main-thread picture schedule.
		LARGE_INTEGER m_frequency{ };
		LONGLONG      m_last_render{ };
		RECT          m_picture_rect{ };
		bool          m_picture_rect_valid{ };

		// render-thread picture placement: the rect the picture was copied
		// from, which is the scaled render when mat_viewportscale is below 1.
		RECT             m_source_rect{ };
		bool             m_source_scaled{ };
		viewport_scale_t m_source_view{ };

		// render-thread resources; released before device Reset.
		IDirect3DTexture9*    m_picture{ };
		IDirect3DStateBlock9* m_state_block{ };
		UINT                  m_picture_width{ };
		UINT                  m_picture_height{ };
		D3DFORMAT             m_picture_format{ D3DFMT_UNKNOWN };
		std::atomic_bool      m_picture_ready{ };

		std::atomic_int      m_pending_calls{ };
		std::atomic_uint64_t m_rendered{ };
		std::atomic_uint64_t m_reused{ };
	};

}
