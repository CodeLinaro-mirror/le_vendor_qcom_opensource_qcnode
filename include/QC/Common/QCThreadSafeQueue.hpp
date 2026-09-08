// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_THREAD_SAFE_QUEUE_HPP
#define QC_THREAD_SAFE_QUEUE_HPP

#include <cstdint>
#include <shared_mutex>

#include "QC/Common/QCDefs.hpp"
#include "QC/Common/QCQueue.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCThreadSafeQueue<T, N>
//
// Self-locking wrapper around QCQueue<T, N>: an internal std::shared_mutex
// serialises every access, so callers no longer need to pair QCQueue with
// their own external mutex. Like the wrapped queue it is fixed-capacity with
// no runtime memory allocation, and every operation that can fail returns
// QCStatus_e.
//
// Mutators (Push/Pop/Clear) take an exclusive lock; read-only queries
// (GetFront/GetBack/Size/Empty/Full) take a shared lock, so concurrent readers
// do not block each other — the common case for a producer/consumer queue
// whose depth is polled more often than it is mutated.
//
// ---- Why this does not offer QCQueue::Front()/Back() ----
//
// QCQueue::Front()/Back() hand back a pointer into the queue's storage that
// stays valid only "until that element is popped or the queue cleared" — a
// contract that holds because QCQueue performs no locking and the caller is
// expected to hold one external lock across the pointer's entire useful
// lifetime. A self-locking wrapper cannot honour that: if the lock is only
// held for the duration of Front() and released before the caller reads
// through the pointer, another thread can Pop()/Clear() the element in
// between. QCThreadSafeQueue therefore does not expose Front() or Back() at
// all. The only inspection it offers is GetFront()/GetBack() and the copy-out
// Pop(), each of which copies the value out while the lock is held and
// releases the lock before returning — the result can never dangle. This is
// why T must stay nothrow-copyable/assignable (QCQueue already asserts this on
// the wrapped queue; restating it here is unnecessary since the member
// triggers it).
//
// Pop() is offered ONLY in its copy-out form. The element-discarding
// QCQueue::Pop() overload is deliberately omitted: the single-threaded
// "inspect via Front(), then Pop()" idiom it exists to serve is precisely the
// racy pattern this class removes, so the only way to take an element out here
// is the one that hands it to the caller atomically under the lock.
//
// Copy and move are deleted: a std::shared_mutex is itself
// non-copyable/non-movable, and copying a queue while another thread holds the
// lock has no meaningful semantics. This differs from QCQueue, which is
// copyable because it is plain inline storage.
//
// Thread safety: full. Every public method is internally synchronised. Note
// the usual compound-operation caveat — a Full() check followed by a Push() is
// two separately locked operations and another thread may push in between, so
// callers must branch on Push()'s QC_STATUS_NO_RESOURCE rather than probing
// first.
//
// ISO 26262:6 Table 8 §1a : single return statement per function.
// MISRA C++:2008 Rule 11-0-1: all data members private.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCThreadSafeQueue
{
public:
    using ValueType = T;

    QCThreadSafeQueue() = default;
    ~QCThreadSafeQueue() = default;

    QCThreadSafeQueue( const QCThreadSafeQueue & ) = delete;
    QCThreadSafeQueue &operator=( const QCThreadSafeQueue & ) = delete;
    QCThreadSafeQueue( QCThreadSafeQueue && ) = delete;
    QCThreadSafeQueue &operator=( QCThreadSafeQueue && ) = delete;

    // ------------------------------------------------------------------
    // Push — exclusive lock. See QCQueue::Push.
    // ------------------------------------------------------------------
    QCStatus_e Push( const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Push( value );
    }

    // ------------------------------------------------------------------
    // Pop — exclusive lock. Copies the front element into outValue and
    // removes it in one locked step, so the value stays valid after the lock
    // is released and no other thread can take it in between.
    // See QCQueue::Pop.
    // ------------------------------------------------------------------
    QCStatus_e Pop( T &outValue )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Pop( outValue );
    }

    // ------------------------------------------------------------------
    // Clear — exclusive lock. See QCQueue::Clear.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Clear();
    }

    // ------------------------------------------------------------------
    // GetFront — shared lock. Copies the front element into outValue and
    // leaves it queued, so the result stays valid after the lock is released.
    // See QCQueue::GetFront.
    // ------------------------------------------------------------------
    QCStatus_e GetFront( T &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.GetFront( outValue );
    }

    // ------------------------------------------------------------------
    // GetBack — shared lock. See QCQueue::GetBack.
    // ------------------------------------------------------------------
    QCStatus_e GetBack( T &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.GetBack( outValue );
    }

    // ------------------------------------------------------------------
    // Size / Empty / Full / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Size();
    }

    bool Empty() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Empty();
    }

    bool Full() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        return m_queue.Full();
    }

    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

private:
    mutable std::shared_mutex m_mutex;
    QCQueue<T, N> m_queue;
};

}   // namespace QC

#endif   // QC_THREAD_SAFE_QUEUE_HPP
