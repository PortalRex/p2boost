#include "partner_view.h"

#include "address.h"
#include "display_adjustments.h"
#include "game_profile.h"
#include "sdk.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace p2boost {

	PartnerView* PartnerView::m_instance{ };

	namespace {
		// a queued render-thread call. CMatQueuedRenderContext's call queue
		// AddRefs the functor through slot 0, drops the caller's reference
		// through slot 1, and later invokes slot 3; slot 2 is never called.
		class device_call_t final {
		public:
			device_call_t( PartnerView* owner, bool save, const RECT& rect, std::atomic_int* pending ) noexcept
			    : m_owner{ owner }, m_save{ save }, m_rect{ rect }, m_pending{ pending } {
				m_pending->fetch_add( 1, std::memory_order_acq_rel );
			}

			virtual int AddRef( ) {
				return m_references.fetch_add( 1, std::memory_order_acq_rel ) + 1;
			}

			virtual int Release( ) {
				const int remaining = m_references.fetch_sub( 1, std::memory_order_acq_rel ) - 1;
				if( remaining == 0 ) {
					std::atomic_int* pending = m_pending;
					delete this;
					pending->fetch_sub( 1, std::memory_order_acq_rel );
				}
				return remaining;
			}

			virtual int Reserved( ) {
				return 0;
			}

			virtual void Run( ) {
				if( m_owner )
					m_owner->run_device_call( m_save, m_rect );
			}

		private:
			std::atomic_int  m_references{ 1 };
			PartnerView*      m_owner;
			bool             m_save;
			RECT             m_rect;
			std::atomic_int* m_pending;
		};

		struct picture_vertex_t {
			float m_x, m_y, m_z, m_rhw;
			float m_u, m_v;
		};

		constexpr DWORD picture_vertex_format{ D3DFVF_XYZRHW | D3DFVF_TEX1 };

		// the queued context returns &m_CallQueueExternal (lea eax,[ecx+disp32];
		// ret) and the immediate context returns null (xor eax,eax; ret).
		bool call_queue_getter_shape( const Module& module, Address function ) noexcept {
			if( !module.contains( function, 7 ) )
				return false;

			const auto* bytes     = function.as< const std::uint8_t* >( );
			const bool  queued    = bytes[ 0 ] == 0x8d && bytes[ 1 ] == 0x81 && bytes[ 6 ] == 0xc3;
			const bool  immediate = bytes[ 0 ] == 0x33 && bytes[ 1 ] == 0xc0 && bytes[ 2 ] == 0xc3;
			return queued || immediate;
		}
	}

	PartnerView::~PartnerView( ) {
		remove( );
	}

	bool PartnerView::install( const portal2_modules_t&      modules,
	                          const Portal2Interfaces&      interfaces,
	                          const ConsoleCommandRegistry& commands,
	                          IDirect3DDevice9*             device,
	                          DisplayAdjustments*           display ) {
		if( m_instance || !modules.m_client )
			return false;

		// CViewRender::Render is vtable slot 5; RenderView is the next slot.
		const Address render = modules.m_client.find( game::active( )->view_render );
		void**        render_slot{ modules.m_client.unique_slot( render ) };
		if( !render_slot )
			return false;

		void** render_view_slot = render_slot + ( game::active( )->vmt_render_view - game::active( )->vmt_view_render );
		if( !modules.m_client.contains( Address{ *render_view_slot } ) )
			return false;

		const Address split_player = modules.m_client.find( game::active( )->split_view_player );
		const Address local_mask   = split_player ? split_player.at< Address >( game::active( )->xref_split_view_local_mask ) : Address{ };
		const Address remote       = split_player ? split_player.at< Address >( game::active( )->xref_split_view_remote_players ) : Address{ };
		if( !modules.m_client.contains( local_mask, sizeof( std::uint32_t ) ) ||
		    !modules.m_client.contains( remote, 2 * sizeof( std::uint32_t ) ) )
			return false;

		m_local_slot_mask = local_mask.as< const std::uint32_t* >( );
		m_remote_players  = remote.as< const std::uint32_t* >( );
		m_force_user      = commands.find_variable( "in_forceuser" );

		// picture reuse records and redraws the remote rect on the device, in
		// render-thread order, through the material system's call queue.
		m_material_system = interfaces.material_system( );
		if( m_material_system && device && display ) {
			if( sdk::IMatRenderContext* context = m_material_system->GetRenderContext( ) ) {
				m_reuse_supported = call_queue_getter_shape( modules.m_material_system, context->GetCallQueueAddress( ) );
				context->Release( );
			}
		}
		if( m_reuse_supported ) {
			m_device  = device;
			m_display = display;
			m_display->add_reset_listener( &ResetListener, this );
		}

		m_instance = this;
		if( !m_render_view.install( render_view_slot, reinterpret_cast< void* >( &RenderView ) ) ) {
			remove( );
			return false;
		}
		return true;
	}

	void PartnerView::remove( ) {
		m_render_view.remove( );
		m_max_fps.store( 0, std::memory_order_relaxed );

		// queued calls execute on the render thread; let every pending one
		// finish before its code and the device resources go away.
		for( int waited = 0; m_pending_calls.load( std::memory_order_acquire ) > 0 && waited < 1000; ++waited )
			Sleep( 1 );

		if( m_display )
			m_display->remove_reset_listener( this );
		release_device_resources( );
		m_display            = nullptr;
		m_device             = nullptr;
		m_material_system    = nullptr;
		m_local_slot_mask    = nullptr;
		m_remote_players     = nullptr;
		m_force_user         = nullptr;
		m_reuse_supported    = false;
		m_picture_rect_valid = false;
		if( m_instance == this )
			m_instance = nullptr;
	}

	void PartnerView::set_max_fps( int fps ) noexcept {
		m_max_fps.store( std::clamp( fps, 0, 1000 ), std::memory_order_relaxed );
		invalidate( );
	}

	int PartnerView::view_slot( const void* self, const void* view ) const noexcept {
		if( !self || !view || !m_local_slot_mask || !m_remote_players )
			return -1;

		const auto first  = reinterpret_cast< std::uintptr_t >( self ) + game::active( )->view_render_slots;
		const auto offset = reinterpret_cast< std::uintptr_t >( view ) - first;
		const auto stride = static_cast< std::uintptr_t >( game::active( )->view_render_slot_stride );
		if( reinterpret_cast< std::uintptr_t >( view ) < first || offset % stride != 0 || offset / stride > 1 )
			return -1;

		return static_cast< int >( offset / stride );
	}

	// a local split-screen player owns its slot outright; a remote slot is one
	// the engine filled with a remote (+remote_view) player.
	bool PartnerView::slot_is_remote( int slot ) const noexcept {
		return slot >= 0 && slot <= 1 && m_local_slot_mask && m_remote_players &&
		       ( m_local_slot_mask[ 0 ] & ( 1u << slot ) ) == 0 && m_remote_players[ slot ] != 0;
	}

	// the slot whose picture may be reused: with two local players, the one
	// in_forceuser does not send input to; otherwise the remote-view slot.
	int PartnerView::partner_slot( ) const noexcept {
		if( !m_local_slot_mask || !m_remote_players )
			return -1;

		if( ( m_local_slot_mask[ 0 ] & 3u ) == 3u ) {
			const int controlled = m_force_user && m_force_user->value_integer( ) == 1 ? 1 : 0;
			return 1 - controlled;
		}
		for( int slot = 0; slot <= 1; ++slot )
			if( slot_is_remote( slot ) )
				return slot;
		return -1;
	}

	bool PartnerView::queue_device_call( bool save, const RECT& rect ) {
		if( !m_material_system )
			return false;

		sdk::IMatRenderContext* context = m_material_system->GetRenderContext( );
		if( !context )
			return false;

		auto* call = new( std::nothrow ) device_call_t{ this, save, rect, &m_pending_calls };
		if( !call ) {
			context->Release( );
			return false;
		}

		// with mat_queue_mode 0 there is no queue and rendering is immediate,
		// so the device call runs inline at exactly the same point.
		if( sdk::ICallQueue* queue = context->GetCallQueue( ) ) {
			queue->QueueFunctor( call );
		} else {
			call->Run( );
			call->Release( );
		}
		context->Release( );
		return true;
	}

	void __fastcall PartnerView::RenderView( void* self, void*, const void* view, const void* hud_view, int clear_flags, int what_to_draw ) {
		PartnerView* instance = m_instance;
		const auto  original = instance ? instance->m_render_view.previous< render_view_fn_t >( ) : nullptr;
		if( !original )
			return;

		// only the partner's slot is ever capped; the controlled view renders.
		const int slot = instance->view_slot( self, view );
		const int fps  = instance->m_max_fps.load( std::memory_order_relaxed );
		if( fps <= 0 || slot < 0 || slot != instance->partner_slot( ) ) {
			original( self, view, hud_view, clear_flags, what_to_draw );
			return;
		}

		// the device calls copy and redraw the slot's finished picture: the
		// unscaled rect, which with mat_viewportscale below 1 holds the expanded
		// scene under the HUD RenderView paints into it.
		const game::profile_t* game = game::active( );
		const Address          setup{ const_cast< void* >( view ) };
		const LONG             left = setup.at< int >( game->view_unscaled_x );
		const LONG             top  = setup.at< int >( game->view_unscaled_y );
		const RECT             rect{ left, top, left + setup.at< int >( game->view_unscaled_width ), top + setup.at< int >( game->view_unscaled_height ) };
		const bool             reusable = instance->reuse_available( ) && rect.right > rect.left && rect.bottom > rect.top;

		const Qpc::tick_t now      = Qpc::now( );
		const Qpc::tick_t interval = reusable ? Qpc::frequency( ) / fps : 0;
		const bool        same_rect =
		    instance->m_picture_rect_valid && std::memcmp( &instance->m_picture_rect, &rect, sizeof( rect ) ) == 0;

		if( reusable && same_rect && instance->m_picture_ready.load( std::memory_order_acquire ) &&
		    now - instance->m_last_render < interval && instance->queue_device_call( false, rect ) ) {
			instance->m_reused.fetch_add( 1, std::memory_order_relaxed );
			return;
		}

		original( self, view, hud_view, clear_flags, what_to_draw );
		instance->m_rendered.fetch_add( 1, std::memory_order_relaxed );

		if( reusable && instance->queue_device_call( true, rect ) ) {
			instance->m_last_render        = now;
			instance->m_picture_rect       = rect;
			instance->m_picture_rect_valid = true;
		}
	}

	void PartnerView::run_device_call( bool save, const RECT& requested ) {
		IDirect3DDevice9* device = m_device;
		if( !device )
			return;

		// only act on the back buffer: the remote slot always finishes there.
		IDirect3DSurface9* render_target{ };
		IDirect3DSurface9* back_buffer{ };
		D3DSURFACE_DESC    description{ };
		const bool         ready = SUCCEEDED( device->GetRenderTarget( 0, &render_target ) ) && render_target &&
		                   SUCCEEDED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer ) ) && back_buffer &&
		                   render_target == back_buffer && SUCCEEDED( back_buffer->GetDesc( &description ) );
		if( render_target )
			render_target->Release( );

		const RECT rect{ std::max< LONG >( requested.left, 0 ),
		                 std::max< LONG >( requested.top, 0 ),
		                 std::min< LONG >( requested.right, static_cast< LONG >( description.Width ) ),
		                 std::min< LONG >( requested.bottom, static_cast< LONG >( description.Height ) ) };
		const bool valid_rect = ready && rect.right > rect.left && rect.bottom > rect.top;
		const UINT width      = valid_rect ? static_cast< UINT >( rect.right - rect.left ) : 0;
		const UINT height     = valid_rect ? static_cast< UINT >( rect.bottom - rect.top ) : 0;

		if( save ) {
			bool               saved = false;
			IDirect3DSurface9* surface{ };
			// the HUD normally sets the slot's unscaled viewport, which expands a
			// scaled scene; a slot that drew no HUD still has it pending here.
			if( valid_rect && m_display )
				m_display->finish_scaled_view( device, rect );
			if( valid_rect && ensure_picture( device, width, height, description.Format ) &&
			    SUCCEEDED( m_picture->GetSurfaceLevel( 0, &surface ) ) && surface ) {
				// a multisampled back buffer resolves into the single-sample copy.
				saved = SUCCEEDED( device->StretchRect( back_buffer, &rect, surface, nullptr, D3DTEXF_NONE ) );
				surface->Release( );
			}
			m_picture_ready.store( saved, std::memory_order_release );
		} else if( valid_rect && m_picture && m_picture_ready.load( std::memory_order_acquire ) && m_picture_width == width &&
		           m_picture_height == height ) {
			// draw instead of StretchRect: copying into a multisampled back buffer
			// is not portable D3D9.
			draw_picture( device, rect );
		}

		if( back_buffer )
			back_buffer->Release( );
	}

	bool PartnerView::ensure_picture( IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format ) {
		if( m_picture && m_picture_width == width && m_picture_height == height && m_picture_format == format )
			return true;

		if( m_picture )
			m_picture->Release( );
		m_picture = nullptr;
		m_picture_ready.store( false, std::memory_order_release );
		if( FAILED( device->CreateTexture( width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, &m_picture, nullptr ) ) ) {
			m_picture = nullptr;
			return false;
		}

		m_picture_width  = width;
		m_picture_height = height;
		m_picture_format = format;
		return true;
	}

	bool PartnerView::draw_picture( IDirect3DDevice9* device, const RECT& rect ) {
		if( !m_state_block && FAILED( device->CreateStateBlock( D3DSBT_ALL, &m_state_block ) ) ) {
			m_state_block = nullptr;
			return false;
		}
		if( FAILED( m_state_block->Capture( ) ) )
			return false;

		D3DSURFACE_DESC    description{ };
		IDirect3DSurface9* back_buffer{ };
		if( FAILED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer ) ) || !back_buffer )
			return false;
		back_buffer->GetDesc( &description );
		back_buffer->Release( );

		const float            left   = static_cast< float >( rect.left ) - 0.5f;
		const float            top    = static_cast< float >( rect.top ) - 0.5f;
		const float            right  = static_cast< float >( rect.right ) - 0.5f;
		const float            bottom = static_cast< float >( rect.bottom ) - 0.5f;
		const picture_vertex_t vertices[]{
		    { left, top, 0.0f, 1.0f, 0.0f, 0.0f },
		    { right, top, 0.0f, 1.0f, 1.0f, 0.0f },
		    { left, bottom, 0.0f, 1.0f, 0.0f, 1.0f },
		    { right, bottom, 0.0f, 1.0f, 1.0f, 1.0f },
		};
		const D3DVIEWPORT9 viewport{ 0, 0, description.Width, description.Height, 0.0f, 1.0f };

		device->SetViewport( &viewport );
		device->SetVertexShader( nullptr );
		device->SetPixelShader( nullptr );
		device->SetFVF( picture_vertex_format );
		device->SetTexture( 0, m_picture );
		device->SetTexture( 1, nullptr );
		device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
		device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
		device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		device->SetTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
		device->SetTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
		device->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
		device->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
		device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
		device->SetRenderState( D3DRS_STENCILENABLE, FALSE );
		device->SetRenderState( D3DRS_SCISSORTESTENABLE, FALSE );
		device->SetRenderState( D3DRS_FOGENABLE, FALSE );
		device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
		device->SetRenderState( D3DRS_SRGBWRITEENABLE, FALSE );
		device->SetRenderState( D3DRS_COLORWRITEENABLE, 0x0f );
		device->SetSamplerState( 0, D3DSAMP_SRGBTEXTURE, 0 );
		device->SetSamplerState( 0, D3DSAMP_MINFILTER, D3DTEXF_POINT );
		device->SetSamplerState( 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT );
		device->SetSamplerState( 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE );
		device->SetSamplerState( 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP );
		device->SetSamplerState( 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP );
		const bool drawn = SUCCEEDED( device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, 2, vertices, sizeof( picture_vertex_t ) ) );

		m_state_block->Apply( );
		return drawn;
	}

	void PartnerView::release_device_resources( ) noexcept {
		m_picture_ready.store( false, std::memory_order_release );
		if( m_state_block )
			m_state_block->Release( );
		if( m_picture )
			m_picture->Release( );
		m_state_block    = nullptr;
		m_picture        = nullptr;
		m_picture_width  = 0;
		m_picture_height = 0;
		m_picture_format = D3DFMT_UNKNOWN;
	}

	void PartnerView::ResetListener( void* context ) {
		// runs inside the device Reset hook, on the thread that owns the device.
		if( auto* instance = static_cast< PartnerView* >( context ) )
			instance->release_device_resources( );
	}

}
