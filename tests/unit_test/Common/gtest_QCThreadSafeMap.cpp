// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "QC/Common/QCThreadSafeMap.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCThreadSafeMap<Key, Value, N, ...> unit tests.
//
// The wrapper delegates to QCMap, so the single-threaded cases here check that
// delegation and the deliberately reduced API surface (copy-out GetValue only;
// no Find, no Emplace, no iterators) rather than re-testing the associative
// semantics covered by gtest_QCMap. The concurrency cases check that the
// capacity ceiling holds and that readers never observe a torn entry while
// writers mutate.
// ---------------------------------------------------------------------------

// The two whole values a racing writer alternates between; a reader must always
// observe one of them, never a mixture of their bytes. File-scope so the writer
// lambda can name them without capturing.
static constexpr uint32_t ValueA = 0x11111111u;
static constexpr uint32_t ValueB = 0x22222222u;

TEST( QCThreadSafeMap, Sanity_EmptyOnConstruction )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;

    EXPECT_TRUE( map.Empty() );
    EXPECT_EQ( 0u, map.Size() );
    EXPECT_EQ( 4u, map.Capacity() );
    EXPECT_EQ( 4u, map.MaxSize() );
}

TEST( QCThreadSafeMap, Insert_AddsEntryAndRejectsDuplicate )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( 1u, map.Size() );
    EXPECT_TRUE( map.Contains( 1u ) );

    EXPECT_EQ( QC_STATUS_ALREADY, map.Insert( 1u, 99u ) );
    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 10u, value );
}

TEST( QCThreadSafeMap, Insert_AtCapacityReturnsNoResource )
{
    QCThreadSafeMap<uint32_t, uint32_t, 2> map;

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Insert( 3u, 30u ) );
    EXPECT_EQ( 2u, map.Size() );
}

TEST( QCThreadSafeMap, InsertOrAssign_OverwritesExisting )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;

    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, 11u ) );
    EXPECT_EQ( 1u, map.Size() );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );
}

TEST( QCThreadSafeMap, Update_OverwritesOnlyExistingKeys )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.Update( 1u, 10u ) );
    EXPECT_TRUE( map.Empty() );

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Update( 1u, 11u ) );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );
}

TEST( QCThreadSafeMap, Erase_RemovesEntryAndReportsMissingKey )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );

    EXPECT_EQ( QC_STATUS_OK, map.Erase( 1u ) );
    EXPECT_FALSE( map.Contains( 1u ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.Erase( 1u ) );
}

TEST( QCThreadSafeMap, GetValue_LeavesOutValueOnMiss )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;

    uint32_t value = 777u;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.GetValue( 1u, value ) );
    EXPECT_EQ( 777u, value );

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 10u, value );
}

TEST( QCThreadSafeMap, Clear_EmptiesMapAndAllowsReuse )
{
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;
    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i, i ) );
    }

    EXPECT_EQ( QC_STATUS_OK, map.Clear() );
    EXPECT_TRUE( map.Empty() );
    EXPECT_EQ( QC_STATUS_OK, map.Clear() );

    // Every node returned to the pool, so the map refills completely.
    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i + 50u, i ) );
    }
    EXPECT_EQ( 4u, map.Size() );
}

TEST( QCThreadSafeMap, Concurrent_InsertsRespectCapacity )
{
    // Many more distinct keys than capacity, from several threads: exactly N
    // inserts must succeed however the threads interleave.
    constexpr uint32_t Capacity = 64u;
    QCThreadSafeMap<uint32_t, uint32_t, Capacity> map;
    std::atomic<int> accepted( 0 );

    std::vector<std::thread> threads;
    for ( uint32_t t = 0u; t < 4u; ++t )
    {
        threads.emplace_back( [&map, &accepted, t]() {
            for ( uint32_t i = 0u; i < 100u; ++i )
            {
                const uint32_t key = ( t * 100u ) + i;
                if ( QC_STATUS_OK == map.Insert( key, key ) )
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
    EXPECT_EQ( Capacity, map.Size() );
}

TEST( QCThreadSafeMap, Concurrent_DuplicateKeyInsertedExactlyOnce )
{
    // Every thread races to insert the same key. Exactly one must win with OK;
    // all others must observe ALREADY.
    QCThreadSafeMap<uint32_t, uint32_t, 8> map;
    std::atomic<int> okCount( 0 );
    std::atomic<int> alreadyCount( 0 );

    std::vector<std::thread> threads;
    for ( int t = 0; t < 8; ++t )
    {
        threads.emplace_back( [&map, &okCount, &alreadyCount]() {
            const QCStatus_e status = map.Insert( 1u, 10u );
            if ( QC_STATUS_OK == status )
            {
                okCount.fetch_add( 1 );
            }
            else if ( QC_STATUS_ALREADY == status )
            {
                alreadyCount.fetch_add( 1 );
            }
            else
            {
                // Any other status is a failure of the locking contract.
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }

    EXPECT_EQ( 1, okCount.load() );
    EXPECT_EQ( 7, alreadyCount.load() );
    EXPECT_EQ( 1u, map.Size() );
}

TEST( QCThreadSafeMap, Concurrent_ReadersObserveWholeValues )
{
    // A writer flips a value between two states while readers copy it out. Each
    // read must yield one of the two written values, never a torn mixture, and
    // Size() must never exceed capacity.
    QCThreadSafeMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, ValueA ) );

    std::atomic<bool> bStop( false );
    std::atomic<int> badObservation( 0 );

    std::vector<std::thread> threads;
    threads.emplace_back( [&map, &bStop]() {
        for ( int i = 0; i < 20000; ++i )
        {
            (void) map.InsertOrAssign( 1u, ( 0 == ( i % 2 ) ) ? ValueA : ValueB );
        }
        bStop.store( true );
    } );
    for ( int r = 0; r < 2; ++r )
    {
        threads.emplace_back( [&map, &bStop, &badObservation]() {
            while ( !bStop.load() )
            {
                uint32_t value = 0u;
                const QCStatus_e status = map.GetValue( 1u, value );
                if ( QC_STATUS_OK != status )
                {
                    badObservation.fetch_add( 1 );
                }
                else if ( ( ValueA != value ) && ( ValueB != value ) )
                {
                    badObservation.fetch_add( 1 );
                }
                else if ( map.Size() > map.Capacity() )
                {
                    badObservation.fetch_add( 1 );
                }
                else
                {
                    // Consistent observation.
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

TEST( QCThreadSafeMap, Concurrent_InsertEraseChurnKeepsPoolConsistent )
{
    // Sustained insert/erase churn from several threads must never leak a pool
    // node: after the churn the map must still accept a full complement of
    // entries.
    constexpr uint32_t Capacity = 16u;
    QCThreadSafeMap<uint32_t, uint32_t, Capacity> map;

    std::vector<std::thread> threads;
    for ( uint32_t t = 0u; t < 4u; ++t )
    {
        threads.emplace_back( [&map, t]() {
            for ( uint32_t i = 0u; i < 500u; ++i )
            {
                const uint32_t key = ( t * 1000u ) + ( i % 8u );
                (void) map.Insert( key, i );
                (void) map.Erase( key );
            }
        } );
    }
    for ( std::thread &thread : threads )
    {
        thread.join();
    }

    EXPECT_EQ( QC_STATUS_OK, map.Clear() );
    for ( uint32_t i = 0u; i < Capacity; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i, i ) )
                << "pool leaked a node during churn; insert " << i << " failed";
    }
    EXPECT_EQ( Capacity, map.Size() );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
