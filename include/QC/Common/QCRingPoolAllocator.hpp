// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_RING_POOL_ALLOCATOR_HPP
#define QC_RING_POOL_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <new>

#include "QC/Common/QCRingPool.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCRingPoolAllocator<T, N>
//
// std::allocator-compatible adapter over QCRingPool<T, N>. Used as the 4th
// template parameter of std::map so every node allocate()/deallocate() call
// draws from a fixed-capacity pool instead of the heap. All storage is inline
// (one QCRingPool<T, N> member) — no heap allocation at any point.
//
// std::map rebinds this allocator to its internal node type (the key/value
// pair plus tree bookkeeping), not the bare pair — rebind is therefore
// required (allocator_traits' default rebind does not apply because N is a
// non-type template parameter).
//
// ---- On the value of n ----
//
// std::map allocates exactly one node per element, so it only ever calls
// allocate(1); that is the only path this allocator supports:
//
//   n == 0  -> returns nullptr. The standard leaves allocate(0) unspecified;
//              returning nullptr (rather than burning a slot) is safe because
//              a zero-length region is never dereferenced, and deallocate()
//              tolerates nullptr.
//   n == 1  -> one slot from the ring pool. O(1).
//   n > 1   -> not supported; throws std::bad_alloc. This allocator backs
//              only node-based containers (e.g. std::map) that call
//              allocate(1); it is not meant for containers that request an
//              array in one call (e.g. std::vector).
//
// ---- max_size() ----
//
// max_size() is overridden to report N. The allocator_traits default is
// SIZE_MAX / sizeof(T) — around 2.3e18 for an 8-byte T — which would tell a
// container that an arbitrarily large request is feasible when the real
// ceiling is N. Containers consult max_size() to decide whether to attempt a
// request or throw length_error up front, so leaving the default in place
// makes them attempt allocations this pool can never serve.
//
// ---- On throwing ----
//
// allocate() throws std::bad_alloc on exhaustion because that is the only
// failure channel the std::allocator interface provides. Callers on the
// QCNode operational path must not rely on it: QCMap checks Size() < N before
// every insert, so the pool is never empty when the container reaches
// allocate() and the throw is unreachable dead code there. The throw exists
// to satisfy the standard contract, not as a runtime error path.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCRingPoolAllocator
{
    static_assert( N > 0u && 0u == ( N & ( N - 1u ) ),
                   "QCRingPoolAllocator capacity N must be a power of 2" );

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind
    {
        using other = QCRingPoolAllocator<U, N>;
    };

    QCRingPoolAllocator() noexcept = default;

    ~QCRingPoolAllocator() = default;

    template<typename U>
    explicit QCRingPoolAllocator( const QCRingPoolAllocator<U, N> & ) noexcept
    {}

    // ------------------------------------------------------------------
    // allocate
    //
    // Returns storage for one T. See the "On the value of n" section above
    // for the per-n behaviour.
    // Throws std::bad_alloc when the request cannot be satisfied.
    // ------------------------------------------------------------------
    T *allocate( size_type n )
    {
        T *pResult = nullptr;
        bool bFailed = false;

        if ( 0u == n )
        {
            // Zero-length request: no slot consumed, nothing dereferenceable.
            pResult = nullptr;
        }
        else if ( 1u == n )
        {
            pResult = m_pool.Acquire();
            if ( nullptr == pResult )
            {
                bFailed = true;
            }
        }
        else
        {
            // Not supported: this allocator only backs allocate(1) callers.
            bFailed = true;
        }

        if ( bFailed )
        {
            throw std::bad_alloc{};
        }

        return pResult;
    }

    // ------------------------------------------------------------------
    // deallocate
    //
    // Returns storage obtained from allocate(). Tolerates nullptr (the
    // allocate(0) result).
    // ------------------------------------------------------------------
    void deallocate( T *pSlot, size_type n ) noexcept
    {
        if ( nullptr == pSlot )
        {
            // Matches allocate(0): no slot was acquired.
        }
        else if ( 1u != n )
        {
            QC_LOG_ERROR( "QCRingPoolAllocator::deallocate: unsupported count %lu",
                          static_cast<unsigned long>( n ) );
        }
        else
        {
            const QCStatus_e status = m_pool.Release( pSlot );
            if ( QC_STATUS_OK != status )
            {
                QC_LOG_ERROR( "QCRingPoolAllocator::deallocate: release failed %d", status );
            }
        }
    }

    // ------------------------------------------------------------------
    // max_size
    //
    // The true ceiling: N elements. Overrides the allocator_traits default
    // (SIZE_MAX / sizeof(T)), which would misreport this pool's capacity.
    // ------------------------------------------------------------------
    size_type max_size() const noexcept { return static_cast<size_type>( N ); }

    uint32_t Capacity() const noexcept { return N; }

private:
    QCRingPool<T, N> m_pool;
};

// Two allocators of the same type/capacity are equal only when they share
// the same underlying pool instance — a node freed via one must be released
// back into the same pool it was acquired from.
template<typename T, typename U, uint32_t N>
bool operator==( const QCRingPoolAllocator<T, N> &lhs,
                 const QCRingPoolAllocator<U, N> &rhs ) noexcept
{
    return &lhs == &rhs;
}

template<typename T, typename U, uint32_t N>
bool operator!=( const QCRingPoolAllocator<T, N> &lhs,
                 const QCRingPoolAllocator<U, N> &rhs ) noexcept
{
    return !( lhs == rhs );
}

}   // namespace QC

#endif   // QC_RING_POOL_ALLOCATOR_HPP
