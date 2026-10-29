// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_THREAD_SAFE_LIST_HPP
#define QC_THREAD_SAFE_LIST_HPP

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

#include "QC/Common/QCList.hpp"

namespace QC
{

// Self-locking wrapper around QCList. Reads copy values while holding a shared
// lock; pointers and iterators are deliberately not exposed because they could
// become invalid after the lock is released.
template<typename T>
class QCThreadSafeList
{
public:
    using ValueType = T;

    static_assert( std::is_nothrow_copy_assignable<T>::value,
                   "QCThreadSafeList T must be nothrow-copy-assignable" );

    QCThreadSafeList() = default;
    ~QCThreadSafeList() = default;

    QCThreadSafeList( const QCThreadSafeList & ) = delete;
    QCThreadSafeList &operator=( const QCThreadSafeList & ) = delete;
    QCThreadSafeList( QCThreadSafeList && ) = delete;
    QCThreadSafeList &operator=( QCThreadSafeList && ) = delete;

    QCStatus_e Init( uint32_t capacity )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_list.Init( capacity );
    }

    QCStatus_e PushBack( const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_list.PushBack( value );
    }

    QCStatus_e PopFront( T &outValue )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        T *value = nullptr;
        QCStatus_e status = m_list.Front( value );
        if ( QC_STATUS_OK == status )
        {
            outValue = *value;
            status = m_list.PopFront();
        }
        return status;
    }

    QCStatus_e RemoveFirst( const T &value )
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        typename QCList<T>::Iterator current;
        typename QCList<T>::Iterator end;
        QCStatus_e status = m_list.Begin( current );
        if ( QC_STATUS_OK == status )
        {
            status = m_list.End( end );
        }
        while ( ( QC_STATUS_OK == status ) && ( end != current ) )
        {
            if ( *current == value )
            {
                typename QCList<T>::Iterator next;
                status = m_list.Erase( current, next );
                return status;
            }
            ++current;
        }
        return ( QC_STATUS_OK == status ) ? QC_STATUS_NO_RESOURCE : status;
    }

    QCStatus_e Clear()
    {
        const std::unique_lock<std::shared_mutex> lock( m_mutex );
        return m_list.Clear();
    }

    QCStatus_e GetFront( T &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        const T *value = nullptr;
        const QCStatus_e status = m_list.Front( value );
        if ( QC_STATUS_OK == status )
        {
            outValue = *value;
        }
        return status;
    }

    QCStatus_e GetBack( T &outValue ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        const T *value = nullptr;
        const QCStatus_e status = m_list.Back( value );
        if ( QC_STATUS_OK == status )
        {
            outValue = *value;
        }
        return status;
    }

    bool Contains( const T &value ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        typename QCList<T>::ConstIterator current;
        typename QCList<T>::ConstIterator end;
        bool found = false;
        if ( QC_STATUS_OK == m_list.Begin( current ) )
        {
            (void) m_list.End( end );
            while ( end != current )
            {
                if ( *current == value )
                {
                    found = true;
                    break;
                }
                ++current;
            }
        }
        return found;
    }

    template<typename Callback>
    QCStatus_e ForEach( Callback callback ) const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );

        typename QCList<T>::ConstIterator current;
        typename QCList<T>::ConstIterator end;
        const QCStatus_e status = m_list.Begin( current );
        if ( QC_STATUS_OK != status )
        {
            return status;
        }
        (void) m_list.End( end );
        while ( end != current )
        {
            callback( *current );
            ++current;
        }

        return QC_STATUS_OK;
    }

    uint32_t Size() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        uint32_t size = 0U;
        (void) m_list.Size( size );
        return size;
    }

    bool Empty() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        bool empty = false;
        (void) m_list.Empty( empty );
        return empty;
    }

    bool Full() const
    {
        const std::shared_lock<std::shared_mutex> lock( m_mutex );
        uint32_t size = 0U;
        uint32_t maxSize = 0U;
        if ( QC_STATUS_OK != m_list.Size( size ) )
        {
            return false;
        }
        (void) m_list.MaxSize( maxSize );
        return size >= maxSize;
    }

private:
    mutable std::shared_mutex m_mutex;
    QCList<T> m_list;
};

}   // namespace QC

#endif   // QC_THREAD_SAFE_LIST_HPP
