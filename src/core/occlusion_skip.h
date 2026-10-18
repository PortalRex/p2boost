#pragma once

#include "hook.h"
#include "portal2_context.h"

#include <atomic>
#include <cstdint>

namespace p2boost {

	// decide whether ExtractOccludedRenderables can return its input unchanged.
	// the engine's occlusion system builds its edge list only from the world's
	// func_occluder polygons, so with none every IsOccluded query is false.
	bool occlusion_pass_redundant( const void* world_brush, std::ptrdiff_t occluder_count_offset ) noexcept;

	// CClientLeafSystem::BuildRenderablesList runs ExtractOccludedRenderables
	// for every view, asking the engine about each studio model and static prop
	// through a mutex-guarded IsOccluded call. No Portal 2 map ships occluders,
	// so the pass always returns its input untouched; skip it in exactly that case.
	class OcclusionSkip {
	public:
		~OcclusionSkip( );

		bool install( const portal2_modules_t& modules );
		void remove( );

		void set_enabled( bool enabled ) noexcept {
			m_enabled.store( enabled, std::memory_order_relaxed );
		}

		bool enabled( ) const noexcept {
			return m_enabled.load( std::memory_order_relaxed );
		}

		bool installed( ) const noexcept {
			return m_call.installed( );
		}

		std::uint64_t skipped_count( ) const noexcept {
			return m_skipped.load( std::memory_order_relaxed );
		}

	private:
		using extract_fn_t = int( __fastcall* )( void* leaf_system, void*, int count, void* renderables, void* infos );

		static int __fastcall ExtractOccluded( void* leaf_system, void*, int count, void* renderables, void* infos );

		static OcclusionSkip* m_instance;

		CallSitePatch                m_call;
		extract_fn_t                 m_original{ };
		void* const*                 m_world_brush{ };
		std::atomic_bool             m_enabled{ };
		std::atomic< std::uint64_t > m_skipped{ };
	};

}
