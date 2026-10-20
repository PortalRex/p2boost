#include "vgui_screen_shadow_skip.h"

#include "address.h"
#include "game_profile.h"

namespace p2boost {

	VguiScreenShadowSkip* VguiScreenShadowSkip::m_instance{ };

	VguiScreenShadowSkip::~VguiScreenShadowSkip( ) {
		remove( );
	}

	bool VguiScreenShadowSkip::install( const portal2_modules_t& modules ) {
		if( m_instance || m_draw_model.installed( ) || !modules.m_client )
			return false;

		// SetupCurrentView stores its view id into g_CurrentViewID; the shadow
		// depth view passes VIEW_SHADOW_DEPTH_TEXTURE.
		const game::profile_t* game  = game::active( );
		const Address          store = modules.m_client.find( game->current_view_id_store );
		const Address          view  = store ? store.at< Address >( game->xref_current_view_id ) : Address{ };
		if( !modules.m_client.contains( view, sizeof( int ) ) )
			return false;

		// DrawModel has exactly one vtable entry: C_VGuiScreen's renderable base.
		const Address draw_model = modules.m_client.find( game->vgui_screen_draw_model );
		void**        slot       = modules.m_client.unique_slot( draw_model );
		if( !slot )
			return false;

		m_current_view_id = view.as< const int* >( );
		m_draw_model_slot = slot;
		m_instance        = this;
		if( !m_draw_model.install( slot, reinterpret_cast< void* >( &DrawModel ) ) ) {
			m_instance        = nullptr;
			m_current_view_id = nullptr;
			m_draw_model_slot = nullptr;
			return false;
		}
		return true;
	}

	void VguiScreenShadowSkip::remove( ) {
		m_draw_model.remove( );
		m_enabled.store( false, std::memory_order_relaxed );
		m_current_view_id = nullptr;
		m_draw_model_slot = nullptr;

		if( m_instance == this )
			m_instance = nullptr;
	}

	int __fastcall VguiScreenShadowSkip::DrawModel( void* self, void*, int flags, const void* instance ) {
		VguiScreenShadowSkip* skip     = m_instance;
		const auto            original = skip ? skip->m_draw_model.previous< draw_model_fn_t >( ) : nullptr;
		if( !original )
			return 0;

		// a depth-only pass cannot use the painted panel; report nothing drawn,
		// exactly as the stock method does for an inactive screen.
		if( skip->enabled( ) && skip->m_current_view_id &&
		    *skip->m_current_view_id == game::view_shadow_depth_texture ) {
			skip->m_skipped.fetch_add( 1, std::memory_order_relaxed );
			return 0;
		}

		return original( self, flags, instance );
	}

}
