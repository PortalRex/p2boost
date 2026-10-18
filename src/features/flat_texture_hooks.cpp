#include "flat_textures.h"

#include "flat_material_pass.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace p2boost {
	namespace {
		// bind metadata is consumed by SetTexture in the same render-thread call chain.
		thread_local bool          g_material_excluded{ };
		thread_local std::uint32_t g_flat_sampler_mask{ };
		thread_local std::uint32_t g_normal_sampler_mask{ };
		thread_local unsigned int  g_material_bind_depth{ };
		thread_local bool          g_material_pass_active{ };
		thread_local unsigned int  g_vgui_paint_depth{ };

		// discard the pending world pass before VGui_Paint submits UI textures.
		void clear_material_pass( ) noexcept {
			// most draws occur without a pending Bind, so keep this hook nearly free.
			if( !g_material_pass_active )
				return;

			g_material_excluded    = false;
			g_flat_sampler_mask    = 0;
			g_normal_sampler_mask  = 0;
			g_material_pass_active = false;
		}

		unsigned int mip_dimension( unsigned int base, unsigned int level ) noexcept {
			return std::max( 1u, level >= 31u ? 1u : base >> level );
		}

	}

	HRESULT __stdcall FlatTextures::CreateTexture( IDirect3DDevice9* device, unsigned int width, unsigned int height, unsigned int levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* shared_handle ) {
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_create_texture.previous< create_texture_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// create the original first so the optional feature cannot break allocation.
		const HRESULT result = original( device, width, height, levels, usage, format, pool, texture, shared_handle );
		if( FAILED( result ) || !texture || !*texture )
			return result;

		// only sampled, lockable color textures can receive safe companions.
		const DWORD excluded_usage = D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP;
		const bool  sampled_pool   = pool == D3DPOOL_DEFAULT || pool == D3DPOOL_MANAGED;
		if( usage & excluded_usage || !sampled_pool || !flat_texture_format_supported( format ) ||
		    !instance->install_texture_hooks( *texture ) )
			return result;

		IDirect3DTexture9* flat{ };
		if( instance->enabled( ) ) {
			const HRESULT flat_result = original( device, width, height, levels, usage, format, pool, &flat, nullptr );
			if( FAILED( flat_result ) )
				flat = nullptr;
		}

		std::unique_lock lock{ instance->m_mutex };
		instance->m_textures.emplace( *texture, texture_record_t{ flat, format, width, height, levels, usage, pool } );
		return result;
	}

	HRESULT __stdcall FlatTextures::UpdateSurface( IDirect3DDevice9* device, IDirect3DSurface9* source, const RECT* source_rect, IDirect3DSurface9* destination, const POINT* destination_point ) {
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_update_surface.previous< update_surface_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// mirror successful Source uploads into the matching flat companion.
		const HRESULT result = original( device, source, source_rect, destination, destination_point );
		if( FAILED( result ) || !source || !destination || !instance->enabled( ) )
			return result;

		IDirect3DTexture9* destination_texture{ };
		if( FAILED( destination->GetContainer( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void** >( &destination_texture ) ) ) ||
		    !destination_texture )
			return result;

		texture_record_t record;
		{
			std::shared_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( destination_texture );
			if( found == instance->m_textures.end( ) || !found->second.m_flat ) {
				destination_texture->Release( );
				return result;
			}
			record = found->second;
		}

		D3DSURFACE_DESC source_description{ };
		D3DSURFACE_DESC destination_description{ };
		if( FAILED( source->GetDesc( &source_description ) ) || FAILED( destination->GetDesc( &destination_description ) ) ||
		    source_description.Format != record.m_format || destination_description.Format != record.m_format ) {
			destination_texture->Release( );
			return result;
		}

		unsigned int       level  = std::numeric_limits< unsigned int >::max( );
		const unsigned int levels = destination_texture->GetLevelCount( );
		for( unsigned int candidate = 0; candidate < levels; ++candidate ) {
			D3DSURFACE_DESC candidate_description{ };
			if( SUCCEEDED( destination_texture->GetLevelDesc( candidate, &candidate_description ) ) &&
			    candidate_description.Width == destination_description.Width &&
			    candidate_description.Height == destination_description.Height ) {
				level = candidate;
				break;
			}
		}
		if( level == std::numeric_limits< unsigned int >::max( ) ) {
			destination_texture->Release( );
			return result;
		}

		const RECT         upload_source      = source_rect ? *source_rect : RECT{ 0, 0, static_cast< LONG >( source_description.Width ), static_cast< LONG >( source_description.Height ) };
		const POINT        upload_destination = destination_point ? *destination_point : POINT{ 0, 0 };
		const unsigned int width              = static_cast< unsigned int >( upload_source.right - upload_source.left );
		const unsigned int height             = static_cast< unsigned int >( upload_source.bottom - upload_source.top );
		if( !width || !height || upload_destination.x < 0 || upload_destination.y < 0 ||
		    static_cast< unsigned int >( upload_destination.x ) + width > destination_description.Width ||
		    static_cast< unsigned int >( upload_destination.y ) + height > destination_description.Height ) {
			destination_texture->Release( );
			return result;
		}

		const RECT         flat_rect{ upload_destination.x,
                              upload_destination.y,
                              upload_destination.x + static_cast< LONG >( width ),
                              upload_destination.y + static_cast< LONG >( height ) };
		IDirect3DSurface9* flat_surface{ };
		D3DLOCKED_RECT     source_bits{ };
		D3DLOCKED_RECT     destination_bits{ };
		if( FAILED( record.m_flat->GetSurfaceLevel( level, &flat_surface ) ) || !flat_surface ||
		    FAILED( source->LockRect( &source_bits, &upload_source, D3DLOCK_READONLY ) ) ) {
			if( flat_surface )
				flat_surface->Release( );
			destination_texture->Release( );
			return result;
		}

		if( FAILED( flat_surface->LockRect( &destination_bits, &flat_rect, 0 ) ) ) {
			source->UnlockRect( );
			flat_surface->Release( );
			destination_texture->Release( );
			return result;
		}

		const bool complete_base = level == 0 && upload_source.left == 0 && upload_source.top == 0 && width == source_description.Width &&
		                           height == source_description.Height && upload_destination.x == 0 && upload_destination.y == 0 &&
		                           width == destination_description.Width && height == destination_description.Height;
		const flat_color_t color   = record.m_color_valid
		                                 ? record.m_color
		                                 : classify_flat_texture( record.m_format, source_bits.pBits, source_bits.Pitch, width, height );
		const bool         written = write_flat_texture( record.m_format,
		                                                 destination_bits.pBits,
		                                                 destination_bits.Pitch,
		                                                 source_bits.pBits,
		                                                 source_bits.Pitch,
		                                                 width,
		                                                 height,
		                                                 color );
		if( written )
			instance->m_device_uploads.fetch_add( 1, std::memory_order_relaxed );
		flat_surface->UnlockRect( );
		source->UnlockRect( );
		flat_surface->Release( );

		if( written && complete_base ) {
			std::unique_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( destination_texture );
			if( found != instance->m_textures.end( ) ) {
				found->second.m_color       = color;
				found->second.m_color_valid = true;
			}
		}
		destination_texture->Release( );
		return result;
	}

	HRESULT __stdcall FlatTextures::UpdateTexture( IDirect3DDevice9* device, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* destination ) {
		// mirror complete managed-to-default uploads across every mip level.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_update_texture.previous< update_texture_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( device, source, destination );
		if( FAILED( result ) || !source || !destination || !instance->enabled( ) || source->GetType( ) != D3DRTYPE_TEXTURE ||
		    destination->GetType( ) != D3DRTYPE_TEXTURE )
			return result;

		texture_record_t record;
		{
			std::shared_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( destination ) );
			if( found == instance->m_textures.end( ) || !found->second.m_flat )
				return result;
			record = found->second;
		}

		auto*              source_texture     = static_cast< IDirect3DTexture9* >( source );
		const unsigned int source_levels      = source_texture->GetLevelCount( );
		const unsigned int destination_levels = record.m_flat->GetLevelCount( );
		if( !source_levels || source_levels != destination_levels )
			return result;

		D3DSURFACE_DESC base_description{ };
		D3DLOCKED_RECT  base_source{ };
		if( FAILED( source_texture->GetLevelDesc( 0, &base_description ) ) ||
		    FAILED( source_texture->LockRect( 0, &base_source, nullptr, D3DLOCK_READONLY ) ) )
			return result;

		// Source uploads carry the complete texture, so derive one color from
		// the full-resolution visible pixels and reuse it for every mip level.
		const flat_color_t color = classify_flat_texture( record.m_format, base_source.pBits, base_source.Pitch, base_description.Width, base_description.Height );
		source_texture->UnlockRect( 0 );

		bool complete = true;
		for( unsigned int level = 0; level < source_levels; ++level ) {
			D3DSURFACE_DESC description{ };
			D3DLOCKED_RECT  source_bits{ };
			D3DLOCKED_RECT  destination_bits{ };
			if( FAILED( source_texture->GetLevelDesc( level, &description ) ) ||
			    FAILED( source_texture->LockRect( level, &source_bits, nullptr, D3DLOCK_READONLY ) ) ) {
				complete = false;
				break;
			}

			if( FAILED( record.m_flat->LockRect( level, &destination_bits, nullptr, 0 ) ) ) {
				source_texture->UnlockRect( level );
				complete = false;
				break;
			}

			complete = write_flat_texture( record.m_format,
			                               destination_bits.pBits,
			                               destination_bits.Pitch,
			                               source_bits.pBits,
			                               source_bits.Pitch,
			                               description.Width,
			                               description.Height,
			                               color );
			record.m_flat->UnlockRect( level );
			source_texture->UnlockRect( level );
			if( !complete )
				break;
		}

		if( complete ) {
			instance->m_device_uploads.fetch_add( 1, std::memory_order_relaxed );
			std::unique_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( destination ) );
			if( found != instance->m_textures.end( ) ) {
				found->second.m_color       = color;
				found->second.m_color_valid = true;
			}
		}

		return result;
	}

	HRESULT __stdcall FlatTextures::SetTexture( IDirect3DDevice9* device, DWORD stage, IDirect3DBaseTexture9* texture ) {
		// learn each texture's role during ordinary draws. later override passes can
		// then reuse the correct solid color or neutral-normal replacement.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_set_texture.previous< set_texture_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;
		if( g_vgui_paint_depth )
			return original( device, stage, texture );

		IDirect3DBaseTexture9* selected = texture;
		if( texture && stage < 32u && !g_material_excluded && instance->enabled( ) ) {
			const bool       current_flat   = ( g_flat_sampler_mask & ( 1u << stage ) ) != 0;
			const bool       current_normal = ( g_normal_sampler_mask & ( 1u << stage ) ) != 0;
			std::unique_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( static_cast< IDirect3DTexture9* >( texture ) );
			if( found != instance->m_textures.end( ) ) {
				// remember both roles so shadow and depth materials do not restore detail.
				found->second.m_flat_texture   = found->second.m_flat_texture || current_flat;
				found->second.m_normal_texture = found->second.m_normal_texture || current_normal;
			}

			const bool learned_normal = found != instance->m_textures.end( ) && found->second.m_normal_texture;
			const bool learned_flat   = found != instance->m_textures.end( ) && found->second.m_flat_texture;
			if( ( current_normal || ( learned_normal && !learned_flat ) ) && instance->m_flat_normal ) {
				selected = instance->m_flat_normal;
				instance->m_substitutions.fetch_add( 1, std::memory_order_relaxed );
			} else if( found != instance->m_textures.end( ) && ( current_flat || learned_flat ) && found->second.m_flat &&
			           found->second.m_color_valid ) {
				selected = found->second.m_flat;
				instance->m_substitutions.fetch_add( 1, std::memory_order_relaxed );
			}
		}

		return original( device, stage, selected );
	}

	ULONG __stdcall FlatTextures::ReleaseTexture( IDirect3DTexture9* texture ) {
		// release bookkeeping only when DXVK destroys the original object.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_release_texture.previous< release_texture_fn_t >( ) : nullptr;
		if( !original )
			return 0;

		const ULONG references = original( texture );
		if( references )
			return references;

		IDirect3DTexture9* companion{ };
		{
			std::unique_lock lock{ instance->m_mutex };
			auto             found = instance->m_textures.find( texture );
			if( found != instance->m_textures.end( ) ) {
				companion = found->second.m_flat;
				instance->m_textures.erase( found );
			}
			for( auto region = instance->m_locks.begin( ); region != instance->m_locks.end( ); ) {
				if( region->first.m_texture == texture )
					region = instance->m_locks.erase( region );
				else
					++region;
			}
			for( auto surface = instance->m_surfaces.begin( ); surface != instance->m_surfaces.end( ); ) {
				if( surface->second.m_texture == texture ) {
					instance->m_surface_locks.erase( surface->first );
					surface = instance->m_surfaces.erase( surface );
				} else {
					++surface;
				}
			}
		}

		if( companion )
			original( companion );
		return references;
	}

	HRESULT __stdcall FlatTextures::GetSurfaceLevel( IDirect3DTexture9* texture, unsigned int level, IDirect3DSurface9** surface ) {
		// remember which parent texture owns each surfaced mip level.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_get_surface_level.previous< get_surface_level_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( texture, level, surface );
		if( FAILED( result ) || !surface || !*surface )
			return result;

		{
			std::shared_lock lock{ instance->m_mutex };
			if( instance->m_textures.find( texture ) == instance->m_textures.end( ) )
				return result;
		}

		if( !instance->install_surface_hooks( *surface ) )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		if( instance->m_textures.find( texture ) != instance->m_textures.end( ) )
			instance->m_surfaces[ *surface ] = { texture, level };
		return result;
	}

	HRESULT __stdcall FlatTextures::LockTexture( IDirect3DTexture9* texture, unsigned int level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags ) {
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_lock_texture.previous< lock_texture_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		// save writable upload memory until Source completes it in UnlockRect.
		const HRESULT result = original( texture, level, locked, rect, flags );
		if( FAILED( result ) || !locked || !locked->pBits || flags & D3DLOCK_READONLY )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		if( instance->m_textures.find( texture ) == instance->m_textures.end( ) )
			return result;

		locked_region_t region;
		region.m_source   = *locked;
		region.m_flags    = flags;
		region.m_has_rect = rect != nullptr;
		if( rect )
			region.m_rect = *rect;
		instance->m_locks[ { texture, level } ] = region;
		return result;
	}

	HRESULT __stdcall FlatTextures::UnlockTexture( IDirect3DTexture9* texture, unsigned int level ) {
		// copy the completed upload before the original texture is unlocked.
		FlatTextures* instance        = m_instance;
		auto          original_unlock = instance ? instance->m_unlock_texture.previous< unlock_texture_fn_t >( ) : nullptr;
		auto          original_lock   = instance ? instance->m_lock_texture.previous< lock_texture_fn_t >( ) : nullptr;
		if( !original_unlock )
			return D3DERR_INVALIDCALL;

		texture_record_t record;
		locked_region_t  region;
		bool             found_upload = false;
		{
			std::unique_lock lock{ instance->m_mutex };
			auto             texture_record = instance->m_textures.find( texture );
			auto             locked_region  = instance->m_locks.find( { texture, level } );
			if( texture_record != instance->m_textures.end( ) && locked_region != instance->m_locks.end( ) ) {
				record = texture_record->second;
				region = locked_region->second;
				instance->m_locks.erase( locked_region );
				found_upload = true;
			}
		}

		if( found_upload && record.m_flat && original_lock ) {
			const unsigned int level_width  = mip_dimension( record.m_width, level );
			const unsigned int level_height = mip_dimension( record.m_height, level );
			const unsigned int width        = region.m_has_rect ? static_cast< unsigned int >( region.m_rect.right - region.m_rect.left ) : level_width;
			const unsigned int height       = region.m_has_rect ? static_cast< unsigned int >( region.m_rect.bottom - region.m_rect.top ) : level_height;
			flat_color_t       color        = record.m_color_valid
			                                      ? record.m_color
			                                      : classify_flat_texture( record.m_format, region.m_source.pBits, region.m_source.Pitch, width, height );

			D3DLOCKED_RECT destination{ };
			const RECT*    rect = region.m_has_rect ? &region.m_rect : nullptr;
			if( SUCCEEDED( original_lock( record.m_flat, level, &destination, rect, 0 ) ) ) {
				const bool written = write_flat_texture( record.m_format,
				                                         destination.pBits,
				                                         destination.Pitch,
				                                         region.m_source.pBits,
				                                         region.m_source.Pitch,
				                                         width,
				                                         height,
				                                         color );
				original_unlock( record.m_flat, level );
				if( written ) {
					instance->m_texture_uploads.fetch_add( 1, std::memory_order_relaxed );
					const bool complete_base = level == 0 && ( !region.m_has_rect || ( region.m_rect.left == 0 && region.m_rect.top == 0 && region.m_rect.right == static_cast< LONG >( level_width ) && region.m_rect.bottom == static_cast< LONG >( level_height ) ) );
					if( complete_base ) {
						std::unique_lock lock{ instance->m_mutex };
						auto             current = instance->m_textures.find( texture );
						if( current != instance->m_textures.end( ) ) {
							current->second.m_color       = color;
							current->second.m_color_valid = true;
						}
					}
				}
			}
		}

		return original_unlock( texture, level );
	}

	ULONG __stdcall FlatTextures::ReleaseSurface( IDirect3DSurface9* surface ) {
		// discard mappings only after the original surface reaches refcount zero.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_release_surface.previous< release_surface_fn_t >( ) : nullptr;
		if( !original )
			return 0;

		const ULONG references = original( surface );
		if( !references ) {
			std::unique_lock lock{ instance->m_mutex };
			instance->m_surfaces.erase( surface );
			instance->m_surface_locks.erase( surface );
		}
		return references;
	}

	HRESULT __stdcall FlatTextures::LockSurface( IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags ) {
		// surface uploads follow the same deferred copy rule as texture uploads.
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_lock_surface.previous< lock_surface_fn_t >( ) : nullptr;
		if( !original )
			return D3DERR_INVALIDCALL;

		const HRESULT result = original( surface, locked, rect, flags );
		if( FAILED( result ) || !locked || !locked->pBits || flags & D3DLOCK_READONLY || !instance->enabled( ) )
			return result;

		std::unique_lock lock{ instance->m_mutex };
		auto             mapped = instance->m_surfaces.find( surface );
		if( mapped == instance->m_surfaces.end( ) )
			return result;
		auto texture = instance->m_textures.find( mapped->second.m_texture );
		if( texture == instance->m_textures.end( ) || !texture->second.m_flat )
			return result;

		locked_region_t region;
		region.m_source   = *locked;
		region.m_flags    = flags;
		region.m_has_rect = rect != nullptr;
		if( rect )
			region.m_rect = *rect;
		instance->m_surface_locks[ surface ] = region;
		return result;
	}

	HRESULT __stdcall FlatTextures::UnlockSurface( IDirect3DSurface9* surface ) {
		// resolve the surface back to its companion texture and mip level.
		FlatTextures* instance        = m_instance;
		auto          original_unlock = instance ? instance->m_unlock_surface.previous< unlock_surface_fn_t >( ) : nullptr;
		auto          original_lock   = instance ? instance->m_lock_surface.previous< lock_surface_fn_t >( ) : nullptr;
		if( !original_unlock )
			return D3DERR_INVALIDCALL;

		surface_record_t surface_record;
		texture_record_t texture_record;
		locked_region_t  region;
		bool             found_upload = false;
		{
			std::unique_lock lock{ instance->m_mutex };
			auto             mapped = instance->m_surfaces.find( surface );
			auto             locked = instance->m_surface_locks.find( surface );
			if( mapped != instance->m_surfaces.end( ) && locked != instance->m_surface_locks.end( ) ) {
				auto texture = instance->m_textures.find( mapped->second.m_texture );
				if( texture != instance->m_textures.end( ) && texture->second.m_flat ) {
					surface_record = mapped->second;
					texture_record = texture->second;
					region         = locked->second;
					found_upload   = true;
				}
				instance->m_surface_locks.erase( locked );
			}
		}

		if( found_upload && original_lock ) {
			const unsigned int level_width  = mip_dimension( texture_record.m_width, surface_record.m_level );
			const unsigned int level_height = mip_dimension( texture_record.m_height, surface_record.m_level );
			const unsigned int width        = region.m_has_rect ? static_cast< unsigned int >( region.m_rect.right - region.m_rect.left ) : level_width;
			const unsigned int height       = region.m_has_rect ? static_cast< unsigned int >( region.m_rect.bottom - region.m_rect.top ) : level_height;
			const flat_color_t color        = texture_record.m_color_valid ? texture_record.m_color : classify_flat_texture( texture_record.m_format, region.m_source.pBits, region.m_source.Pitch, width, height );

			IDirect3DSurface9* flat_surface{ };
			if( SUCCEEDED( texture_record.m_flat->GetSurfaceLevel( surface_record.m_level, &flat_surface ) ) && flat_surface ) {
				D3DLOCKED_RECT destination{ };
				const RECT*    rect = region.m_has_rect ? &region.m_rect : nullptr;
				if( SUCCEEDED( original_lock( flat_surface, &destination, rect, 0 ) ) ) {
					const bool written = write_flat_texture( texture_record.m_format,
					                                         destination.pBits,
					                                         destination.Pitch,
					                                         region.m_source.pBits,
					                                         region.m_source.Pitch,
					                                         width,
					                                         height,
					                                         color );
					original_unlock( flat_surface );
					if( written )
						instance->m_surface_uploads.fetch_add( 1, std::memory_order_relaxed );

					const bool complete_base = surface_record.m_level == 0 &&
					                           ( !region.m_has_rect || ( region.m_rect.left == 0 && region.m_rect.top == 0 && region.m_rect.right == static_cast< LONG >( level_width ) && region.m_rect.bottom == static_cast< LONG >( level_height ) ) );
					if( written && complete_base ) {
						std::unique_lock lock{ instance->m_mutex };
						auto             current = instance->m_textures.find( surface_record.m_texture );
						if( current != instance->m_textures.end( ) ) {
							current->second.m_color       = color;
							current->second.m_color_valid = true;
						}
					}
				}
				flat_surface->Release( );
			}
		}

		return original_unlock( surface );
	}

	void __fastcall FlatTextures::BindMaterial( void* context, void*, sdk::IMaterial* material, void* proxy_data ) {
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_bind_material.previous< bind_material_fn_t >( ) : nullptr;
		if( !original )
			return;

		// a disabled mode never substitutes a sampler, so the render workers must not
		// pay for string classification on every material bind.
		if( !instance->enabled( ) ) {
			clear_material_pass( );
			original( context, material, proxy_data );
			return;
		}

		// preserve the outer pass when the material system performs a nested Bind.
		const bool          previous_excluded    = g_material_excluded;
		const std::uint32_t previous_flat_mask   = g_flat_sampler_mask;
		const std::uint32_t previous_normal_mask = g_normal_sampler_mask;
		const bool          previous_active      = g_material_pass_active;
		const bool          nested_bind          = g_material_bind_depth++ != 0;

		// classification remains live through the deferred D3D texture binds.
		const material_pass_t pass = classify_material_pass( material );
		g_material_excluded        = pass.m_excluded;
		g_flat_sampler_mask        = pass.m_flat_samplers;
		g_normal_sampler_mask      = pass.m_normal_samplers;
		g_material_pass_active     = true;
		original( context, material, proxy_data );
		--g_material_bind_depth;

		// an inner Bind returns control to its outer material. a top-level Bind stays
		// active until the next material or the dedicated VGui_Paint boundary.
		if( nested_bind ) {
			g_material_excluded    = previous_excluded;
			g_flat_sampler_mask    = previous_flat_mask;
			g_normal_sampler_mask  = previous_normal_mask;
			g_material_pass_active = previous_active;
		}
	}

	int __fastcall FlatTextures::VguiPaint( void* engine_vgui, void*, int mode ) {
		FlatTextures* instance = m_instance;
		auto          original = instance ? instance->m_vgui_paint.previous< vgui_paint_fn_t >( ) : nullptr;
		if( !original )
			return 0;

		// VGui_Paint owns menus, console, HUD, cursor, and third-party overlays.
		// bypassing substitution for the whole boundary requires no material names.
		++g_vgui_paint_depth;
		clear_material_pass( );
		const int result = original( engine_vgui, mode );
		clear_material_pass( );
		--g_vgui_paint_depth;
		return result;
	}

}
