#pragma once

#include "console_commands.h"
#include "hook.h"
#include "portal2_context.h"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstdint>

namespace p2boost {

	class DisplayAdjustments;

	// a held +remote_view assigns the co-op partner to split-screen slot 1, and
	// CViewRender::Render then runs a complete second RenderView for that slot
	// every frame: visibility, render lists, portal recursion, shadow depth,
	// and water for a picture only ss_pipscale of the screen wide. two opt-in,
	// independent reductions apply to that remote slot only:
	//   p2boost_remote_view_fps N        render it at most N times per second and
	//                                    redraw the last picture in between
	//   p2boost_remote_view_lowdetail L  render it with fewer portal recursions,
	//                                    no 3D skybox or water reflection (L>=1)
	//                                    and no particles, coarser LOD (L>=2)
	// local split-screen players are never affected.
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

		void set_low_detail( int level ) noexcept;
		int  low_detail( ) const noexcept {
			return m_low_detail.load( std::memory_order_relaxed );
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
		struct detail_override_t {
			const char* m_name;
			int         m_level;
			int         m_value;
			con_var_t*  m_variable;
			int         m_saved_integer;
			float       m_saved_float;
		};

		using render_view_fn_t = void( __thiscall* )( void*, const void*, const void*, int, int );

		static void __fastcall RenderView( void* self, void*, const void* view, const void* hud_view, int clear_flags, int what_to_draw );
		static void            ResetListener( void* context );

		int  remote_slot( const void* self, const void* view ) const noexcept;
		bool queue_device_call( bool save, const RECT& rect );
		void apply_low_detail( int level ) noexcept;
		void restore_low_detail( int level ) noexcept;
		bool ensure_picture( IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format );
		bool draw_picture( IDirect3DDevice9* device, const RECT& rect );
		void release_device_resources( ) noexcept;

		static RemoteView* m_instance;

		VmtHook               m_render_view;
		IDirect3DDevice9*     m_device{ };
		DisplayAdjustments*   m_display{ };
		sdk::IMaterialSystem* m_material_system{ };
		con_var_t*            m_viewport_scale{ };
		const std::uint32_t*  m_local_slot_mask{ };
		const std::uint32_t*  m_remote_players{ };
		bool                  m_reuse_supported{ };

		std::atomic_int m_max_fps{ };
		std::atomic_int m_low_detail{ };

		// main-thread picture schedule.
		LARGE_INTEGER m_frequency{ };
		LONGLONG      m_last_render{ };
		RECT          m_picture_rect{ };
		bool          m_picture_rect_valid{ };

		detail_override_t m_overrides[ 6 ]{
		    { "r_portal_stencil_depth", 1, 1 },
		    { "r_3dsky", 1, 0 },
		    { "r_WaterDrawReflection", 1, 0 },
		    { "r_WaterDrawRefraction", 1, 0 },
		    { "r_drawparticles", 2, 0 },
		    { "r_lod", 2, 2 },
		};

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
