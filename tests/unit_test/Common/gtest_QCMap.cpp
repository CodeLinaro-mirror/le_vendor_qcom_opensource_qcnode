// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include <cstdint>

#include "QC/Common/QCMap.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCMap<Key, Value, N, ...> unit tests.
//
// Covers the fixed-capacity associative contract: status returns on every
// failing path, the capacity ceiling N enforced independently of the
// allocator, the distinction between Insert (never overwrites) /
// InsertOrAssign (always writes) / Update (never inserts), pointer-seating
// Find versus copy-out GetValue, and ordered iteration.
//
// Every instantiation here uses the default QCRingPoolAllocator, so no map
// operation reaches the heap; capacity exhaustion must surface as
// QC_STATUS_NO_RESOURCE rather than an allocation attempt.
// ---------------------------------------------------------------------------

// Value type that satisfies QCMap's nothrow-copy contract.
typedef struct
{
    uint32_t id;
} Payload_t;

TEST( QCMap, Sanity_EmptyOnConstruction )
{
    QCMap<uint32_t, uint32_t, 4> map;

    EXPECT_TRUE( map.Empty() );
    EXPECT_EQ( 0u, map.Size() );
    EXPECT_EQ( 4u, map.Capacity() );
    EXPECT_EQ( 4u, map.MaxSize() );
    EXPECT_EQ( map.begin(), map.end() );
}

TEST( QCMap, Insert_AddsEntryAndRejectsDuplicate )
{
    QCMap<uint32_t, uint32_t, 4> map;

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 100u ) );
    EXPECT_EQ( 1u, map.Size() );
    EXPECT_TRUE( map.Contains( 1u ) );

    // A duplicate key is reported and the existing entry is kept unchanged.
    EXPECT_EQ( QC_STATUS_ALREADY, map.Insert( 1u, 999u ) );
    EXPECT_EQ( 1u, map.Size() );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 100u, value );
}

TEST( QCMap, Insert_AtCapacityReturnsNoResource )
{
    QCMap<uint32_t, uint32_t, 4> map;

    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i, i * 10u ) );
    }
    EXPECT_EQ( 4u, map.Size() );

    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Insert( 99u, 1u ) );
    EXPECT_EQ( 4u, map.Size() );
    EXPECT_FALSE( map.Contains( 99u ) );
}

TEST( QCMap, Insert_DuplicateIsReportedEvenAtCapacity )
{
    // Re-inserting an existing key consumes no node, so ALREADY is the correct
    // answer at capacity — not NO_RESOURCE.
    QCMap<uint32_t, uint32_t, 2> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );

    EXPECT_EQ( QC_STATUS_ALREADY, map.Insert( 1u, 11u ) );
    EXPECT_EQ( 2u, map.Size() );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 10u, value );
}

TEST( QCMap, InsertOrAssign_OverwritesExistingAndInsertsMissing )
{
    QCMap<uint32_t, uint32_t, 4> map;

    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, 10u ) );
    EXPECT_EQ( 1u, map.Size() );

    // Overwrites in place: no additional node consumed.
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, 11u ) );
    EXPECT_EQ( 1u, map.Size() );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );
}

TEST( QCMap, InsertOrAssign_NewKeyAtCapacityReturnsNoResource )
{
    QCMap<uint32_t, uint32_t, 2> map;
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 2u, 20u ) );

    // A new key needs a node that does not exist.
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.InsertOrAssign( 3u, 30u ) );
    EXPECT_EQ( 2u, map.Size() );

    // An existing key still assigns fine at capacity.
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 2u, 22u ) );
    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 2u, value ) );
    EXPECT_EQ( 22u, value );
}

TEST( QCMap, Emplace_ConstructsInPlaceAndRejectsDuplicate )
{
    QCMap<uint32_t, Payload_t, 4> map;

    Payload_t payload;
    payload.id = 7u;
    EXPECT_EQ( QC_STATUS_OK, map.Emplace( 1u, payload ) );
    EXPECT_EQ( 1u, map.Size() );

    Payload_t out;
    out.id = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, out ) );
    EXPECT_EQ( 7u, out.id );

    EXPECT_EQ( QC_STATUS_ALREADY, map.Emplace( 1u, payload ) );
    EXPECT_EQ( 1u, map.Size() );
}

TEST( QCMap, Emplace_AtCapacityReturnsNoResource )
{
    QCMap<uint32_t, Payload_t, 2> map;

    Payload_t payload;
    payload.id = 1u;
    EXPECT_EQ( QC_STATUS_OK, map.Emplace( 1u, payload ) );
    EXPECT_EQ( QC_STATUS_OK, map.Emplace( 2u, payload ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Emplace( 3u, payload ) );
    EXPECT_EQ( 2u, map.Size() );
}

TEST( QCMap, Update_OverwritesOnlyExistingKeys )
{
    QCMap<uint32_t, uint32_t, 4> map;

    // Update never inserts.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.Update( 1u, 10u ) );
    EXPECT_TRUE( map.Empty() );

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Update( 1u, 11u ) );
    EXPECT_EQ( 1u, map.Size() );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );
}

TEST( QCMap, Erase_RemovesEntryAndReportsMissingKey )
{
    QCMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );

    EXPECT_EQ( QC_STATUS_OK, map.Erase( 1u ) );
    EXPECT_TRUE( map.Empty() );
    EXPECT_FALSE( map.Contains( 1u ) );

    // Erasing a key that is not present is reported, not silently ignored.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.Erase( 1u ) );
}

TEST( QCMap, Erase_ReturnsNodeToThePoolForReuse )
{
    // The pool is fixed at N: without a working release path the map could
    // never accept another insert after being filled and drained.
    QCMap<uint32_t, uint32_t, 2> map;

    for ( uint32_t round = 0u; round < 10u; ++round )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( round, round ) );
        EXPECT_EQ( QC_STATUS_OK, map.Insert( round + 100u, round ) );
        EXPECT_EQ( 2u, map.Size() );
        EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Insert( round + 200u, round ) );

        EXPECT_EQ( QC_STATUS_OK, map.Erase( round ) );
        EXPECT_EQ( QC_STATUS_OK, map.Erase( round + 100u ) );
        EXPECT_TRUE( map.Empty() );
    }
}

TEST( QCMap, Find_SeatsPointerAndReportsMiss )
{
    QCMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );

    uint32_t *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, map.Find( 1u, pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 10u, *pValue );

    // Writing through the seated pointer mutates the stored value.
    *pValue = 11u;
    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );

    // A miss sets the pointer to nullptr.
    uint32_t sentinel = 0u;
    pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.Find( 2u, pValue ) );
    EXPECT_EQ( nullptr, pValue );
}

TEST( QCMap, Find_ConstOverloadIsReadable )
{
    QCMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );

    const QCMap<uint32_t, uint32_t, 4> &constMap = map;
    const uint32_t *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, constMap.Find( 1u, pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 10u, *pValue );

    const uint32_t sentinel = 0u;
    pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, constMap.Find( 2u, pValue ) );
    EXPECT_EQ( nullptr, pValue );
}

TEST( QCMap, GetValue_CopiesOutAndLeavesOutValueOnMiss )
{
    QCMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );

    uint32_t value = 999u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 10u, value );

    // On a miss outValue is left untouched.
    value = 777u;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, map.GetValue( 2u, value ) );
    EXPECT_EQ( 777u, value );
}

TEST( QCMap, Contains_ReflectsCurrentContents )
{
    QCMap<uint32_t, uint32_t, 4> map;

    EXPECT_FALSE( map.Contains( 1u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_TRUE( map.Contains( 1u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Erase( 1u ) );
    EXPECT_FALSE( map.Contains( 1u ) );
}

TEST( QCMap, Clear_EmptiesMapAndReturnsAllNodes )
{
    QCMap<uint32_t, uint32_t, 4> map;
    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i, i ) );
    }

    EXPECT_EQ( QC_STATUS_OK, map.Clear() );
    EXPECT_TRUE( map.Empty() );
    EXPECT_EQ( 0u, map.Size() );
    EXPECT_EQ( map.begin(), map.end() );

    // Clear on an empty map is a no-op, not a failure.
    EXPECT_EQ( QC_STATUS_OK, map.Clear() );

    // Every node returned to the pool, so the map can be refilled completely.
    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, map.Insert( i + 50u, i ) );
    }
    EXPECT_EQ( 4u, map.Size() );
}

TEST( QCMap, Iteration_VisitsEntriesInKeyOrder )
{
    QCMap<uint32_t, uint32_t, 8> map;

    // Insert out of order; std::map keeps them sorted by key.
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 3u, 30u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );

    uint32_t expectedKey = 1u;
    uint32_t count = 0u;
    for ( const auto &entry : map )
    {
        EXPECT_EQ( expectedKey, entry.first );
        EXPECT_EQ( expectedKey * 10u, entry.second );
        ++expectedKey;
        ++count;
    }
    EXPECT_EQ( 3u, count );
}

TEST( QCMap, Iteration_MutableIteratorWritesThrough )
{
    QCMap<uint32_t, uint32_t, 4> map;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );

    for ( auto &entry : map )
    {
        entry.second += 1u;
    }

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 11u, value );
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 2u, value ) );
    EXPECT_EQ( 21u, value );

    // cbegin/cend span the same entries.
    uint32_t count = 0u;
    for ( auto it = map.cbegin(); it != map.cend(); ++it )
    {
        ++count;
    }
    EXPECT_EQ( 2u, count );
}

TEST( QCMap, StructValue_RoundTripsByValue )
{
    QCMap<uint32_t, Payload_t, 4> map;

    Payload_t in;
    in.id = 42u;
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, in ) );

    Payload_t out;
    out.id = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, out ) );
    EXPECT_EQ( 42u, out.id );

    // InsertOrAssign overwrites the struct in place.
    in.id = 43u;
    EXPECT_EQ( QC_STATUS_OK, map.InsertOrAssign( 1u, in ) );
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, out ) );
    EXPECT_EQ( 43u, out.id );
}

TEST( QCMap, CapacityOne_DegenerateSizeStillWorks )
{
    QCMap<uint32_t, uint32_t, 1> map;

    EXPECT_EQ( 1u, map.Capacity() );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Insert( 2u, 20u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Erase( 1u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );
    EXPECT_EQ( 1u, map.Size() );
}

TEST( QCMap, HeapAllocator_ProvidesTheSameStatusApi )
{
    // Opting out of the pool keeps the status-returning API and the N ceiling;
    // it only trades away the zero-heap guarantee.
    QCMap<uint32_t, uint32_t, 2, std::less<uint32_t>,
          std::allocator<std::pair<const uint32_t, uint32_t>>>
            map;

    EXPECT_EQ( QC_STATUS_OK, map.Insert( 1u, 10u ) );
    EXPECT_EQ( QC_STATUS_OK, map.Insert( 2u, 20u ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, map.Insert( 3u, 30u ) );
    EXPECT_EQ( QC_STATUS_ALREADY, map.Insert( 1u, 11u ) );

    uint32_t value = 0u;
    EXPECT_EQ( QC_STATUS_OK, map.GetValue( 1u, value ) );
    EXPECT_EQ( 10u, value );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
