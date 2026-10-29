// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_RING_POOL_HPP
#define QC_RING_POOL_HPP

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "QC/Common/QCDefs.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCRingPool<T, N>
//
// Fixed-capacity slot pool of N raw, uninitialized storage slots sized for
// T, with no runtime memory allocation. Free-slot indices are tracked in a
// ring buffer; Acquire()/Release() are O(1) index pop/push. N must be a
// power of 2 so the ring indices can wrap with a mask instead of a modulo.
//
// Storage is raw (std::aligned_storage), not a live T[N] array — the pool
// only hands out memory; it never constructs or destroys T itself. That is
// the allocator's job (placement-new after Acquire, explicit destructor call
// before Release), so T need not be default-constructible or destructible
// at pool-construction/destruction time.
//
// Acquire() returns nullptr when the pool is exhausted; the caller maps that
// to QC_STATUS_NO_RESOURCE / QC_STATUS_NOMEM.
//
// Release() validates its argument and returns QCStatus_e rather than failing
// silently. It rejects three classes of bad pointer, each of which would
// otherwise corrupt the free ring and cause the pool to hand the same slot
// out twice:
//   - a pointer outside this pool's storage    -> QC_STATUS_BAD_ARGUMENTS
//   - a pointer that is not slot-aligned (i.e. does not sit exactly on a
//     slot boundary, e.g. an interior pointer) -> QC_STATUS_BAD_ARGUMENTS
//   - a slot that is not currently acquired
//     (double release / never acquired)        -> QC_STATUS_BAD_STATE
// The last check is what makes double-release safe: without it, releasing
// one slot twice pushes its index onto the ring twice, so the pool reports
// more free slots than it owns and later hands the same memory to two
// callers. m_acquired tracks per-slot ownership to make that detectable.
//
// Init() is idempotent-by-refusal rather than destructive: re-initialising a
// pool that still has live slots would mark those slots free while callers
// still hold pointers into them, aliasing live objects. Init() therefore
// returns QC_STATUS_BAD_STATE and changes nothing if any slot is live. The
// constructor calls it on a pool that is by definition empty, so it always
// succeeds there.
//
// Thread safety: none. The caller must hold its own mutex around any
// Acquire()/Release() pair that can run concurrently with another.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCRingPool
{
    static_assert( N > 0u && 0u == ( N & ( N - 1u ) ),
                   "QCRingPool capacity N must be a power of 2" );

    using Slot = typename std::aligned_storage<sizeof( T ), alignof( T )>::type;

public:
    QCRingPool() noexcept
    {
        // Cannot fail on a fresh pool: m_used is 0, so Init() takes its
        // success path. The status is discarded only because it is
        // unconditionally QC_STATUS_OK here.
        (void) Init();
    }

    ~QCRingPool() = default;

    QCRingPool( const QCRingPool & ) = delete;
    QCRingPool &operator=( const QCRingPool & ) = delete;
    QCRingPool( QCRingPool && ) = delete;
    QCRingPool &operator=( QCRingPool && ) = delete;

    // ------------------------------------------------------------------
    // Init
    //
    // Seeds the ring so that all N slots are free. Called by the
    // constructor; may be called again only on a pool with no live slots.
    // Returns QC_STATUS_BAD_STATE (and changes nothing) if any slot is
    // currently acquired — resetting then would alias live objects.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Init() noexcept
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u != m_used )
        {
            // Reinitialisation guard: slots are still checked out.
            QC_LOG_ERROR( "QCRingPool::Init: %lu slot(s) still acquired, refusing to reset",
                          static_cast<unsigned long>( m_used ) );
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            for ( uint32_t i = 0u; i < N; ++i )
            {
                m_ring[i] = i;
                m_acquired[i] = false;
            }
            m_head = 0u;
            m_tail = N;
            m_used = 0u;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Acquire
    //
    // Borrows one slot. Returns nullptr when the pool is empty.
    // ------------------------------------------------------------------
    T *Acquire() noexcept
    {
        T *pSlot = nullptr;

        if ( m_head != m_tail )
        {
            const uint32_t index = m_ring[m_head & ( N - 1u )];
            ++m_head;
            m_acquired[index] = true;
            ++m_used;
            pSlot = reinterpret_cast<T *>( &m_slot[index] );
        }
        else
        {
            QC_LOG_ERROR( "QCRingPool::Acquire: pool exhausted (capacity %lu)",
                          static_cast<unsigned long>( N ) );
        }

        return pSlot;
    }

    // ------------------------------------------------------------------
    // Release
    //
    // Returns a previously acquired slot to the pool.
    // Returns QC_STATUS_NULL_PTR if pSlot is null.
    // Returns QC_STATUS_BAD_ARGUMENTS if pSlot is outside this pool's
    // storage, or not exactly on a slot boundary.
    // Returns QC_STATUS_BAD_STATE if that slot is not currently acquired
    // (double release, or a slot this pool never handed out).
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Release( T *pSlot ) noexcept
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( nullptr == pSlot )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else
        {
            const Slot *pRawSlot = reinterpret_cast<const Slot *>( pSlot );
            const Slot *pBase = &m_slot[0];

            if ( ( pRawSlot < pBase ) || ( pRawSlot > &m_slot[N - 1u] ) )
            {
                // Not our memory.
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else if ( 0u != ( static_cast<std::size_t>( reinterpret_cast<const char *>( pRawSlot ) -
                                                        reinterpret_cast<const char *>( pBase ) ) %
                              sizeof( Slot ) ) )
            {
                // In range but not on a slot boundary (interior/misaligned
                // pointer): the index arithmetic below would be meaningless.
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else
            {
                const uint32_t index = static_cast<uint32_t>( pRawSlot - pBase );

                if ( !m_acquired[index] )
                {
                    // Double release, or a slot that was never acquired.
                    // Pushing it again would over-subscribe the ring.
                    status = QC_STATUS_BAD_STATE;
                }
                else
                {
                    m_acquired[index] = false;
                    --m_used;
                    m_ring[m_tail & ( N - 1u )] = index;
                    ++m_tail;
                }
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Size / Available / Empty / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const noexcept { return m_used; }
    uint32_t Available() const noexcept { return N - m_used; }
    bool Empty() const noexcept { return 0u == m_used; }
    static uint32_t Capacity() noexcept { return N; }

private:
    Slot m_slot[N];
    uint32_t m_ring[N];
    // Per-slot ownership, so a double release is detectable rather than
    // silently corrupting the ring.
    bool m_acquired[N];
    uint32_t m_head = 0u;
    uint32_t m_tail = 0u;
    uint32_t m_used = 0u;
};

}   // namespace QC

#endif   // QC_RING_POOL_HPP
