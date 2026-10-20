#pragma once

#include <atomic>

namespace p2boost::sdk {

	// Source's CFunctor ABI as the material system's call queue and its frame
	// jobs use it: AddRef (slot 0), Release (slot 1), a slot neither calls
	// (2), then operator() (slot 3). The queue AddRefs a functor it takes and
	// Releases the caller's reference before returning; the last Release
	// deletes it. derived_t implements Run(). There is deliberately no
	// virtual destructor, which would add slots: Release deletes derived_t.
	template < typename derived_t >
	class Functor {
	public:
		Functor( )                           = default;
		Functor( const Functor& )            = delete;
		Functor& operator=( const Functor& ) = delete;

		virtual int AddRef( ) {
			return m_references.fetch_add( 1, std::memory_order_acq_rel ) + 1;
		}

		virtual int Release( ) {
			const int remaining = m_references.fetch_sub( 1, std::memory_order_acq_rel ) - 1;
			if( remaining == 0 )
				delete static_cast< derived_t* >( this );
			return remaining;
		}

		// keeps Run in slot 3; the engine never calls it.
		virtual void Unused( ) {
		}

		virtual void Run( ) = 0;

	protected:
		~Functor( ) = default;

	private:
		std::atomic_int m_references{ 1 };
	};

}
