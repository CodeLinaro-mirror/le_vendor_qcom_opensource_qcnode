// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include "QC/Common/QCVector.hpp"

using namespace QC;

// ---------------------------------------------------------------------------
// QCVector<T, N> unit tests.
//
// Covers the fixed-capacity sequence contract: status returns on every failing
// path, index-bound rejection, element shifting on Insert/RemoveAt, Resize
// grow/shrink, Find/Contains, and iteration over the live prefix only.
// ---------------------------------------------------------------------------

// Element whose default-constructed state is distinguishable from any value a
// test stores, and whose copy-assignment records each overwrite by a
// default-constructed instance. Slot clearing is not observable by reading
// elements back: a vacated slot is always reassigned before it can be read
// again (Resize grow writes its fill, PushBack writes the new value), so a
// container that skipped the reset would still return the expected values.
// Counting the clearing assignment itself is what makes the reset observable.
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

TEST( QCVector, Sanity_EmptyOnConstruction )
{
    QCVector<int, 4> vector;

    EXPECT_TRUE( vector.Empty() );
    EXPECT_FALSE( vector.Full() );
    EXPECT_EQ( 0u, vector.Size() );
    EXPECT_EQ( 4u, vector.Capacity() );
    EXPECT_EQ( 4u, vector.MaxSize() );

    // Iteration over an empty vector must yield nothing.
    EXPECT_EQ( vector.begin(), vector.end() );
}

TEST( QCVector, PushBack_AppendsInOrder )
{
    QCVector<int, 4> vector;

    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i * 3 ) );
    }
    EXPECT_TRUE( vector.Full() );
    EXPECT_EQ( 4u, vector.Size() );

    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( static_cast<int>( i ) * 3, value );
    }
}

TEST( QCVector, PushBack_AtCapacityReturnsNoResource )
{
    QCVector<int, 2> vector;

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.PushBack( 3 ) );

    // The rejected push must leave the contents untouched.
    EXPECT_EQ( 2u, vector.Size() );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 1u, value ) );
    EXPECT_EQ( 2, value );
}

TEST( QCVector, PopBack_OnEmptyReturnsBadState )
{
    QCVector<int, 4> vector;

    EXPECT_EQ( QC_STATUS_BAD_STATE, vector.PopBack() );

    int value = -99;
    EXPECT_EQ( QC_STATUS_BAD_STATE, vector.PopBack( value ) );
    EXPECT_EQ( -99, value );
}

TEST( QCVector, PopBack_RemovesFromTheBack )
{
    QCVector<int, 4> vector;

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack( value ) );
    EXPECT_EQ( 2, value );
    EXPECT_EQ( 1u, vector.Size() );

    EXPECT_EQ( QC_STATUS_OK, vector.PopBack() );
    EXPECT_TRUE( vector.Empty() );
}

TEST( QCVector, GetSetValue_RejectOutOfRangeIndex )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 5 ) );

    int value = -99;
    // Index equal to Size() is out of range, not the first free slot.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.GetValue( 1u, value ) );
    EXPECT_EQ( -99, value );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.GetValue( 99u, value ) );

    // SetValue never grows the vector.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.SetValue( 1u, 7 ) );
    EXPECT_EQ( 1u, vector.Size() );

    EXPECT_EQ( QC_STATUS_OK, vector.SetValue( 0u, 6 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 6, value );
}

TEST( QCVector, At_SeatsPointerAndRejectsBadIndex )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 11 ) );

    int *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, vector.At( 0u, pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 11, *pValue );

    // Writing through the seated pointer must mutate the stored element.
    *pValue = 12;
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 12, value );

    int sentinel = 0;
    pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.At( 1u, pValue ) );
    EXPECT_EQ( nullptr, pValue );
}

TEST( QCVector, At_ConstOverloadIsReadable )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 21 ) );

    const QCVector<int, 4> &constVector = vector;
    const int *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, constVector.At( 0u, pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 21, *pValue );

    const int *pSentinel = pValue;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, constVector.At( 5u, pSentinel ) );
    EXPECT_EQ( nullptr, pSentinel );
}

TEST( QCVector, FrontBack_OnEmptyReturnBadStateAndNullPointer )
{
    QCVector<int, 4> vector;
    int sentinel = 0;

    int *pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_STATE, vector.Front( pValue ) );
    EXPECT_EQ( nullptr, pValue );

    pValue = &sentinel;
    EXPECT_EQ( QC_STATUS_BAD_STATE, vector.Back( pValue ) );
    EXPECT_EQ( nullptr, pValue );
}

TEST( QCVector, FrontBack_SeatFirstAndLastElements )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 3 ) );

    int *pValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, vector.Front( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 1, *pValue );

    EXPECT_EQ( QC_STATUS_OK, vector.Back( pValue ) );
    ASSERT_NE( nullptr, pValue );
    EXPECT_EQ( 3, *pValue );

    const QCVector<int, 4> &constVector = vector;
    const int *pConstValue = nullptr;
    EXPECT_EQ( QC_STATUS_OK, constVector.Front( pConstValue ) );
    ASSERT_NE( nullptr, pConstValue );
    EXPECT_EQ( 1, *pConstValue );
    EXPECT_EQ( QC_STATUS_OK, constVector.Back( pConstValue ) );
    ASSERT_NE( nullptr, pConstValue );
    EXPECT_EQ( 3, *pConstValue );
}

TEST( QCVector, Insert_ShiftsTailTowardTheBack )
{
    QCVector<int, 5> vector;
    for ( int i = 0; i < 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );   // 0,1,2
    }

    EXPECT_EQ( QC_STATUS_OK, vector.Insert( 1u, 99 ) );   // 0,99,1,2
    EXPECT_EQ( 4u, vector.Size() );

    const int expected[] = { 0, 99, 1, 2 };
    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( expected[i], value );
    }
}

TEST( QCVector, Insert_AtFrontAndAtSize )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.Insert( 0u, 1 ) );   // 1,2
    // index == Size() appends.
    EXPECT_EQ( QC_STATUS_OK, vector.Insert( 2u, 3 ) );   // 1,2,3

    const int expected[] = { 1, 2, 3 };
    for ( uint32_t i = 0u; i < 3u; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( expected[i], value );
    }
}

TEST( QCVector, Insert_RejectsBadIndexAndFullVector )
{
    QCVector<int, 2> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );

    // index > Size() is rejected before any capacity consideration.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.Insert( 2u, 9 ) );
    EXPECT_EQ( 1u, vector.Size() );

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );
    ASSERT_TRUE( vector.Full() );

    // A valid index on a full vector reports the capacity limit.
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Insert( 0u, 9 ) );
    EXPECT_EQ( 2u, vector.Size() );

    // Bad index is still reported as such even when full.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.Insert( 99u, 9 ) );
}

TEST( QCVector, RemoveAt_ShiftsTailTowardTheFront )
{
    QCVector<int, 5> vector;
    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );   // 0,1,2,3
    }

    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 1u ) );   // 0,2,3
    EXPECT_EQ( 3u, vector.Size() );

    const int expected[] = { 0, 2, 3 };
    for ( uint32_t i = 0u; i < 3u; ++i )
    {
        int value = -1;
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( expected[i], value );
    }

    // Removing the first and the last element.
    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 0u ) );   // 2,3
    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 1u ) );   // 2
    EXPECT_EQ( 1u, vector.Size() );
    int value = -1;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 2, value );
}

TEST( QCVector, RemoveAt_RejectsOutOfRangeIndex )
{
    QCVector<int, 4> vector;

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.RemoveAt( 0u ) );

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.RemoveAt( 1u ) );
    EXPECT_EQ( 1u, vector.Size() );
}

TEST( QCVector, RemoveAt_ThenPushBackDoesNotResurrectValue )
{
    // The vacated tail slot must be reset, so a later PushBack cannot expose
    // the removed element.
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 0u ) );   // 2

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 3 ) );   // 2,3
    int value = -1;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 1u, value ) );
    EXPECT_EQ( 3, value );
}

TEST( QCVector, Find_ReturnsFirstMatchAndReportsMiss )
{
    QCVector<int, 8> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 10 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 20 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 20 ) );

    uint32_t index = 999u;
    EXPECT_EQ( QC_STATUS_OK, vector.Find( 20, index ) );
    EXPECT_EQ( 1u, index );   // first match, not the last

    // A miss leaves outIndex untouched.
    index = 777u;
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Find( 30, index ) );
    EXPECT_EQ( 777u, index );
}

TEST( QCVector, Find_OnEmptyVectorReportsMiss )
{
    QCVector<int, 4> vector;

    uint32_t index = 5u;
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Find( 1, index ) );
    EXPECT_EQ( 5u, index );
}

TEST( QCVector, Contains_MatchesOnlyLiveElements )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 2 ) );

    EXPECT_TRUE( vector.Contains( 1 ) );
    EXPECT_TRUE( vector.Contains( 2 ) );
    EXPECT_FALSE( vector.Contains( 3 ) );

    // A popped element is no longer contained.
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack() );
    EXPECT_FALSE( vector.Contains( 2 ) );
}

TEST( QCVector, Resize_GrowsWithFillAndShrinksCleanly )
{
    QCVector<int, 8> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );

    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 4u, -7 ) );
    EXPECT_EQ( 4u, vector.Size() );

    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 1, value );   // existing element preserved
    for ( uint32_t i = 1u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( -7, value );   // added slots carry the fill
    }

    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 1u ) );
    EXPECT_EQ( 1u, vector.Size() );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, vector.GetValue( 1u, value ) );
}

TEST( QCVector, Resize_RegrowDoesNotResurrectDroppedValues )
{
    // The dropped slots must be reset on shrink, so a later regrow exposes a
    // default-constructed element rather than the value that was there before.
    // A non-zero fill is used for the initial elements so that stale data is
    // distinguishable from the default-constructed 0 a correct Resize yields.
    QCVector<int, 8> vector;
    for ( int i = 0; i < 4; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 50 + i ) );
    }

    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 1u ) );
    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 4u ) );

    int value = -1;
    for ( uint32_t i = 1u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.GetValue( i, value ) );
        EXPECT_EQ( 0, value ) << "slot " << i << " retained a value dropped by Resize";
    }
}

TEST( QCVector, Resize_ShrinkClearsDroppedSlots )
{
    // Shrinking must reset each dropped slot, so the element it held is not
    // retained in storage after the size has moved below it.
    QCVector<ClearSpy_t, 8> vector;

    for ( uint32_t i = 0u; i < 4u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( MakeLiveSpy() ) );
    }

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 2u ) );

    // Two slots were dropped, so exactly two clearing assignments must occur.
    EXPECT_EQ( 2, s_clearAssignCount );
}

TEST( QCVector, PopBack_ClearsVacatedSlot )
{
    QCVector<ClearSpy_t, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( MakeLiveSpy() ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( MakeLiveSpy() ) );

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack() );
    EXPECT_EQ( 1, s_clearAssignCount );

    // The copy-out overload clears its slot too.
    ClearSpy_t out;
    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack( out ) );
    EXPECT_TRUE( out.bLive );   // the caller receives the live element
    EXPECT_EQ( 1, s_clearAssignCount );
}

TEST( QCVector, RemoveAt_ClearsVacatedTailSlot )
{
    QCVector<ClearSpy_t, 4> vector;
    for ( uint32_t i = 0u; i < 3u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( MakeLiveSpy() ) );
    }

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.RemoveAt( 0u ) );

    // The tail shift copies live elements down; only the vacated last slot is
    // cleared, so exactly one clearing assignment must occur.
    EXPECT_EQ( 1, s_clearAssignCount );
    EXPECT_EQ( 2u, vector.Size() );
}

TEST( QCVector, Clear_ResetsFullStorage )
{
    QCVector<ClearSpy_t, 8> vector;
    for ( uint32_t i = 0u; i < 3u; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( MakeLiveSpy() ) );
    }

    s_clearAssignCount = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.Clear() );

    // Clear resets the full fixed-capacity backing array.
    EXPECT_EQ( 8, s_clearAssignCount );
    EXPECT_TRUE( vector.Empty() );
}

TEST( QCVector, Resize_BeyondCapacityReturnsNoResource )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );

    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.Resize( 5u ) );
    EXPECT_EQ( 1u, vector.Size() );   // unchanged by the rejected resize

    // Resize to exactly N is allowed.
    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 4u ) );
    EXPECT_TRUE( vector.Full() );

    // Resize to zero empties the vector.
    EXPECT_EQ( QC_STATUS_OK, vector.Resize( 0u ) );
    EXPECT_TRUE( vector.Empty() );
}

TEST( QCVector, Clear_EmptiesVectorAndAllowsReuse )
{
    QCVector<int, 4> vector;
    for ( int i = 0; i < 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );
    }

    EXPECT_EQ( QC_STATUS_OK, vector.Clear() );
    EXPECT_TRUE( vector.Empty() );
    EXPECT_EQ( 0u, vector.Size() );
    EXPECT_EQ( vector.begin(), vector.end() );

    // Clear on an already-empty vector is a no-op, not a failure.
    EXPECT_EQ( QC_STATUS_OK, vector.Clear() );

    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 42 ) );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 42, value );
}

TEST( QCVector, Iteration_SpansLivePrefixOnly )
{
    QCVector<int, 8> vector;
    for ( int i = 1; i <= 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );
    }

    // Range-based for must visit exactly Size() elements, not all N slots.
    uint32_t count = 0u;
    int sum = 0;
    for ( const int &element : vector )
    {
        ++count;
        sum += element;
    }
    EXPECT_EQ( 3u, count );
    EXPECT_EQ( 6, sum );

    // Mutating through a non-const iterator writes through to storage.
    for ( int &element : vector )
    {
        element *= 10;
    }
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 2u, value ) );
    EXPECT_EQ( 30, value );

    // cbegin/cend span the same range.
    count = 0u;
    for ( QCVector<int, 8>::ConstIterator it = vector.cbegin(); it != vector.cend(); ++it )
    {
        ++count;
    }
    EXPECT_EQ( 3u, count );
}

TEST( QCVector, Data_ExposesContiguousStorage )
{
    QCVector<int, 4> vector;
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 4 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 5 ) );

    int *pData = vector.Data();
    ASSERT_NE( nullptr, pData );
    EXPECT_EQ( 4, pData[0] );
    EXPECT_EQ( 5, pData[1] );

    // Elements are contiguous, so Data() is usable as a plain buffer.
    pData[0] = 6;
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, vector.GetValue( 0u, value ) );
    EXPECT_EQ( 6, value );

    const QCVector<int, 4> &constVector = vector;
    EXPECT_EQ( 6, constVector.Data()[0] );
}

TEST( QCVector, CapacityOne_DegenerateSizeStillWorks )
{
    // N need not be a power of 2 for QCVector; exercise the smallest case.
    QCVector<int, 1> vector;

    EXPECT_EQ( 1u, vector.Capacity() );
    EXPECT_EQ( QC_STATUS_OK, vector.PushBack( 1 ) );
    EXPECT_TRUE( vector.Full() );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.PushBack( 2 ) );
    EXPECT_EQ( QC_STATUS_OK, vector.PopBack() );
    EXPECT_TRUE( vector.Empty() );
}

TEST( QCVector, NonPowerOfTwoCapacity_IsSupported )
{
    // Unlike QCQueue, QCVector places no power-of-2 constraint on N.
    QCVector<int, 3> vector;

    EXPECT_EQ( 3u, vector.Capacity() );
    for ( int i = 0; i < 3; ++i )
    {
        EXPECT_EQ( QC_STATUS_OK, vector.PushBack( i ) );
    }
    EXPECT_TRUE( vector.Full() );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, vector.PushBack( 3 ) );
}

TEST( QCVector, CopyConstruct_IsIndependentOfSource )
{
    QCVector<int, 4> source;
    EXPECT_EQ( QC_STATUS_OK, source.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_OK, source.PushBack( 2 ) );

    QCVector<int, 4> copy( source );
    EXPECT_EQ( 2u, copy.Size() );

    EXPECT_EQ( QC_STATUS_OK, copy.PopBack() );
    EXPECT_EQ( 1u, copy.Size() );
    EXPECT_EQ( 2u, source.Size() );   // source unaffected

    QCVector<int, 4> assigned;
    assigned = source;
    EXPECT_EQ( 2u, assigned.Size() );
    int value = 0;
    EXPECT_EQ( QC_STATUS_OK, assigned.GetValue( 1u, value ) );
    EXPECT_EQ( 2, value );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
