#include "flat_textures.h"

#include "address.h"
#include "build_23973718.h"
#include "d3d9_slots.h"
#include "vmt.h"

#include <algorithm>
#include <cstring>

namespace p2boost {
	FlatTextures* FlatTextures::m_instance{ };

	FlatTextures::~FlatTextures( ) {
		remove( );
	}

	// validate the shared runtime bindings, then install every hook as one unit.
	bool FlatTextures::install(
	    const portal2_modules_t& modules, const Portal2Interfaces& interfaces, IDirect3DDevice9* device ) {
		if( m_instance || !device || !modules.m_material_system || !modules.m_engine || !modules.m_plugin )
			return false;

		auto* material_system = interfaces.material_system( );
		auto* engine_vgui     = interfaces.engine_vgui( );
		if( !material_system || !engine_vgui )
			return false;

		// these recovered slots are callable only when their current targets still
		// belong to the fingerprinted modules that define their ABIs.
		if( !modules.m_material_system.contains( material_system->ReloadTexturesAddress( ) ) ||
		    !modules.m_material_system.contains( material_system->ReloadMaterialsAddress( ) ) ||
		    !modules.m_material_system.contains( material_system->GetRenderContextAddress( ) ) )
			return false;

		const Address paint        = engine_vgui->PaintAddress( );
		const bool    engine_paint = modules.m_engine.matches( paint, portal2::signature::overlay_engine_vgui_paint );
		const bool    plugin_paint = modules.m_plugin.contains( paint );
		if( !engine_paint && !plugin_paint )
			return false;

		sdk::IMatRenderContext* render_context = material_system->GetRenderContext( );
		if( !render_context )
			return false;

		if( !modules.m_material_system.contains( render_context->BindAddress( ) ) ) {
			render_context->Release( );
			return false;
		}

		void** device_vmt = vmt_of( device );
		if( !device_vmt ) {
			render_context->Release( );
			return false;
		}

		// publish borrowed objects before a newly installed hook can observe them.
		m_device          = device;
		m_material_system = material_system;
		m_engine_vgui     = engine_vgui;
		m_instance        = this;

		const bool installed = m_bind_material.install( render_context->BindSlot( ), reinterpret_cast< void* >( &BindMaterial ) ) &&
		                       m_vgui_paint.install( engine_vgui->PaintSlot( ), reinterpret_cast< void* >( &VguiPaint ) ) &&
		                       m_create_texture.install( device_vmt + d3d9_slot::create_texture, reinterpret_cast< void* >( &CreateTexture ) ) &&
		                       m_update_surface.install( device_vmt + d3d9_slot::update_surface, reinterpret_cast< void* >( &UpdateSurface ) ) &&
		                       m_update_texture.install( device_vmt + d3d9_slot::update_texture, reinterpret_cast< void* >( &UpdateTexture ) ) &&
		                       m_set_texture.install( device_vmt + d3d9_slot::set_texture, reinterpret_cast< void* >( &SetTexture ) );
		render_context->Release( );

		if( installed )
			return true;

		// remove is idempotent and unwinds the exact reverse hook order.
		remove( );
		return false;
	}

	void FlatTextures::remove( ) {
		// disable selection before any companion object can be released.
		m_enabled.store( false, std::memory_order_relaxed );
		m_set_texture.remove( );
		m_update_texture.remove( );
		m_update_surface.remove( );
		m_create_texture.remove( );
		m_vgui_paint.remove( );
		m_bind_material.remove( );
		release_companions( companion_policy_t::forget_originals );
		m_unlock_surface.remove( );
		m_lock_surface.remove( );
		m_release_surface.remove( );
		m_surface_vmt = nullptr;
		m_unlock_texture.remove( );
		m_lock_texture.remove( );
		m_get_surface_level.remove( );
		m_release_texture.remove( );
		m_texture_vmt     = nullptr;
		m_engine_vgui     = nullptr;
		m_material_system = nullptr;
		m_device          = nullptr;
		m_substitutions.store( 0, std::memory_order_relaxed );
		m_texture_uploads.store( 0, std::memory_order_relaxed );
		m_surface_uploads.store( 0, std::memory_order_relaxed );
		m_device_uploads.store( 0, std::memory_order_relaxed );
		if( m_instance == this )
			m_instance = nullptr;
	}

	bool FlatTextures::set_enabled( bool enabled ) {
		// a toggle is valid only after the full hook set was installed.
		if( !available( ) )
			return false;
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) == enabled )
			return true;

		// existing map textures predate the toggle. allocate their companions,
		// then ask Source to resend the actual texture pixels through our hooks.
		if( enabled && ( !create_flat_normal( ) || !create_missing_companions( ) || !reload_textures( ) ) ) {
			m_enabled.store( !enabled, std::memory_order_relaxed );
			release_companions( companion_policy_t::retain_originals );
			return false;
		}

		// refresh cached material bindings after the pixels have been rebuilt.
		if( !reload_materials( ) ) {
			m_enabled.store( !enabled, std::memory_order_relaxed );
			if( enabled )
				release_companions( companion_policy_t::retain_originals );
			return false;
		}
		if( !enabled )
			release_companions( companion_policy_t::retain_originals );
		return true;
	}

	void FlatTextures::discard_pending_uploads( ) {
		// Source retains many textures between maps. their COM Release hooks remove
		// genuinely destroyed resources; clearing the whole map here would orphan
		// cached textures and make the toggle stop working after a transition.
		std::unique_lock lock{ m_mutex };
		m_locks.clear( );
		m_surface_locks.clear( );
	}

	size_t FlatTextures::texture_count( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return m_textures.size( );
	}

	size_t FlatTextures::ready_texture_count( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return static_cast< size_t >( std::count_if( m_textures.begin( ), m_textures.end( ), []( const auto& pair ) {
			return pair.second.m_flat && pair.second.m_color_valid;
		} ) );
	}

	bool FlatTextures::normal_ready( ) const noexcept {
		std::shared_lock lock{ m_mutex };
		return m_flat_normal != nullptr;
	}

	bool FlatTextures::install_texture_hooks( IDirect3DTexture9* texture ) {
		// DXVK uses one shared implementation VMT for ordinary 2D textures.
		if( !texture )
			return false;

		std::lock_guard hook_lock{ m_hook_mutex };
		auto**          vmt = vmt_of( texture );
		if( m_texture_vmt )
			return m_texture_vmt == vmt;

		m_texture_vmt = vmt;
		if( !m_release_texture.install( vmt + d3d9_slot::texture_release, reinterpret_cast< void* >( &ReleaseTexture ) ) ||
		    !m_get_surface_level.install( vmt + d3d9_slot::texture_get_surface_level, reinterpret_cast< void* >( &GetSurfaceLevel ) ) ||
		    !m_lock_texture.install( vmt + d3d9_slot::texture_lock, reinterpret_cast< void* >( &LockTexture ) ) ||
		    !m_unlock_texture.install( vmt + d3d9_slot::texture_unlock, reinterpret_cast< void* >( &UnlockTexture ) ) ) {
			m_unlock_texture.remove( );
			m_lock_texture.remove( );
			m_get_surface_level.remove( );
			m_release_texture.remove( );
			m_texture_vmt = nullptr;
			return false;
		}

		return true;
	}

	bool FlatTextures::install_surface_hooks( IDirect3DSurface9* surface ) {
		// surface objects likewise share one VMT in the supported DXVK build.
		if( !surface )
			return false;

		std::lock_guard hook_lock{ m_hook_mutex };
		auto**          vmt = vmt_of( surface );
		if( m_surface_vmt )
			return m_surface_vmt == vmt;

		m_surface_vmt = vmt;
		if( !m_release_surface.install( vmt + d3d9_slot::surface_release, reinterpret_cast< void* >( &ReleaseSurface ) ) ||
		    !m_lock_surface.install( vmt + d3d9_slot::surface_lock, reinterpret_cast< void* >( &LockSurface ) ) ||
		    !m_unlock_surface.install( vmt + d3d9_slot::surface_unlock, reinterpret_cast< void* >( &UnlockSurface ) ) ) {
			m_unlock_surface.remove( );
			m_lock_surface.remove( );
			m_release_surface.remove( );
			m_surface_vmt = nullptr;
			return false;
		}

		return true;
	}

	bool FlatTextures::create_flat_normal( ) {
		{
			std::shared_lock lock{ m_mutex };
			if( m_flat_normal )
				return true;
		}

		auto create = m_create_texture.previous< create_texture_fn_t >( );
		if( !create || !m_device )
			return false;

		// one RGBA texel is neutral for both ordinary RGB normals and Source's
		// DXT5-style AG unpacking. every normal sampler can safely share it.
		IDirect3DTexture9* normal{ };
		if( FAILED( create( m_device, 1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &normal, nullptr ) ) || !normal )
			return false;

		D3DLOCKED_RECT locked{ };
		if( FAILED( normal->LockRect( 0, &locked, nullptr, 0 ) ) || !locked.pBits ) {
			normal->Release( );
			return false;
		}

		// little-endian A8R8G8B8 memory stores this value as B=255,G=128,R=128,A=128.
		const std::uint32_t neutral_normal{ 0x808080ffu };
		std::memcpy( locked.pBits, &neutral_normal, sizeof( neutral_normal ) );
		normal->UnlockRect( 0 );
		{
			std::unique_lock lock{ m_mutex };
			if( !m_flat_normal ) {
				m_flat_normal = normal;
				return true;
			}
		}

		// another caller won the publication race while the driver allocated.
		normal->Release( );
		return true;
	}

	bool FlatTextures::create_missing_companions( ) {
		auto create = m_create_texture.previous< create_texture_fn_t >( );
		if( !create || !m_device )
			return false;

		// snapshot records before entering DXVK. resource creation can invoke
		// callbacks, so the map lock must never span the driver call.
		std::vector< std::pair< IDirect3DTexture9*, texture_record_t > > pending;
		{
			std::shared_lock lock{ m_mutex };
			pending.reserve( m_textures.size( ) );
			for( const auto& pair : m_textures ) {
				if( !pair.second.m_flat )
					pending.push_back( pair );
			}
		}

		bool complete = true;
		for( const auto& pair : pending ) {
			const texture_record_t& record = pair.second;
			IDirect3DTexture9*      flat{ };
			if( FAILED( create( m_device,
			                    record.m_width,
			                    record.m_height,
			                    record.m_levels,
			                    record.m_usage,
			                    record.m_format,
			                    record.m_pool,
			                    &flat,
			                    nullptr ) ) ||
			    !flat ) {
				complete = false;
				continue;
			}

			std::unique_lock lock{ m_mutex };
			auto             current = m_textures.find( pair.first );
			if( current != m_textures.end( ) && !current->second.m_flat )
				current->second.m_flat = flat;
			else
				flat->Release( );
		}
		return complete;
	}

	bool FlatTextures::reload_textures( ) const {
		if( !m_material_system )
			return false;

		m_material_system->ReloadTextures( );
		return true;
	}

	bool FlatTextures::reload_materials( ) const {
		if( !m_material_system )
			return false;

		m_material_system->ReloadMaterials( );
		return true;
	}

	void FlatTextures::release_companions( companion_policy_t policy ) {
		// detach under the lock and perform every COM release after unlocking.
		auto                              release = m_release_texture.previous< release_texture_fn_t >( );
		std::vector< IDirect3DTexture9* > companions;
		IDirect3DTexture9*                flat_normal{ };
		{
			std::unique_lock lock{ m_mutex };
			flat_normal   = m_flat_normal;
			m_flat_normal = nullptr;
			companions.reserve( m_textures.size( ) );
			for( auto& pair : m_textures ) {
				if( pair.second.m_flat )
					companions.push_back( pair.second.m_flat );

				// toggling off preserves Source's original resource bookkeeping.
				if( policy == companion_policy_t::retain_originals ) {
					pair.second.m_flat        = nullptr;
					pair.second.m_color_valid = false;
				}
			}

			if( policy == companion_policy_t::forget_originals ) {
				m_textures.clear( );
				m_surfaces.clear( );
			}

			m_locks.clear( );
			m_surface_locks.clear( );
		}

		for( IDirect3DTexture9* texture : companions )
			release ? release( texture ) : texture->Release( );
		if( flat_normal )
			release ? release( flat_normal ) : flat_normal->Release( );
	}

}
