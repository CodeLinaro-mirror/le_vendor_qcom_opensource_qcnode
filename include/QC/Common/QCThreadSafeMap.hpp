// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_THREAD_SAFE_MAP_HPP
#define QC_THREAD_SAFE_MAP_HPP

#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <utility>

#include "QC/Common/QCDefs.hpp"
#include "QC/Common/QCMap.hpp"
#include "QC/Common/QCRingPoolAllocator.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCThreadSafeMap<Key, Value, N, Compare, Allocator>
//
// Self-locking wrapper around QCMap<Key, Value, N, Compare, Allocator>: an
// internal std::shared_mutex serialises every access, so callers no longer
// need to pair QCMap with their own external mutex (the pattern used today
// by OpticalFlow/DepthFromStereo/Voxelization's m_memMap/m_bufferMap).
// Mutators (Insert/InsertOrAssign/Update/Erase/Clear) take an exclusive
// lock; read-only queries (GetValue/Contains/Size/Empty) take a shared lock,
// so concurrent readers do not block each other — the common case for a
// map that is mutated rarely (buffer registration) but probed every frame
// (buffer lookup in the callback path).
//
// ---- Why this does not offer QCMap::Find() ----
//
// QCMap::Find() hands back a pointer into the map's storage that "stays
// valid until that entry is erased or the map cleared" — a contract that
// only holds because QCMap itself performs no locking and the caller is
// expected to hold one external lock across the pointer's entire useful
// lifetime. A self-locking wrapper cannot honour that: if the lock is only
// held for the duration of Find() and released before the caller reads
// through the pointer, another thread can Erase()/Clear() the entry in
// between, so QCThreadSafeMap does not expose Find() or iterators
// (begin()/end()) at all. The only lookup this class offers is GetValue(),
// which copies the value out while the shared lock is held and releases the
// lock before returning — the result can never dangle. This is why Value
// must stay nothrow-copyable/assignable (QCMap already asserts this on the
// wrapped map; restated here is unnecessary since the member triggers it).
//
// No Emplace(): forwarding in-place construction under a lock that is
// otherwise copy-out-only invites callers to assume they can safely hold a
// reference to the constructed value past the call, which is exactly the
// footgun this class exists to avoid. Callers needing Emplace() semantics
// should construct the Value first and use InsertOrAssign().
//
// Copy and move are deleted: QCMap disallows them for the same reason
// (the allocator owns its pool inline and compares by address), and a
// std::shared_mutex is itself non-copyable/non-movable.
// ---------------------------------------------------------------------------
template<typename Key, typename Value, uint32_t N, typename Compare = std::less<Key>,
         typename Allocator = QCRingPoolAllocator<std::pair<const Key, Value>, N>>
class QCThreadSafeMap
{
public:
    QCThreadSafeMap() = default;
    ~QCThreadSafeMap() = default;

    QCThreadSafeMap( const QCThreadSafeMap & ) = delete;
    QCThreadSafeMap &operator=( const QCThreadSafeMap & ) = delete;
    QCThreadSafeMap( QCThreadSafeMap && ) = delete;
    QCThreadSafeMap &operator=( QCThreadSafeMap && ) = delete;

    // ------------------------------------------------------------------
    // Insert — exclusive lock. See QCMap::Insert.
    // ------------------------------------------------------------------
    QCStatus_e Insert( const Key &key, const Value &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Insert( key, value );
    }

    // ------------------------------------------------------------------
    // InsertOrAssign — exclusive lock. See QCMap::InsertOrAssign.
    // ------------------------------------------------------------------
    QCStatus_e InsertOrAssign( const Key &key, const Value &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_map.InsertOrAssign( key, value );
    }

    // ------------------------------------------------------------------
    // Update — exclusive lock. See QCMap::Update.
    // ------------------------------------------------------------------
    QCStatus_e Update( const Key &key, const Value &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Update( key, value );
    }

    // ------------------------------------------------------------------
    // Erase — exclusive lock. See QCMap::Erase.
    // ------------------------------------------------------------------
    QCStatus_e Erase( const Key &key )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Erase( key );
    }

    // ------------------------------------------------------------------
    // Clear — exclusive lock. See QCMap::Clear.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Clear();
    }

    // ------------------------------------------------------------------
    // GetValue — shared lock. Copies the stored value into outValue, so
    // the result stays valid after the lock is released. See QCMap::GetValue.
    // ------------------------------------------------------------------
    QCStatus_e GetValue( const Key &key, Value &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_map.GetValue( key, outValue );
    }

    // ------------------------------------------------------------------
    // Contains — shared lock. See QCMap::Contains.
    // ------------------------------------------------------------------
    bool Contains( const Key &key ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Contains( key );
    }

    // ------------------------------------------------------------------
    // Size / Empty / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Size();
    }

    bool Empty() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_map.Empty();
    }

    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

private:
    mutable std::shared_mutex m_mutex;
    QCMap<Key, Value, N, Compare, Allocator> m_map;
};

}   // namespace QC

#endif   // QC_THREAD_SAFE_MAP_HPP
