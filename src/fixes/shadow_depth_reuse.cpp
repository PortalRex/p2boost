#include "shadow_depth_reuse.h"

#include "address.h"
#include "client_view.h"
#include "game_profile.h"
#include "projected_frustum.h"
#include "sdk.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace p2boost {

	ShadowDepthReuse* ShadowDepthReuse::m_instance{ };

	namespace {
		// a player's body and held gun around the eye: the hull is 32 units
		// wide and the eye 64 above the feet (28 crouched); generous on every
		// side, since a miss only costs a re-render.
		constexpr vec3_t body_below{ 48.0f, 48.0f, 80.0f };
		constexpr vec3_t body_above{ 48.0f, 48.0f, 24.0f };
	}

	ShadowDepthReuse::key_t ShadowDepthReuse::make_key( const void* view ) noexcept {
		// the CViewSetup lives on ComputeShadowDepthTextures' stack without a
		// constructor, so only the fields it writes are compared.
		const game::profile_t* game  = game::active( );
		const auto*            bytes = static_cast< const std::uint8_t* >( view );
		key_t                  key{ };
		std::size_t            cursor = 0;
		auto                   append = [ & ]( std::ptrdiff_t begin, std::ptrdiff_t end ) {
            const auto length = static_cast< std::size_t >( end - begin );
            std::memcpy( key.data( ) + cursor, bytes + begin, length );
            cursor += length;
		};

		append( game->view_width, game->view_width + 4 );
		append( game->view_height, game->view_height + 4 );
		append( game->view_ortho, game->view_ortho + 1 );
		if( bytes[ game->view_ortho ] )
			append( game->view_ortho_rect, game->view_ortho_rect_end );
		else
			cursor += game->view_ortho_rect_end - game->view_ortho_rect;
		// m_bViewToProjectionOverride is a bool: the three bytes after it are
		// leftover stack, equal between two views of one frame but not across
		// frames.
		append( game->view_flags, game->view_flags + 1 );
		append( game->view_fov, game->view_projection_end );
		append( game->view_draw_flags, game->view_draw_flags + 1 );
		return key;
	}

	ShadowDepthReuse::~ShadowDepthReuse( ) {
		remove( );
	}

	bool ShadowDepthReuse::install( const portal2_modules_t& modules, const Portal2Interfaces& interfaces ) {
		if( m_instance || m_update_depth.installed( ) || !modules.m_client )
			return false;

		// CViewRender::Render is vtable slot 5; UpdateShadowDepthTexture is 29.
		const game::profile_t* game        = game::active( );
		void**                 render_slot = modules.m_client.unique_slot( modules.m_client.find( game->view_render ) );
		if( !render_slot )
			return false;

		void** update_slot = render_slot + ( game->vmt_update_shadow_depth - game->vmt_view_render );
		if( !modules.m_client.contains( Address{ *update_slot } ) )
			return false;

		// gpGlobals separates frames: a map is only reused within one frame.
		const Address frame   = modules.m_client.find( game->globals_frame );
		const Address globals = frame ? frame.at< Address >( game->xref_globals_pointer ) : Address{ };
		if( !modules.m_client.contains( globals, sizeof( void* ) ) )
			return false;

		LARGE_INTEGER frequency{ };
		QueryPerformanceFrequency( &frequency );
		m_frequency = frequency.QuadPart;
		m_globals   = globals.as< void* const* >( );
		m_materials = interfaces.material_system( );
		m_entries   = { };
		m_instance  = this;
		if( !m_update_depth.install( update_slot, reinterpret_cast< void* >( &UpdateShadowDepthTexture ) ) ) {
			m_instance = nullptr;
			m_globals  = nullptr;
			return false;
		}
		return true;
	}

	void ShadowDepthReuse::remove( ) {
		m_update_depth.remove( );
		m_enabled.store( false, std::memory_order_relaxed );
		m_globals   = nullptr;
		m_materials = nullptr;
		m_viewer    = nullptr;
		m_entries   = { };

		if( m_instance == this )
			m_instance = nullptr;
	}

	void ShadowDepthReuse::begin_view( const void* view ) noexcept {
		m_viewer     = view;
		m_viewer_eye = view ? client_view_t{ view }.origin( ) : vec3_t{ };
	}

	// a map rendered for another view holds the same pixels only when neither
	// view's player could be drawn into it.
	bool ShadowDepthReuse::shareable( const void* light_view, const entry_t& entry ) const noexcept {
		if( entry.m_viewer == m_viewer )
			return true;
		if( !entry.m_viewer || !m_viewer )
			return false;

		const client_view_t          light{ light_view };
		const frustum::camera_view_t camera{ light.origin( ), light.angles( ), light.near_z( ), light.far_z( ), light.fov( ), light.aspect_ratio( ) };
		frustum::frustum_t           volume{ };
		if( !frustum::camera_frustum( camera, volume ) )
			return false;
		for( const vec3_t& eye : { entry.m_viewer_eye, m_viewer_eye } )
			if( frustum::frusta_intersect( volume, frustum::box( eye - body_below, eye + body_above ) ) )
				return false;
		return true;
	}

	// CShadowDepthView::Draw sets ClearColor3ub( 255, 255, 255 ) and nothing
	// else that outlives its Push3DView/PopView; the main view clears with
	// that color when its camera is in solid space (out of bounds).
	void ShadowDepthReuse::repeat_draw_state( ) const noexcept {
		sdk::IMatRenderContext* context = m_materials ? m_materials->GetRenderContext( ) : nullptr;
		if( !context )
			return;
		using clear_color_fn_t = void( __thiscall* )( void*, unsigned char, unsigned char, unsigned char );
		const auto clear_color = ( *reinterpret_cast< clear_color_fn_t* const* >( context ) )[ game::active( )->vmt_context_clear_color3 ];
		clear_color( context, 255, 255, 255 );
		context->Release( );
	}

	bool __fastcall ShadowDepthReuse::UpdateShadowDepthTexture( void* self, void*, void* color, void* depth, const void* view ) {
		ShadowDepthReuse* reuse    = m_instance;
		const auto        original = reuse ? reuse->m_update_depth.previous< update_depth_fn_t >( ) : nullptr;
		if( !original )
			return false;

		const void* globals = reuse->m_globals ? *reuse->m_globals : nullptr;
		if( !depth || !view || !globals ) {
			reuse->m_rendered.fetch_add( 1, std::memory_order_relaxed );
			return original( self, color, depth, view );
		}

		// runs on the main thread inside ComputeShadowDepthTextures; the
		// queued render worker draws these in the same order.
		if( reuse->m_invalidate.exchange( false, std::memory_order_acq_rel ) )
			reuse->m_entries = { };

		LARGE_INTEGER now{ };
		QueryPerformanceCounter( &now );
		const game::profile_t* game  = game::active( );
		const int              frame = Address{ globals }.at< int >( game->globals_framecount );
		const key_t            key   = make_key( view );
		auto                   entry = std::find_if( reuse->m_entries.begin( ), reuse->m_entries.end( ), [ depth ]( const entry_t& candidate ) {
            return candidate.m_texture == depth;
        } );

		// the same light in the same texture: always reusable later in the same
		// frame, and across frames while its refresh interval has not elapsed.
		if( entry != reuse->m_entries.end( ) && reuse->enabled( ) && entry->m_frame >= 0 && entry->m_key == key ) {
			const int  fps   = reuse->m_max_fps.load( std::memory_order_relaxed );
			const bool fresh = fps > 0 && now.QuadPart - entry->m_time < reuse->m_frequency / fps;
			if( ( entry->m_frame == frame || fresh ) && reuse->shareable( view, *entry ) ) {
				reuse->repeat_draw_state( );
				reuse->m_reused.fetch_add( 1, std::memory_order_relaxed );
				return true;
			}
		}

		const bool rendered = original( self, color, depth, view );
		reuse->m_rendered.fetch_add( 1, std::memory_order_relaxed );

		// track every render, even while disabled, so the texture's contents
		// are known when reuse is switched on mid-frame.
		if( entry == reuse->m_entries.end( ) ) {
			entry = std::min_element( reuse->m_entries.begin( ), reuse->m_entries.end( ), []( const entry_t& left, const entry_t& right ) {
				return left.m_time < right.m_time;
			} );
		}
		entry->m_texture    = depth;
		entry->m_frame      = rendered ? frame : -1;
		entry->m_time       = now.QuadPart;
		entry->m_key        = key;
		entry->m_viewer     = reuse->m_viewer;
		entry->m_viewer_eye = reuse->m_viewer_eye;

		const Address setup{ const_cast< void* >( view ) };
		for( int axis = 0; axis < 3; ++axis ) {
			entry->m_view.m_origin[ axis ] = setup.at< float >( game->view_origin + 4 * axis );
			entry->m_view.m_angles[ axis ] = setup.at< float >( game->view_angles + 4 * axis );
		}
		entry->m_view.m_fov   = setup.at< float >( game->view_fov );
		entry->m_view.m_frame = entry->m_frame;
		return rendered;
	}

	std::array< ShadowDepthReuse::light_view_t, 32 > ShadowDepthReuse::views( ) const noexcept {
		std::array< light_view_t, 32 > result{ };
		for( std::size_t index = 0; index < m_entries.size( ); ++index )
			result[ index ] = m_entries[ index ].m_texture ? m_entries[ index ].m_view : light_view_t{ };
		return result;
	}

}
