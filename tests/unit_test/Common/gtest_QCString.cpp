// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include <cstring>
#include <functional>
#include <type_traits>

#include "QC/Common/QCString.hpp"

using namespace QC;

// Yields an index without letting the compiler treat it as a constant.
//
// GCC's -Wstringop-overflow/-Wstringop-overread run after the accessors are
// inlined and evaluate the buffer arithmetic on paths the range guard makes
// unreachable. A literal out-of-range index therefore produces a diagnostic
// ("writing 1 byte into a region of size 0", "at offset 99") for a write that
// never executes, which -Werror turns into a build failure. Reading the value
// back through a volatile hides it from constant propagation, so the guard is
// still exercised at runtime but the unreachable path is no longer folded
// against a known offset.
static uint32_t OpaqueIndex( uint32_t index )
{
    volatile uint32_t opaque = index;
    return opaque;
}

// ---------------------------------------------------------------------------
// QCString<T> unit tests.
//
// Covers the fixed-capacity string contract: status returns on every failing
// path, the capacity ceiling T (which counts characters excluding the
// terminator), the always-null-terminated invariant, bounded C-string
// scanning, and the reference_wrapper-based element accessors.
// ---------------------------------------------------------------------------

TEST( QCString, Sanity_EmptyOnConstruction )
{
    QCString<16> str;

    EXPECT_TRUE( str.IsEmpty() );
    EXPECT_EQ( 0u, str.Size() );
    EXPECT_EQ( 16u, str.Capacity() );
    EXPECT_EQ( 16u, str.MaxSize() );

    // Storage is null-terminated even when empty, so CStr() is always usable.
    ASSERT_NE( nullptr, str.CStr() );
    EXPECT_STREQ( "", str.CStr() );
}

TEST( QCString, PushBack_AppendsAndKeepsTerminator )
{
    QCString<4> str;

    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'a' ) );
    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'b' ) );
    EXPECT_EQ( 2u, str.Size() );
    EXPECT_STREQ( "ab", str.CStr() );
}

TEST( QCString, PushBack_AtCapacityReturnsNomem )
{
    QCString<2> str;

    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'a' ) );
    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'b' ) );
    // T excludes the terminator, so a third character does not fit.
    EXPECT_EQ( QC_STATUS_NOMEM, str.PushBack( 'c' ) );
    EXPECT_EQ( 2u, str.Size() );
    EXPECT_STREQ( "ab", str.CStr() );
}

TEST( QCString, PopBack_RemovesLastAndRejectsEmpty )
{
    QCString<8> str;

    EXPECT_EQ( QC_STATUS_BAD_STATE, str.PopBack() );

    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ab" ) );
    EXPECT_EQ( QC_STATUS_OK, str.PopBack() );
    EXPECT_EQ( 1u, str.Size() );
    EXPECT_STREQ( "a", str.CStr() );

    EXPECT_EQ( QC_STATUS_OK, str.PopBack() );
    EXPECT_TRUE( str.IsEmpty() );
    EXPECT_STREQ( "", str.CStr() );
    EXPECT_EQ( QC_STATUS_BAD_STATE, str.PopBack() );
}

TEST( QCString, Append_WithExplicitLength )
{
    QCString<8> str;

    EXPECT_EQ( QC_STATUS_OK, str.Append( "abc", 3u ) );
    EXPECT_STREQ( "abc", str.CStr() );
    EXPECT_EQ( QC_STATUS_OK, str.Append( "de", 2u ) );
    EXPECT_STREQ( "abcde", str.CStr() );
    EXPECT_EQ( 5u, str.Size() );

    // A length shorter than the literal copies only that prefix.
    EXPECT_EQ( QC_STATUS_OK, str.Append( "xyz", 1u ) );
    EXPECT_STREQ( "abcdex", str.CStr() );
}

TEST( QCString, Append_RejectsNullPointerAndOverflow )
{
    QCString<4> str;

    EXPECT_EQ( QC_STATUS_NULL_PTR, str.Append( nullptr, 2u ) );
    EXPECT_EQ( QC_STATUS_NULL_PTR, str.Append( nullptr ) );

    EXPECT_EQ( QC_STATUS_OK, str.Append( "abc", 3u ) );
    // Would need 5 characters of capacity but only 4 exist.
    EXPECT_EQ( QC_STATUS_NOMEM, str.Append( "de", 2u ) );
    // The rejected append leaves the string untouched.
    EXPECT_STREQ( "abc", str.CStr() );
    EXPECT_EQ( 3u, str.Size() );

    // Exactly filling capacity is allowed.
    EXPECT_EQ( QC_STATUS_OK, str.Append( "d", 1u ) );
    EXPECT_STREQ( "abcd", str.CStr() );
}

TEST( QCString, Append_CStringOverloadScansBounded )
{
    QCString<8> str;

    EXPECT_EQ( QC_STATUS_OK, str.Append( "hi" ) );
    EXPECT_STREQ( "hi", str.CStr() );

    // A C string longer than capacity is rejected, not truncated.
    EXPECT_EQ( QC_STATUS_NOMEM, str.Append( "0123456789" ) );
    EXPECT_STREQ( "hi", str.CStr() );
}

TEST( QCString, Append_UnterminatedInputIsTreatedAsTooLong )
{
    // BoundedLength never reads past T + 1 bytes: a buffer with no terminator
    // within the capacity window must be rejected rather than over-read.
    QCString<4> str;

    char raw[8];
    std::memset( raw, 'x', sizeof( raw ) );   // deliberately not terminated

    EXPECT_EQ( QC_STATUS_NOMEM, str.Append( raw ) );
    EXPECT_TRUE( str.IsEmpty() );
}

TEST( QCString, Assign_ReplacesContents )
{
    QCString<8> str;

    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc", 3u ) );
    EXPECT_STREQ( "abc", str.CStr() );

    // Assign replaces rather than appends.
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "z", 1u ) );
    EXPECT_STREQ( "z", str.CStr() );
    EXPECT_EQ( 1u, str.Size() );

    EXPECT_EQ( QC_STATUS_OK, str.Assign( "" ) );
    EXPECT_TRUE( str.IsEmpty() );
}

TEST( QCString, Assign_RejectsNullPointerAndOverflow )
{
    QCString<4> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ab" ) );

    EXPECT_EQ( QC_STATUS_NULL_PTR, str.Assign( nullptr, 1u ) );
    EXPECT_EQ( QC_STATUS_NULL_PTR, str.Assign( nullptr ) );
    EXPECT_EQ( QC_STATUS_NOMEM, str.Assign( "01234", 5u ) );

    // Failed assignments leave the previous contents intact.
    EXPECT_STREQ( "ab", str.CStr() );
}

TEST( QCString, OperatorAssign_FromCStringDeleted )
{
    EXPECT_FALSE( ( std::is_assignable<QCString<8> &, const char *>::value ) );
}

TEST( QCString, RemoveAt_ShiftsTailAndRejectsBadIndex )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcd" ) );

    EXPECT_EQ( QC_STATUS_OK, str.RemoveAt( 1u ) );
    EXPECT_STREQ( "acd", str.CStr() );
    EXPECT_EQ( 3u, str.Size() );

    // Index == Size() is out of range.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveAt( 3u ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveAt( OpaqueIndex( 99u ) ) );
    EXPECT_STREQ( "acd", str.CStr() );

    // Removing the last remaining characters empties the string.
    EXPECT_EQ( QC_STATUS_OK, str.RemoveAt( 2u ) );
    EXPECT_EQ( QC_STATUS_OK, str.RemoveAt( 0u ) );
    EXPECT_STREQ( "c", str.CStr() );
}

TEST( QCString, RemoveRange_RemovesHalfOpenRange )
{
    QCString<16> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcdef" ) );

    // [1, 3) removes 'b' and 'c' but keeps 'd'.
    EXPECT_EQ( QC_STATUS_OK, str.RemoveRange( 1u, 3u ) );
    EXPECT_STREQ( "adef", str.CStr() );

    // An empty range is rejected rather than treated as a no-op: the guard is
    // startIndex >= endIndex, so start == end is out of contract.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveRange( 2u, 2u ) );
    EXPECT_STREQ( "adef", str.CStr() );

    // Removing to the end.
    EXPECT_EQ( QC_STATUS_OK, str.RemoveRange( 1u, str.Size() ) );
    EXPECT_STREQ( "a", str.CStr() );
}

TEST( QCString, RemoveRange_RejectsInvalidRange )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcd" ) );

    // start > end, and end beyond size.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveRange( 3u, 1u ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveRange( 0u, 99u ) );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.RemoveRange( 99u, 99u ) );
    EXPECT_STREQ( "abcd", str.CStr() );
}

TEST( QCString, Clear_EmptiesAndKeepsTerminator )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );

    EXPECT_EQ( QC_STATUS_OK, str.Clear() );
    EXPECT_TRUE( str.IsEmpty() );
    EXPECT_EQ( 0u, str.Size() );
    EXPECT_STREQ( "", str.CStr() );

    // Clear on an already-empty string is a no-op, not a failure.
    EXPECT_EQ( QC_STATUS_OK, str.Clear() );
    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'x' ) );
    EXPECT_STREQ( "x", str.CStr() );
}

TEST( QCString, At_SeatsReferenceAndRejectsBadIndex )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ab" ) );

    char fallback = '?';
    std::reference_wrapper<char> ref( fallback );
    EXPECT_EQ( QC_STATUS_OK, str.At( 0u, ref ) );
    EXPECT_EQ( 'a', ref.get() );

    // The seated reference writes through to storage.
    ref.get() = 'A';
    EXPECT_STREQ( "Ab", str.CStr() );

    // Index == Size() is out of range and reported as NO_RESOURCE.
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, str.At( 2u, ref ) );
}

TEST( QCString, At_ConstOverloadIsReadable )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "xy" ) );

    const QCString<8> &constStr = str;
    const char fallback = '?';
    std::reference_wrapper<const char> ref( fallback );
    EXPECT_EQ( QC_STATUS_OK, constStr.At( 1u, ref ) );
    EXPECT_EQ( 'y', ref.get() );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, constStr.At( 2u, ref ) );
}

TEST( QCString, FrontBack_ReportEmptyAndSeatEnds )
{
    QCString<8> str;

    char fallback = '?';
    std::reference_wrapper<char> ref( fallback );
    EXPECT_EQ( QC_STATUS_BAD_STATE, str.Front( ref ) );
    EXPECT_EQ( QC_STATUS_BAD_STATE, str.Back( ref ) );

    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );
    EXPECT_EQ( QC_STATUS_OK, str.Front( ref ) );
    EXPECT_EQ( 'a', ref.get() );
    EXPECT_EQ( QC_STATUS_OK, str.Back( ref ) );
    EXPECT_EQ( 'c', ref.get() );

    const QCString<8> &constStr = str;
    const char constFallback = '?';
    std::reference_wrapper<const char> constRef( constFallback );
    EXPECT_EQ( QC_STATUS_OK, constStr.Front( constRef ) );
    EXPECT_EQ( 'a', constRef.get() );
    EXPECT_EQ( QC_STATUS_OK, constStr.Back( constRef ) );
    EXPECT_EQ( 'c', constRef.get() );
}

TEST( QCString, Insert_ShiftsTailAndValidates )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ac" ) );

    EXPECT_EQ( QC_STATUS_OK, str.Insert( 1u, 'b' ) );
    EXPECT_STREQ( "abc", str.CStr() );

    // index == Size() appends.
    EXPECT_EQ( QC_STATUS_OK, str.Insert( str.Size(), 'd' ) );
    EXPECT_STREQ( "abcd", str.CStr() );

    // index > Size() is rejected.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.Insert( 99u, 'x' ) );
    EXPECT_STREQ( "abcd", str.CStr() );

    // Insert at the front.
    EXPECT_EQ( QC_STATUS_OK, str.Insert( 0u, 'Z' ) );
    EXPECT_STREQ( "Zabcd", str.CStr() );
}

TEST( QCString, Insert_AtCapacityReturnsNomem )
{
    QCString<3> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );

    EXPECT_EQ( QC_STATUS_NOMEM, str.Insert( 0u, 'x' ) );
    EXPECT_STREQ( "abc", str.CStr() );
}

TEST( QCString, Substr_CopiesAndClampsCount )
{
    QCString<16> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcdef" ) );

    QCString<16> out;
    EXPECT_EQ( QC_STATUS_OK, str.Substr( 1u, 3u, out ) );
    EXPECT_STREQ( "bcd", out.CStr() );

    // count is clamped to the remaining length, matching std::string::substr.
    EXPECT_EQ( QC_STATUS_OK, str.Substr( 4u, 99u, out ) );
    EXPECT_STREQ( "ef", out.CStr() );

    // startIndex == size yields an empty result.
    EXPECT_EQ( QC_STATUS_OK, str.Substr( str.Size(), 3u, out ) );
    EXPECT_TRUE( out.IsEmpty() );

    // A smaller destination that cannot hold the request reports NOMEM.
    QCString<2> small;
    EXPECT_EQ( QC_STATUS_NOMEM, str.Substr( 0u, 6u, small ) );
}

TEST( QCString, Substr_RejectsBadStartAndSelfAliasing )
{
    QCString<16> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );

    QCString<16> out;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.Substr( OpaqueIndex( 99u ), 1u, out ) );

    // Writing a substring of *this into *this is rejected outright.
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, str.Substr( 0u, 2u, str ) );
    EXPECT_STREQ( "abc", str.CStr() );
}

TEST( QCString, Find_LocatesFirstOccurrenceFromIndex )
{
    QCString<16> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcabc" ) );

    uint32_t index = 999u;
    EXPECT_EQ( QC_STATUS_OK, str.Find( "bc", 2u, 0u, index ) );
    EXPECT_EQ( 1u, index );

    // fromIndex skips the earlier match.
    EXPECT_EQ( QC_STATUS_OK, str.Find( "bc", 2u, 2u, index ) );
    EXPECT_EQ( 4u, index );

    // A miss leaves outIndex untouched.
    index = 777u;
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, str.Find( "zz", 2u, 0u, index ) );
    EXPECT_EQ( 777u, index );
}

TEST( QCString, Find_RejectsNullNeedleAndOversizedSearch )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );

    uint32_t index = 0u;
    EXPECT_EQ( QC_STATUS_NULL_PTR, str.Find( nullptr, 1u, 0u, index ) );

    // A needle longer than the string, or a fromIndex past the end, is a miss.
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, str.Find( "abcd", 4u, 0u, index ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, str.Find( "a", 1u, 99u, index ) );
}

TEST( QCString, Compare_AndEqualityOperators )
{
    QCString<8> a;
    QCString<8> b;
    EXPECT_EQ( QC_STATUS_OK, a.Assign( "abc" ) );
    EXPECT_EQ( QC_STATUS_OK, b.Assign( "abc" ) );

    EXPECT_EQ( 0, a.Compare( b ) );
    EXPECT_TRUE( a == b );
    EXPECT_FALSE( a != b );

    EXPECT_EQ( QC_STATUS_OK, b.Assign( "abd" ) );
    EXPECT_LT( a.Compare( b ), 0 );
    EXPECT_GT( b.Compare( a ), 0 );
    EXPECT_TRUE( a != b );

    // Self-comparison is zero.
    EXPECT_EQ( 0, a.Compare( a ) );

    // Comparison against a raw C string, including the nullptr case.
    EXPECT_TRUE( a == "abc" );
    EXPECT_TRUE( a != "xyz" );
    EXPECT_FALSE( a == nullptr );
    EXPECT_TRUE( a != nullptr );
}

TEST( QCString, Compare_AcrossDifferentCapacities )
{
    // The comparison operators are templated on the other capacity.
    QCString<8> small;
    QCString<32> large;
    EXPECT_EQ( QC_STATUS_OK, small.Assign( "same" ) );
    EXPECT_EQ( QC_STATUS_OK, large.Assign( "same" ) );

    EXPECT_TRUE( small == large );
    EXPECT_EQ( 0, small.Compare( large ) );
}

TEST( QCString, StartsWithEndsWithContains )
{
    QCString<16> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abcdef" ) );

    EXPECT_TRUE( str.StartsWith( "abc", 3u ) );
    EXPECT_FALSE( str.StartsWith( "bcd", 3u ) );
    EXPECT_TRUE( str.EndsWith( "def", 3u ) );
    EXPECT_FALSE( str.EndsWith( "cde", 3u ) );
    EXPECT_TRUE( str.Contains( "cd", 2u ) );
    EXPECT_FALSE( str.Contains( "zz", 2u ) );

    // Pure queries: a null pointer or an over-long length yields false, never
    // an out-of-bounds read.
    EXPECT_FALSE( str.StartsWith( nullptr, 1u ) );
    EXPECT_FALSE( str.EndsWith( nullptr, 1u ) );
    EXPECT_FALSE( str.Contains( nullptr, 1u ) );
    EXPECT_FALSE( str.StartsWith( "abcdefgh", 8u ) );
    EXPECT_FALSE( str.EndsWith( "abcdefgh", 8u ) );
}

TEST( QCString, Resize_GrowsWithFillAndShrinks )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ab" ) );

    EXPECT_EQ( QC_STATUS_OK, str.Resize( 5u, 'x' ) );
    EXPECT_EQ( 5u, str.Size() );
    EXPECT_STREQ( "abxxx", str.CStr() );

    // Shrinking re-terminates at the new size.
    EXPECT_EQ( QC_STATUS_OK, str.Resize( 2u ) );
    EXPECT_EQ( 2u, str.Size() );
    EXPECT_STREQ( "ab", str.CStr() );

    // Resize to zero empties the string.
    EXPECT_EQ( QC_STATUS_OK, str.Resize( 0u ) );
    EXPECT_TRUE( str.IsEmpty() );
    EXPECT_STREQ( "", str.CStr() );
}

TEST( QCString, Resize_BeyondCapacityReturnsNomem )
{
    QCString<4> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "ab" ) );

    EXPECT_EQ( QC_STATUS_NOMEM, str.Resize( 5u ) );
    EXPECT_EQ( 2u, str.Size() );   // unchanged by the rejected resize

    // Resize to exactly T is allowed.
    EXPECT_EQ( QC_STATUS_OK, str.Resize( 4u, 'z' ) );
    EXPECT_STREQ( "abzz", str.CStr() );
}

TEST( QCString, Swap_ExchangesContents )
{
    QCString<8> a;
    QCString<8> b;
    EXPECT_EQ( QC_STATUS_OK, a.Assign( "aaa" ) );
    EXPECT_EQ( QC_STATUS_OK, b.Assign( "b" ) );

    EXPECT_EQ( QC_STATUS_OK, a.Swap( b ) );
    EXPECT_STREQ( "b", a.CStr() );
    EXPECT_EQ( 1u, a.Size() );
    EXPECT_STREQ( "aaa", b.CStr() );
    EXPECT_EQ( 3u, b.Size() );

    // Self-swap is a no-op, not a corruption.
    EXPECT_EQ( QC_STATUS_OK, a.Swap( a ) );
    EXPECT_STREQ( "b", a.CStr() );
    EXPECT_EQ( 1u, a.Size() );
}

TEST( QCString, Data_ExposesNullTerminatedStorage )
{
    QCString<8> str;
    EXPECT_EQ( QC_STATUS_OK, str.Assign( "abc" ) );

    char *pData = str.Data();
    ASSERT_NE( nullptr, pData );
    EXPECT_EQ( 'a', pData[0] );
    EXPECT_EQ( '\0', pData[str.Size()] );

    // Writing through Data() is visible via CStr().
    pData[0] = 'A';
    EXPECT_STREQ( "Abc", str.CStr() );

    const QCString<8> &constStr = str;
    EXPECT_STREQ( "Abc", constStr.Data() );
    EXPECT_EQ( constStr.Data(), constStr.CStr() );
}

TEST( QCString, CopyAndAssign_AreIndependent )
{
    QCString<8> source;
    EXPECT_EQ( QC_STATUS_OK, source.Assign( "abc" ) );

    QCString<8> copy( source );
    EXPECT_STREQ( "abc", copy.CStr() );

    // Mutating the copy must not disturb the source.
    EXPECT_EQ( QC_STATUS_OK, copy.PushBack( 'd' ) );
    EXPECT_STREQ( "abcd", copy.CStr() );
    EXPECT_STREQ( "abc", source.CStr() );

    QCString<8> assigned;
    assigned = source;
    EXPECT_STREQ( "abc", assigned.CStr() );
    EXPECT_EQ( 3u, assigned.Size() );
}

TEST( QCString, CapacityOne_DegenerateSizeStillWorks )
{
    QCString<1> str;

    EXPECT_EQ( 1u, str.Capacity() );
    EXPECT_EQ( QC_STATUS_OK, str.PushBack( 'a' ) );
    EXPECT_EQ( QC_STATUS_NOMEM, str.PushBack( 'b' ) );
    EXPECT_STREQ( "a", str.CStr() );
    EXPECT_EQ( QC_STATUS_OK, str.PopBack() );
    EXPECT_TRUE( str.IsEmpty() );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
