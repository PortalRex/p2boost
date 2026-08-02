#include "display_adjustments.h"

#include "address.h"
#include "monitor_gamma_shader.h"
#include "pattern.h"
#include "portal2.h"

#include <algorithm>
#include <cmath>

namespace p2boost {
	namespace {
		constexpr float neutral_monitor_gamma{ 2.2f };
		constexpr float gamma_epsilon{ 0.001f };

		struct screen_vertex_t {
			float m_x;
			float m_y;
			float m_z;
			float m_rhw;
			float m_u;
			float m_v;
		};

		constexpr DWORD screen_vertex_format{ D3DFVF_XYZRHW | D3DFVF_TEX1 };
	}

	DisplayAdjustments*				  DisplayAdjustments::m_instance{ };
	DisplayAdjustments::PresentFn	  DisplayAdjustments::m_next_present{ };
	DisplayAdjustments::ResetFn		  DisplayAdjustments::m_next_reset{ };
	DisplayAdjustments::SetViewportFn DisplayAdjustments::m_next_set_viewport{ };

	float monitor_gamma_curve( float monitor_gamma ) noexcept {
		return std::clamp( monitor_gamma, 1.6f, 2.6f ) / neutral_monitor_gamma;
	}

	DisplayAdjustments::~DisplayAdjustments( ) {
		remove( );
	}

	bool DisplayAdjustments::install( const ConsoleCommandRegistry& commands ) {
		if( m_instance )
			return false;

		m_monitor_gamma	   = commands.find_variable( "mat_monitorgamma" );
		m_viewport_scale   = commands.find_variable( "mat_viewportscale" );
		m_viewport_upscale = commands.find_variable( "mat_viewportupscale" );
		if( !m_monitor_gamma || !m_viewport_scale || !m_viewport_upscale )
			return false;

		// reuse the Ghidra-verified CreateDevice xref used by flat textures. the
		// relocated global contains DXVK's live D3D9 device without a fixed RVA.
		const Address shader{ GetModuleHandleW( L"shaderapivk.dll" ) };
		if( !shader )
			return false;

		const Address device_xref	= pattern::find_unique( shader, portal2::signature::flat_texture_device );
		const Address device_global = device_xref.add( portal2::xref::flat_texture_device_global ).at< Address >( );
		m_device					= device_global.to< IDirect3DDevice9* >( );
		if( !m_device )
			return false;

		auto** device_vmt = *reinterpret_cast< void*** >( m_device );
		m_instance		  = this;

		// publish each forward target as soon as its hook becomes callable. this
		// keeps the render thread safe if it runs during plugin initialization.
		if( !m_reset.install( device_vmt + portal2::vmt::d3d9_reset, reinterpret_cast< void* >( &Reset ) ) ) {
			remove( );
			return false;
		}
		m_next_reset = reinterpret_cast< ResetFn >( m_reset.previous( ) );

		if( !m_present.install( device_vmt + portal2::vmt::d3d9_present, reinterpret_cast< void* >( &Present ) ) ) {
			remove( );
			return false;
		}
		m_next_present = reinterpret_cast< PresentFn >( m_present.previous( ) );

		if( !m_set_viewport.install( device_vmt + portal2::vmt::d3d9_set_viewport, reinterpret_cast< void* >( &SetViewport ) ) ) {
			remove( );
			return false;
		}
		m_next_set_viewport = reinterpret_cast< SetViewportFn >( m_set_viewport.previous( ) );

		return true;
	}

	void DisplayAdjustments::remove( ) {
		m_set_viewport.remove( );
		m_present.remove( );
		m_reset.remove( );
		release_resources( );
		m_monitor_gamma		= nullptr;
		m_viewport_scale	= nullptr;
		m_viewport_upscale	= nullptr;
		m_device			= nullptr;
		m_viewport_applied	= false;
		m_scaled_view_seen	= false;
		m_internal_viewport = false;
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
										   &m_copy_texture,
										   nullptr ) ) )
			return false;

		m_copy_width  = description.Width;
		m_copy_height = description.Height;
		m_copy_format = description.Format;
		return true;
	}

	bool DisplayAdjustments::ensure_gamma_resources( IDirect3DDevice9* device ) {
		if( !m_gamma_shader && FAILED( device->CreatePixelShader( reinterpret_cast< const DWORD* >( g_monitor_gamma_shader ), &m_gamma_shader ) ) )
			return false;
		if( !m_state_block && FAILED( device->CreateStateBlock( D3DSBT_ALL, &m_state_block ) ) )
			return false;
		return true;
	}

	bool DisplayAdjustments::copy_back_buffer( IDirect3DDevice9* device, const RECT* source, D3DTEXTUREFILTERTYPE filter ) {
		IDirect3DSurface9* back_buffer{ };
		if( FAILED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer ) ) || !back_buffer )
			return false;

		D3DSURFACE_DESC	   description{ };
		IDirect3DSurface9* copy_surface{ };
		const bool		   ready = SUCCEEDED( back_buffer->GetDesc( &description ) ) && ensure_copy_target( device, description ) &&
						   SUCCEEDED( m_copy_texture->GetSurfaceLevel( 0, &copy_surface ) ) && copy_surface;
		if( !ready ) {
			if( copy_surface )
				copy_surface->Release( );
			back_buffer->Release( );
			return false;
		}

		const RECT destination{ 0, 0, static_cast< LONG >( description.Width ), static_cast< LONG >( description.Height ) };
		const bool copied = SUCCEEDED( device->StretchRect( back_buffer, source, copy_surface, &destination, filter ) );
		copy_surface->Release( );
		back_buffer->Release( );
		return copied;
	}

	bool DisplayAdjustments::apply_viewport_scale( IDirect3DDevice9* device ) {
		if( !m_viewport_scale || !m_viewport_upscale || !m_viewport_upscale->value_integer( ) )
			return false;

		const float scale = std::clamp( m_viewport_scale->value_float( ), 1.0f / 640.0f, 1.0f );
		if( scale >= 1.0f - gamma_epsilon )
			return false;

		IDirect3DSurface9* back_buffer{ };
		if( FAILED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer ) ) || !back_buffer )
			return false;
		D3DSURFACE_DESC description{ };
		const bool		described = SUCCEEDED( back_buffer->GetDesc( &description ) );
		back_buffer->Release( );
		if( !described )
			return false;

		const RECT source{ 0,
						   0,
						   std::max< LONG >( 1, static_cast< LONG >( description.Width * scale ) ),
						   std::max< LONG >( 1, static_cast< LONG >( description.Height * scale ) ) };
		if( !copy_back_buffer( device, &source, D3DTEXF_LINEAR ) )
			return false;

		IDirect3DSurface9* copy_surface{ };
		IDirect3DSurface9* destination{ };
		const bool		   ready = SUCCEEDED( m_copy_texture->GetSurfaceLevel( 0, &copy_surface ) ) && copy_surface &&
						   SUCCEEDED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &destination ) ) && destination;
		const bool expanded = ready && SUCCEEDED( device->StretchRect( copy_surface, nullptr, destination, nullptr, D3DTEXF_NONE ) );
		if( destination )
			destination->Release( );
		if( copy_surface )
			copy_surface->Release( );
		return expanded;
	}

	bool DisplayAdjustments::rendering_to_back_buffer( IDirect3DDevice9* device, D3DSURFACE_DESC& description ) const {
		IDirect3DSurface9* render_target{ };
		IDirect3DSurface9* back_buffer{ };
		const bool		   acquired = SUCCEEDED( device->GetRenderTarget( 0, &render_target ) ) && render_target &&
							  SUCCEEDED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer ) ) && back_buffer;
		const bool matches = acquired && render_target == back_buffer && SUCCEEDED( back_buffer->GetDesc( &description ) );
		if( back_buffer )
			back_buffer->Release( );
		if( render_target )
			render_target->Release( );
		return matches;
	}

	bool DisplayAdjustments::apply_gamma( IDirect3DDevice9* device ) {
		if( !m_monitor_gamma )
			return false;

		const float monitor_gamma = std::clamp( m_monitor_gamma->value_float( ), 1.6f, 2.6f );
		if( std::fabs( monitor_gamma - neutral_monitor_gamma ) <= gamma_epsilon )
			return false;
		if( !copy_back_buffer( device, nullptr, D3DTEXF_NONE ) || !ensure_gamma_resources( device ) || FAILED( m_state_block->Capture( ) ) )
			return false;

		const float			  width	 = static_cast< float >( m_copy_width );
		const float			  height = static_cast< float >( m_copy_height );
		const screen_vertex_t vertices[]{
			{ -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
			{ width - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
			{ -0.5f, height - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
			{ width - 0.5f, height - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
		};
		const D3DVIEWPORT9 viewport{ 0, 0, m_copy_width, m_copy_height, 0.0f, 1.0f };
		const float		   curve[ 4 ]{ monitor_gamma_curve( monitor_gamma ), 0.0f, 0.0f, 0.0f };

		m_internal_viewport = true;
		device->SetViewport( &viewport );
		m_internal_viewport = false;
		device->SetVertexShader( nullptr );
		device->SetPixelShader( m_gamma_shader );
		device->SetFVF( screen_vertex_format );
		device->SetTexture( 0, m_copy_texture );
		device->SetPixelShaderConstantF( 0, curve, 1 );
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
		device->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );
		device->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, 0 );
		device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		const bool drawn	= SUCCEEDED( device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, vertices, sizeof( screen_vertex_t ) ) );
		const bool restored = SUCCEEDED( m_state_block->Apply( ) );
		return drawn && restored;
	}

	void DisplayAdjustments::release_resources( ) {
		if( m_state_block )
			m_state_block->Release( );
		if( m_gamma_shader )
			m_gamma_shader->Release( );
		if( m_copy_texture )
			m_copy_texture->Release( );
		m_state_block  = nullptr;
		m_gamma_shader = nullptr;
		m_copy_texture = nullptr;
		m_copy_width   = 0;
		m_copy_height  = 0;
		m_copy_format  = D3DFMT_UNKNOWN;
	}

	HRESULT __stdcall DisplayAdjustments::Present( IDirect3DDevice9* device,
												   const RECT*		 source,
												   const RECT*		 destination,
												   HWND				 window,
												   const RGNDATA*	 dirty_region ) {
		DisplayAdjustments* instance = m_instance;
		if( instance && instance->apply_gamma( device ) )
			instance->m_gamma_passes.fetch_add( 1, std::memory_order_relaxed );
		if( instance )
			instance->m_viewport_applied = false;
		if( instance )
			instance->m_scaled_view_seen = false;
		return m_next_present ? m_next_present( device, source, destination, window, dirty_region ) : D3DERR_INVALIDCALL;
	}

	HRESULT __stdcall DisplayAdjustments::Reset( IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters ) {
		DisplayAdjustments* instance = m_instance;
		if( instance )
			instance->release_resources( );
		return m_next_reset ? m_next_reset( device, parameters ) : D3DERR_INVALIDCALL;
	}

	HRESULT __stdcall DisplayAdjustments::SetViewport( IDirect3DDevice9* device, const D3DVIEWPORT9* viewport ) {
		DisplayAdjustments* instance = m_instance;
		if( !instance || instance->m_internal_viewport || !viewport )
			return m_next_set_viewport ? m_next_set_viewport( device, viewport ) : D3DERR_INVALIDCALL;

		// the default value is 1.0, so ordinary gameplay takes this inexpensive
		// branch without querying COM surfaces on every SetViewport call.
		const float scale = std::clamp( instance->m_viewport_scale->value_float( ), 1.0f / 640.0f, 1.0f );
		if( scale >= 1.0f - gamma_epsilon || !instance->m_viewport_upscale->value_integer( ) )
			return m_next_set_viewport ? m_next_set_viewport( device, viewport ) : D3DERR_INVALIDCALL;

		D3DSURFACE_DESC description{ };
		if( instance->rendering_to_back_buffer( device, description ) ) {
			const unsigned int scaled_width		   = std::max( 1u, static_cast< unsigned int >( description.Width * scale ) );
			const unsigned int scaled_height	   = std::max( 1u, static_cast< unsigned int >( description.Height * scale ) );
			const bool		   inside_scaled_frame = viewport->X + viewport->Width <= scaled_width + 1u &&
											 viewport->Y + viewport->Height <= scaled_height + 1u;
			const bool full_frame = viewport->X == 0 && viewport->Y == 0 && viewport->Width == description.Width &&
									viewport->Height == description.Height;

			if( inside_scaled_frame )
				instance->m_scaled_view_seen = true;
			if( full_frame && instance->m_scaled_view_seen && !instance->m_viewport_applied ) {
				instance->m_viewport_applied = true;
				if( instance->apply_viewport_scale( device ) )
					instance->m_viewport_passes.fetch_add( 1, std::memory_order_relaxed );
			}
		}

		return m_next_set_viewport ? m_next_set_viewport( device, viewport ) : D3DERR_INVALIDCALL;
	}

}
