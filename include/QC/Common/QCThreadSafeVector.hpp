// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_THREAD_SAFE_VECTOR_HPP
#define QC_THREAD_SAFE_VECTOR_HPP

#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "QC/Common/QCDefs.hpp"
#include "QC/Common/QCVector.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCThreadSafeVector<T, N>
//
// Self-locking wrapper around QCVector<T, N>: an internal std::shared_mutex
// serialises every access, so callers no longer need to pair QCVector with
// their own external mutex. Like the wrapped vector it is fixed-capacity with
// no runtime memory allocation, and every operation that can fail returns
// QCStatus_e.
//
// Mutators (PushBack/PopBack/Insert/RemoveAt/SetValue/Resize/Clear) take an
// exclusive lock; read-only queries (GetValue/Find/Contains/Size/Empty/Full)
// take a shared lock, so concurrent readers do not block each other — the
// common case for a list that is mutated rarely (registration) but read every
// frame.
//
// ---- Why this does not offer QCVector::At()/Front()/Back()/Data() ----
//
// Those methods hand back a pointer into the vector's storage that stays valid
// only "until the vector is resized or the element removed" — a contract that
// holds because QCVector performs no locking and the caller is expected to hold
// one external lock across the pointer's entire useful lifetime. A self-locking
// wrapper cannot honour that: if the lock is only held for the duration of
// At() and released before the caller reads through the pointer, another thread
// can RemoveAt()/Resize()/Clear() in between. QCThreadSafeVector therefore
// exposes neither the pointer accessors nor Data() nor iterators
// (begin()/end()) at all. The only reads it offers are GetValue()/Find() and
// the copy-out PopBack(), each of which copies the value out while the lock is
// held and releases the lock before returning — the result can never dangle.
// This is why T must stay nothrow-copyable/assignable (QCVector already asserts
// this on the wrapped vector; restating it here is unnecessary since the member
// triggers it).
//
// Iteration is offered as ForEach(): the callback runs under the shared lock,
// so the elements it observes cannot be moved or removed mid-traversal. The
// callback receives each element by const reference and must not call back into
// this container — std::shared_mutex is not recursive, so a nested call would
// deadlock (a nested mutating call) or is at best undefined (a nested shared
// acquire while a writer waits). It must also not retain the reference past its
// own return, for the same dangling reason the pointer accessors are absent.
//
// PopBack() is offered ONLY in its copy-out form, for the same reason
// QCThreadSafeQueue offers only the copy-out Pop(): the element-discarding
// overload exists to serve the single-threaded "inspect via Back(), then
// PopBack()" idiom, which is precisely the racy pattern this class removes.
//
// Copy and move are deleted: a std::shared_mutex is itself
// non-copyable/non-movable, and copying a vector while another thread holds the
// lock has no meaningful semantics. This differs from QCVector, which is
// copyable because it is plain inline storage.
//
// Thread safety: full. Every public method is internally synchronised. Note
// the usual compound-operation caveat — an index obtained from Find() or a
// bound obtained from Size() may be stale by the time a following call runs,
// since the lock is released in between. Callers that need index-stable access
// must branch on the following call's QCStatus_e rather than trusting the
// earlier result, or do the whole traversal inside one ForEach().
//
// ISO 26262:6 Table 8 §1a : single return statement per function.
// MISRA C++:2008 Rule 11-0-1: all data members private.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCThreadSafeVector
{
public:
    using ValueType = T;

    QCThreadSafeVector() = default;
    ~QCThreadSafeVector() = default;

    QCThreadSafeVector( const QCThreadSafeVector & ) = delete;
    QCThreadSafeVector &operator=( const QCThreadSafeVector & ) = delete;
    QCThreadSafeVector( QCThreadSafeVector && ) = delete;
    QCThreadSafeVector &operator=( QCThreadSafeVector && ) = delete;

    // ------------------------------------------------------------------
    // PushBack — exclusive lock. See QCVector::PushBack.
    // ------------------------------------------------------------------
    QCStatus_e PushBack( const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.PushBack( value );
    }

    // ------------------------------------------------------------------
    // PopBack — exclusive lock. Copies the last element into outValue and
    // removes it in one locked step, so the value stays valid after the lock
    // is released and no other thread can take it in between.
    // See QCVector::PopBack.
    // ------------------------------------------------------------------
    QCStatus_e PopBack( T &outValue )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.PopBack( outValue );
    }

    // ------------------------------------------------------------------
    // Insert — exclusive lock. See QCVector::Insert.
    // ------------------------------------------------------------------
    QCStatus_e Insert( uint32_t index, const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Insert( index, value );
    }

    // ------------------------------------------------------------------
    // RemoveAt — exclusive lock. See QCVector::RemoveAt.
    // ------------------------------------------------------------------
    QCStatus_e RemoveAt( uint32_t index )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.RemoveAt( index );
    }

    // ------------------------------------------------------------------
    // SetValue — exclusive lock. See QCVector::SetValue.
    // ------------------------------------------------------------------
    QCStatus_e SetValue( uint32_t index, const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.SetValue( index, value );
    }

    // ------------------------------------------------------------------
    // Resize — exclusive lock. See QCVector::Resize.
    // ------------------------------------------------------------------
    QCStatus_e Resize( uint32_t newSize, const T &fill = T{} )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Resize( newSize, fill );
    }

    // ------------------------------------------------------------------
    // Clear — exclusive lock. See QCVector::Clear.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Clear();
    }

    // ------------------------------------------------------------------
    // GetValue — shared lock. Copies the element at index into outValue, so
    // the result stays valid after the lock is released.
    // See QCVector::GetValue.
    // ------------------------------------------------------------------
    QCStatus_e GetValue( uint32_t index, T &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.GetValue( index, outValue );
    }

    // ------------------------------------------------------------------
    // Find — shared lock. See QCVector::Find. The index returned may be stale
    // by the time a following call runs; see the compound-operation caveat in
    // the class comment.
    // ------------------------------------------------------------------
    QCStatus_e Find( const T &value, uint32_t &outIndex ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Find( value, outIndex );
    }

    // ------------------------------------------------------------------
    // Contains — shared lock. See QCVector::Contains.
    // ------------------------------------------------------------------
    bool Contains( const T &value ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Contains( value );
    }

    // ------------------------------------------------------------------
    // ForEach
    //
    // Applies callback to each element in order under the shared lock, so the
    // sequence cannot be mutated mid-traversal. The callback takes each element
    // by const reference; it must not call back into this container (the mutex
    // is not recursive) and must not retain the reference past its own return.
    // Never fails; returns QCStatus_e for call-site symmetry.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    template<typename Callback>
    QCStatus_e ForEach( Callback callback ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );

        for ( const T &element : m_vector )
        {
            callback( element );
        }

        return QC_STATUS_OK;
    }

    // ------------------------------------------------------------------
    // Size / Empty / Full / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Size();
    }

    bool Empty() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Empty();
    }

    bool Full() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_vector.Full();
    }

    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

private:
    mutable std::shared_mutex m_mutex;
    QCVector<T, N> m_vector;
};

}   // namespace QC

#endif   // QC_THREAD_SAFE_VECTOR_HPP
