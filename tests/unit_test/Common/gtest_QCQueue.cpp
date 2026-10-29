// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include "QC/Common/QCQueue.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCQueue<T, N> unit tests.
//
// Covers the fixed-capacity FIFO contract: status returns on every failing
// path, ring-index wraparound past N, vacated-slot clearing, and the
// value-type copy semantics.
// ---------------------------------------------------------------------------

// Element whose copy-assignment records each overwrite by a
// default-constructed instance. Slot clearing is not observable by reading
// elements back: a vacated slot is always reassigned before it can be read
// again (a later Push writes the new value), so a queue that skipped the reset
// would still return the expected values. Counting the clearing assignment
// itself is what makes the reset observable.
static int s_clearAssignCount = 0;

typedef struct ClearSpy
{
    bool bLive;

    ClearSpy() noexcept : bLive( false ) {}
    ClearSpy( const ClearSpy & ) noexcept = default;

    ClearSpy &operator=( const ClearSpy &other ) noexcept
    {
        bLive = other.bLive;
        if ( !other.bLive )
        {
            ++s_clearAssignCount;
        }
        return *this;
    }
} ClearSpy_t;

// Returns a value that a cleared slot can never hold.
static ClearSpy_t MakeLiveSpy()
{
    ClearSpy_t spy;
    spy.bLive = true;
    return spy;
}

typedef struct
{
    int value;
} TestPod_t;

TEST( QCQueue, Sanity_EmptyOnConstruction )
{
    QCQueue<int, 4> queue;

    EXPECT_TRUE( queue.Empty() );
    EXPECT_FALSE( queue.Full() );
    EXPECT_EQ( 0u, queue.Size() );
    EXPECT_EQ( 4u, queue.Capacity() );
    EXPECT_EQ( 4u, queue.MaxSize() );
}

TEST( QCQueue, PushPop_PreservesFifoOrder )
{
    QCQueue<int, 4> queue;

    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
    }
    EXPECT_TRUE( queue.Full() );
    EXPECT_EQ( 4u, queue.Size() );

    for ( int i = 0; i < 4; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( i, value );
    }
    EXPECT_TRUE( queue.Empty() );
}

TEST( QCQueue, Push_AtCapacityReturnsNoResource )
{
    QCQueue<int, 2> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 2 ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, queue.Push( 3 ) );

    // The rejected push must not overwrite the oldest element or grow the queue.
    EXPECT_EQ( 2u, queue.Size() );
    int value = -1;
    EXPECT_EQ( QC_STATUS_OK, queue.GetFront( value ) );
    EXPECT_EQ( 1, value );
    EXPECT_EQ( QC_STATUS_OK, queue.GetBack( value ) );
    EXPECT_EQ( 2, value );
}

TEST( QCQueue, Pop_OnEmptyReturnsBadState )
{
    QCQueue<int, 4> queue;

    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Pop() );

    // The copy-out overload must leave outValue untouched on failure.
    int value = -99;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Pop( value ) );
    EXPECT_EQ( -99, value );
}

TEST( QCQueue, FrontBack_OnEmptyReturnBadStateAndNullPointer )
{
    QCQueue<int, 4> queue;
    int sentinel = 0;

    int *pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Front( pValue ) );
    EXPECT_EQ( nullptr, pValue );

    pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Back( pValue ) );
    EXPECT_EQ( nullptr, pValue );

    int value = 0;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.GetFront( value ) );
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.GetBack( value ) );
}

TEST( QCQueue, FrontBack_SeatPointersToOldestAndNewest )
{
    QCQueue<int, 4> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 10 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 20 ) );

    int *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, queue.Front( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 10, *pValue );

    EXPECT_EQ( QC_STATUS_OK, queue.Back( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 20, *pValue );

    // Writing through the seated pointer must mutate the stored element.
    *pValue = 21;
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.GetBack( value ) );
    EXPECT_EQ( 21, value );
}

TEST( QCQueue, Front_ConstOverloadIsReadable )
{
    QCQueue<int, 4> queue;
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 7 ) );

    const QCQueue<int, 4> &constQueue = queue;
    const int *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, constQueue.Front( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 7, *pValue );

    EXPECT_EQ( QC_STATUS_OK, constQueue.Back( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 7, *pValue );
}

TEST( QCQueue, WrapAround_SingleElementCyclesPastCapacity )
{
    // Push/pop far more than N times so the head and tail indices wrap the
    // mask repeatedly; a broken mask shows up as a wrong value or a stuck size.
    QCQueue<int, 4> queue;

    for ( int round = 0; round < 25; ++round )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( round ) );
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( round, value );
        EXPECT_TRUE( queue.Empty() );
    }
}

TEST( QCQueue, WrapAround_PartiallyFullStraddlesBoundary )
{
    // Keep two elements in flight while the indices cross the wrap point.
    QCQueue<int, 4> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 100 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 101 ) );

    for ( int round = 0; round < 20; ++round )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( 100 + round, value );
        EXPECT_EQ( QC_STATUS_OK, queue.Push( 102 + round ) );
        EXPECT_EQ( 2u, queue.Size() );
    }
}

TEST( QCQueue, WrapAround_RefillAfterFullDrain )
{
    QCQueue<int, 4> queue;

    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
    }
    for ( int i = 0; i < 4; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( i, value );
    }

    // Head is now mid-array; a full refill must still succeed and stay ordered.
    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i * 10 ) );
    }
    EXPECT_TRUE( queue.Full() );
    for ( int i = 0; i < 4; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( i * 10, value );
    }
}

TEST( QCQueue, Pop_ResetsVacatedSlot )
{
    // A popped slot must hold a default-constructed T, so the dequeued value
    // is not retained in queue storage once the caller owns it.
    QCQueue<ClearSpy_t, 2> queue;
    EXPECT_EQ( QC_STATUS_OK, queue.Push( MakeLiveSpy() ) );

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Pop() );
    EXPECT_EQ( 1, s_clearAssignCount );

    // The copy-out overload hands the caller the live element and still clears
    // the slot it came from.
    EXPECT_EQ( QC_STATUS_OK, queue.Push( MakeLiveSpy() ) );
    ClearSpy_t out;
    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( out ) );
    EXPECT_TRUE( out.bLive );
    EXPECT_EQ( 1, s_clearAssignCount );
}

TEST( QCQueue, Clear_ResetsFullStorage )
{
    QCQueue<ClearSpy_t, 8> queue;
    for ( uint32_t i = 0u; i < 3u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( MakeLiveSpy() ) );
    }

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Clear() );

    // Clear resets the full fixed-capacity backing array.
    EXPECT_EQ( 8, s_clearAssignCount );
    EXPECT_TRUE( queue.Empty() );
}

TEST( QCQueue, Pop_DoesNotResurrectPoppedValue )
{
    // End-to-end check on the plain value type: a vacated slot handed out
    // again must carry only the newly pushed value.
    QCQueue<int, 2> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 7 ) );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    EXPECT_EQ( 7, value );

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 8 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 9 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    EXPECT_EQ( 8, value );
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    EXPECT_EQ( 9, value );
}

TEST( QCQueue, Clear_EmptiesQueueAndAllowsReuse )
{
    QCQueue<int, 4> queue;

    for ( int i = 0; i < 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
    }

    EXPECT_EQ( QC_STATUS_OK, queue.Clear() );
    EXPECT_TRUE( queue.Empty() );
    EXPECT_EQ( 0u, queue.Size() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.GetFront( value ) );

    // Clear on an already-empty queue is a no-op, not a failure.
    EXPECT_EQ( QC_STATUS_OK, queue.Clear() );

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 42 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.GetFront( value ) );
    EXPECT_EQ( 42, value );
}

TEST( QCQueue, Clear_OnWrappedQueueResetsLiveSpan )
{
    // Drive the head off zero, then clear while the live span wraps.
    QCQueue<int, 4> queue;
    int value = 0;

    for ( int i = 0; i < 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    }
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 2 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 3 ) );

    EXPECT_EQ( QC_STATUS_OK, queue.Clear() );
    EXPECT_TRUE( queue.Empty() );
    EXPECT_EQ( QC_STATUS_BAD_STATE, queue.Pop( value ) );
}

TEST( QCQueue, GetFrontGetBack_LeaveElementQueued )
{
    QCQueue<int, 4> queue;

    EXPECT_EQ( QC_STATUS_OK, queue.Push( 5 ) );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 6 ) );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.GetFront( value ) );
    EXPECT_EQ( 5, value );
    EXPECT_EQ( QC_STATUS_OK, queue.GetBack( value ) );
    EXPECT_EQ( 6, value );

    // Neither accessor consumes.
    EXPECT_EQ( 2u, queue.Size() );
}

TEST( QCQueue, CapacityOne_DegenerateRingStillWorks )
{
    QCQueue<int, 1> queue;

    EXPECT_EQ( 1u, queue.Capacity() );
    EXPECT_EQ( QC_STATUS_OK, queue.Push( 1 ) );
    EXPECT_TRUE( queue.Full() );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, queue.Push( 2 ) );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
    EXPECT_EQ( 1, value );
    EXPECT_TRUE( queue.Empty() );

    // Reuse the single slot repeatedly.
    for ( int i = 0; i < 5; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, queue.Push( i ) );
        EXPECT_EQ( QC_STATUS_OK, queue.Pop( value ) );
        EXPECT_EQ( i, value );
    }
}

TEST( QCQueue, CopyConstruct_IsIndependentOfSource )
{
    QCQueue<int, 4> source;
    EXPECT_EQ( QC_STATUS_OK, source.Push( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, source.Push( 2 ) );

    QCQueue<int, 4> copy( source );
    EXPECT_EQ( 2u, copy.Size() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, copy.Pop( value ) );
    EXPECT_EQ( 1, value );

    // Draining the copy must not disturb the source.
    EXPECT_EQ( 2u, source.Size() );
    EXPECT_EQ( QC_STATUS_OK, source.GetFront( value ) );
    EXPECT_EQ( 1, value );
}

TEST( QCQueue, CopyAssign_ReplacesDestinationContents )
{
    QCQueue<int, 4> source;
    EXPECT_EQ( QC_STATUS_OK, source.Push( 8 ) );

    QCQueue<int, 4> dest;
    EXPECT_EQ( QC_STATUS_OK, dest.Push( 99 ) );
    EXPECT_EQ( QC_STATUS_OK, dest.Push( 98 ) );

    dest = source;
    EXPECT_EQ( 1u, dest.Size() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, dest.Pop( value ) );
    EXPECT_EQ( 8, value );
    EXPECT_TRUE( dest.Empty() );
}

TEST( QCQueue, StructElement_RoundTripsByValue )
{
    QCQueue<TestPod_t, 4> queue;

    TestPod_t in;
    in.value = 77;
    EXPECT_EQ( QC_STATUS_OK, queue.Push( in ) );

    TestPod_t out;
    out.value = 0;
    EXPECT_EQ( QC_STATUS_OK, queue.Pop( out ) );
    EXPECT_EQ( 77, out.value );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
