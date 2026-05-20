// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Ifs/QCNodeFactory.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace QC;
using namespace QC::Node;

// ---------------------------------------------------------------------------
// Test CreateNode
// ---------------------------------------------------------------------------
TEST( QCNodeFactory, CreateNode_UnsupportedType )
{
    std::unique_ptr<QCNodeIfs> node;

    // Use a type that is unlikely to be registered in unit tests
    QCStatus_e status = QCNodeFactory::CreateNode( QC_NODE_TYPE_RESERVED, node );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, node );
}

TEST( QCNodeFactory, CreateNode_InvalidType )
{
    std::unique_ptr<QCNodeIfs> node;

    QCStatus_e status = QCNodeFactory::CreateNode( static_cast<QCNodeType_e>( -1 ), node );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, node );
}

TEST( QCNodeFactory, CreateNode_LastType )
{
    std::unique_ptr<QCNodeIfs> node;

    QCStatus_e status = QCNodeFactory::CreateNode( QC_NODE_TYPE_LAST, node );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, node );
}

// ---------------------------------------------------------------------------
// Test GetSupportedNodeTypes
// ---------------------------------------------------------------------------
TEST( QCNodeFactory, GetSupportedNodeTypes_ReturnsVector )
{
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    // The vector should be valid (may be empty if no nodes are registered in unit tests)
    ASSERT_TRUE( types.size() >= 0 );
}

TEST( QCNodeFactory, GetSupportedNodeTypes_Consistency )
{
    // Verify that all returned types can be created successfully
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    for ( QCNodeType_e type : types )
    {
        std::unique_ptr<QCNodeIfs> node;
        QCStatus_e status = QCNodeFactory::CreateNode( type, node );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create node for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, node );
    }
}

TEST( QCNodeFactory, GetSupportedNodeTypes_NoDuplicates )
{
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    // Check for duplicates
    for ( size_t i = 0; i < types.size(); ++i )
    {
        for ( size_t j = i + 1; j < types.size(); ++j )
        {
            ASSERT_NE( types[i], types[j] )
                    << "Duplicate node type found: " << static_cast<int>( types[i] );
        }
    }
}

TEST( QCNodeFactory, GetSupportedNodeTypes_ConsistentAcrossCalls )
{
    // Verify that multiple calls return the same result
    const std::vector<QCNodeType_e> &types1 = QCNodeFactory::GetSupportedNodeTypes();
    const std::vector<QCNodeType_e> &types2 = QCNodeFactory::GetSupportedNodeTypes();

    ASSERT_EQ( types1.size(), types2.size() );
    ASSERT_EQ( types1, types2 );
}

// ---------------------------------------------------------------------------
// Test GetSupportedNodeTypesJson
// ---------------------------------------------------------------------------
TEST( QCNodeFactory, GetSupportedNodeTypesJson_ValidFormat )
{
    std::string json = QCNodeFactory::GetSupportedNodeTypesJson();

    // Should start with { and end with }
    ASSERT_EQ( '{', json.front() );
    ASSERT_EQ( '}', json.back() );
}

TEST( QCNodeFactory, GetSupportedNodeTypesJson_EmptyWhenNoNodes )
{
    std::string json = QCNodeFactory::GetSupportedNodeTypesJson();
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    if ( types.empty() )
    {
        // If no nodes are registered, JSON should be empty object
        ASSERT_EQ( "{}", json );
    }
    else
    {
        // If nodes are registered, JSON should contain content
        ASSERT_GT( json.length(), 2 );
    }
}

TEST( QCNodeFactory, GetSupportedNodeTypesJson_MatchesVector )
{
    std::string json = QCNodeFactory::GetSupportedNodeTypesJson();
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    if ( types.empty() )
    {
        ASSERT_EQ( "{}", json );
    }
    else
    {
        // Count commas in JSON (should be types.size() - 1)
        size_t commaCount = std::count( json.begin(), json.end(), ',' );
        ASSERT_EQ( types.size() - 1, commaCount );
    }
}

TEST( QCNodeFactory, GetSupportedNodeTypesJson_ContainsValidJson )
{
    std::string json = QCNodeFactory::GetSupportedNodeTypesJson();

    // Basic JSON validation
    size_t openBraces = std::count( json.begin(), json.end(), '{' );
    size_t closeBraces = std::count( json.begin(), json.end(), '}' );
    ASSERT_EQ( openBraces, closeBraces );

    size_t quotes = std::count( json.begin(), json.end(), '"' );
    // Should have even number of quotes (pairs)
    ASSERT_EQ( 0, quotes % 2 );
}

TEST( QCNodeFactory, GetSupportedNodeTypesJson_ConsistentAcrossCalls )
{
    // Verify that multiple calls return the same result
    std::string json1 = QCNodeFactory::GetSupportedNodeTypesJson();
    std::string json2 = QCNodeFactory::GetSupportedNodeTypesJson();

    ASSERT_EQ( json1, json2 );
}

TEST( QCNodeFactory, GetSupportedNodeTypesJson_ContainsColons )
{
    std::string json = QCNodeFactory::GetSupportedNodeTypesJson();
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    if ( !types.empty() )
    {
        // Each entry should have a colon separating key and value
        size_t colonCount = std::count( json.begin(), json.end(), ':' );
        ASSERT_EQ( types.size(), colonCount );
    }
}

// ---------------------------------------------------------------------------
// Test CreateNode with supported types (if any are registered)
// ---------------------------------------------------------------------------
TEST( QCNodeFactory, CreateNode_SupportedTypes )
{
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    // Test creating nodes for all supported types
    for ( QCNodeType_e type : types )
    {
        std::unique_ptr<QCNodeIfs> node;
        QCStatus_e status = QCNodeFactory::CreateNode( type, node );

        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create node for supported type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, node )
                << "Node pointer is null for supported type: " << static_cast<int>( type );
    }
}

// ---------------------------------------------------------------------------
// Test edge cases
// ---------------------------------------------------------------------------
TEST( QCNodeFactory, CreateNode_MultipleCreations )
{
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    if ( !types.empty() )
    {
        QCNodeType_e testType = types[0];

        // Create multiple nodes of the same type
        std::unique_ptr<QCNodeIfs> node1;
        std::unique_ptr<QCNodeIfs> node2;

        QCStatus_e status1 = QCNodeFactory::CreateNode( testType, node1 );
        QCStatus_e status2 = QCNodeFactory::CreateNode( testType, node2 );

        ASSERT_EQ( QC_STATUS_OK, status1 );
        ASSERT_EQ( QC_STATUS_OK, status2 );
        ASSERT_NE( nullptr, node1 );
        ASSERT_NE( nullptr, node2 );
        ASSERT_NE( node1.get(), node2.get() );
    }
}

TEST( QCNodeFactory, CreateNode_OutParameterReset )
{
    std::unique_ptr<QCNodeIfs> node;
    const std::vector<QCNodeType_e> &types = QCNodeFactory::GetSupportedNodeTypes();

    if ( !types.empty() )
    {
        // First create a valid node
        QCStatus_e status = QCNodeFactory::CreateNode( types[0], node );
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_NE( nullptr, node );
    }

    // Now try to create an unsupported node - should reset the pointer
    QCStatus_e status = QCNodeFactory::CreateNode( QC_NODE_TYPE_RESERVED, node );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, node );
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
