// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Ifs/QCNodeFrameDescriptorFactory.hpp"
#include "QC/Node/NodeFrameDescriptor.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <memory>
#include <vector>

using namespace QC;
using namespace QC::Node;

// ---------------------------------------------------------------------------
// QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR is registered for
// NodeFrameDescriptor in the production translation unit
// source/Node/Base/NodeFrameDescriptor.cpp (linked here via QCNode).  These
// tests therefore exercise the registration performed by production code; they
// must NOT register the type again, otherwise the duplicate-registration
// assertion in RegisterFrameDescriptorCreator() would fire.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test CreateFrameDescriptor — registry path
// ---------------------------------------------------------------------------
TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_RegisteredType )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 4U, descriptor );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_SingleBuffer )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 1U, descriptor );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_LargeBufferCount )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 64U, descriptor );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, descriptor );
}

// ---------------------------------------------------------------------------
// Test CreateFrameDescriptor — error paths
// ---------------------------------------------------------------------------
TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_UnregisteredType_Unsupported )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_LAST, 4U, descriptor );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_InvalidType_Unsupported )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            static_cast<QCNodeFrameDescType_e>( -1 ), 4U, descriptor );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_ZeroBuffers_BadArguments )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;

    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 0U, descriptor );

    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
    ASSERT_EQ( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_OutParameterResetOnError )
{
    // Pre-populate the out-parameter with a valid descriptor
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;
    QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 2U, descriptor );
    ASSERT_NE( nullptr, descriptor );

    // Unregistered type — out-parameter must be reset to null
    QCStatus_e status = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_LAST, 2U, descriptor );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, descriptor );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_MultipleCreations_IndependentObjects )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor1;
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor2;

    QCStatus_e status1 = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 3U, descriptor1 );
    QCStatus_e status2 = QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 3U, descriptor2 );

    ASSERT_EQ( QC_STATUS_OK, status1 );
    ASSERT_EQ( QC_STATUS_OK, status2 );
    ASSERT_NE( nullptr, descriptor1 );
    ASSERT_NE( nullptr, descriptor2 );
    ASSERT_NE( descriptor1.get(), descriptor2.get() );
}

// ---------------------------------------------------------------------------
// Test CreateFrameDescriptor — functional verification
// ---------------------------------------------------------------------------
TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_SetAndGetBuffer )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;
    QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 2U, descriptor );
    ASSERT_NE( nullptr, descriptor );

    QCBufferDescriptorBase_t buf;
    ASSERT_EQ( QC_STATUS_OK, buf.name.Assign( "TestBuffer" ) );
    buf.type = QC_BUFFER_TYPE_RAW;

    QCStatus_e setStatus = descriptor->SetBuffer( 0U, buf );
    ASSERT_EQ( QC_STATUS_OK, setStatus );

    QCBufferDescriptorBase_t &retrieved = descriptor->GetBuffer( 0U );
    ASSERT_EQ( buf.name, retrieved.name );
    ASSERT_EQ( buf.type, retrieved.type );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_SetBuffer_OutOfBound )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;
    QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 2U, descriptor );
    ASSERT_NE( nullptr, descriptor );

    QCBufferDescriptorBase_t buf;
    QCStatus_e status = descriptor->SetBuffer( 2U, buf );
    ASSERT_EQ( QC_STATUS_OUT_OF_BOUND, status );
}

TEST( QCNodeFrameDescriptorFactory, CreateFrameDescriptor_Clear )
{
    std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;
    QCNodeFrameDescriptorFactory::CreateFrameDescriptor(
            QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR, 1U, descriptor );
    ASSERT_NE( nullptr, descriptor );

    QCBufferDescriptorBase_t buf;
    ASSERT_EQ( QC_STATUS_OK, buf.name.Assign( "TestBuffer" ) );
    buf.type = QC_BUFFER_TYPE_IMAGE;
    descriptor->SetBuffer( 0U, buf );

    descriptor->Clear();
    QCBufferDescriptorBase_t &cleared = descriptor->GetBuffer( 0U );
    ASSERT_EQ( QC_BUFFER_TYPE_MAX, cleared.type );
}

// ---------------------------------------------------------------------------
// Test GetSupportedNodeFrameDescriptorTypes
// ---------------------------------------------------------------------------
TEST( QCNodeFrameDescriptorFactory, GetSupportedNodeFrameDescriptorTypes_NotEmpty )
{
    std::vector<QCNodeFrameDescType_e> types;
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types ),
               QC_STATUS_OK );

    ASSERT_FALSE( types.empty() );
}

TEST( QCNodeFrameDescriptorFactory, GetSupportedNodeFrameDescriptorTypes_ContainsRegisteredType )
{
    std::vector<QCNodeFrameDescType_e> types;
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types ),
               QC_STATUS_OK );

    auto it =
            std::find( types.begin(), types.end(), QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR );
    ASSERT_NE( types.end(), it );
}

TEST( QCNodeFrameDescriptorFactory, GetSupportedNodeFrameDescriptorTypes_NoDuplicates )
{
    std::vector<QCNodeFrameDescType_e> types;
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types ),
               QC_STATUS_OK );

    for ( size_t i = 0; i < types.size(); ++i )
    {
        for ( size_t j = i + 1; j < types.size(); ++j )
        {
            ASSERT_NE( types[i], types[j] )
                    << "Duplicate type found: " << static_cast<int>( types[i] );
        }
    }
}

TEST( QCNodeFrameDescriptorFactory, GetSupportedNodeFrameDescriptorTypes_ConsistentAcrossCalls )
{
    std::vector<QCNodeFrameDescType_e> types1;
    std::vector<QCNodeFrameDescType_e> types2;
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types1 ),
               QC_STATUS_OK );
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types2 ),
               QC_STATUS_OK );

    ASSERT_EQ( types1.size(), types2.size() );
    ASSERT_EQ( types1, types2 );
}

TEST( QCNodeFrameDescriptorFactory, GetSupportedNodeFrameDescriptorTypes_Consistency )
{
    // Verify that all returned types can be created successfully
    std::vector<QCNodeFrameDescType_e> types;
    ASSERT_EQ( QCNodeFrameDescriptorFactory::GetSupportedNodeFrameDescriptorTypes( types ),
               QC_STATUS_OK );

    for ( QCNodeFrameDescType_e type : types )
    {
        std::unique_ptr<QCFrameDescriptorNodeIfs> descriptor;
        QCStatus_e status =
                QCNodeFrameDescriptorFactory::CreateFrameDescriptor( type, 2U, descriptor );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create descriptor for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, descriptor );
    }
}

#ifndef GTEST_QCNODE
#if __CTC__
extern "C" void ctc_append_all( void );
#endif
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    int nVal = RUN_ALL_TESTS();
#if __CTC__
    ctc_append_all();
#endif
    return nVal;
}
#endif
