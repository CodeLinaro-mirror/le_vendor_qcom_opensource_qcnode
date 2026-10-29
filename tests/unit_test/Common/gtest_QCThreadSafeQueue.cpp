// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include <atomic>
#include <thread>
#include <vector>

#include "QC/Common/QCThreadSafeQueue.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCThreadSafeQueue<T, N> unit tests.
//
// The wrapper delegates to QCQueue, so the single-threaded cases here check
// that delegation and the deliberately reduced API surface (copy-out only, no
// pointer accessors, no element-discarding Pop) rather than re-testing the
// FIFO semantics covered by gtest_QCQueue. The concurrency cases check that
// every element crosses the queue exactly once under contention.
// ---------------------------------------------------------------------------

TEST( QCThreadSafeQueue, Sanity_EmptyOnConstruction )
{
    QCThreadSafeQueue<int, 4> queue;

    EXPECT_TRUE( queue.Empty() );
    EXPECT_FALSE( queue.Full() );
    EXPECT_EQ( 0u, queue.Size() );
    EXPECT_EQ( 4u, queue.Capacity() );
    EXPECT_EQ( 4u, queue.MaxSize() );
}

TEST( QCThreadSafeQueue, PushPop_PreservesFifoOrder )
{
    QCThreadSafeQueue<int, 4> queue;

    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
    }
    EXPECT_TRUE( queue.Full() );

    for ( int i = 0; i < 4; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( i, value );
    }
    EXPECT_TRUE( queue.Empty() );
}

TEST( QCThreadSafeQueue, Push_AtCapacityReturnsNoResource )
{
    QCThreadSafeQueue<int, 2> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 2 ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, queue.Push( 3 ) );
    EXPECT_EQ( 2u, queue.Size() );
}

TEST( QCThreadSafeQueue, Pop_OnEmptyReturnsBadStateAndLeavesOutValue )
{
    QCThreadSafeQueue<int, 4> queue;

    int value = -99;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Pop( value ) );
    EXPECT_EQ( -99, value );
}

TEST( QCThreadSafeQueue, GetFrontGetBack_CopyOutWithoutConsuming )
{
    QCThreadSafeQueue<int, 4> queue;

    int value = 0;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.GetFront( value ) );
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.GetBack( value ) );

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 5 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 6 ) );

    EXPECT_EQ( QC_STATUS_OK, queue.GetFront( value ) );
    EXPECT_EQ( 5, value );
    EXPECT_EQ( QC_STATUS_OK, queue.GetBack( value ) );
    EXPECT_EQ( 6, value );
    EXPECT_EQ( 2u, queue.Size() );
}

TEST( QCThreadSafeQueue, Clear_EmptiesQueue )
{
    QCThreadSafeQueue<int, 4> queue;
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 2 ) );

    EXPECT_EQ( QC_STATUS_OK, queue.Clear() );
    EXPECT_TRUE( queue.Empty() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Pop( value ) );

    // Reusable after Clear.
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 7 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    EXPECT_EQ( 7, value );
}

TEST( QCThreadSafeQueue, Concurrent_ProducersAndConsumersTransferEachElementOnce )
{
    // Four producers and four consumers share a queue far smaller than the
    // total traffic, so Push() back-pressure and Pop() starvation are both
    // exercised. Every value must be delivered exactly once: a lost or
    // duplicated element indicates a broken critical section.
    constexpr int ProducerCount = 4;
    constexpr int PerProducer = 2000;
    constexpr int TotalCount = ProducerCount * PerProducer;

    QCThreadSafeQueue<int, 16> queue;
    std::atomic<int> popped( 0 );
    std::vector<std::atomic<int>> seen( TotalCount );
    for ( int i = 0; i < TotalCount; ++i )
    {
        seen[i].store( 0 );
    }

    std::vector<std::thread> threads;
    for ( int p = 0; p < ProducerCount; ++p )
    {
        threads.emplace_back( [&queue, p]() {
            for ( int i = 0; i < PerProducer; ++i )
            {
                const int value = ( p * PerProducer ) + i;
                // NO_RESOURCE is expected back-pressure, not an error: retry.
                while ( QC_STATUS_OK != queue.Push( value ) )
                {
                    std::this_thread::yield();
                }
            }
        } );
    }
    for ( int c = 0; c < ProducerCount; ++c )
    {
        threads.emplace_back( [&queue, &popped, &seen]() {
            while ( popped.load() < TotalCount )
            {
                int value = -1;
                const QCStatus_e status = queue.Pop( value );
                if ( QC_STATUS_OK == status )
                {
                    seen[value].fetch_add( 1 );
                    popped.fetch_add( 1 );
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }

    EXPECT_EQ( TotalCount, popped.load() );
    EXPECT_TRUE( queue.Empty() );
    for ( int i = 0; i < TotalCount; ++i )
    {
        EXPECT_EQ( 1, seen[i].load() ) << "value " << i << " not transferred exactly once";
    }
}

TEST( QCThreadSafeQueue, Concurrent_CapacityIsNeverExceeded )
{
    // Many more pushes than capacity, with no consumer: exactly N must be
    // accepted and the rest rejected, however the threads interleave.
    constexpr uint32_t Capacity = 8u;
    QCThreadSafeQueue<int, Capacity> queue;
    std::atomic<int> accepted( 0 );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( [&queue, &accepted]() {
            for ( int i = 0; i < 100; ++i )
            {
                if ( QC_STATUS_OK == queue.Push( i ) )
                {
                    accepted.fetch_add( 1 );
                }
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }

    EXPECT_EQ( static_cast<int>( Capacity ), accepted.load() );
    EXPECT_EQ( Capacity, queue.Size() );
    EXPECT_TRUE( queue.Full() );
}

TEST( QCThreadSafeQueue, Concurrent_ReadersDoNotObserveInvalidState )
{
    // Readers run concurrently with a writer; a torn read would surface as a
    // size above capacity or a successful copy-out of an out-of-range value.
    QCThreadSafeQueue<int, 8> queue;
    std::atomic<bool> bStop( false );
    std::atomic<int> badObservation( 0 );

    std::vector<std::thread> threads;
    threads.emplace_back( [&queue, &bStop]() {
        for ( int i = 0; i < 5000; ++i )
        {
            (void) queue.Push( 1 );
            int value = 0;
            (void) queue.Pop( value );
        }
        bStop.store( true );
    } );
    for ( int r = 0; r < 2; ++r )
    {
        threads.emplace_back( [&queue, &bStop, &badObservation]() {
            while ( !bStop.load() )
            {
                if ( queue.Size() > queue.Capacity() )
                {
                    badObservation.fetch_add( 1 );
                }
                int value = 0;
                const QCStatus_e status = queue.GetFront( value );
                if ( ( QC_STATUS_OK == status ) && ( 1 != value ) )
                {
                    badObservation.fetch_add( 1 );
                }
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }

    EXPECT_EQ( 0, badObservation.load() );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
