// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_MAP2_HPP
#define QC_MAP2_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <type_traits>
#include <utility>

#include "QC/Common/QCDefs.hpp"
#include "QC/Common/QCRingPoolAllocator.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCMap<Key, Value, N, Compare, Allocator>
//
// Associative container built on std::map. N is this instance's logical
// capacity and must be a power of 2; QCMap itself enforces it (every
// insertion path checks Size() < N), independent of whatever the Allocator
// can physically provide.
//
// Allocator defaults to QCRingPoolAllocator<pair, N> — every tree node then
// comes from a fixed-capacity QCRingPool instead of the heap (the pool is
// sized in units of the map's internal node, the key/value pair plus
// parent/left/right pointers and a colour bit, not the bare pair, via
// QCRingPoolAllocator's rebind). Callers that don't need the zero-heap
// guarantee may plug in std::allocator<std::pair<const Key, Value>> instead
// to get QCMap's status-returning API over an ordinary heap-backed std::map;
// see the "On throwing" note below for how that changes the no-throw
// guarantee.
//
// Every operation that can fail returns QCStatus_e. With the default
// QCRingPoolAllocator, there is no try/catch anywhere in this class: instead,
// the two things that could throw are both eliminated by construction.
//
//   1. Node allocation. std::map calls allocate(1) per insert, and
//      QCRingPoolAllocator throws std::bad_alloc only when its pool is
//      empty. Every insertion path here first checks Size() < N and returns
//      QC_STATUS_NO_RESOURCE instead, so the pool is never empty when the
//      container reaches allocate(). One map entry == one pool slot, so the
//      check is exact, not conservative. The allocator's throw is therefore
//      dead code on this path.
//
//   2. Key/Value copy and assignment. Enforced nothrow by the static_asserts
//      below, so the copy performed while constructing or overwriting an
//      entry cannot throw either.
//
// Together these make each method's failure set fully described by its
// QCStatus_e return: no exception can escape, and none needs to be caught.
//
// The static_asserts are the contract that keeps this true. A future Value
// that allocates (e.g. one holding a std::string or std::vector) will fail
// to compile here rather than silently reintroduce a throwing path. Such a
// type does not belong in a zero-allocation container; use QCString or a
// fixed-capacity member instead.
//
// ---- On throwing, with a non-default Allocator ----
//
// The Size() < N guard above only makes allocate() unreachable-when-empty
// for a pool-backed allocator whose capacity IS N (QCRingPoolAllocator).
// Plugging in std::allocator<std::pair<const Key, Value>> opts OUT of that
// guarantee along with the zero-heap-allocation property: std::allocator can
// still throw std::bad_alloc if the process is genuinely out of heap memory,
// even though Size() < N held. That is expected for this variant — the point
// of choosing std::allocator is to trade the no-throw/no-heap guarantee for
// ordinary heap-backed storage, not to keep both. QCMap does not add a
// try/catch for this case; a caller who needs the no-throw guarantee must
// use the default QCRingPoolAllocator.
//
// Note the comparator is deliberately NOT asserted nothrow: the standard
// library does not mark std::less<T>::operator() noexcept even for integral
// types, so asserting it would reject std::less<uint64_t>. A comparator that
// genuinely throws is out of contract for this container.
//
// operator[] is deliberately not provided: it default-constructs on a miss
// and cannot report pool exhaustion. Use InsertOrAssign() instead.
//
// Logging: this container uses the process-wide default logger via the
// QC_LOG_* macros, NOT a QC_DECLARE_LOGGER() member. A member Logger would
// make simply *constructing* a QCMap allocate on the heap — Logger::Init()
// calls DecideLoggerLevel(), which takes its name by value and concatenates
// "<name>_QC_LOG_LEVEL" (two allocations), plus the create-callback's own
// context allocation. That would defeat the purpose of a zero-allocation
// container, and is the same reason QCString carries no logger. By contrast
// Logger::GetDefault() is a singleton: it initialises once for the whole
// process, after which each QC_LOG_* is a level compare plus a formatted
// write, with no per-object and no per-call allocation.
//
// Log levels used, and why:
//   ERROR — a caller contract violation or a real resource limit: capacity
//           exhausted, or a mutation naming a key that is not present. These
//           are the cases a developer needs to see without enabling anything.
//   DEBUG — outcomes that are normal control flow for some callers but useful
//           when tracing: a duplicate Insert, a Find/GetValue miss. Lookup
//           misses are deliberately DEBUG and not ERROR: RegisterMemory()
//           probes the map on every frame and a miss is its expected
//           first-time path, so logging that at ERROR would flood the log on
//           the operational path.
// Contains() and the iterators stay silent — they are pure queries with no
// failure to report, and are called per frame.
//
// Because %zu/%p are not portable across the QNX and Linux log backends in
// this tree, sizes are cast to unsigned long and logged with %lu.
//
// Thread safety: none. The caller must serialise all access with its own
// mutex; pointers and iterators are valid only while no concurrent
// modification occurs.
//
// Copy and move are deleted: the allocator owns its pool inline and compares
// by address, so nodes cannot be transferred between QCMap instances.
//
// Requirements on Value: nothrow-copy-constructible and
// nothrow-copy-assignable. Default construction is NOT required.
// ---------------------------------------------------------------------------
template<typename Key, typename Value, uint32_t N, typename Compare = std::less<Key>,
         typename Allocator = QCRingPoolAllocator<std::pair<const Key, Value>, N>>
class QCMap
{
    static_assert( N > 0u && 0u == ( N & ( N - 1u ) ), "QCMap capacity N must be a power of 2" );

    // std::map rebinds Allocator to its own internal node type, but the
    // value_type Allocator is instantiated WITH here must already agree with
    // the map's element type — otherwise the allocator plugged in silently
    // targets the wrong object layout.
    static_assert( std::is_same<typename Allocator::value_type, std::pair<const Key, Value>>::value,
                   "QCMap Allocator::value_type must be std::pair<const Key, Value>" );

    // The no-try/catch guarantee rests on these: if a copy cannot throw and
    // a node is always available, no operation below has a throwing path.
    static_assert( std::is_nothrow_copy_constructible<Key>::value,
                   "QCMap Key must be nothrow-copy-constructible: QCMap has no exception "
                   "handling, so a throwing Key copy would escape the container" );
    static_assert( std::is_nothrow_copy_constructible<Value>::value,
                   "QCMap Value must be nothrow-copy-constructible: QCMap has no exception "
                   "handling, so a throwing Value copy would escape the container. Use a "
                   "fixed-capacity type (e.g. QCString) instead of one that allocates" );
    static_assert( std::is_nothrow_copy_assignable<Value>::value,
                   "QCMap Value must be nothrow-copy-assignable: Update()/InsertOrAssign() "
                   "overwrite in place and must not be able to throw" );

public:
    using AllocatorType = Allocator;
    using MapType = std::map<Key, Value, Compare, AllocatorType>;
    using ValueType = typename MapType::value_type;
    using Iterator = typename MapType::iterator;
    using ConstIterator = typename MapType::const_iterator;

    QCMap() = default;
    ~QCMap() = default;

    QCMap( const QCMap & ) = delete;
    QCMap &operator=( const QCMap & ) = delete;
    QCMap( QCMap && ) = delete;
    QCMap &operator=( QCMap && ) = delete;

    // ------------------------------------------------------------------
    // Insert
    //
    // Inserts value under key. Does not overwrite an existing entry.
    // Returns QC_STATUS_ALREADY     if key is already present.
    // Returns QC_STATUS_NO_RESOURCE if the map is at capacity N.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Insert( const Key &key, const Value &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        // Duplicate is checked before capacity: re-inserting an existing key
        // consumes no node, so ALREADY is the correct answer even at N.
        const ConstIterator it = m_map.find( key );

        if ( it != m_map.cend() )
        {
            // Normal control flow for callers that probe-then-insert, so
            // DEBUG rather than ERROR.
            QC_LOG_DEBUG( "QCMap::Insert: key already present, entry kept unchanged" );
            status = QC_STATUS_ALREADY;
        }
        else if ( m_map.size() >= N )
        {
            QC_LOG_ERROR( "QCMap::Insert: at capacity %lu, cannot insert",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            // Guarded above: a free pool slot exists, and the pair copy is
            // nothrow per the static_asserts, so this cannot throw.
            const std::size_t previousSize = m_map.size();
            (void) m_map.emplace( key, value );
            if ( m_map.size() != ( previousSize + 1u ) )
            {
                QC_LOG_ERROR( "QCMap::Insert: emplace did not grow the map" );
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // InsertOrAssign
    //
    // Inserts value under key, overwriting any existing entry. This is the
    // replacement for the std::map `m[key] = value` idiom.
    // Returns QC_STATUS_NO_RESOURCE if key is absent and the map is at N.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e InsertOrAssign( const Key &key, const Value &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        const Iterator it = m_map.find( key );

        if ( it != m_map.end() )
        {
            // Overwrite in place: no node needed, nothrow assignment.
            it->second = value;
        }
        else if ( m_map.size() >= N )
        {
            QC_LOG_ERROR( "QCMap::InsertOrAssign: at capacity %lu, cannot insert new key",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            const std::size_t previousSize = m_map.size();
            (void) m_map.emplace( key, value );
            if ( m_map.size() != ( previousSize + 1u ) )
            {
                QC_LOG_ERROR( "QCMap::InsertOrAssign: emplace did not grow the map" );
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Emplace
    //
    // In-place constructs the value under key, forwarding args to the
    // value's constructor. Does not overwrite an existing entry.
    // The caller is responsible for args yielding a nothrow construction;
    // the nothrow-copy contract asserted above covers the copy/assign paths
    // only.
    // Returns QC_STATUS_ALREADY     if key is already present.
    // Returns QC_STATUS_NO_RESOURCE if the map is at capacity N.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    template<typename... Args>
    QCStatus_e Emplace( const Key &key, Args &&...args )
    {
        QCStatus_e status = QC_STATUS_OK;

        const ConstIterator it = m_map.find( key );

        if ( it != m_map.cend() )
        {
            QC_LOG_DEBUG( "QCMap::Emplace: key already present, entry kept unchanged" );
            status = QC_STATUS_ALREADY;
        }
        else if ( m_map.size() >= N )
        {
            QC_LOG_ERROR( "QCMap::Emplace: at capacity %lu, cannot emplace",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            // Args construct one Value in one map node; they do not consume
            // additional map capacity.
            const std::size_t previousSize = m_map.size();
            (void) m_map.emplace( std::piecewise_construct, std::forward_as_tuple( key ),
                                  std::forward_as_tuple( std::forward<Args>( args )... ) );
            if ( m_map.size() != ( previousSize + 1u ) )
            {
                QC_LOG_ERROR( "QCMap::Emplace: emplace did not grow the map" );
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Update
    //
    // Overwrites the value stored under an existing key. Never inserts.
    // Returns QC_STATUS_BAD_ARGUMENTS if key is not present.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Update( const Key &key, const Value &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        const Iterator it = m_map.find( key );

        if ( it == m_map.end() )
        {
            // Update names an entry the caller believes exists: a miss is a
            // contract violation, not routine flow.
            QC_LOG_ERROR( "QCMap::Update: key not found, nothing updated" );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            it->second = value;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Erase
    //
    // Removes the entry identified by key and returns its node to the pool.
    // Erasing destroys a node and relinks the tree — no allocation, no throw.
    // Returns QC_STATUS_BAD_ARGUMENTS if key is not present.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Erase( const Key &key )
    {
        QCStatus_e status = QC_STATUS_OK;
        const std::size_t previousSize = m_map.size();

        if ( 0u == m_map.erase( key ) )
        {
            QC_LOG_ERROR( "QCMap::Erase: key not found, nothing erased" );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( m_map.size() != ( previousSize - 1u ) )
        {
            QC_LOG_ERROR( "QCMap::Erase: erase did not shrink the map" );
            status = QC_STATUS_FAIL;
        }
        else
        {
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Find
    //
    // Seats a pointer to the stored value into pOutValue. pOutValue is set
    // to nullptr and QC_STATUS_BAD_ARGUMENTS returned when key is absent.
    // The pointer stays valid until that entry is erased or the map cleared.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Find( const Key &key, Value *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        const Iterator it = m_map.find( key );

        if ( it == m_map.end() )
        {
            // A miss is the expected first-time path for per-frame callers
            // (e.g. RegisterMemory), so DEBUG keeps the operational log clean.
            QC_LOG_DEBUG( "QCMap::Find: key not found" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            pOutValue = &( it->second );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Find (const overload)
    // ------------------------------------------------------------------
    QCStatus_e Find( const Key &key, const Value *&pOutValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        const ConstIterator it = m_map.find( key );

        if ( it == m_map.cend() )
        {
            QC_LOG_DEBUG( "QCMap::Find: key not found" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            pOutValue = &( it->second );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // GetValue
    //
    // Copies the stored value into outValue, so no pointer into container
    // storage escapes and the result cannot dangle.
    // Returns QC_STATUS_BAD_ARGUMENTS if key is not present.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e GetValue( const Key &key, Value &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        const ConstIterator it = m_map.find( key );

        if ( it == m_map.cend() )
        {
            QC_LOG_DEBUG( "QCMap::GetValue: key not found, outValue untouched" );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            outValue = it->second;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Contains — pure query, cannot fail. Intentionally silent: it is
    // called per frame and has no failure to report.
    // ------------------------------------------------------------------
    bool Contains( const Key &key ) const { return m_map.find( key ) != m_map.cend(); }

    // ------------------------------------------------------------------
    // Clear
    //
    // Removes all entries and returns every node to the pool. Never fails;
    // returns QCStatus_e for call-site symmetry with the other mutators.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        QCStatus_e status = QC_STATUS_OK;

        QC_LOG_DEBUG( "QCMap::Clear: releasing %lu entries",
                      static_cast<unsigned long>( m_map.size() ) );
        m_map.clear();
        if ( !m_map.empty() )
        {
            QC_LOG_ERROR( "QCMap::Clear: map is not empty after clear" );
            status = QC_STATUS_FAIL;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Size / Empty / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const { return static_cast<uint32_t>( m_map.size() ); }
    bool Empty() const { return m_map.empty(); }
    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

    // ------------------------------------------------------------------
    // Iteration
    //
    // Lower-case names are required by range-based for; this is a
    // deliberate deviation from the UpperCamelCase method convention.
    // ------------------------------------------------------------------
    Iterator begin() { return m_map.begin(); }
    Iterator end() { return m_map.end(); }
    ConstIterator begin() const { return m_map.cbegin(); }
    ConstIterator end() const { return m_map.cend(); }
    ConstIterator cbegin() const { return m_map.cbegin(); }
    ConstIterator cend() const { return m_map.cend(); }

private:
    MapType m_map;
};

}   // namespace QC

#endif   // QC_MAP2_HPP
