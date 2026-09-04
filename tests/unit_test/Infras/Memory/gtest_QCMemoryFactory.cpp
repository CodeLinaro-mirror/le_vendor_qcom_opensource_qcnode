// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/Ifs/QCMemoryFactory.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace QC;
using namespace QC::Memory;

// ---------------------------------------------------------------------------
// Test CreateAllocator
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, CreateAllocator_Heap )
{
    std::unique_ptr<QCMemoryAllocatorIfs> allocator;
    QCMemoryAllocatorConfigInit_t config = {};

    QCStatus_e status =
            QCMemoryFactory::CreateAllocator( QC_MEMORY_ALLOCATOR_HEAP, config, allocator );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, allocator );
}

TEST( QCMemoryFactory, CreateAllocator_DMA )
{
    std::unique_ptr<QCMemoryAllocatorIfs> allocator;
    QCMemoryAllocatorConfigInit_t config = {};

    QCStatus_e status =
            QCMemoryFactory::CreateAllocator( QC_MEMORY_ALLOCATOR_DMA, config, allocator );

#if defined( __linux__ ) || defined( __QNX__ )
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, allocator );
#else
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, allocator );
#endif
}

TEST( QCMemoryFactory, CreateAllocator_UnsupportedType )
{
    std::unique_ptr<QCMemoryAllocatorIfs> allocator;
    QCMemoryAllocatorConfigInit_t config = {};

    QCStatus_e status =
            QCMemoryFactory::CreateAllocator( QC_MEMORY_ALLOCATOR_LAST, config, allocator );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, allocator );
}

TEST( QCMemoryFactory, CreateAllocator_InvalidType )
{
    std::unique_ptr<QCMemoryAllocatorIfs> allocator;
    QCMemoryAllocatorConfigInit_t config = {};

    QCStatus_e status = QCMemoryFactory::CreateAllocator( static_cast<QCMemoryAllocator_e>( -1 ),
                                                          config, allocator );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, allocator );
}

TEST( QCMemoryFactory, CreateBufferDescriptor_CustomType )
{
    std::unique_ptr<QCBufferDescriptorBase_t> descriptor;

    QCStatus_e status =
            QCMemoryFactory::CreateBufferDescriptor( QC_BUFFER_TYPE_CUSTOM_0, descriptor );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, descriptor );
}

TEST( QCMemoryFactory, CreateBufferDescriptor_InvalidType )
{
    std::unique_ptr<QCBufferDescriptorBase_t> descriptor;

    QCStatus_e status = QCMemoryFactory::CreateBufferDescriptor( QC_BUFFER_TYPE_LAST, descriptor );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, descriptor );
}

// ---------------------------------------------------------------------------
// Test CreateMemoryManager
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, CreateMemoryManager_Local )
{
    std::unique_ptr<QCMemoryManagerIfs> manager;

    QCStatus_e status = QCMemoryFactory::CreateMemoryManager( QC_MEMORY_MANAGER_LOCAL, manager );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_NE( nullptr, manager );
}

TEST( QCMemoryFactory, CreateMemoryManager_UnsupportedType )
{
    std::unique_ptr<QCMemoryManagerIfs> manager;

    QCStatus_e status = QCMemoryFactory::CreateMemoryManager( QC_MEMORY_MANAGER_LAST, manager );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, manager );
}

TEST( QCMemoryFactory, CreateMemoryManager_InvalidType )
{
    std::unique_ptr<QCMemoryManagerIfs> manager;

    QCStatus_e status = QCMemoryFactory::CreateMemoryManager(
            static_cast<QCMemoryManagerType_e>( -1 ), manager );

    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, manager );
}

// ---------------------------------------------------------------------------
// Test GetSupportedAllocatorTypes
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedAllocatorTypes_NotEmpty )
{
    std::vector<QCMemoryAllocator_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypes( types ), QC_STATUS_OK );

    ASSERT_FALSE( types.empty() );
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypes_ContainsHeap )
{
    std::vector<QCMemoryAllocator_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypes( types ), QC_STATUS_OK );

    auto it = std::find( types.begin(), types.end(), QC_MEMORY_ALLOCATOR_HEAP );
    ASSERT_NE( types.end(), it );
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypes_PlatformSpecific )
{
    std::vector<QCMemoryAllocator_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypes( types ), QC_STATUS_OK );

#if defined( __linux__ ) || defined( __QNX__ )
    // DMA allocators should be present on Linux and QNX
    auto it = std::find( types.begin(), types.end(), QC_MEMORY_ALLOCATOR_DMA );
    ASSERT_NE( types.end(), it );
#else
    // DMA allocators should not be present on other platforms
    auto it = std::find( types.begin(), types.end(), QC_MEMORY_ALLOCATOR_DMA );
    ASSERT_EQ( types.end(), it );
#endif
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypes_Consistency )
{
    // Verify that all returned types can be created successfully
    std::vector<QCMemoryAllocator_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypes( types ), QC_STATUS_OK );
    QCMemoryAllocatorConfigInit_t config = {};

    for ( QCMemoryAllocator_e type : types )
    {
        std::unique_ptr<QCMemoryAllocatorIfs> allocator;
        QCStatus_e status = QCMemoryFactory::CreateAllocator( type, config, allocator );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create allocator for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, allocator );
    }
}

// ---------------------------------------------------------------------------
// Test GetSupportedBufferDescriptorTypes
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypes_NotEmpty )
{
    std::vector<QCBufferType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypes( types ), QC_STATUS_OK );

    ASSERT_FALSE( types.empty() );
}

TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypes_ContainsBasicTypes )
{
    std::vector<QCBufferType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypes( types ), QC_STATUS_OK );

    auto itRaw = std::find( types.begin(), types.end(), QC_BUFFER_TYPE_RAW );
    auto itImage = std::find( types.begin(), types.end(), QC_BUFFER_TYPE_IMAGE );
    auto itTensor = std::find( types.begin(), types.end(), QC_BUFFER_TYPE_TENSOR );
    auto itTxt = std::find( types.begin(), types.end(), QC_BUFFER_TYPE_TXT );

    ASSERT_NE( types.end(), itRaw );
    ASSERT_NE( types.end(), itImage );
    ASSERT_NE( types.end(), itTensor );
    ASSERT_NE( types.end(), itTxt );
}

TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypes_Consistency )
{
    // Verify that all returned types can be created successfully
    std::vector<QCBufferType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypes( types ), QC_STATUS_OK );

    for ( QCBufferType_e type : types )
    {
        std::unique_ptr<QCBufferDescriptorBase_t> descriptor;
        QCStatus_e status = QCMemoryFactory::CreateBufferDescriptor( type, descriptor );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create descriptor for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, descriptor );
    }
}

// ---------------------------------------------------------------------------
// Test GetSupportedMemoryManagerTypes
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedMemoryManagerTypes_NotEmpty )
{
    std::vector<QCMemoryManagerType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypes( types ), QC_STATUS_OK );

    ASSERT_FALSE( types.empty() );
}

TEST( QCMemoryFactory, GetSupportedMemoryManagerTypes_ContainsLocal )
{
    std::vector<QCMemoryManagerType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypes( types ), QC_STATUS_OK );

    auto it = std::find( types.begin(), types.end(), QC_MEMORY_MANAGER_LOCAL );
    ASSERT_NE( types.end(), it );
}

TEST( QCMemoryFactory, GetSupportedMemoryManagerTypes_Consistency )
{
    // Verify that all returned types can be created successfully
    std::vector<QCMemoryManagerType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypes( types ), QC_STATUS_OK );

    for ( QCMemoryManagerType_e type : types )
    {
        std::unique_ptr<QCMemoryManagerIfs> manager;
        QCStatus_e status = QCMemoryFactory::CreateMemoryManager( type, manager );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create manager for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, manager );
    }
}

// ---------------------------------------------------------------------------
// Test GetSupportedAllocatorTypesJson
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedAllocatorTypesJson_NotEmpty )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypesJson( json ), QC_STATUS_OK );

    ASSERT_FALSE( json.empty() );
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypesJson_ValidFormat )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypesJson( json ), QC_STATUS_OK );

    // Should start with { and end with }
    ASSERT_EQ( '{', json.front() );
    ASSERT_EQ( '}', json.back() );
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypesJson_ContainsHeap )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypesJson( json ), QC_STATUS_OK );

    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_ALLOCATOR_HEAP" ) );
}

TEST( QCMemoryFactory, GetSupportedAllocatorTypesJson_MatchesVector )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypesJson( json ), QC_STATUS_OK );
    std::vector<QCMemoryAllocator_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedAllocatorTypes( types ), QC_STATUS_OK );

    // Count commas in JSON (should be types.size() - 1)
    size_t commaCount = std::count( json.begin(), json.end(), ',' );
    ASSERT_EQ( types.size() - 1, commaCount );
}

// ---------------------------------------------------------------------------
// Test GetSupportedBufferDescriptorTypesJson
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypesJson_NotEmpty )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypesJson( json ), QC_STATUS_OK );

    ASSERT_FALSE( json.empty() );
}

TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypesJson_ValidFormat )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypesJson( json ), QC_STATUS_OK );

    // Should start with { and end with }
    ASSERT_EQ( '{', json.front() );
    ASSERT_EQ( '}', json.back() );
}

TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypesJson_ContainsBasicTypes )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypesJson( json ), QC_STATUS_OK );

    ASSERT_NE( std::string::npos, json.find( "QC_BUFFER_TYPE_RAW" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_BUFFER_TYPE_IMAGE" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_BUFFER_TYPE_TENSOR" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_BUFFER_TYPE_TXT" ) );
}

TEST( QCMemoryFactory, GetSupportedBufferDescriptorTypesJson_MatchesVector )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypesJson( json ), QC_STATUS_OK );
    std::vector<QCBufferType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedBufferDescriptorTypes( types ), QC_STATUS_OK );

    // Count commas in JSON (should be types.size() - 1)
    size_t commaCount = std::count( json.begin(), json.end(), ',' );
    ASSERT_EQ( types.size() - 1, commaCount );
}

// ---------------------------------------------------------------------------
// Test GetSupportedMemoryManagerTypesJson
// ---------------------------------------------------------------------------
TEST( QCMemoryFactory, GetSupportedMemoryManagerTypesJson_NotEmpty )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypesJson( json ), QC_STATUS_OK );

    ASSERT_FALSE( json.empty() );
}

TEST( QCMemoryFactory, GetSupportedMemoryManagerTypesJson_ValidFormat )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypesJson( json ), QC_STATUS_OK );

    // Should start with { and end with }
    ASSERT_EQ( '{', json.front() );
    ASSERT_EQ( '}', json.back() );
}

TEST( QCMemoryFactory, GetSupportedMemoryManagerTypesJson_ContainsLocal )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypesJson( json ), QC_STATUS_OK );

    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_MANAGER_LOCAL" ) );
}

TEST( QCMemoryFactory, GetSupportedMemoryManagerTypesJson_MatchesVector )
{
    std::string json;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypesJson( json ), QC_STATUS_OK );
    std::vector<QCMemoryManagerType_e> types;
    ASSERT_EQ( QCMemoryFactory::GetSupportedMemoryManagerTypes( types ), QC_STATUS_OK );

    // Count commas in JSON (should be types.size() - 1)
    size_t commaCount = std::count( json.begin(), json.end(), ',' );
    ASSERT_EQ( types.size() - 1, commaCount );
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
