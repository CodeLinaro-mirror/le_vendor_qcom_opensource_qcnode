// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"

#include "QC/Common/QCThreadSafeList.hpp"

using namespace QC;

TEST( QCThreadSafeList, PushPopAndCopyOut )
{
    QCThreadSafeList<int> list;
    int value = 0;

    EXPECT_EQ( QC_STATUS_BAD_STATE, list.PushBack( 8 ) );
    ASSERT_EQ( QC_STATUS_OK, list.Init( 2U ) );
    EXPECT_EQ( QC_STATUS_ALREADY, list.Init( 2U ) );
    EXPECT_EQ( QC_STATUS_OK, list.PushBack( 8 ) );
    EXPECT_EQ( QC_STATUS_OK, list.PushBack( 9 ) );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, list.PushBack( 10 ) );
    EXPECT_EQ( QC_STATUS_OK, list.PopFront( value ) );
    EXPECT_EQ( 8, value );
    EXPECT_EQ( QC_STATUS_OK, list.GetFront( value ) );
    EXPECT_EQ( 9, value );
}

TEST( QCThreadSafeList, FindRemoveAndForEach )
{
    QCThreadSafeList<int> list;
    uint32_t count = 0U;
    int sum = 0;

    ASSERT_EQ( QC_STATUS_OK, list.Init( 4U ) );
    EXPECT_EQ( QC_STATUS_OK, list.PushBack( 3 ) );
    EXPECT_EQ( QC_STATUS_OK, list.PushBack( 5 ) );
    EXPECT_TRUE( list.Contains( 5 ) );
    EXPECT_EQ( QC_STATUS_OK, list.RemoveFirst( 3 ) );
    EXPECT_EQ( QC_STATUS_OK, list.ForEach( [&count, &sum]( const int value ) {
        ++count;
        sum += value;
    } ) );
    EXPECT_EQ( 1U, count );
    EXPECT_EQ( 5, sum );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
