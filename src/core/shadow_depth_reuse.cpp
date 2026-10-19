#include "shadow_depth_reuse.h"

#include "address.h"
#include "game_profile.h"

#include <algorithm>
#include <cstring>

namespace p2boost {

	ShadowDepthReuse* ShadowDepthReuse::m_instance{ };

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
		append( game->view_flags, game->view_flags + 4 );
		append( game->view_fov, game->view_projection_end );
		append( game->view_draw_flags, game->view_draw_flags + 1 );
		return key;
	}

	ShadowDepthReuse::~ShadowDepthReuse( ) {
		remove( );
	}

	bool ShadowDepthReuse::install( const portal2_modules_t& modules ) {
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

		m_globals  = globals.as< void* const* >( );
		m_entries  = { };
		m_instance = this;
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
		m_globals = nullptr;
		m_entries = { };

		if( m_instance == this )
			m_instance = nullptr;
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
		const int   frame = Address{ globals }.at< int >( game::active( )->globals_framecount );
		const key_t key   = make_key( view );
		auto        entry = std::find_if( reuse->m_entries.begin( ), reuse->m_entries.end( ), [ depth ]( const entry_t& candidate ) {
            return candidate.m_texture == depth;
        } );

		if( entry != reuse->m_entries.end( ) && reuse->enabled( ) && entry->m_frame == frame && entry->m_key == key ) {
			reuse->m_reused.fetch_add( 1, std::memory_order_relaxed );
			return true;
		}

		const bool rendered = original( self, color, depth, view );
		reuse->m_rendered.fetch_add( 1, std::memory_order_relaxed );

		// track every render, even while disabled, so the texture's contents
		// are known when reuse is switched on mid-frame.
		if( entry == reuse->m_entries.end( ) ) {
			entry = std::min_element( reuse->m_entries.begin( ), reuse->m_entries.end( ), []( const entry_t& left, const entry_t& right ) {
				return left.m_frame < right.m_frame;
			} );
		}
		entry->m_texture = depth;
		entry->m_frame   = rendered ? frame : -1;
		entry->m_key     = key;
		return rendered;
	}

}
