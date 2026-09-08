// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_VECTOR_HPP
#define QC_VECTOR_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "QC/Common/QCDefs.hpp"
#include "QC/Infras/Log/Logger.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCVector<T, N>
//
// Fixed-capacity, contiguous, random-access sequence with NO runtime memory
// allocation. N is the maximum number of elements the vector can hold;
// storage is a compile-time sized std::array held inline (stack/member
// storage only). Capacity can never grow past N — there is no heap fallback,
// so a request that would exceed N fails with QC_STATUS_NO_RESOURCE instead
// of allocating, growing, or truncating silently.
//
// Unlike QCQueue, N need NOT be a power of 2: this container indexes directly
// and never wraps, so there is no mask to satisfy. Elements always occupy the
// prefix [0, Size()) of the array, which is what makes Data() a valid
// contiguous buffer for the whole live range.
//
// Storage layout: the array holds N live, default-constructed T objects for
// the whole lifetime of the vector; m_size is the number currently in use.
// Elements are written and moved by assignment, never by
// construction/destruction, which is what keeps every operation
// allocation-free and nothrow (see the static_asserts below).
//
// A slot vacated by PopBack()/RemoveAt()/Resize()/Clear() is reset to a
// default-constructed T rather than left holding the removed element, so a
// removed value (e.g. one holding a file descriptor or a buffer handle) is not
// retained in vector storage after the caller has taken ownership of it.
//
// Why no Reserve()/ShrinkToFit(): capacity is fixed at compile time (N) and
// never changes, so those operations would be no-ops by construction. They
// are intentionally omitted rather than kept as dead API surface, the same
// reason QCString omits them.
//
// operator[] is deliberately not provided: it cannot report an out-of-range
// index, which is exactly the failure this container exists to surface. Use
// At() for checked access, or Data() when the caller has already validated the
// range and wants raw contiguous access.
//
// Every operation that can fail returns QCStatus_e, and there is no try/catch
// anywhere in this class: the two things that could throw are eliminated by
// construction.
//
//   1. Allocation. There is none — storage is inline and its size is fixed at
//      compile time. Every growth path first checks Size() < N and returns
//      QC_STATUS_NO_RESOURCE instead of growing.
//
//   2. Element copy and assignment. Enforced nothrow by the static_asserts
//      below, so writing a value into a slot or shifting the tail cannot throw.
//
// Together these make each method's failure set fully described by its
// QCStatus_e return: no exception can escape, and none needs to be caught.
// A future T that allocates (e.g. one holding a std::string or std::vector)
// will fail to compile here rather than silently reintroduce a throwing path.
// Such a type does not belong in a zero-allocation container; use QCString or
// a fixed-capacity member instead.
//
// No Emplace/EmplaceBack(): the N slots are already constructed, so an
// "in-place construct" would in fact build a temporary and assign it into a
// slot. That is what PushBack() already does, and offering EmplaceBack() would
// imply a construction guarantee this storage model does not provide. Callers
// construct the value first and PushBack() it.
//
// Status codes:
//   QC_STATUS_NO_RESOURCE   — the vector is at capacity N, or the requested
//                             size exceeds N.
//   QC_STATUS_BAD_STATE     — the vector is empty, so there is no element to
//                             inspect or remove.
//   QC_STATUS_BAD_ARGUMENTS — the supplied index or range is out of bounds.
//
// Log levels used, and why:
//   ERROR — a real resource limit or a caller contract violation: growth past
//           capacity, an out-of-range index, or a PopBack()/Front()/Back() on
//           an empty vector. These are the cases a developer needs to see
//           without enabling anything.
//   DEBUG — outcomes that are normal control flow for some callers but useful
//           when tracing: the element count released by Clear(), and a Find()
//           miss. A Find() miss is deliberately DEBUG and not ERROR: callers
//           probe before inserting and a miss is the expected first-time path,
//           so logging it at ERROR would flood the log on the operational path.
// Size()/Empty()/Full()/Capacity()/Data() and the copy-out queries stay
// silent — they are pure queries with no failure to report, and are called
// per frame.
//
// Because %zu/%p are not portable across the QNX and Linux log backends in
// this tree, sizes and indices are cast to unsigned long and logged with %lu.
//
// Logging: this container uses the process-wide default logger via the
// QC_LOG_* macros, NOT a QC_DECLARE_LOGGER() member. A member Logger would
// make simply *constructing* a QCVector allocate on the heap — Logger::Init()
// concatenates "<name>_QC_LOG_LEVEL" and the create-callback allocates its own
// context — which would defeat the purpose of a zero-allocation container, the
// same reason QCMap and QCString carry no logger. Logger::GetDefault() is a
// singleton: it initialises once for the whole process, so no QCVector
// operation allocates, and a log call whose level is suppressed costs only a
// level compare. Note that a line which IS emitted may allocate inside the log
// backend itself (the Linux backend builds a std::string per line in
// Logger::DefaultLog); that cost belongs to the logger, not this container, and
// on the operational path it is only reached by the failure branches.
//
// Thread safety: none. The caller must serialise all access with its own
// mutex; pointers and iterators are valid only while no concurrent
// modification occurs. Use QCThreadSafeVector for a self-locking variant.
//
// Copy and move are memberwise over inline storage, so the compiler-generated
// versions are allocation-free and cannot throw. They are defaulted rather
// than deleted so that a struct holding a QCVector keeps its own implicit
// copy/assign instead of having them deleted by contagion. Move is a copy:
// there is no heap buffer to steal.
//
// Requirements on T: nothrow-default-constructible, nothrow-copy-constructible
// and nothrow-copy-assignable. Find()/Contains() additionally require T to be
// equality-comparable, but only if those methods are instantiated.
//
// ISO 26262:6 Table 8 §1a : single return statement per function.
// ISO 26262:6 Table 8 §1b : At()/Front()/Back()/Find() out-parameters are only
//                            valid on QC_STATUS_OK.
// MISRA C++:2008 Rule 11-0-1: all data members private.
// MISRA C++:2008 Rule 8-5-1 : all members explicitly value-initialised.
// ---------------------------------------------------------------------------
template<typename T, uint32_t N>
class QCVector
{
    static_assert( N > 0u, "QCVector capacity N must be greater than zero" );

    // The no-try/catch guarantee rests on these: if the slots can be created
    // and assigned without throwing, no operation below has a throwing path.
    static_assert( std::is_nothrow_default_constructible<T>::value,
                   "QCVector T must be nothrow-default-constructible: the N slots are created "
                   "with the vector and vacated slots are reset to a default-constructed T" );
    static_assert( std::is_nothrow_copy_constructible<T>::value,
                   "QCVector T must be nothrow-copy-constructible: QCVector has no exception "
                   "handling, so a throwing T copy would escape the container. Use a "
                   "fixed-capacity type (e.g. QCString) instead of one that allocates" );
    static_assert( std::is_nothrow_copy_assignable<T>::value,
                   "QCVector T must be nothrow-copy-assignable: PushBack()/Insert()/RemoveAt() "
                   "write and shift elements by assignment and must not be able to throw" );

public:
    using ValueType = T;
    using ContainerType = std::array<T, N>;
    using Iterator = T *;
    using ConstIterator = const T *;

    QCVector() noexcept = default;

    ~QCVector() = default;

    QCVector( const QCVector & ) noexcept = default;
    QCVector &operator=( const QCVector & ) noexcept = default;
    QCVector( QCVector && ) noexcept = default;
    QCVector &operator=( QCVector && ) noexcept = default;

    // ------------------------------------------------------------------
    // PushBack
    //
    // Appends value to the back of the vector.
    // Returns QC_STATUS_NO_RESOURCE if the vector is already at capacity N;
    // the vector is left unchanged.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e PushBack( const T &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size >= N )
        {
            QC_LOG_ERROR( "QCVector::PushBack: at capacity %lu, cannot push",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            // Guarded above: slot m_size is free, and the assignment is
            // nothrow per the static_asserts.
            m_storage[m_size] = value;
            ++m_size;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // PopBack
    //
    // Removes the last element and resets its slot to a default-constructed
    // T, so the removed value is not retained in vector storage.
    // Returns QC_STATUS_BAD_STATE if the vector is empty.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e PopBack()
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCVector::PopBack: vector is empty, nothing popped" );
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            --m_size;
            m_storage[m_size] = T{};
        }

        return status;
    }

    // ------------------------------------------------------------------
    // PopBack (copy out)
    //
    // Copies the last element into outValue and then removes it. The value is
    // copied out of container storage, so the result cannot dangle.
    // Returns QC_STATUS_BAD_STATE if the vector is empty; outValue is then
    // left untouched.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e PopBack( T &outValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCVector::PopBack: vector is empty, outValue untouched" );
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            --m_size;
            outValue = m_storage[m_size];
            m_storage[m_size] = T{};
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Insert
    //
    // Inserts value at index, shifting the elements from index onward one
    // position toward the back. index == Size() appends.
    // Returns QC_STATUS_BAD_ARGUMENTS if index > Size().
    // Returns QC_STATUS_NO_RESOURCE   if the vector is at capacity N.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Insert( uint32_t index, const T &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index > m_size )
        {
            QC_LOG_ERROR( "QCVector::Insert: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( m_size >= N )
        {
            QC_LOG_ERROR( "QCVector::Insert: at capacity %lu, cannot insert",
                          static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            (void) std::move_backward( m_storage.begin() + index,
                                       m_storage.begin() + m_size,
                                       m_storage.begin() + m_size + 1u );

            m_storage[index] = value;
            ++m_size;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // RemoveAt
    //
    // Removes the element at index, shifting the elements after it one
    // position toward the front, and resets the vacated tail slot to a
    // default-constructed T.
    // Returns QC_STATUS_BAD_ARGUMENTS if index >= Size().
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e RemoveAt( uint32_t index )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index >= m_size )
        {
            QC_LOG_ERROR( "QCVector::RemoveAt: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            (void) std::move( m_storage.begin() + index + 1u,
                              m_storage.begin() + m_size,
                              m_storage.begin() + index );

            --m_size;
            m_storage[m_size] = T{};
        }

        return status;
    }

    // ------------------------------------------------------------------
    // At
    //
    // Seats a pointer to the element at index into pOutValue. The pointer
    // stays valid until the vector is resized or the element removed.
    // Returns QC_STATUS_BAD_ARGUMENTS if index >= Size(); pOutValue is then
    // set to nullptr.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e At( uint32_t index, T *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index >= m_size )
        {
            QC_LOG_ERROR( "QCVector::At: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            pOutValue = &m_storage[index];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // At (const overload)
    // ------------------------------------------------------------------
    QCStatus_e At( uint32_t index, const T *&pOutValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index >= m_size )
        {
            QC_LOG_ERROR( "QCVector::At: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            pOutValue = &m_storage[index];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // GetValue
    //
    // Copies the element at index into outValue, so no pointer into container
    // storage escapes and the result cannot dangle.
    // Returns QC_STATUS_BAD_ARGUMENTS if index >= Size(); outValue is then
    // left untouched.
    // ISO 26262:6 Table 8 §1b : outValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e GetValue( uint32_t index, T &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index >= m_size )
        {
            QC_LOG_ERROR( "QCVector::GetValue: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            outValue = m_storage[index];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // SetValue
    //
    // Overwrites the element at index. Never grows the vector.
    // Returns QC_STATUS_BAD_ARGUMENTS if index >= Size().
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e SetValue( uint32_t index, const T &value )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( index >= m_size )
        {
            QC_LOG_ERROR( "QCVector::SetValue: index %lu out of range for size %lu",
                          static_cast<unsigned long>( index ),
                          static_cast<unsigned long>( m_size ) );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_storage[index] = value;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Front
    //
    // Seats a pointer to the first element into pOutValue.
    // Returns QC_STATUS_BAD_STATE if the vector is empty; pOutValue is then
    // set to nullptr.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Front( T *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCVector::Front: vector is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[0];
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
            QC_LOG_ERROR( "QCVector::Front: vector is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[0];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Back
    //
    // Seats a pointer to the last element into pOutValue.
    // Returns QC_STATUS_BAD_STATE if the vector is empty; pOutValue is then
    // set to nullptr.
    // ISO 26262:6 Table 8 §1b : pOutValue is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Back( T *&pOutValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0u == m_size )
        {
            QC_LOG_ERROR( "QCVector::Back: vector is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[m_size - 1u];
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
            QC_LOG_ERROR( "QCVector::Back: vector is empty" );
            pOutValue = nullptr;
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            pOutValue = &m_storage[m_size - 1u];
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Find
    //
    // Seats the index of the first element equal to value into outIndex.
    // Returns QC_STATUS_NO_RESOURCE if no element matches; outIndex is then
    // left untouched.
    // Requires T to be equality-comparable.
    // ISO 26262:6 Table 8 §1b : outIndex is only valid on QC_STATUS_OK.
    // ------------------------------------------------------------------
    QCStatus_e Find( const T &value, uint32_t &outIndex ) const
    {
        QCStatus_e status = QC_STATUS_NO_RESOURCE;
        const auto begin = m_storage.cbegin();
        const auto end = begin + static_cast<typename ContainerType::difference_type>( m_size );
        const auto it = std::find( begin, end, value );

        if ( it != end )
        {
            outIndex = static_cast<uint32_t>( it - begin );
            status = QC_STATUS_OK;
        }

        if ( QC_STATUS_OK != status )
        {
            // Callers probe before inserting, so a miss is routine flow.
            QC_LOG_DEBUG( "QCVector::Find: value not found among %lu element(s)",
                          static_cast<unsigned long>( m_size ) );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Contains — pure query, cannot fail. Intentionally silent: it is called
    // per frame and has no failure to report. Requires T to be
    // equality-comparable.
    // ------------------------------------------------------------------
    bool Contains( const T &value ) const
    {
        const auto begin = m_storage.cbegin();
        const auto end = begin + static_cast<typename ContainerType::difference_type>( m_size );
        return std::find( begin, end, value ) != end;
    }

    // ------------------------------------------------------------------
    // Resize
    //
    // Changes the element count to newSize. Growth fills the added slots with
    // fill; shrinking resets the dropped slots to a default-constructed T.
    // Returns QC_STATUS_NO_RESOURCE if newSize > N.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Resize( uint32_t newSize, const T &fill = T{} )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( newSize > N )
        {
            QC_LOG_ERROR( "QCVector::Resize: requested size %lu exceeds capacity %lu",
                          static_cast<unsigned long>( newSize ), static_cast<unsigned long>( N ) );
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            if ( newSize > m_size )
            {
                std::fill( m_storage.begin() +
                                   static_cast<typename ContainerType::difference_type>( m_size ),
                           m_storage.begin() +
                                   static_cast<typename ContainerType::difference_type>( newSize ),
                           fill );
            }
            else if ( newSize < m_size )
            {
                std::fill( m_storage.begin() +
                                   static_cast<typename ContainerType::difference_type>( newSize ),
                           m_storage.begin() +
                                   static_cast<typename ContainerType::difference_type>( m_size ),
                           T{} );
            }
            else
            {
            }

            m_size = newSize;
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
        QC_LOG_DEBUG( "QCVector::Clear: releasing %lu element(s)",
                      static_cast<unsigned long>( m_size ) );

        m_storage.fill( T{} );

        m_size = 0u;

        return QC_STATUS_OK;
    }

    // ------------------------------------------------------------------
    // Data
    //
    // Raw contiguous access to the live prefix [0, Size()). Valid only while
    // no resize occurs. Returns a pointer to storage even when the vector is
    // empty, in which case no element may be dereferenced.
    // ------------------------------------------------------------------
    T *Data() { return m_storage.data(); }
    const T *Data() const { return m_storage.data(); }

    // ------------------------------------------------------------------
    // Size / Empty / Full / Capacity
    // ------------------------------------------------------------------
    uint32_t Size() const { return m_size; }
    bool Empty() const { return 0u == m_size; }
    bool Full() const { return m_size >= N; }
    static uint32_t Capacity() { return N; }
    static uint32_t MaxSize() { return N; }

    // ------------------------------------------------------------------
    // Iteration
    //
    // Spans the live prefix [0, Size()) only, not all N slots. Lower-case
    // names are required by range-based for; this is a deliberate deviation
    // from the UpperCamelCase method convention, the same one QCMap makes.
    // ------------------------------------------------------------------
    Iterator begin() { return m_storage.data(); }
    Iterator end() { return m_storage.data() + m_size; }
    ConstIterator begin() const { return m_storage.data(); }
    ConstIterator end() const { return m_storage.data() + m_size; }
    ConstIterator cbegin() const { return m_storage.data(); }
    ConstIterator cend() const { return m_storage.data() + m_size; }

private:
    ContainerType m_storage{};
    uint32_t m_size = 0u;
};

}   // namespace QC

#endif   // QC_VECTOR_HPP
