// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_STRING_HPP
#define QC_STRING_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <functional>

#include "QC/Common/QCDefs.hpp"

namespace QC
{

// ---------------------------------------------------------------------------
// QCString<T>
//
// Fixed-capacity, status-returning string with NO runtime memory allocation.
// T is the maximum number of characters the string can hold (excluding the
// terminating null); storage is a compile-time-sized std::array held inline
// (stack/member storage only). Capacity can never grow past T — there is no
// heap fallback, so a request that would exceed T fails with
// QC_STATUS_NOMEM instead of allocating, growing, or truncating silently.
//
// Why no Reserve()/shrink_to_fit(): capacity is fixed at compile time (T)
// and never changes, so those operations would be no-ops by construction.
// They are intentionally omitted rather than kept as dead API surface.
//
// Raw-pointer aliasing contract: Append/Assign/Insert forward their input
// to std::memcpy/std::memmove internally; callers must not pass a pointer
// into *this's own storage (Data()/CStr()) as the source — same contract
// std::memcpy itself carries. Substr() guards against *this-aliasing
// explicitly (see below) because it is a common enough footgun to reject
// outright rather than leave as caller responsibility.
//
// Validation rules:
//   PushBack     — capacity must allow +1; back() must equal the char.
//   PopBack      — container must be non-empty.
//   Append       — capacity must allow +length; null-terminator invariant checked.
//   Assign       — capacity must allow length; null-terminator invariant checked.
//   RemoveAt     — index must be in range.
//   RemoveRange  — range [start,end) must be valid.
//   Clear        — container must be empty after the call.
//   At           — index must be in range; outValue valid only on QC_STATUS_OK.
//   Front/Back   — container must be non-empty; outValue valid only on OK.
//   Insert       — index must be <= size; capacity must allow +1.
//   Substr       — startIndex must be in range; outValue must not alias *this.
//   Find         — returns QC_STATUS_NO_RESOURCE if needle is not found.
//   Resize       — newSize must be <= T.
//   Swap         — self-swap short-circuits to a no-op.
//
// Bounded C-string scanning: the single-pointer convenience overloads of
// Append/Assign scan for the terminating null via BoundedLength(), which
// never reads past (T + 1) bytes. A caller-supplied buffer that lacks a
// null terminator within that range is treated as "too long" (QC_STATUS_NOMEM)
// rather than causing an out-of-bounds read.
//
// Thread safety (AUTOAISW-7376): NONE — this type carries no lock. It is a
// value type: copy/move are a flat byte copy of inline storage and every
// method is allocation-free. Concurrent access to the SAME instance must be
// serialised by the owner (a buffer descriptor's name is written by the
// producer before hand-off and only read afterwards, so no lock is needed on
// the callback path). An internal std::shared_mutex was removed because it
// (a) made the type non-trivially-copyable, which deleted the implicit
// copy/assign of every struct embedding it — including the whole
// QCBufferDescriptorBase family — and (b) put a lock acquisition on the
// per-frame path, which is a WCET cost Pattern F exists to avoid.
//
// No logger member (AUTOAISW-7376): this container carries no QC_DECLARE_LOGGER
// and emits no QC_ERROR. Logger::Init() allocates on the heap twice per object
// (DecideLoggerLevel's "<name>_QC_LOG_LEVEL" concat + DefaultCreate's context
// new), which would make simply *constructing* a QCString allocate — defeating
// the purpose on the operational path where these are built per callback.
// Every failure is already reported through the QCStatus_e return, so the
// diagnostics were redundant; the caller logs if it wants to.
//
// ISO 26262:6 Table 8 §1a : single return statement per function.
// ISO 26262:6 Table 8 §1b : At()/Substr()/Find() return non-OK when the
//                            request cannot be satisfied — outValue/outIndex
//                            are only valid on QC_STATUS_OK.
// MISRA C++:2008 Rule 11-0-1: all data members private.
// MISRA C++:2008 Rule 8-5-1 : all members explicitly value-initialised.
// ---------------------------------------------------------------------------
template<std::size_t T>
class QCString
{
    static_assert( T > 0u, "QCString capacity T must be greater than zero" );

public:
    using ContainerType = std::array<char, T + 1u>;

    QCString() noexcept = default;

    ~QCString() = default;

    // Copy/move are memberwise over inline storage (std::array<char, T+1> +
    // std::size_t), so the compiler-generated versions are a flat byte copy —
    // no allocation, cannot throw. They are defaulted rather than deleted so
    // that a struct holding a QCString (e.g. QCBufferDescriptorBase) keeps its
    // own implicit copy/assign instead of having them deleted by contagion.
    // Move is a copy: there is no heap buffer to steal, and leaving the source
    // intact is cheaper than clearing it.
    QCString( const QCString & ) noexcept = default;
    QCString &operator=( const QCString & ) noexcept = default;
    QCString( QCString && ) noexcept = default;
    QCString &operator=( QCString && ) noexcept = default;

    // ------------------------------------------------------------------
    // PushBack
    //
    // Appends a single character to the back of the string.
    // Returns QC_STATUS_NOMEM if the string is already at capacity T.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e PushBack( char ch )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size >= T )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            m_buffer[m_size] = ch;
            ++m_size;
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size - 1u] != ch || m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // PopBack
    //
    // Removes the last character.
    // Returns QC_STATUS_BAD_STATE if the string is empty.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e PopBack()
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size == 0u )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            --m_size;
            m_buffer[m_size] = '\0';
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Append (explicit length)
    //
    // Appends the first length bytes of data to the back of the string.
    // Returns QC_STATUS_NULL_PTR if data is nullptr.
    // Returns QC_STATUS_NOMEM if the result would exceed capacity T.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Append( const char *data, uint32_t length )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( data == nullptr )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else if ( m_size + static_cast<std::size_t>( length ) > T )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            std::memcpy( &m_buffer[m_size], data, length );
            m_size += static_cast<std::size_t>( length );
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Append (bounded C string)
    //
    // Convenience overload — length is derived via a scan bounded to
    // (T + 1) bytes, so a missing null terminator cannot cause an
    // out-of-bounds read.
    // ------------------------------------------------------------------
    QCStatus_e Append( const char *cstr )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( cstr == nullptr )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else
        {
            const std::size_t length = BoundedLength( cstr );

            if ( m_size + length > T )
            {
                status = QC_STATUS_NOMEM;
            }
            else
            {
                std::memcpy( &m_buffer[m_size], cstr, length );
                m_size += length;
                m_buffer[m_size] = '\0';
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Assign (explicit length)
    //
    // Replaces the entire content with the first length bytes of data.
    // Returns QC_STATUS_NULL_PTR if data is nullptr.
    // Returns QC_STATUS_NOMEM if length exceeds capacity T.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Assign( const char *data, uint32_t length )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( data == nullptr )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else if ( static_cast<std::size_t>( length ) > T )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            std::memcpy( m_buffer.data(), data, length );
            m_size = static_cast<std::size_t>( length );
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Assign (bounded C string)
    //
    // Convenience overload — see Append(const char*) for the bounded-scan
    // rationale.
    // ------------------------------------------------------------------
    QCStatus_e Assign( const char *cstr )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( cstr == nullptr )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else
        {
            const std::size_t length = BoundedLength( cstr );

            if ( length > T )
            {
                status = QC_STATUS_NOMEM;
            }
            else
            {
                std::memcpy( m_buffer.data(), cstr, length );
                m_size = length;
                m_buffer[m_size] = '\0';
            }
        }

        return status;
    }

    QCString &operator=( const char *cstr ) noexcept = delete;

    // ------------------------------------------------------------------
    // RemoveAt
    //
    // Erases the character at the given index.
    // Returns QC_STATUS_BAD_ARGUMENTS if index is out of range.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e RemoveAt( uint32_t index )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<std::size_t>( index ) >= m_size )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            const std::size_t tailCount = m_size - static_cast<std::size_t>( index ) - 1u;

            std::memmove( &m_buffer[index], &m_buffer[index + 1u], tailCount );
            --m_size;
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // RemoveRange
    //
    // Erases all characters in the half-open range [startIndex, endIndex).
    // Returns QC_STATUS_BAD_ARGUMENTS if the range is invalid or out of bounds.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e RemoveRange( uint32_t startIndex, uint32_t endIndex )
    {
        QCStatus_e status = QC_STATUS_OK;
        const std::size_t start = static_cast<std::size_t>( startIndex );
        const std::size_t end = static_cast<std::size_t>( endIndex );

        if ( start >= end || end > T || end > m_size )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            const std::size_t countToErase = end - start;
            const std::size_t tailCount = m_size - end;

            for ( std::size_t index = 0u; index < tailCount; index++ )
            {
                m_buffer[start + index] = m_buffer[end + index];
            }
            m_size -= countToErase;
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Clear
    //
    // Removes all characters.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Clear()
    {
        QCStatus_e status = QC_STATUS_OK;

        m_size = 0u;
        m_buffer[0] = '\0';

        if ( m_size != 0u || m_buffer[0] != '\0' )
        {
            status = QC_STATUS_FAIL;
        }

        return status;
    }

    // ------------------------------------------------------------------
    // At
    //
    // Seats a reference to the character at index into outValue.
    // outValue is only valid when QC_STATUS_OK is returned.
    // Returns QC_STATUS_NO_RESOURCE if index is out of range.
    // ISO 26262:6 Table 8 §1b.
    // ------------------------------------------------------------------
    QCStatus_e At( uint32_t index, std::reference_wrapper<char> &outValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<std::size_t>( index ) >= m_size )
        {
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            outValue = std::ref( m_buffer[index] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // At (const overload)
    // ------------------------------------------------------------------
    QCStatus_e At( uint32_t index, std::reference_wrapper<const char> &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<std::size_t>( index ) >= m_size )
        {
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            outValue = std::cref( m_buffer[index] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Size
    // ------------------------------------------------------------------
    uint32_t Size() const { return static_cast<uint32_t>( m_size ); }

    // ------------------------------------------------------------------
    // IsEmpty
    // ------------------------------------------------------------------
    bool IsEmpty() const { return m_size == 0u; }

    // ------------------------------------------------------------------
    // Front (mutable)
    //
    // Returns a reference to the first character via outValue.
    // outValue is only valid when QC_STATUS_OK is returned.
    // Returns QC_STATUS_BAD_STATE if the container is empty.
    // ------------------------------------------------------------------
    QCStatus_e Front( std::reference_wrapper<char> &outValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size == 0u )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = std::ref( m_buffer[0] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Front (const overload)
    // ------------------------------------------------------------------
    QCStatus_e Front( std::reference_wrapper<const char> &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size == 0u )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = std::cref( m_buffer[0] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Back (mutable)
    //
    // Returns a reference to the last character via outValue.
    // outValue is only valid when QC_STATUS_OK is returned.
    // Returns QC_STATUS_BAD_STATE if the container is empty.
    // ------------------------------------------------------------------
    QCStatus_e Back( std::reference_wrapper<char> &outValue )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size == 0u )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = std::ref( m_buffer[m_size - 1u] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Back (const overload)
    // ------------------------------------------------------------------
    QCStatus_e Back( std::reference_wrapper<const char> &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( m_size == 0u )
        {
            status = QC_STATUS_BAD_STATE;
        }
        else
        {
            outValue = std::cref( m_buffer[m_size - 1u] );
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Insert
    //
    // Inserts ch before the character at the given index.
    // An index equal to Size() appends at the back.
    // Returns QC_STATUS_BAD_ARGUMENTS if index > size.
    // Returns QC_STATUS_NOMEM if the string is already at capacity T.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Insert( uint32_t index, char ch )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<std::size_t>( index ) > m_size )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( m_size >= T )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            const std::size_t tailCount = m_size - static_cast<std::size_t>( index );

            std::memmove( &m_buffer[index + 1u], &m_buffer[index], tailCount );
            m_buffer[index] = ch;
            ++m_size;
            m_buffer[m_size] = '\0';

            if ( m_buffer[index] != ch || m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Substr
    //
    // Copies the substring [startIndex, startIndex + count) into outValue.
    // count is clamped to the remaining length, matching std::string::substr.
    // outValue must not alias *this (rejected outright — an overlapping
    // self-substr would need a temporary, which this class cannot allocate).
    // Returns QC_STATUS_BAD_ARGUMENTS if startIndex > size, or if outValue
    // aliases *this.
    // ------------------------------------------------------------------
    template<std::size_t U>
    QCStatus_e Substr( uint32_t startIndex, uint32_t count, QCString<U> &outValue ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<const void *>( &outValue ) == static_cast<const void *>( this ) )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {

            if ( static_cast<std::size_t>( startIndex ) > m_size )
            {
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else
            {
                const std::size_t available = m_size - static_cast<std::size_t>( startIndex );
                const std::size_t copyLength =
                        std::min( static_cast<std::size_t>( count ), available );

                status = outValue.Assign( &m_buffer[startIndex],
                                          static_cast<uint32_t>( copyLength ) );
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Find
    //
    // Seats the index of the first occurrence of needle (searching from
    // fromIndex) into outIndex. Performs a bounded memcmp-based search —
    // no temporary strings are constructed.
    // outIndex is only valid when QC_STATUS_OK is returned.
    // Returns QC_STATUS_NULL_PTR if needle is nullptr.
    // Returns QC_STATUS_NO_RESOURCE if needle is not found.
    // ------------------------------------------------------------------
    QCStatus_e Find( const char *needle, uint32_t needleLength, uint32_t fromIndex,
                     uint32_t &outIndex ) const
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( needle == nullptr )
        {
            status = QC_STATUS_NULL_PTR;
        }
        else if ( static_cast<std::size_t>( fromIndex ) > m_size ||
                  static_cast<std::size_t>( needleLength ) > m_size )
        {
            status = QC_STATUS_NO_RESOURCE;
        }
        else
        {
            bool found = false;

            for ( std::size_t i = fromIndex;
                  ( i + static_cast<std::size_t>( needleLength ) ) <= m_size && !found; ++i )
            {
                if ( std::memcmp( &m_buffer[i], needle, needleLength ) == 0 )
                {
                    outIndex = static_cast<uint32_t>( i );
                    found = true;
                }
            }

            if ( !found )
            {
                status = QC_STATUS_NO_RESOURCE;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Compare
    //
    // Pure query — cannot fail. Mirrors std::string::compare's tri-state
    // result (< 0, 0, > 0). Both buffers are always null-terminated by
    // construction, so strcmp cannot read out of bounds.
    // Self-comparison still short-circuits to 0 — strcmp of a buffer
    // against itself is 0 anyway, so this only skips the redundant work.
    // ------------------------------------------------------------------
    template<std::size_t U>
    int Compare( const QCString<U> &other ) const
    {
        int result = 0;

        if ( static_cast<const void *>( &other ) != static_cast<const void *>( this ) )
        {
            result = std::strcmp( m_buffer.data(), other.m_buffer.data() );
        }

        return result;
    }

    // ------------------------------------------------------------------
    // operator== / operator!=
    //
    // Pure queries built on Compare() — cannot fail, allocate nothing.
    // Provided so that call sites (and gtest's ASSERT_EQ) can compare two
    // names directly, as they could when this field was a std::string.
    // ------------------------------------------------------------------
    template<std::size_t U>
    bool operator==( const QCString<U> &other ) const
    {
        return Compare( other ) == 0;
    }

    template<std::size_t U>
    bool operator!=( const QCString<U> &other ) const
    {
        return Compare( other ) != 0;
    }

    bool operator==( const char *cstr ) const
    {
        return ( cstr != nullptr ) && ( std::strcmp( m_buffer.data(), cstr ) == 0 );
    }

    bool operator!=( const char *cstr ) const { return !( *this == cstr ); }

    // ------------------------------------------------------------------
    // StartsWith / EndsWith / Contains
    //
    // Pure queries — cannot fail (a null pointer or out-of-range length
    // simply yields false). Bounded memcmp-based checks; no allocation.
    // ------------------------------------------------------------------
    bool StartsWith( const char *prefix, uint32_t length ) const
    {
        return ( prefix != nullptr ) && ( static_cast<std::size_t>( length ) <= m_size ) &&
               ( std::memcmp( m_buffer.data(), prefix, length ) == 0 );
    }

    bool EndsWith( const char *suffix, uint32_t length ) const
    {
        return ( suffix != nullptr ) && ( static_cast<std::size_t>( length ) <= m_size ) &&
               ( std::memcmp( &m_buffer[m_size - static_cast<std::size_t>( length )], suffix,
                              length ) == 0 );
    }

    bool Contains( const char *needle, uint32_t length ) const
    {
        bool found = false;

        if ( needle != nullptr && static_cast<std::size_t>( length ) <= m_size )
        {
            for ( std::size_t i = 0u;
                  ( i + static_cast<std::size_t>( length ) ) <= m_size && !found; ++i )
            {
                if ( std::memcmp( &m_buffer[i], needle, length ) == 0 )
                {
                    found = true;
                }
            }
        }

        return found;
    }

    // ------------------------------------------------------------------
    // Resize
    //
    // Resizes the string to newSize characters, filling any newly exposed
    // characters with fill.
    // Returns QC_STATUS_NOMEM if newSize exceeds capacity T.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Resize( uint32_t newSize, char fill = '\0' )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( static_cast<std::size_t>( newSize ) > T )
        {
            status = QC_STATUS_NOMEM;
        }
        else
        {
            if ( static_cast<std::size_t>( newSize ) > m_size )
            {
                std::fill( m_buffer.begin() + static_cast<std::ptrdiff_t>( m_size ),
                           m_buffer.begin() + static_cast<std::ptrdiff_t>( newSize ), fill );
            }

            m_size = static_cast<std::size_t>( newSize );
            m_buffer[m_size] = '\0';

            if ( m_buffer[m_size] != '\0' )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // Swap
    //
    // Exchanges the contents of this string with other (same capacity T).
    // Element-wise exchange over fixed storage — no allocation involved.
    // Self-swap short-circuits to a no-op — swapping a buffer with itself
    // is a no-op anyway, so this only skips the redundant work.
    // ISO 26262:6 Table 8 §1a : single return statement at end of function.
    // ------------------------------------------------------------------
    QCStatus_e Swap( QCString<T> &other )
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( &other != this )
        {
            const std::size_t thisSize = m_size;
            const std::size_t otherSize = other.m_size;

            m_buffer.swap( other.m_buffer );
            std::swap( m_size, other.m_size );

            if ( m_size != otherSize || other.m_size != thisSize )
            {
                status = QC_STATUS_FAIL;
            }
        }

        return status;
    }

    // ------------------------------------------------------------------
    // MaxSize / Capacity
    //
    // Both always equal T — capacity is fixed at compile time and never
    // changes.
    // ------------------------------------------------------------------
    uint32_t MaxSize() const { return static_cast<uint32_t>( T ); }

    uint32_t Capacity() const { return static_cast<uint32_t>( T ); }

    // ------------------------------------------------------------------
    // Data (mutable)
    //
    // Returns a raw pointer to the underlying null-terminated storage.
    // ------------------------------------------------------------------
    char *Data() { return m_buffer.data(); }

    // ------------------------------------------------------------------
    // Data (const overload)
    // ------------------------------------------------------------------
    const char *Data() const { return m_buffer.data(); }

    // ------------------------------------------------------------------
    // CStr
    //
    // Returns a raw pointer to the underlying null-terminated storage.
    // Equivalent to Data() const — provided for callers that want the
    // conventional c_str() name at call sites taking a C string.
    // ------------------------------------------------------------------
    const char *CStr() const { return m_buffer.data(); }

private:
    template<std::size_t U>
    friend class QCString;

    // Bounded scan for a terminating null — never reads past (T + 1)
    // bytes, so a missing null terminator on caller input can never
    // cause an out-of-bounds read. A return value of (T + 1) signals
    // "no terminator found within the capacity window" (i.e. overflow).
    static std::size_t BoundedLength( const char *cstr )
    {
        std::size_t length = 0u;

        while ( length <= T && cstr[length] != '\0' )
        {
            ++length;
        }

        return length;
    }

    // MISRA C++:2008 Rule 8-5-1: all members explicitly value-initialised
    ContainerType m_buffer = {};
    std::size_t m_size = 0u;
};

}   // namespace QC

#endif   // QC_STRING_HPP
