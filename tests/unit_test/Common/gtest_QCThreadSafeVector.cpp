// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include <atomic>
#include <thread>
#include <vector>

#include "QC/Common/QCThreadSafeVector.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCThreadSafeVector<T, N> unit tests.
//
// The wrapper delegates to QCVector, so the single-threaded cases here check
// that delegation, the ForEach traversal, and the deliberately reduced API
// surface (copy-out only, no pointer accessors, no Data(), no iterators)
// rather than re-testing the sequence semantics covered by gtest_QCVector.
// The concurrency cases check that capacity holds and readers never observe a
// partially updated container under contention.
// ---------------------------------------------------------------------------

TEST( QCThreadSafeVector, Sanity_EmptyOnConstruction )
{
    QCThreadSafeVector<int, 4> vector;

    EXPECT_TRUE( vector.Empty() );
    EXPECT_FALSE( vector.Full() );
    EXPECT_EQ( 0u, vector.Size() );
    EXPECT_EQ( 4u, vector.Capacity() );
    EXPECT_EQ( 4u, vector.MaxSize() );
}

TEST( QCThreadSafeVector, PushBack_AppendsUntilCapacity )
{
    QCThreadSafeVector<int, 2> vector;

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );
    EXPECT_TRUE( vector.Full() );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.PushBack( 3 ) );
    EXPECT_EQ( 2u, vector.Size() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 1, value );
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 1u, value ) );
    EXPECT_EQ( 2, value );
}

TEST( QCThreadSafeVector, GetValue_RejectsOutOfRangeIndex )
{
    QCThreadSafeVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 5 ) );

    int value = -99;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.GetValue( 1u, value ) );
    EXPECT_EQ( -99, value );
}

TEST( QCThreadSafeVector, SetValue_OverwritesWithoutGrowing )
{
    QCThreadSafeVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.SetValue( 0u, 9 ) );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 9, value );

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.SetValue( 1u, 9 ) );
    EXPECT_EQ( 1u, vector.Size() );
}

TEST( QCThreadSafeVector, InsertRemoveAt_ShiftElements )
{
    QCThreadSafeVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.Insert( 0u, 1 ) );   // 1,2
    EXPECT_EQ( 2u, vector.Size() );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 1, value );

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.Insert( 9u, 3 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 0u ) );   // 2
    EXPECT_EQ( 1u, vector.Size() );
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 2, value );

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.RemoveAt( 1u ) );
}

TEST( QCThreadSafeVector, PopBack_CopiesOutAndReportsEmpty )
{
    QCThreadSafeVector<int, 4> vector;

    int value = -99;
    EXPECT_EQ( QC_STATUS_BAD_STATE, vector.PopBack( value ) );
    EXPECT_EQ( -99, value );

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 3 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack( value ) );
    EXPECT_EQ( 3, value );
    EXPECT_TRUE( vector.Empty() );
}

TEST( QCThreadSafeVector, FindContains_LocateLiveElements )
{
    QCThreadSafeVector<int, 8> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 10 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 20 ) );

    uint32_t index = 999u;
    EXPECT_EQ( QC_STATUS_OK, vector.Find( 20, index ) );
    EXPECT_EQ( 1u, index );

    index = 777u;
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Find( 30, index ) );
    EXPECT_EQ( 777u, index );

    EXPECT_TRUE( vector.Contains( 10 ) );
    EXPECT_FALSE( vector.Contains( 30 ) );
}

TEST( QCThreadSafeVector, Resize_GrowsAndShrinks )
{
    QCThreadSafeVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 3u, -5 ) );
    EXPECT_EQ( 3u, vector.Size() );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 2u, value ) );
    EXPECT_EQ( -5, value );

    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Resize( 5u ) );
    EXPECT_EQ( 3u, vector.Size() );

    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 1u ) );
    EXPECT_EQ( 1u, vector.Size() );
}

TEST( QCThreadSafeVector, Clear_EmptiesVector )
{
    QCThreadSafeVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.Clear() );
    EXPECT_TRUE( vector.Empty() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.GetValue( 0u, value ) );
}

TEST( QCThreadSafeVector, ForEach_VisitsEveryLiveElementInOrder )
{
    QCThreadSafeVector<int, 8> vector;
    for ( int i = 1; i <= 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );
    }

    // The callback must see exactly Size() elements, in order, not all N slots.
    std::vector<int> visited;
    EXPECT_EQ( QC_STATUS_OK, vector.ForEach( [&visited]( const int &element ) {
        visited.push_back( element );
    } ) );

    ASSERT_EQ( 3u, visited.size() );
    EXPECT_EQ( 1, visited[0] );
    EXPECT_EQ( 2, visited[1] );
    EXPECT_EQ( 3, visited[2] );
}

TEST( QCThreadSafeVector, ForEach_OnEmptyVectorInvokesNothing )
{
    QCThreadSafeVector<int, 4> vector;

    uint32_t count = 0u;
    EXPECT_EQ( QC_STATUS_OK, vector.ForEach( [&count]( const int & ) { ++count; } ) );
    EXPECT_EQ( 0u, count );
}

TEST( QCThreadSafeVector, Concurrent_CapacityIsNeverExceeded )
{
    // Far more pushes than capacity from several threads: exactly N must be
    // accepted, however the threads interleave.
    constexpr uint32_t Capacity = 256u;
    QCThreadSafeVector<int, Capacity> vector;
    std::atomic<int> accepted( 0 );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 4; ++t )
    {
        threads.emplace_back( [&vector, &accepted]() {
            for ( int i = 0; i < 500; ++i )
            {
                if ( QC_STATUS_OK == vector.PushBack( i ) )
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
    EXPECT_EQ( Capacity, vector.Size() );
    EXPECT_TRUE( vector.Full() );
}

TEST( QCThreadSafeVector, Concurrent_ReadersTraverseConsistentSnapshots )
{
    // Readers call Size() and ForEach() while writers mutate. A broken lock
    // would surface as a size above capacity or a traversal that visits more
    // elements than the container can hold.
    constexpr uint32_t Capacity = 128u;
    QCThreadSafeVector<int, Capacity> vector;
    std::atomic<bool> bStop( false );
    std::atomic<int> badObservation( 0 );

    std::vector<std::thread> threads;
    for ( int w = 0; w < 2; ++w )
    {
        threads.emplace_back( [&vector]() {
            for ( int i = 0; i < 2000; ++i )
            {
                (void) vector.PushBack( i );
                int value = 0;
                (void) vector.PopBack( value );
            }
        } );
    }
    for ( int r = 0; r < 2; ++r )
    {
        threads.emplace_back( [&vector, &bStop, &badObservation]() {
            while ( !bStop.load() )
            {
                if ( vector.Size() > Capacity )
                {
                    badObservation.fetch_add( 1 );
                }

                uint32_t count = 0u;
                (void) vector.ForEach( [&count]( const int & ) { ++count; } );
                if ( count > Capacity )
                {
                    badObservation.fetch_add( 1 );
                }
            }
        } );
    }

    // Join the writers first, then release the readers.
    threads[0].join();
    threads[1].join();
    bStop.store( true );
    threads[2].join();
    threads[3].join();

    EXPECT_EQ( 0, badObservation.load() );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
