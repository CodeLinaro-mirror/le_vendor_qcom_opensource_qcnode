// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_QUEUE_HPP
#define QC_QUEUE_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

#include "QC/Common/QCDefs.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCQueue<T, N>
//
// Fixed-capacity FIFO queue with NO runtime memory allocation. N is the
// maximum number of elements the queue can hold; storage is a compile-time
// sized std::array held inline (stack/member storage only). Capacity can
// never grow past N — there is no heap fallback, so a push into a full queue
// fails with QC_STATUS_NO_RESOURCE instead of allocating, growing, or
// overwriting the oldest element.
//
// N must be a power of 2 so the head/tail indices wrap with a mask instead of
// a modulo, the same reason QCRingPool requires it.
//
// Storage layout: the array holds N live, default-constructed T objects for
// the whole lifetime of the queue; m_head is the index of the front element
// and m_size the number of elements currently queued, so the occupied range
// is the masked span [m_head, m_head + m_size). Elements are moved in and out
// by assignment, never by construction/destruction, which is what keeps every
// operation allocation-free and nothrow (see the static_asserts below).
//
// A slot vacated by Pop()/Clear() is reset to a default-constructed T rather
// than left holding the popped element, so a dequeued value (e.g. one holding
// a file descriptor or a buffer handle) is not retained in queue storage
// after the caller has taken ownership of it.
//
// Every operation that can fail returns QCStatus_e, and there is no try/catch
// anywhere in this class: the two things that could throw are eliminated by
// construction.
//
//   1. Allocation. There is none — storage is inline and its size is fixed at
//      compile time. Every push path first checks Size() < N and returns
//      QC_STATUS_NO_RESOURCE instead of growing.
//
//   2. Element copy and assignment. Enforced nothrow by the static_asserts
//      below, so moving a value into or out of a slot cannot throw.
//
// Together these make each method's failure set fully described by its
// QCStatus_e return: no exception can escape, and none needs to be caught.
// A future T that allocates (e.g. one holding a std::string or std::vector)
// will fail to compile here rather than silently reintroduce a throwing path.
// Such a type does not belong in a zero-allocation container; use QCString or
// a fixed-capacity member instead.
//
// No Emplace(): the N slots are already constructed, so an "in-place
// construct" would in fact build a temporary and assign it into a slot. That
// is what Push() already does, and offering Emplace() would imply a
// construction guarantee this storage model does not provide. Callers
// construct the value first and Push() it.
//
// Status codes:
//   QC_STATUS_NO_RESOURCE — the queue is at capacity N and cannot accept a push.
//   QC_STATUS_BAD_STATE   — the queue is empty, so there is no element to
//                           inspect or remove.
//
// Log levels used, and why:
//   ERROR — a real resource limit or a caller contract violation: a push into
//           a full queue, or a Pop()/Front()/Back() on an empty one. These are
//           the cases a developer needs to see without enabling anything.
//   DEBUG — outcomes that are normal control flow for some callers but useful
//           when tracing: the element count released by Clear().
// Size()/Empty()/Full()/Capacity() and the copy-out queries stay silent —
// they are pure queries with no failure to report, and are called per frame.
//
// Because %zu/%p are not portable across the QNX and Linux log backends in
// this tree, sizes are cast to unsigned long and logged with %lu.
//
// Logging: this container uses the process-wide default logger via the
// QC_LOG_* macros, NOT a QC_DECLARE_LOGGER() member. A member Logger would
// make simply *constructing* a QCQueue allocate on the heap — Logger::Init()
// concatenates "<name>_QC_LOG_LEVEL" and the create-callback allocates its own
// context — which would defeat the purpose of a zero-allocation container, the
// same reason QCMap and QCString carry no logger. Logger::GetDefault() is a
// singleton: it initialises once for the whole process, so no QCQueue
// operation allocates, and a log call whose level is suppressed costs only a
// level compare. Note that a line which IS emitted may allocate inside the log
// backend itself (the Linux backend builds a std::string per line in
// Logger::DefaultLog); that cost belongs to the logger, not this container, and
// on the operational path it is only reached by the failure branches.
//
// Thread safety: none. The caller must serialise all access with its own
// mutex; pointers returned by Front()/Back() are valid only while no
// concurrent modification occurs. Use QCThreadSafeQueue for a self-locking
// variant.
//
// Copy and move are memberwise over inline storage, so the compiler-generated
// versions are allocation-free and cannot throw. They are defaulted rather
// than deleted so that a struct holding a QCQueue keeps its own implicit
// copy/assign instead of having them deleted by contagion. Move is a copy:
// there is no heap buffer to steal.
//
// Requirements on T: nothrow-default-constructible, nothrow-copy-constructible
// and nothrow-copy-assignable.
//
// ISO 26262:6 Table 8 §1a : single return statement per function.
// ISO 26262:6 Table 8 §1b : Front()/Back()/Pop() out-parameters are only
//                            valid on QC_STATUS_OK.
// MISRA C++:2008 Rule 11-0-1: all data members private.
// MISRA C++:2008 Rule 8-5-1 : all members explicitly value-initialised.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCQueue
{
    static_assert( N > 0u && 0u == ( N & ( N - 1u ) ), "QCQueue capacity N must be a power of 2" );

    // The no-try/catch guarantee rests on these: if the slots can be created
    // and assigned without throwing, no operation below has a throwing path.
    static_assert( std::is_nothrow_default_constructible<T>::value,
                   "QCQueue T must be nothrow-default-constructible: the N slots are created "
                   "with the queue and vacated slots are reset to a default-constructed T" );
    static_assert( std::is_nothrow_copy_constructible<T>::value,
                   "QCQueue T must be nothrow-copy-constructible: QCQueue has no exception "
                   "handling, so a throwing T copy would escape the container. Use a "
                   "fixed-capacity type (e.g. QCString) instead of one that allocates" );
    static_assert( std::is_nothrow_copy_assignable<T>::value,
                   "QCQueue T must be nothrow-copy-assignable: Push()/Pop() move values "
                   "between slots by assignment and must not be able to throw" );

public:
    using ValueType = T;
    using ContainerType = std::array<T, N>;

    QCQueue() noexcept = default;

    ~QCQueue() = default;

    QCQueue( const QCQueue & ) noexcept = default;
    QCQueue &operator=( const QCQueue & ) noexcept = default;
    QCQueue( QCQueue && ) noexcept = default;
    QCQueue &operator=( QCQueue && ) noexcept = default;

    // ------------------------------------------------------------------
    // Push
    //
    // Appends value to the back of the queue.
    // Returns QC_STATUS_NO_RESOURCE if the queue is already at capacity N;
    // the queue is left unchanged and no element is overwritten.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Push( const T &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size >= N )
        {
            QC_LOG_ERROR( "QCQueue::Push: at capacity %lu, cannot push",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            // Guarded above: the slot one past the last element is free, and
            // the assignment is nothrow per the static_asserts.
            m_storage[TailIndex()] = value;
            ++m_size;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Pop
    //
    // Removes the front element and resets its slot to a default-constructed
    // T, so the dequeued value is not retained in queue storage.
    // Returns QC_STATUS_BAD_STATE if the queue is empty.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Pop()
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Pop: queue is empty, nothing popped" );
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            m_storage[m_head] = T{};
            m_head = ( m_head + 1u ) & ( N - 1u );
            --m_size;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Pop (copy out)
    //
    // Copies the front element into outValue and then removes it. The value
    // is copied out of container storage, so the result cannot dangle.
    // Returns QC_STATUS_BAD_STATE if the queue is empty; outValue is then
    // left untouched.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Pop( T &outValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Pop: queue is empty, outValue untouched" );
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = m_storage[m_head];
            m_storage[m_head] = T{};
            m_head = ( m_head + 1u ) & ( N - 1u );
            --m_size;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Front
    //
    // Seats a pointer to the front element into pOutValue. The pointer stays
    // valid until that element is popped or the queue cleared.
    // Returns QC_STATUS_BAD_STATE if the queue is empty; pOutValue is then
    // set to nullptr.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Front( T *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Front: queue is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[m_head];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Front (const overload)
    // ------------------------------------------------------------------
    QCStatus_e Front( const T *&pOutValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Front: queue is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[m_head];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Back
    //
    // Seats a pointer to the most recently pushed element into pOutValue.
    // Returns QC_STATUS_BAD_STATE if the queue is empty; pOutValue is then
    // set to nullptr.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Back( T *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Back: queue is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[BackIndex()];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Back (const overload)
    // ------------------------------------------------------------------
    QCStatus_e Back( const T *&pOutValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCQueue::Back: queue is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[BackIndex()];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // GetFront
    //
    // Copies the front element into outValue and leaves it queued, so no
    // pointer into container storage escapes and the result cannot dangle.
    // Returns QC_STATUS_BAD_STATE if the queue is empty.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e GetFront( T &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = m_storage[m_head];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // GetBack
    //
    // Copies the most recently pushed element into outValue and leaves it
    // queued.
    // Returns QC_STATUS_BAD_STATE if the queue is empty.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e GetBack( T &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = m_storage[BackIndex()];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Clear
    //
    // Removes all elements and resets every occupied slot to a
    // default-constructed T. Never fails; returns QCStatus_e for call-site
    // symmetry with the other mutators.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        QC_LOG_DEBUG( "QCQueue::Clear: releasing %lu element(s)",
                      static_cast<unsigned long>( m_size ) );

        std::fill( m_storage.begin(), m_storage.end(), T{} );

        m_head = 0u;
        m_size = 0u;

        return QC_STATUS_OK;
    }

    // ------------------------------------------------------------------
    // Size / Empty / Full / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const { return m_size; }
    bool Empty() const { return 0u == m_size; }
    bool Full() const { return m_size >= N; }
    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

private:
    // Index of the free slot just past the last element, masked into the ring.
    uint32_t TailIndex() const { return ( m_head + m_size ) & ( N - 1u ); }

    // Index of the last element. Only meaningful when the queue is non-empty;
    // every caller checks that first.
    uint32_t BackIndex() const { return ( m_head + m_size - 1u ) & ( N - 1u ); }

    ContainerType m_storage{};
    uint32_t m_head = 0u;
    uint32_t m_size = 0u;
};

}   // namespace QC

#endif   // QC_QUEUE_HPP
