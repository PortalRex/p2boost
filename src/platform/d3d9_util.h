#pragma once

#include <Windows.h>
#include <d3d9.h>
#include <wrl/client.h>

#include <array>

namespace p2boost {

	// an owning COM reference; released automatically, so device code never
	// pairs Get* calls with manual Release calls.
	template < typename interface_t >
	using com_ptr = Microsoft::WRL::ComPtr< interface_t >;

	// the swap chain's back buffer, or null.
	inline com_ptr< IDirect3DSurface9 > back_buffer( IDirect3DDevice9* device ) noexcept {
		com_ptr< IDirect3DSurface9 > surface;
		if( FAILED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, surface.ReleaseAndGetAddressOf( ) ) ) )
			surface.Reset( );
		return surface;
	}

	// the back buffer's description; false when it cannot be read.
	inline bool back_buffer_description( IDirect3DDevice9* device, D3DSURFACE_DESC& description ) noexcept {
		const com_ptr< IDirect3DSurface9 > surface = back_buffer( device );
		return surface && SUCCEEDED( surface->GetDesc( &description ) );
	}

	// whether render target 0 is the back buffer; fills its description if so.
	inline bool back_buffer_is_target( IDirect3DDevice9* device, D3DSURFACE_DESC& description ) noexcept {
		com_ptr< IDirect3DSurface9 > target;
		if( FAILED( device->GetRenderTarget( 0, target.ReleaseAndGetAddressOf( ) ) ) || !target )
			return false;
		const com_ptr< IDirect3DSurface9 > surface = back_buffer( device );
		return surface && target.Get( ) == surface.Get( ) && SUCCEEDED( surface->GetDesc( &description ) );
	}

	// one pre-transformed, textured vertex for drawing in screen pixels.
	struct screen_vertex_t {
		float x, y, z, rhw;
		float u, v;
	};

	constexpr DWORD screen_vertex_format{ D3DFVF_XYZRHW | D3DFVF_TEX1 };

	using screen_quad_t = std::array< screen_vertex_t, 4 >;

	// a triangle strip covering a pixel rect with the whole texture, shifted
	// half a pixel so D3D9 maps each texel to exactly one pixel.
	inline screen_quad_t screen_quad( float left, float top, float right, float bottom ) noexcept {
		return { {
		    { left - 0.5f, top - 0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
		    { right - 0.5f, top - 0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
		    { left - 0.5f, bottom - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
		    { right - 0.5f, bottom - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
		} };
	}

	// draw a screen quad with whatever texture and state the caller set.
	inline bool draw_screen_quad( IDirect3DDevice9* device, const screen_quad_t& quad ) noexcept {
		device->SetFVF( screen_vertex_format );
		return SUCCEEDED( device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, quad.data( ), sizeof( screen_vertex_t ) ) );
	}

}
