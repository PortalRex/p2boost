#include "display_adjustments.h"

#include "game_profile.h"
#include "d3d9_slots.h"
#include "d3d9_util.h"
#include "monitor_gamma_shader.h"
#include "portal2_context.h"
#include "qpc.h"
#include "vmt.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace p2boost {
	namespace {
		constexpr float neutral_monitor_gamma{ 2.2f };
		constexpr float gamma_epsilon{ 0.001f };

		bool same_rect( const RECT& left, const RECT& right ) noexcept {
			return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
		}
	}

	DisplayAdjustments* DisplayAdjustments::m_instance{ };

	D3DVIEWPORT9 clamp_degenerate_viewport( const D3DVIEWPORT9& viewport ) noexcept {
		D3DVIEWPORT9 clamped = viewport;
		if( clamped.Width == 0 || clamped.Height == 0 ) {
			clamped.Width  = 1;
			clamped.Height = 1;
		}
		return clamped;
	}

	float monitor_gamma_curve( float monitor_gamma ) noexcept {
		return std::clamp( monitor_gamma, 1.6f, 2.6f ) / neutral_monitor_gamma;
	}

	bool scaled_viewport_rect( const D3DVIEWPORT9&    viewport,
	                           const D3DSURFACE_DESC& target,
	                           float                  scale,
	                           viewport_scale_t&      result ) noexcept {
		if( scale <= 0.0f || scale >= 1.0f - gamma_epsilon || viewport.Width < 16 || viewport.Height < 16 )
			return false;

		unsigned int width  = std::max( 1u, static_cast< unsigned int >( std::lround( viewport.Width / scale ) ) );
		unsigned int height = std::max( 1u, static_cast< unsigned int >( std::lround( viewport.Height / scale ) ) );
		if( width <= viewport.Width || height <= viewport.Height )
			return false;
		if( viewport.X + width > target.Width + 1u || viewport.Y + height > target.Height + 1u )
			return false;

		// lround can leave the destination a pixel or two short of the target
		// edge at non-even scales; that stale strip of the previous frame would
		// show through beside the expanded image. snap to the exact edge.
		const LONG dest_right  = static_cast< LONG >( viewport.X + width );
		const LONG dest_bottom = static_cast< LONG >( viewport.Y + height );
		if( dest_right < static_cast< LONG >( target.Width ) && static_cast< LONG >( target.Width ) - dest_right <= 2 )
			width += static_cast< unsigned int >( static_cast< LONG >( target.Width ) - dest_right );
		if( dest_bottom < static_cast< LONG >( target.Height ) && static_cast< LONG >( target.Height ) - dest_bottom <= 2 )
			height += static_cast< unsigned int >( static_cast< LONG >( target.Height ) - dest_bottom );

		result.m_source      = { static_cast< LONG >( viewport.X ),
		                         static_cast< LONG >( viewport.Y ),
		                         static_cast< LONG >( viewport.X + viewport.Width ),
		                         static_cast< LONG >( viewport.Y + viewport.Height ) };
		result.m_destination = { static_cast< LONG >( viewport.X ),
		                         static_cast< LONG >( viewport.Y ),
		                         static_cast< LONG >( viewport.X + width ),
		                         static_cast< LONG >( viewport.Y + height ) };
		return true;
	}

	DisplayAdjustments::~DisplayAdjustments( ) {
		remove( );
	}

	bool DisplayAdjustments::install( const ConsoleCommandRegistry& commands, IDirect3DDevice9* device, FrameMetrics* metrics, bool engine_gamma_ramp ) {
		if( m_instance )
			return false;

		m_metrics           = metrics;
		m_engine_gamma_ramp = engine_gamma_ramp;
		m_monitor_gamma     = commands.find_variable( "mat_monitorgamma" );
		m_viewport_scale    = commands.find_variable( "mat_viewportscale" );
		m_viewport_upscale  = commands.find_variable( "mat_viewportupscale" );
		if( !m_monitor_gamma || !m_viewport_scale || !m_viewport_upscale )
			return false;

		m_device = device;
		if( !m_device )
			return false;

		// Reset keeps this current; the device may already be fullscreen.
		com_ptr< IDirect3DSwapChain9 > swap_chain;
		D3DPRESENT_PARAMETERS          parameters{ };
		if( SUCCEEDED( m_device->GetSwapChain( 0, swap_chain.ReleaseAndGetAddressOf( ) ) ) && swap_chain &&
		    SUCCEEDED( swap_chain->GetPresentParameters( &parameters ) ) )
			m_windowed.store( parameters.Windowed != FALSE, std::memory_order_relaxed );

		auto** device_vmt = vmt_of( m_device );
		m_instance        = this;

		// publish each forward target as soon as its hook becomes callable. this
		// keeps the render thread safe if it runs during plugin initialization.
		if( !m_reset.install( device_vmt + d3d9_slot::reset, reinterpret_cast< void* >( &Reset ) ) ) {
			remove( );
			return false;
		}
		if( !m_present.install( device_vmt + d3d9_slot::present, reinterpret_cast< void* >( &Present ) ) ) {
			remove( );
			return false;
		}
		if( !m_set_viewport.install( device_vmt + d3d9_slot::set_viewport, reinterpret_cast< void* >( &SetViewport ) ) ) {
			remove( );
			return false;
		}
		return true;
	}

	bool DisplayAdjustments::install_viewport_commit_patch( const portal2_modules_t& modules ) {
		if( m_viewport_commit.installed( ) )
			return true;

		// CommitSetViewports guards both dimensions with conditional branches:
		//   83 7E 08 00 74 33   cmp [esi+8],0 ; je skip    (width)
		//   83 7E 0C 00 74 2D   cmp [esi+0xc],0 ; je skip  (height)
		// the D3D9 device therefore never sees the hidden slot's 0-size viewport
		// and keeps the fullscreen boundary viewport active. neutralising both
		// branches (NOP) falls through to the device dispatch so the degenerate
		// rect reaches the device, where SetViewport clamps it to one pixel.
		const Address commit = modules.m_shader_api.find( game::active( )->viewport_commit_skip );
		if( !commit )
			return false;

		const std::vector< std::uint8_t > expected{ 0x83, 0x7e, 0x08, 0x00, 0x74, 0x33, 0x83, 0x7e, 0x0c, 0x00, 0x74, 0x2d };
		const std::vector< std::uint8_t > patched{ 0x83, 0x7e, 0x08, 0x00, 0x90, 0x90, 0x83, 0x7e, 0x0c, 0x00, 0x90, 0x90 };
		return m_viewport_commit.install( commit.as< void* >( ), expected, patched );
	}

	void DisplayAdjustments::remove( ) {
		m_viewport_commit.remove( );
		m_set_viewport.remove( );
		m_present.remove( );
		m_reset.remove( );
		release_resources( );
		m_monitor_gamma       = nullptr;
		m_viewport_scale      = nullptr;
		m_viewport_upscale    = nullptr;
		m_device              = nullptr;
		m_scaled_view         = { };
		m_scaled_view_pending = false;
		m_internal_viewport   = false;
		m_intensity.store( 1.0f, std::memory_order_relaxed );
		if( m_instance == this )
			m_instance = nullptr;
	}

	bool DisplayAdjustments::ensure_copy_target( IDirect3DDevice9* device, const D3DSURFACE_DESC& description ) {
		if( m_copy_texture && m_copy_width == description.Width && m_copy_height == description.Height &&
		    m_copy_format == description.Format )
			return true;

		release_resources( );
		if( FAILED( device->CreateTexture( description.Width,
		                                   description.Height,
		                                   1,
		                                   D3DUSAGE_RENDERTARGET,
		                                   description.Format,
		                                   D3DPOOL_DEFAULT,
		                                   m_copy_texture.ReleaseAndGetAddressOf( ),
		                                   nullptr ) ) )
			return false;

		m_copy_width  = description.Width;
		m_copy_height = description.Height;
		m_copy_format = description.Format;
		return true;
	}

	bool DisplayAdjustments::ensure_upscale_target( IDirect3DDevice9* device, LONG width, LONG height ) {
		const LONG padded_width  = width + 2;
		const LONG padded_height = height + 2;
		if( m_upscale_texture && m_upscale_width == static_cast< unsigned int >( padded_width ) &&
		    m_upscale_height == static_cast< unsigned int >( padded_height ) )
			return true;

		// one pixel of black padding on every side gives the linear expansion
		// a guaranteed-clean sample at the source boundary. the region outside
		// the source rect is cleared once here and never written again, so the
		// border stays black even when the scale changes mid-frame.
		m_upscale_texture.Reset( );
		D3DSURFACE_DESC description{ };
		if( !back_buffer_description( device, description ) )
			return false;

		if( FAILED( device->CreateTexture( static_cast< UINT >( padded_width ),
		                                   static_cast< UINT >( padded_height ),
		                                   1,
		                                   D3DUSAGE_RENDERTARGET,
		                                   description.Format,
		                                   D3DPOOL_DEFAULT,
		                                   m_upscale_texture.ReleaseAndGetAddressOf( ),
		                                   nullptr ) ) )
			return false;

		m_upscale_width  = static_cast< unsigned int >( padded_width );
		m_upscale_height = static_cast< unsigned int >( padded_height );

		// clear the padding while the surface is bound, then restore the
		// device's original render target so the engine's next draw is safe.
		com_ptr< IDirect3DSurface9 > surface;
		com_ptr< IDirect3DSurface9 > previous_target;
		if( SUCCEEDED( m_upscale_texture->GetSurfaceLevel( 0, surface.ReleaseAndGetAddressOf( ) ) ) && surface &&
		    SUCCEEDED( device->GetRenderTarget( 0, previous_target.ReleaseAndGetAddressOf( ) ) ) ) {
			const D3DVIEWPORT9 padded_viewport{ 0, 0, m_upscale_width, m_upscale_height, 0.0f, 1.0f };
			m_internal_viewport = true;
			device->SetViewport( &padded_viewport );
			device->SetRenderTarget( 0, surface.Get( ) );
			device->Clear( 0, nullptr, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0 );
			device->SetRenderTarget( 0, previous_target.Get( ) );
			m_internal_viewport = false;
		}
		return true;
	}

	bool DisplayAdjustments::ensure_gamma_resources( IDirect3DDevice9* device ) {
		if( !m_gamma_shader &&
		    FAILED( device->CreatePixelShader( reinterpret_cast< const DWORD* >( g_monitor_gamma_shader ), m_gamma_shader.ReleaseAndGetAddressOf( ) ) ) )
			return false;
		if( !m_state_block && FAILED( device->CreateStateBlock( D3DSBT_ALL, m_state_block.ReleaseAndGetAddressOf( ) ) ) )
			return false;
		return true;
	}

	bool DisplayAdjustments::copy_back_buffer( IDirect3DDevice9*    device,
	                                           const RECT*          source,
	                                           const RECT*          destination,
	                                           D3DTEXTUREFILTERTYPE filter ) {
		const com_ptr< IDirect3DSurface9 > surface = back_buffer( device );
		D3DSURFACE_DESC                    description{ };
		com_ptr< IDirect3DSurface9 >       copy_surface;
		if( !surface || FAILED( surface->GetDesc( &description ) ) || !ensure_copy_target( device, description ) ||
		    FAILED( m_copy_texture->GetSurfaceLevel( 0, copy_surface.ReleaseAndGetAddressOf( ) ) ) || !copy_surface )
			return false;

		const RECT  full_target{ 0, 0, static_cast< LONG >( description.Width ), static_cast< LONG >( description.Height ) };
		const RECT* copy_destination = destination ? destination : &full_target;
		return SUCCEEDED( device->StretchRect( surface.Get( ), source, copy_surface.Get( ), copy_destination, filter ) );
	}

	bool DisplayAdjustments::restore_back_buffer( IDirect3DDevice9* device ) {
		if( !m_copy_texture )
			return false;

		com_ptr< IDirect3DSurface9 >       copy_surface;
		const com_ptr< IDirect3DSurface9 > surface = back_buffer( device );
		if( surface && SUCCEEDED( m_copy_texture->GetSurfaceLevel( 0, copy_surface.ReleaseAndGetAddressOf( ) ) ) && copy_surface &&
		    SUCCEEDED( device->StretchRect( copy_surface.Get( ), nullptr, surface.Get( ), nullptr, D3DTEXF_NONE ) ) )
			return true;

		// reverse StretchRect support is driver-dependent, and a multisampled
		// destination can reject it. an identity draw restores the same source.
		return draw_fullscreen_copy( device, 1.0f, 1.0f );
	}

	bool DisplayAdjustments::apply_viewport_scale( IDirect3DDevice9* device, const viewport_scale_t& view ) {
		if( !m_viewport_scale || !m_viewport_upscale || !m_viewport_upscale->value_integer( ) )
			return false;

		const float scale = std::clamp( m_viewport_scale->value_float( ), 1.0f / 640.0f, 1.0f );
		if( scale >= 1.0f - gamma_epsilon )
			return false;

		const LONG source_width  = view.m_source.right - view.m_source.left;
		const LONG source_height = view.m_source.bottom - view.m_source.top;
		if( source_width <= 0 || source_height <= 0 )
			return false;

		// stage the scaled scene in a source-sized texture wrapped in a one-pixel
		// black border. the linear expansion then samples only scene pixels (or
		// the border) instead of whatever the back buffer holds beside the source
		// rect, which otherwise bleeds stale content into the expanded edges.
		if( !ensure_upscale_target( device, source_width, source_height ) )
			return false;

		com_ptr< IDirect3DSurface9 >       stage_surface;
		const com_ptr< IDirect3DSurface9 > surface = back_buffer( device );
		if( !surface || FAILED( m_upscale_texture->GetSurfaceLevel( 0, stage_surface.ReleaseAndGetAddressOf( ) ) ) || !stage_surface )
			return false;

		const RECT stage_rect{ 1, 1, source_width + 1, source_height + 1 };
		return SUCCEEDED( device->StretchRect( surface.Get( ), &view.m_source, stage_surface.Get( ), &stage_rect, D3DTEXF_NONE ) ) &&
		       SUCCEEDED( device->StretchRect( stage_surface.Get( ), &stage_rect, surface.Get( ), &view.m_destination, D3DTEXF_LINEAR ) );
	}

	// D3D9 interprets Clear pRects relative to the current viewport, which at
	// viewport-boundary time is still the scaled 3D rect (or one of the engine's
	// 1px border-clear rects). normalize to the full back buffer before the
	// clear and restore afterwards, so p2boost's rects always land where the
	// expansion expects them.
	void DisplayAdjustments::clear_back_buffer_rects( IDirect3DDevice9* device, const D3DRECT* rects, DWORD count ) {
		if( !device || !rects || !count )
			return;

		D3DVIEWPORT9    previous{ };
		const bool      has_previous = SUCCEEDED( device->GetViewport( &previous ) );
		D3DSURFACE_DESC description{ };
		if( !back_buffer_description( device, description ) )
			return;

		const D3DVIEWPORT9 fullscreen{ 0, 0, description.Width, description.Height, 0.0f, 1.0f };
		m_internal_viewport = true;
		device->SetViewport( &fullscreen );
		device->Clear( count, rects, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0 );
		if( has_previous )
			device->SetViewport( &previous );
		m_internal_viewport = false;
	}

	bool DisplayAdjustments::draw_fullscreen_copy( IDirect3DDevice9* device, float curve_value, float intensity ) {
		if( !m_copy_texture || !m_gamma_shader || !m_state_block || FAILED( m_state_block->Capture( ) ) )
			return false;

		const D3DVIEWPORT9 viewport{ 0, 0, m_copy_width, m_copy_height, 0.0f, 1.0f };
		const float        curve[ 4 ]{ curve_value, intensity, 0.0f, 0.0f };

		m_internal_viewport = true;
		device->SetViewport( &viewport );
		m_internal_viewport = false;
		device->SetVertexShader( nullptr );
		device->SetPixelShader( m_gamma_shader.Get( ) );
		device->SetTexture( 0, m_copy_texture.Get( ) );
		device->SetPixelShaderConstantF( 0, curve, 1 );
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
		device->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );
		device->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, 0 );
		device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		const bool drawn =
		    draw_screen_quad( device, screen_quad( 0.0f, 0.0f, static_cast< float >( m_copy_width ), static_cast< float >( m_copy_height ) ) );

		// restore state even after a failed draw; callers only need to know
		// whether this pass actually copied pixels.
		m_state_block->Apply( );
		return drawn;
	}

	bool DisplayAdjustments::apply_gamma( IDirect3DDevice9* device ) {
		if( !m_monitor_gamma )
			return false;

		const float monitor_gamma    = std::clamp( m_monitor_gamma->value_float( ), 1.6f, 2.6f );
		const float intensity        = std::clamp( m_intensity.load( std::memory_order_relaxed ), min_intensity, max_intensity );
		// native D3D9 applies the engine's gamma ramp in exclusive fullscreen;
		// a second curve here would apply mat_monitorgamma twice.
		const bool  ramp_applied     = m_engine_gamma_ramp && !m_windowed.load( std::memory_order_relaxed );
		const bool  gamma_active     = !ramp_applied && std::fabs( monitor_gamma - neutral_monitor_gamma ) > gamma_epsilon;
		const bool  intensity_active = std::fabs( intensity - 1.0f ) > gamma_epsilon;
		if( !gamma_active && !intensity_active )
			return false;
		if( !copy_back_buffer( device, nullptr, nullptr, D3DTEXF_NONE ) || !ensure_gamma_resources( device ) )
			return false;
		return draw_fullscreen_copy( device, monitor_gamma_curve( monitor_gamma ), intensity );
	}

	void DisplayAdjustments::release_resources( ) {
		m_state_block.Reset( );
		m_gamma_shader.Reset( );
		m_copy_texture.Reset( );
		m_upscale_texture.Reset( );
		m_copy_width     = 0;
		m_copy_height    = 0;
		m_copy_format    = D3DFMT_UNKNOWN;
		m_upscale_width  = 0;
		m_upscale_height = 0;
	}

	HRESULT __stdcall DisplayAdjustments::Present( IDirect3DDevice9* device,
	                                               const RECT*       source,
	                                               const RECT*       destination,
	                                               HWND              window,
	                                               const RGNDATA*    dirty_region ) {
		DisplayAdjustments* instance = m_instance;
		const auto          next     = instance ? instance->m_present.previous< present_fn_t >( ) : nullptr;
		if( instance && instance->m_scaled_view_pending ) {
			// a full-screen destination has nothing to expand into.
			D3DSURFACE_DESC description{ };
			const bool      subview = back_buffer_description( device, description ) &&
			                     !same_rect( instance->m_scaled_view.m_destination,
			                                 RECT{ 0, 0, static_cast< LONG >( description.Width ), static_cast< LONG >( description.Height ) } );
			if( subview && instance->apply_viewport_scale( device, instance->m_scaled_view ) )
				instance->m_viewport_passes.fetch_add( 1, std::memory_order_relaxed );
			instance->m_scaled_view_pending = false;
		}
		const bool gamma_drawn = instance && instance->apply_gamma( device );
		if( gamma_drawn )
			instance->m_gamma_passes.fetch_add( 1, std::memory_order_relaxed );

		// the wall time inside the real Present is the GPU/present cost of the
		// finished frame. folding it into the frame sample separates a CPU-bound
		// render path from one that waits on the display.
		const Qpc::tick_t present_begin = Qpc::now( );
		const HRESULT     result        = next ? next( device, source, destination, window, dirty_region ) : D3DERR_INVALIDCALL;
		if( instance && instance->m_metrics )
			instance->m_metrics->record_present( Qpc::milliseconds( Qpc::now( ) - present_begin ) );

		// present only the corrected pixels. restoring the captured linear frame
		// prevents retained loading frames from feeding the gamma pass repeatedly.
		if( gamma_drawn )
			instance->restore_back_buffer( device );
		return result;
	}

	HRESULT __stdcall DisplayAdjustments::Reset( IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters ) {
		DisplayAdjustments* instance = m_instance;
		const auto          next     = instance ? instance->m_reset.previous< reset_fn_t >( ) : nullptr;
		if( instance ) {
			instance->release_resources( );
			instance->m_scaled_view         = { };
			instance->m_scaled_view_pending = false;
			for( auto& slot : instance->m_reset_listeners )
				if( const auto listener = slot.m_listener.load( std::memory_order_acquire ) )
					listener( slot.m_context );
		}
		const HRESULT result = next ? next( device, parameters ) : D3DERR_INVALIDCALL;
		if( instance && SUCCEEDED( result ) && parameters )
			instance->m_windowed.store( parameters->Windowed != FALSE, std::memory_order_relaxed );
		return result;
	}

	bool DisplayAdjustments::add_reset_listener( reset_listener_fn_t listener, void* context ) noexcept {
		for( auto& slot : m_reset_listeners ) {
			if( slot.m_listener.load( std::memory_order_acquire ) )
				continue;
			slot.m_context = context;
			slot.m_listener.store( listener, std::memory_order_release );
			return true;
		}
		return false;
	}

	void DisplayAdjustments::remove_reset_listener( void* context ) noexcept {
		for( auto& slot : m_reset_listeners )
			if( slot.m_context == context )
				slot.m_listener.store( nullptr, std::memory_order_release );
	}

	void DisplayAdjustments::finish_scaled_view( IDirect3DDevice9* device, const RECT& destination ) {
		if( !m_scaled_view_pending || !same_rect( destination, m_scaled_view.m_destination ) )
			return;

		if( apply_viewport_scale( device, m_scaled_view ) )
			m_viewport_passes.fetch_add( 1, std::memory_order_relaxed );
		m_scaled_view_pending = false;
	}

	HRESULT __stdcall DisplayAdjustments::SetViewport( IDirect3DDevice9* device, const D3DVIEWPORT9* viewport ) {
		DisplayAdjustments* instance = m_instance;
		const auto          next     = instance ? instance->m_set_viewport.previous< set_viewport_fn_t >( ) : nullptr;
		if( !instance || instance->m_internal_viewport || !viewport )
			return next ? next( device, viewport ) : D3DERR_INVALIDCALL;

		// With ss_force_primary_fullscreen the hidden secondary slot is a 1x1 box
		// at the corner. After viewport scaling that becomes a 0-size viewport,
		// which the engine's CommitSetViewports normally swallows. the companion
		// engine patch forwards it to the device, and D3D9 rejects a zero rect and
		// keeps the previous (fullscreen) one. clamp the degenerate rect to one
		// pixel so the hidden 3D scene clips to nothing, exactly as intended.
		const D3DVIEWPORT9 clamped = clamp_degenerate_viewport( *viewport );
		viewport                   = &clamped;

		// the default value is 1.0, so ordinary gameplay takes this inexpensive
		// branch without querying COM surfaces on every SetViewport call.
		const float scale = std::clamp( instance->m_viewport_scale->value_float( ), 1.0f / 640.0f, 1.0f );
		if( scale >= 1.0f - gamma_epsilon )
			return next ? next( device, viewport ) : D3DERR_INVALIDCALL;

		D3DSURFACE_DESC description{ };
		if( back_buffer_is_target( device, description ) ) {
			const RECT incoming{ static_cast< LONG >( viewport->X ),
			                     static_cast< LONG >( viewport->Y ),
			                     static_cast< LONG >( viewport->X + viewport->Width ),
			                     static_cast< LONG >( viewport->Y + viewport->Height ) };

			viewport_scale_t candidate{ };
			const bool       scaled = scaled_viewport_rect( *viewport, description, scale, candidate );

			// Source never clears color. blank the whole scaled slot once per
			// frame, before its 3D scene paints: pixels below or beside the scaled
			// viewport would otherwise freeze the previous fullscreen frame there
			// forever, and unpainted pixels inside it would feed the expansion
			// feedback loop. this runs with upscaling disabled too.
			if( scaled && !instance->m_scaled_view_pending ) {
				const D3DRECT region{ candidate.m_destination.left,
				                      candidate.m_destination.top,
				                      candidate.m_destination.right,
				                      candidate.m_destination.bottom };
				instance->clear_back_buffer_rects( device, &region, 1 );
			}
			if( scaled ) {
				instance->m_scaled_view         = candidate;
				instance->m_scaled_view_pending = true;
			}

			// the next unscaled viewport marks the exact boundary between 3D and
			// native-resolution HUD work for this split-screen slot.
			instance->finish_scaled_view( device, incoming );
		}

		return next ? next( device, viewport ) : D3DERR_INVALIDCALL;
	}

}
