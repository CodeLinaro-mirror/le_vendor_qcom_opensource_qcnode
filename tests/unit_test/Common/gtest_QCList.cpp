// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Common/QCList.hpp"

#include "gtest/gtest.h"

using namespace QC;

TEST( QCList, RequiresInitialization )
{
    QCList<int> list;
    int *value = nullptr;
    bool isEmpty = false;
    QCList<int>::Iterator iterator;

    EXPECT_EQ( QC_STATUS_BAD_STATE, list.Front( value ) );
    EXPECT_EQ( nullptr, value );
    EXPECT_EQ( QC_STATUS_BAD_STATE, list.Empty( isEmpty ) );
    EXPECT_EQ( QC_STATUS_BAD_STATE, list.PushBack( 1 ) );
    EXPECT_EQ( QC_STATUS_BAD_STATE, list.Begin( iterator ) );
}

TEST( QCList, SupportsRuntimeCapacityAndListOperations )
{
    QCList<int> list;
    QCList<int>::Iterator first;
    QCList<int>::Iterator inserted;
    QCList<int>::Iterator next;
    int *value = nullptr;
    uint32_t count = 0U;
    uint32_t capacity = 0U;
    uint32_t removed = 0U;

    ASSERT_EQ( QC_STATUS_OK, list.Init( 3U ) );
    EXPECT_EQ( QC_STATUS_ALREADY, list.Init( 3U ) );
    ASSERT_EQ( QC_STATUS_OK, list.MaxSize( capacity ) );
    EXPECT_EQ( 3U, capacity );

    ASSERT_EQ( QC_STATUS_OK, list.PushBack( 1 ) );
    ASSERT_EQ( QC_STATUS_OK, list.PushBack( 3 ) );
    ASSERT_EQ( QC_STATUS_OK, list.Begin( first ) );
    ++first;
    ASSERT_EQ( QC_STATUS_OK, list.Insert( first, 2, inserted ) );
    EXPECT_EQ( 2, *inserted );
    EXPECT_EQ( QC_STATUS_NO_RESOURCE, list.PushBack( 4 ) );

    ASSERT_EQ( QC_STATUS_OK, list.Front( value ) );
    EXPECT_EQ( 1, *value );
    ASSERT_EQ( QC_STATUS_OK, list.Back( value ) );
    EXPECT_EQ( 3, *value );

    ASSERT_EQ( QC_STATUS_OK, list.Erase( inserted, next ) );
    EXPECT_EQ( 3, *next );
    ASSERT_EQ( QC_STATUS_OK, list.PushFront( 2 ) );
    ASSERT_EQ( QC_STATUS_OK, list.Remove( 1, removed ) );
    EXPECT_EQ( 1U, removed );
    ASSERT_EQ( QC_STATUS_OK, list.Size( count ) );
    EXPECT_EQ( 2U, count );

    ASSERT_EQ( QC_STATUS_OK, list.PopBack() );
    ASSERT_EQ( QC_STATUS_OK, list.PopFront() );
    EXPECT_EQ( QC_STATUS_BAD_STATE, list.PopFront() );
    EXPECT_EQ( QC_STATUS_OK, list.Clear() );
}

TEST( QCList, EmplaceConstructsWithoutDefaultConstructingValues )
{
    struct Payload
    {
        explicit Payload( int input ) noexcept : value( input ) {}
        Payload() = delete;
        int value;
    };

    QCList<Payload> list;
    QCList<Payload>::Iterator inserted;

    ASSERT_EQ( QC_STATUS_OK, list.Init( 1U ) );
    ASSERT_EQ( QC_STATUS_OK, list.EmplaceBack( inserted, 42 ) );
    EXPECT_EQ( 42, inserted->value );
}

#ifndef GTEST_QCNODE
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
#endif
