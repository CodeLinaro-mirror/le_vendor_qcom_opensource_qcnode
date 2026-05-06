// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/DMABUFFAllocator.hpp"
#include "gtest/gtest.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using namespace QC;
using namespace QC::Memory;

class Test_DMABUFFAllocator : public testing::Test
{

protected:
    void SetUp() override
    {
        allocatorIfs =
                new DMABUFFAllocator( { "QC_MEMORY_ALLOCATOR_DMA" }, QC_MEMORY_ALLOCATOR_DMA );
    }

    void TearDown() override { allocatorIfs->~QCMemoryAllocatorIfs(); }

    QCMemoryAllocatorIfs *allocatorIfs;
};

TEST_F( Test_DMABUFFAllocator, SANITY_1 )
{
    QCBufferPropBase_t request;
    request.size = 10;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response.pBuf,
               (long long unsigned int) response.pBuf & ( ~( request.alignment - 1 ) ) );
    ASSERT_EQ( response.size, 10 );
    ASSERT_EQ( response.cache, QC_CACHEABLE );
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_OK, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_2 )
{
    QCBufferPropBase_t request;
    request.size = 128;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response.pBuf,
               (long long unsigned int) response.pBuf & ( ~( request.alignment - 1 ) ) );
    ASSERT_EQ( response.size, 128 );
    ASSERT_EQ( response.cache, QC_CACHEABLE );
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_OK, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_3 )
{
    QCBufferPropBase_t request;
    request.size = 10;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response.pBuf,
               (long long unsigned int) response.pBuf & ( ~( request.alignment - 1 ) ) );
    ASSERT_EQ( response.size, 10 );
    ASSERT_EQ( response.cache, QC_CACHEABLE );
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_OK, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_4 )
{
    QCBufferPropBase_t request;
    request.size = 10;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE_NON;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( response.pBuf, nullptr );
    ASSERT_EQ( response.size, 0 );
    ASSERT_EQ( response.cache, QC_MEMORY_DEFAULT_CACHE_ATTRIBUTES );
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    request.cache = QC_CACHEABLE;
    request.size = 0;
    status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_5 )
{
    QCBufferPropBase_t request;
    request.size = 10;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response.pBuf,
               (long long unsigned int) response.pBuf & ( ~( request.alignment - 1 ) ) );
    ASSERT_EQ( response.size, 10 );
    ASSERT_EQ( response.cache, QC_CACHEABLE );

    uint64_t temp = response.dmaHandle;
    response.dmaHandle = static_cast<uint64_t>( -1 );
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    response.dmaHandle = temp;
    response.size = 0;
    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    response.size = 10;

    status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_OK, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_6 )
{
    QCBufferPropBase_t request;
    request.size = 10;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT * 2;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_7 )
{
    QCBufferPropBase_t request;
    request.size = 0xFFFFFFFFFFFFFFFF;
    request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request.cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response;

    QCStatus_e status = allocatorIfs->Allocate( request, response );
    ASSERT_EQ( QC_STATUS_NOMEM, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_8 )
{

    QCBufferDescriptorBase_t response;

    response.size = 23;
    response.cache = QC_CACHEABLE;
    response.pBuf = (void *) 1000;
    response.dmaHandle = 10000;
    QCStatus_e status = allocatorIfs->Free( response );
    ASSERT_EQ( QC_STATUS_FAIL, status );
}


TEST_F( Test_DMABUFFAllocator, SANITY_multiple_allocations_1 )
{
    QCBufferPropBase_t request[4];
    request[0].size = 10;
    request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[0].cache = QC_CACHEABLE;

    request[1].size = 20;
    request[1].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[1].cache = QC_CACHEABLE;

    request[2].size = 1024 * 2;
    request[2].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[2].cache = QC_CACHEABLE;

    request[3].size = 1024 * 10;
    request[3].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[3].cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response[4];

    QCStatus_e status = allocatorIfs->Allocate( request[0], response[0] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[0].pBuf,
               (long long unsigned int) response[0].pBuf & ( ~( request[0].alignment - 1 ) ) );
    ASSERT_EQ( response[0].size, request[0].size );
    ASSERT_EQ( response[0].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[1], response[1] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[1].pBuf,
               (long long unsigned int) response[1].pBuf & ( ~( request[1].alignment - 1 ) ) );
    ASSERT_EQ( response[1].size, request[1].size );
    ASSERT_EQ( response[1].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[2], response[2] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[2].pBuf,
               (long long unsigned int) response[2].pBuf & ( ~( request[2].alignment - 1 ) ) );
    ASSERT_EQ( response[2].size, request[2].size );
    ASSERT_EQ( response[2].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[3], response[3] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[3].pBuf,
               (long long unsigned int) response[3].pBuf & ( ~( request[3].alignment - 1 ) ) );
    ASSERT_EQ( response[3].size, request[3].size );
    ASSERT_EQ( response[3].cache, QC_CACHEABLE );
    status = allocatorIfs->Free( response[3] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[2] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[1] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[0] );
    ASSERT_EQ( QC_STATUS_OK, status );
}

TEST_F( Test_DMABUFFAllocator, SANITY_multiple_allocations_2 )
{
    QCBufferPropBase_t request[4];
    request[0].size = 10;
    request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[0].cache = QC_CACHEABLE;

    request[1].size = 20;
    request[1].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[1].cache = QC_CACHEABLE;

    request[2].size = 1024 * 2;
    request[2].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[2].cache = QC_CACHEABLE;

    request[3].size = 1024 * 10;
    request[3].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[3].cache = QC_CACHEABLE;
    QCBufferDescriptorBase_t response[4];

    QCStatus_e status = allocatorIfs->Allocate( request[0], response[0] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[0].pBuf,
               (long long unsigned int) response[0].pBuf & ( ~( request[0].alignment - 1 ) ) );
    ASSERT_EQ( response[0].size, request[0].size );
    ASSERT_EQ( response[0].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[1], response[1] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[1].pBuf,
               (long long unsigned int) response[1].pBuf & ( ~( request[1].alignment - 1 ) ) );
    ASSERT_EQ( response[1].size, request[1].size );
    ASSERT_EQ( response[1].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[2], response[2] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[2].pBuf,
               (long long unsigned int) response[2].pBuf & ( ~( request[2].alignment - 1 ) ) );
    ASSERT_EQ( response[2].size, request[2].size );
    ASSERT_EQ( response[2].cache, QC_CACHEABLE );

    status = allocatorIfs->Allocate( request[3], response[3] );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( (long long unsigned int) response[3].pBuf,
               (long long unsigned int) response[3].pBuf & ( ~( request[3].alignment - 1 ) ) );
    ASSERT_EQ( response[3].size, request[3].size );
    ASSERT_EQ( response[3].cache, QC_CACHEABLE );

    status = allocatorIfs->Free( response[3] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[2] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[1] );
    ASSERT_EQ( QC_STATUS_OK, status );

    status = allocatorIfs->Free( response[0] );
    ASSERT_EQ( QC_STATUS_OK, status );

    QCBufferPropBase_t badRequest;
    request[0].size = 10;
    request[0].alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
    request[0].cache = QC_CACHEABLE_WRITE_THROUGH;

    status = allocatorIfs->Allocate( badRequest, response[1] );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
}


/**
 * @test ST_DMA_AllocFree_128_Loop_100000
 * @brief Single-threaded stress test: Allocate and free 128-byte DMA buffers in a tight loop
 *
 * @details
 * - Performs 100,000 iterations of:
 *   - Allocate a 128-byte DMA buffer with default alignment and cacheable attributes
 *   - Validate buffer pointer is non-null and properly aligned
 *   - Validate buffer size matches request
 *   - Validate cache attributes are correct
 *   - Validate DMA handle is valid (non-zero)
 *   - Free the buffer
 * - Goal: Detect memory leaks, DMA handle exhaustion, and allocation/free correctness
 * - Single allocator instance reused across all iterations
 *
 * @note This is a single-threaded stress test focused on resource churn
 */
TEST_F( Test_DMABUFFAllocator, ST_DMA_AllocFree_128_Loop_100000 )
{
    // Total iterations for the stress loop
    const int iters = 100000;

    // Single allocator instance reused across all iterations
    DMABUFFAllocator allocatorIfs( { "QC_MEMORY_ALLOCATOR_DMA" }, QC_MEMORY_ALLOCATOR_DMA );

    for ( int i = 0; i < iters; ++i )
    {
        // Build a standard 128-byte request with default alignment/cache attributes
        QCBufferPropBase_t request;
        request.size = 128;
        request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
        request.cache = QC_CACHEABLE;

        // Allocate and capture the resulting descriptor
        QCBufferDescriptorBase_t response;
        QCStatus_e status = allocatorIfs.Allocate( request, response );

        // Verify allocation succeeded and the descriptor is valid
        ASSERT_EQ( QC_STATUS_OK, status );
        ASSERT_NE( response.pBuf, nullptr );
        // Alignment check: address must be a multiple of 'alignment'
        ASSERT_EQ( (unsigned long long) response.pBuf,
                   (unsigned long long) response.pBuf & ~( request.alignment - 1 ) );
        ASSERT_EQ( response.size, 128 );
        ASSERT_EQ( response.cache, QC_CACHEABLE );
        // DMA-specific: validate DMA handle is valid
        ASSERT_NE( response.dmaHandle, 0 );

        // Free the buffer; allocator must return OK
        status = allocatorIfs.Free( response );
        ASSERT_EQ( QC_STATUS_OK, status );
    }
}

/**
 * @test Concurrency_DMA_AllocFree_ProducerConsumer_2Threads_128_Loop_100000
 * @brief Run DMA allocation on one thread and free on another using a thread-safe queue
 *
 * @details
 * - Producer thread:
 *   - Iterates 100,000 times
 *   - Allocates 128-byte DMA buffers with default alignment and cacheable attributes
 *   - Validates buffer pointer, size, alignment, cache attributes, and DMA handle
 *   - Enqueues buffer descriptors to a thread-safe queue
 * - Consumer thread:
 *   - Dequeues buffer descriptors from the queue
 *   - Frees DMA buffers
 *   - Continues until producer signals completion and queue is empty
 * - Synchronization:
 *   - Uses mutex + condition_variable to guard a std::deque of descriptors
 *   - Atomic counters track produced and consumed buffers
 * - Final validation:
 *   - Verifies all produced buffers were consumed
 *   - Ensures no memory leaks or orphaned DMA handles
 *
 * @note Validates cross-thread correctness of DMA Allocate/Free operations
 * @note Tests DMA handle validity across thread boundaries
 * @see QC::Memory::DMABUFFAllocator
 * @see QC::Memory::QCBufferDescriptorBase_t
 */
TEST_F( Test_DMABUFFAllocator, Concurrency_DMA_AllocFree_ProducerConsumer_2Threads_128_Loop_100000 )
{
    const int iters = 100000;

    DMABUFFAllocator allocatorIfs( { "QC_MEMORY_ALLOCATOR_DMA" }, QC_MEMORY_ALLOCATOR_DMA );

    std::deque<QCBufferDescriptorBase_t> q;
    std::mutex m;
    std::condition_variable cv;
    std::atomic<int> produced{ 0 };
    std::atomic<int> consumed{ 0 };
    std::atomic<bool> done{ false };

    auto producer = [&] {
        for ( int i = 0; i < iters; ++i )
        {
            QCBufferPropBase_t request{};
            request.size = 128;
            request.alignment = QC_MEMORY_DEFAULT_ALLIGNMENT;
            request.cache = QC_CACHEABLE;

            QCBufferDescriptorBase_t resp{};
            QCStatus_e st = allocatorIfs.Allocate( request, resp );
            ASSERT_EQ( QC_STATUS_OK, st );
            ASSERT_NE( resp.pBuf, nullptr );
            ASSERT_EQ( (unsigned long long) resp.pBuf,
                       (unsigned long long) resp.pBuf & ~( request.alignment - 1 ) );
            ASSERT_EQ( resp.size, 128 );
            ASSERT_EQ( resp.cache, QC_CACHEABLE );
            // DMA-specific: validate DMA handle
            ASSERT_NE( resp.dmaHandle, 0 );

            {
                std::lock_guard<std::mutex> lk( m );
                q.push_back( resp );
                ++produced;
            }
            cv.notify_one();
        }
        done.store( true );
        cv.notify_all();
    };

    auto consumer = [&] {
        while ( true )
        {
            QCBufferDescriptorBase_t item{};
            {
                std::unique_lock<std::mutex> lk( m );
                cv.wait( lk, [&] { return !q.empty() || done.load(); } );
                if ( q.empty() && done.load() ) break;
                if ( q.empty() ) continue;
                item = q.front();
                q.pop_front();
            }
            QCStatus_e st = allocatorIfs.Free( item );
            ASSERT_EQ( QC_STATUS_OK, st );
            ++consumed;
        }
    };

    std::thread tProd( producer );
    std::thread tCons( consumer );
    tProd.join();
    tCons.join();

    ASSERT_EQ( produced.load(), iters );
    ASSERT_EQ( consumed.load(), iters );
}
