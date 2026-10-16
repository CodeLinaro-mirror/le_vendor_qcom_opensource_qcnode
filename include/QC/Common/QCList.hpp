// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LIST_HPP
#define QC_LIST_HPP

#include "QC/Common/QCDefs.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace QC
{

/**
 * @brief A runtime-sized, allocation-free-after-initialization doubly linked list.
 *
 * Init() allocates the complete node array. All later list operations only relink nodes from that
 * array, so they do not allocate or release dynamic memory.
 */
template<typename T>
class QCList final
{
    static_assert( std::is_nothrow_destructible<T>::value,
                   "QCList T must be nothrow-destructible" );

private:
    static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
    struct Storage_t
    {
        alignas( T ) unsigned char data[sizeof( T )];
    };

    struct Node
    {
        Storage_t storage;
        uint32_t previous = kInvalidIndex;
        uint32_t next = kInvalidIndex;
        bool occupied = false;
    };

public:
    class Iterator;

    class ConstIterator
    {
    public:
        ConstIterator() = default;

        const T &operator*() const { return *m_list->ValueAt( m_index ); }
        const T *operator->() const { return m_list->ValueAt( m_index ); }

        ConstIterator &operator++()
        {
            if ( kInvalidIndex != m_index )
            {
                m_index = m_list->m_nodes[m_index].next;
            }
            return *this;
        }

        bool operator==( const ConstIterator &rhs ) const
        {
            return ( m_list == rhs.m_list ) && ( m_index == rhs.m_index );
        }

        bool operator!=( const ConstIterator &rhs ) const { return !( *this == rhs ); }

    private:
        friend class QCList;
        friend class Iterator;

        ConstIterator( const QCList *list, uint32_t index )
            : m_list( list ),
              m_index( index )
        {}

        const QCList *m_list = nullptr;
        uint32_t m_index = kInvalidIndex;
    };

    class Iterator
    {
    public:
        Iterator() = default;

        T &operator*() const { return *m_list->ValueAt( m_index ); }
        T *operator->() const { return m_list->ValueAt( m_index ); }

        Iterator &operator++()
        {
            if ( kInvalidIndex != m_index )
            {
                m_index = m_list->m_nodes[m_index].next;
            }
            return *this;
        }

        bool operator==( const Iterator &rhs ) const
        {
            return ( m_list == rhs.m_list ) && ( m_index == rhs.m_index );
        }

        bool operator!=( const Iterator &rhs ) const { return !( *this == rhs ); }

        operator ConstIterator() const { return ConstIterator( m_list, m_index ); }

    private:
        friend class QCList;

        Iterator( QCList *list, uint32_t index ) : m_list( list ), m_index( index ) {}

        QCList *m_list = nullptr;
        uint32_t m_index = kInvalidIndex;
    };

    QCList() = default;

    ~QCList()
    {
        ClearInternal();
    }

    QCList( const QCList & ) = delete;
    QCList &operator=( const QCList & ) = delete;
    QCList( QCList && ) = delete;
    QCList &operator=( QCList && ) = delete;

    /** @brief Allocates the fixed node storage used for the list lifetime. */
    QCStatus_e Init( uint32_t capacity ) noexcept
    {
        QCStatus_e status = QC_STATUS_OK;

        if ( 0U == capacity )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( nullptr != m_nodes )
        {
            status = QC_STATUS_ALREADY;
        }
        else
        {
            std::unique_ptr<Node[]> nodes( new ( std::nothrow ) Node[capacity] );
            if ( nullptr == nodes )
            {
                status = QC_STATUS_NOMEM;
            }
            else
            {
                m_nodes = std::move( nodes );
                m_capacity = capacity;
                ResetFreeList();
            }
        }

        return status;
    }

    QCStatus_e Front( T *&value ) noexcept
    {
        value = nullptr;
        return GetValue( m_head, value );
    }

    QCStatus_e Front( const T *&value ) const noexcept
    {
        value = nullptr;
        return GetValue( m_head, value );
    }

    QCStatus_e Back( T *&value ) noexcept
    {
        value = nullptr;
        return GetValue( m_tail, value );
    }

    QCStatus_e Back( const T *&value ) const noexcept
    {
        value = nullptr;
        return GetValue( m_tail, value );
    }

    QCStatus_e Empty( bool &result ) const noexcept
    {
        result = true;
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = ( 0U == m_size );
        return QC_STATUS_OK;
    }

    QCStatus_e Size( uint32_t &result ) const noexcept
    {
        result = 0U;
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = m_size;
        return QC_STATUS_OK;
    }

    QCStatus_e MaxSize( uint32_t &result ) const noexcept
    {
        result = 0U;
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = m_capacity;
        return QC_STATUS_OK;
    }

    QCStatus_e PushFront( const T &value ) noexcept
    {
        Iterator inserted;
        return EmplaceFront( inserted, value );
    }

    QCStatus_e PushBack( const T &value ) noexcept
    {
        Iterator inserted;
        return EmplaceBack( inserted, value );
    }

    template<typename... Args>
    QCStatus_e EmplaceFront( Iterator &inserted, Args &&... args ) noexcept
    {
        return EmplaceBefore( m_head, inserted, std::forward<Args>( args )... );
    }

    template<typename... Args>
    QCStatus_e EmplaceBack( Iterator &inserted, Args &&... args ) noexcept
    {
        return EmplaceBefore( kInvalidIndex, inserted, std::forward<Args>( args )... );
    }

    QCStatus_e Insert( ConstIterator position, const T &value, Iterator &inserted ) noexcept
    {
        return Emplace( position, inserted, value );
    }

    template<typename... Args>
    QCStatus_e Emplace( ConstIterator position, Iterator &inserted, Args &&... args ) noexcept
    {
        inserted = Iterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( ( position.m_list != this ) ||
             ( ( kInvalidIndex != position.m_index ) && !IsOccupied( position.m_index ) ) )
        {
            return QC_STATUS_BAD_ARGUMENTS;
        }
        return EmplaceBefore( position.m_index, inserted, std::forward<Args>( args )... );
    }

    QCStatus_e PopFront() noexcept
    {
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( kInvalidIndex == m_head )
        {
            return QC_STATUS_BAD_STATE;
        }
        EraseIndex( m_head );
        return QC_STATUS_OK;
    }

    QCStatus_e PopBack() noexcept
    {
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( kInvalidIndex == m_tail )
        {
            return QC_STATUS_BAD_STATE;
        }
        EraseIndex( m_tail );
        return QC_STATUS_OK;
    }

    QCStatus_e Erase( ConstIterator position, Iterator &next ) noexcept
    {
        next = Iterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( ( position.m_list != this ) || !IsOccupied( position.m_index ) )
        {
            return QC_STATUS_BAD_ARGUMENTS;
        }

        const uint32_t nextIndex = m_nodes[position.m_index].next;
        EraseIndex( position.m_index );
        next = Iterator( this, nextIndex );
        return QC_STATUS_OK;
    }

    QCStatus_e Clear() noexcept
    {
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        ClearInternal();
        return QC_STATUS_OK;
    }

    QCStatus_e Remove( const T &value, uint32_t &removed ) noexcept
    {
        static_assert( noexcept( std::declval<const T &>() == std::declval<const T &>() ),
                       "QCList T equality comparison must not throw" );

        removed = 0U;
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }

        uint32_t nodeIndex = m_head;
        while ( kInvalidIndex != nodeIndex )
        {
            const uint32_t nextIndex = m_nodes[nodeIndex].next;
            if ( *ValueAt( nodeIndex ) == value )
            {
                EraseIndex( nodeIndex );
                ++removed;
            }
            nodeIndex = nextIndex;
        }
        return QC_STATUS_OK;
    }

    QCStatus_e Begin( Iterator &result ) noexcept
    {
        result = Iterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = Iterator( this, m_head );
        return QC_STATUS_OK;
    }

    QCStatus_e End( Iterator &result ) noexcept
    {
        result = Iterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = Iterator( this, kInvalidIndex );
        return QC_STATUS_OK;
    }

    QCStatus_e Begin( ConstIterator &result ) const noexcept
    {
        result = ConstIterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = ConstIterator( this, m_head );
        return QC_STATUS_OK;
    }

    QCStatus_e End( ConstIterator &result ) const noexcept
    {
        result = ConstIterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        result = ConstIterator( this, kInvalidIndex );
        return QC_STATUS_OK;
    }

private:
    template<typename... Args>
    QCStatus_e EmplaceBefore( uint32_t nextIndex, Iterator &inserted, Args &&... args ) noexcept
    {
        static_assert( std::is_nothrow_constructible<T, Args...>::value,
                       "QCList element construction must not throw" );

        inserted = Iterator();
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( kInvalidIndex == m_freeHead )
        {
            return QC_STATUS_NO_RESOURCE;
        }

        const uint32_t nodeIndex = TakeFreeNode();
        Node &node = m_nodes[nodeIndex];
        new ( node.storage.data ) T( std::forward<Args>( args )... );
        node.occupied = true;
        InsertBefore( nodeIndex, nextIndex );
        ++m_size;
        inserted = Iterator( this, nodeIndex );
        return QC_STATUS_OK;
    }

    QCStatus_e GetValue( uint32_t nodeIndex, T *&value ) noexcept
    {
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( kInvalidIndex == nodeIndex )
        {
            return QC_STATUS_BAD_STATE;
        }
        value = ValueAt( nodeIndex );
        return QC_STATUS_OK;
    }

    QCStatus_e GetValue( uint32_t nodeIndex, const T *&value ) const noexcept
    {
        if ( nullptr == m_nodes )
        {
            return QC_STATUS_BAD_STATE;
        }
        if ( kInvalidIndex == nodeIndex )
        {
            return QC_STATUS_BAD_STATE;
        }
        value = ValueAt( nodeIndex );
        return QC_STATUS_OK;
    }

    bool IsOccupied( uint32_t nodeIndex ) const noexcept
    {
        return ( nodeIndex < m_capacity ) && m_nodes[nodeIndex].occupied;
    }

    T *ValueAt( uint32_t nodeIndex ) noexcept
    {
        return reinterpret_cast<T *>( m_nodes[nodeIndex].storage.data );
    }

    const T *ValueAt( uint32_t nodeIndex ) const noexcept
    {
        return reinterpret_cast<const T *>( m_nodes[nodeIndex].storage.data );
    }

    void ResetFreeList() noexcept
    {
        for ( uint32_t index = 0U; index < m_capacity; ++index )
        {
            m_nodes[index].previous = kInvalidIndex;
            m_nodes[index].next = ( index + 1U < m_capacity ) ? ( index + 1U ) : kInvalidIndex;
            m_nodes[index].occupied = false;
        }
        m_head = kInvalidIndex;
        m_tail = kInvalidIndex;
        m_freeHead = 0U;
        m_size = 0U;
    }

    uint32_t TakeFreeNode() noexcept
    {
        const uint32_t nodeIndex = m_freeHead;
        m_freeHead = m_nodes[nodeIndex].next;
        m_nodes[nodeIndex].previous = kInvalidIndex;
        m_nodes[nodeIndex].next = kInvalidIndex;
        return nodeIndex;
    }

    void ReleaseNode( uint32_t nodeIndex ) noexcept
    {
        Node &node = m_nodes[nodeIndex];
        ValueAt( nodeIndex )->~T();
        node.occupied = false;
        node.previous = kInvalidIndex;
        node.next = m_freeHead;
        m_freeHead = nodeIndex;
    }

    void InsertBefore( uint32_t nodeIndex, uint32_t nextIndex ) noexcept
    {
        const uint32_t previousIndex =
                ( kInvalidIndex == nextIndex ) ? m_tail : m_nodes[nextIndex].previous;
        Node &node = m_nodes[nodeIndex];
        node.previous = previousIndex;
        node.next = nextIndex;

        if ( kInvalidIndex == previousIndex )
        {
            m_head = nodeIndex;
        }
        else
        {
            m_nodes[previousIndex].next = nodeIndex;
        }

        if ( kInvalidIndex == nextIndex )
        {
            m_tail = nodeIndex;
        }
        else
        {
            m_nodes[nextIndex].previous = nodeIndex;
        }
    }

    void EraseIndex( uint32_t nodeIndex ) noexcept
    {
        Node &node = m_nodes[nodeIndex];
        if ( kInvalidIndex == node.previous )
        {
            m_head = node.next;
        }
        else
        {
            m_nodes[node.previous].next = node.next;
        }

        if ( kInvalidIndex == node.next )
        {
            m_tail = node.previous;
        }
        else
        {
            m_nodes[node.next].previous = node.previous;
        }

        ReleaseNode( nodeIndex );
        --m_size;
    }

    void ClearInternal() noexcept
    {
        if ( nullptr != m_nodes )
        {
            while ( kInvalidIndex != m_head )
            {
                EraseIndex( m_head );
            }
        }
    }

    std::unique_ptr<Node[]> m_nodes;
    uint32_t m_capacity = 0U;
    uint32_t m_head = kInvalidIndex;
    uint32_t m_tail = kInvalidIndex;
    uint32_t m_freeHead = kInvalidIndex;
    uint32_t m_size = 0U;
};

}   // namespace QC

#endif   // QC_LIST_HPP
