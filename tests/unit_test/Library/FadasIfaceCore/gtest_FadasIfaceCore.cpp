// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "gtest/gtest.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "AEEStdErr.h"
#include "FadasIface.h"
#include "FadasMock.h"
#include <fadas.h>
#include <fadasRemap.h>

#include "MockCLib.hpp"

class FadasIfaceCoreTest : public ::testing::Test
{
protected:
    void SetUp() override { MockApi_ResetAll(); }

    void TearDown() override { MockApi_ResetAll(); }
};

TEST_F( FadasIfaceCoreTest, OpenClose_Success )
{
    remote_handle64 handle = 0;
    AEEResult ret = FadasIface_open( "test_uri", &handle );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, handle );

    ret = FadasIface_close( handle );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_close( 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Open_Fail_PowerSet )
{
    remote_handle64 handle = 0;
    int fail = -1;
    MockApi_Control( MOCK_API_HAP_POWER_SET, MOCK_CONTROL_RETURN, &fail );

    AEEResult ret = FadasIface_open( "test_uri", &handle );
    ASSERT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceCoreTest, Init_Success )
{
    remote_handle64 handle = 1;
    int32_t status = 0;
    uint32_t crcRx = 0;
    FadasError_e result = FADAS_ERROR_NONE;
    int value = 0x1234;
    MockApi_Control( MOCK_API_FADAS_INIT, MOCK_CONTROL_RETURN, &result );
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &value );

    AEEResult ret = FadasIface_FadasInitSafe( handle, &status, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_EQ( FADAS_ERROR_NONE, status );
    ASSERT_EQ( value, crcRx );
}

TEST_F( FadasIfaceCoreTest, Init_Fail_CRC )
{
    remote_handle64 handle = 1;
    int32_t status = 0;
    uint32_t crcRx = 0xdeadbeef;
    FadasError_e result = FADAS_ERROR_NONE;
    int value = 0xFFFF;
    MockApi_Control( MOCK_API_FADAS_INIT, MOCK_CONTROL_RETURN, &result );
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &value );
    AEEResult ret = FadasIface_FadasInitSafe( handle, &status, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );
    ASSERT_EQ( FADAS_ERROR_NONE, status );
    ASSERT_EQ( value, crcRx );
}

TEST_F( FadasIfaceCoreTest, Init_Failure )
{
    remote_handle64 handle = 1;
    int32_t status = 0;
    uint32_t crcRx = 0;
    int value = 0x5678;
    FadasError_e result = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_INIT, MOCK_CONTROL_RETURN, &result );
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &value );

    AEEResult ret = FadasIface_FadasInitSafe( handle, &status, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_EQ( FADAS_ERROR_FAIL, status );
    ASSERT_EQ( value, crcRx );

}

TEST_F( FadasIfaceCoreTest, Version_Success )
{
    remote_handle64 handle = 1;
    uint8_t version[64];
    uint32_t crcRx = 0;
    AEEResult ret = FadasIface_FadasVersionSafe( handle, version, sizeof( version ), 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasVersionSafe( handle, version, sizeof( version ), 0xdeadbeef, &crcRx );
    ASSERT_EQ( AEE_EBADPARM, ret );

    crcRx = 0xdeadbeef;
    ret = FadasIface_FadasVersionSafe( handle, version, sizeof( version ), 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx=nullptr → skip CRC generation → AEE_SUCCESS (covers false branch of crcRx!=nullptr)
TEST_F( FadasIfaceCoreTest, Version_NullCrcRx )
{
    remote_handle64 handle = 1;
    uint8_t version[64];
    AEEResult ret = FadasIface_FadasVersionSafe( handle, version, sizeof( version ), 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, DeInit_Success )
{
    remote_handle64 handle = 1;
    AEEResult ret = FadasIface_FadasDeInitSafe( handle );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_CreateMapFromMap_Success )
{
    remote_handle64 handle = 1;
    uint64 mapPtr = 0;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_CreateMapFromMapSafe( handle, &mapPtr, 640, 480, 640, 480, fd, fd,
                                                  640 * 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP,
                                                  0, 0, nullptr );

    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, mapPtr );

    if ( mapPtr )
    {
        FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );
    }

    ret = FadasIface_FadasRemap_CreateMapFromMapSafe( handle, &mapPtr, 640, 480, 640, 480, fd, fd,
                                                  640 * 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP,
                                                  0, 0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );

    uint32_t crcRx = 0xdeadbeef;
    ret = FadasIface_FadasRemap_CreateMapFromMapSafe( handle, &mapPtr, 640, 480, 640, 480, fd, fd,
                                                  640 * 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP,
                                                  0, 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// crcRx valid (not 0xdeadbeef) → crc32_generate_scatter succeeds (covers false branch)
TEST_F( FadasIfaceCoreTest, Remap_CreateMapFromMap_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint64 mapPtr = 0;
    int32_t fd = 1;
    uint32_t crcRx = 0;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_CreateMapFromMapSafe( handle, &mapPtr, 640, 480, 640, 480, fd, fd,
                                                  640 * 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP,
                                                  0, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, mapPtr );

    if ( mapPtr ) FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_CreateMapFromMap_Fail_MapNull )
{
    remote_handle64 handle = 1;
    uint64 mapPtr = 0;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    void *nullPtr = nullptr;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP, MOCK_CONTROL_RETURN, &nullPtr );

    ret = FadasIface_FadasRemap_CreateMapFromMapSafe( handle, &mapPtr, 640, 480, 640, 480, fd, fd,
                                                  640 * 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP,
                                                  0, 0, nullptr );

    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, fd, 640 * 4, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_CreateMapNoUndistortion_Success )
{
    remote_handle64 handle = 1;
    uint64 mapPtr = 0;

    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe(
            handle, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
            nullptr );

    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, mapPtr );

    if ( mapPtr )
    {
        ret = FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0xdeadbeef );
        ASSERT_EQ( AEE_EBADPARM, ret );

        ret = FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }

    ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0,
                                                         0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );

    uint32_t crcRx = 0xdeadbeef;
    ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0,
                                                         0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasRemapMap *map = nullptr;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION, MOCK_CONTROL_RETURN, &map );
    ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0,
                                                         0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch)
TEST_F( FadasIfaceCoreTest, Remap_CreateMapNoUndistortion_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint64 mapPtr = 0;
    uint32_t crcRx = 0;

    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe(
            handle, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
            &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, mapPtr );

    if ( mapPtr ) FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );
}

TEST_F( FadasIfaceCoreTest, Remap_CreateWorkers_Success )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;

    AEEResult ret = FadasIface_FadasRemap_CreateWorkersSafe(
            handle, &workerPtr, 1, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );

    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, workerPtr );

    if ( workerPtr )
    {
        ret = FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0xdeadbeef );
        ASSERT_EQ( AEE_EBADPARM, ret );

        ret = FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }

    ret = FadasIface_FadasRemap_CreateWorkersSafe(
            handle, &workerPtr, 1, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );

    uint32_t crcRx = 0xdeadbeef;
    ret = FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                               FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );

    void *worker = nullptr;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_WORKERS, MOCK_CONTROL_RETURN, &worker );
    ret = FadasIface_FadasRemap_CreateWorkersSafe(
            handle, &workerPtr, 1, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch)
TEST_F( FadasIfaceCoreTest, Remap_CreateWorkers_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    uint32_t crcRx = 0;

    AEEResult ret = FadasIface_FadasRemap_CreateWorkersSafe(
            handle, &workerPtr, 1, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, workerPtr );

    if ( workerPtr ) FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Success )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                   FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
                                                   nullptr );

    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
            { 640 * 2, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };

    AEEResult ret = FadasIface_mmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 1, &mapPtr, 1, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 640 * 480 * 3, &dstProps, &dstROI, 1,
                                       nullptr, 0, 0 );

    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
    FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_MaxInputs )
{
    remote_handle64 handle = 1;
    int32_t srcFds[65];   // > 64
    // Fill dummy

    AEEResult ret =
            FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, srcFds, 65, nullptr, 0,
                                         nullptr, 0, 0, 0, nullptr, nullptr, 0, nullptr, 0, 0 );

    ASSERT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_LengthMismatch )
{
    remote_handle64 handle = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;

    // srcFdsLen=1, offsetsLen=0 → T||_ (srcFdsLen != offsetsLen is true)
    AEEResult ret =
            FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 1, &offset, 0,
                                         nullptr, 1, 0, 0, nullptr, nullptr, 0, nullptr, 0, 0 );

    ASSERT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_NullDst )
{
    remote_handle64 handle = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = { 0 };
    int32_t dstFd = 2;

    // Fail dstFd mmap
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_HAP_MMAP_GET, MOCK_CONTROL_RETURN, &fail );

    AEEResult ret = FadasIface_mmapSafe( handle, srcFd, 100, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 100, nullptr, nullptr, 0, nullptr, 0,
                                       0 );

    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, srcFd, 100, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_NullSrc )
{
    // Skipping NullSrc due to complexity in mocking sequence of calls with simple mock control
}

TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_Run )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                   FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
                                                   nullptr );

    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
            { 640 * 2, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };

    AEEResult ret = FadasIface_mmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 1, &mapPtr, 1, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 640 * 480 * 3, &dstProps, &dstROI, 1,
                                       nullptr, 0, 0 );

    ASSERT_EQ( AEE_EOFFSET + FADAS_ERROR_FAIL, ret );

    FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
    FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// CRC validation failure in RunMT
TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_CRC )
{
    remote_handle64 handle = 1;
    AEEResult ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, nullptr, 0,
                                                 nullptr, 0, nullptr, 0, 0, 0, nullptr, nullptr, 0,
                                                 nullptr, 0, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// workerPtrs non-null but workerPtrsLen=0 → T&&F branch (line 545)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_WorkerPtrsLenZero )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    uint64 workerPtr = 1;   // non-null pointer value
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // srcFdsLen=0 so inner loop doesn't execute; CRC building covers T&&F for workerPtrs
    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// mapPtrs non-null but mapPtrsLen=0 → T&&F branch (line 555)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_MapPtrsLenZero )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    uint64 mapPtr = 1;   // non-null pointer value
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, &mapPtr, 0, nullptr, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// srcFds=null → F&&_ branch (line 565)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_NullSrcFds )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // srcFds=null, srcFdsLen=0 → F&&_ for srcFds condition
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// offsets=null → F&&_ branch (line 575)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_NullOffsets )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    int32_t srcFd = 1;   // non-null srcFds pointer, but srcFdsLen=0
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // offsets=null, offsetsLen=0, srcFdsLen=0 → F&&_ for offsets condition
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// srcProps non-null but srcPropsLen=0 → T&&F branch (line 585)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_SrcPropsLenZero )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t srcProps = { 0 };   // non-null, but srcPropsLen=0
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                       &srcProps, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// normlz non-null but normlzLen=0 → T&&F branch (line 619); also covers normlz path
TEST_F( FadasIfaceCoreTest, Remap_RunMT_NormlzLenZero )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    FadasIface_FadasNormlzParams_t normlz = { 0.0f, 1.0f, 0.0f };   // non-null, normlzLen=0
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, nullptr, 0,
                                       &normlz, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// srcFdsLen != srcPropsLen → F||T branch (line 652)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_LengthMismatch_PropsDiffer )
{
    remote_handle64 handle = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps[2] = { { 0 }, { 0 } };
    int32_t dstFd = 2;

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // srcFdsLen=1, offsetsLen=1 (equal), srcPropsLen=2 (different) → F||T
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 1, &offset, 1,
                                       srcProps, 2, dstFd, 1024, nullptr, nullptr, 0, nullptr, 0,
                                       0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// src[i] is null → nullptr == src[i] true branch (line 662)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_NullSrcPtr )
{
    remote_handle64 handle = 1;
    int32_t srcFd = 1;   // NOT mapped → FadasIface_GetBufPtr returns null
    int32_t dstFd = 2;   // mapped → dst is non-null
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
            { 640 * 2, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // srcFd not mapped → src[0] = null → AEE_EFAILED
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 640 * 480 * 3, nullptr, &dstROI, 1,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// workerPtrsLen < srcFdsLen and mapPtrsLen < srcFdsLen → i >= workerPtrsLen and i >= mapPtrsLen
// (covers false branches of lines 698 and 703)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_WorkerMapPtrsLenLess )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                   FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
                                                   nullptr );

    int32_t srcFds[2] = { 1, 3 };
    uint32_t offsets[2] = { 0, 0 };
    FadasIface_FadasImgProps_t srcProps[2] = {
            { 640,
              480,
              (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
              { 640 * 2, 0, 0, 0 },
              1,
              { 480, 0, 0, 0 } },
            { 640,
              480,
              (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
              { 640 * 2, 0, 0, 0 },
              1,
              { 480, 0, 0, 0 } } };
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROIs[2] = { { 0, 0, 640, 480 }, { 0, 0, 640, 480 } };

    AEEResult ret = FadasIface_mmapSafe( handle, srcFds[0], 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, srcFds[1], 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    // dst buffer must hold 2 images: dstLen * srcFdsLen = 640*480*3 * 2
    ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // workerPtrsLen=1, mapPtrsLen=1, srcFdsLen=2
    // i=0: 0 < 1 → true (use workerPtrs[0], mapPtrs[0])
    // i=1: 1 < 1 → false (use workerPtrs[0], mapPtrs[0] as default)
    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 1, &mapPtr, 1, srcFds, 2, offsets, 2,
                                       srcProps, 2, dstFd, 640 * 480 * 3, &dstProps, dstROIs, 2,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
    FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, srcFds[0], 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, srcFds[1], 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// Y8UV8 format → covers FADAS_IMAGE_FORMAT_Y8UV8 branch (line 713)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_Y8UV8Format )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                         FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NSP, 0, nullptr );
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                   FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NSP, 0, 0,
                                                   nullptr );

    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_Y8UV8_NSP,
            { 640, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };

    // src buffer: Y plane (640*480) + UV plane (640*240) = 640*720 bytes
    AEEResult ret = FadasIface_mmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 1, &mapPtr, 1, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 640 * 480 * 3, &dstProps, &dstROI, 1,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
    FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// normlzLen=3 → covers 3==normlzLen branch (line 723)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_NormlzLen3 )
{
    remote_handle64 handle = 1;
    uint64 workerPtr = 0;
    FadasIface_FadasRemap_CreateWorkersSafe( handle, &workerPtr, 1,
                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, nullptr );
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortionSafe( handle, &mapPtr, 640, 480, 640, 480,
                                                   FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0, 0,
                                                   nullptr );

    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_UYVY_NSP,
            { 640 * 2, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    int32_t dstFd = 2;
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };
    FadasIface_FadasNormlzParams_t normlz[3] = { { 0.0f, 1.0f, 0.0f },
                                                 { 0.0f, 1.0f, 0.0f },
                                                 { 0.0f, 1.0f, 0.0f } };

    AEEResult ret = FadasIface_mmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRemap_RunMTSafe( handle, &workerPtr, 1, &mapPtr, 1, &srcFd, 1, &offset, 1,
                                       &srcProps, 1, dstFd, 640 * 480 * 3, &dstProps, &dstROI, 1,
                                       normlz, 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasIface_FadasRemap_DestroyWorkersSafe( handle, workerPtr, 0 );
    FadasIface_FadasRemap_DestroyMapSafe( handle, mapPtr, 0 );

    ret = FadasIface_munmapSafe( handle, srcFd, 640 * 480 * 2, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Mmap_Munmap_Success )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceCoreTest, Mmap_Fail_AlreadyUsed )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // This call will fail because it's already mapped
    ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// CRC validation failure in mmap
TEST_F( FadasIfaceCoreTest, Mmap_Fail_CRC )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// HAP_mmap returns 0xFFFFFFFF → null buf check (line 794)
TEST_F( FadasIfaceCoreTest, Mmap_Fail_HapMmap )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    void *badPtr = (void *) 0xFFFFFFFF;
    MockApi_Control( MOCK_API_HAP_MMAP, MOCK_CONTROL_RETURN, &badPtr );

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// CRC validation failure in munmap
TEST_F( FadasIfaceCoreTest, Munmap_Fail_CRC )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    AEEResult ret = FadasIface_munmapSafe( handle, fd, 1024, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// HAP_mmap_get fails (fd not mapped) → AEE_EFAILED (covers false branch of AEE_SUCCESS==ret)
TEST_F( FadasIfaceCoreTest, Munmap_Fail_HapMmapGet )
{
    remote_handle64 handle = 1;
    int32_t fd = 99;   // not mapped

    AEEResult ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// HAP_munmap returns failure → AEE_EFAILED (covers true branch of AEE_SUCCESS!=ret after munmap)
TEST_F( FadasIfaceCoreTest, Munmap_Fail_HapMunmap )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    AEEResult failRet = AEE_EFAILED;
    MockApi_Control( MOCK_API_HAP_UNMAP, MOCK_CONTROL_RETURN, &failRet );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceCoreTest, RegBuf_Success )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasRegBufSafe( handle, (FadasIface_FadasBufType_e) FADAS_BUF_TYPE_IN_NSP, fd,
                                  1024, 0, 1, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// CRC validation failure in RegBuf
TEST_F( FadasIfaceCoreTest, RegBuf_Fail_CRC )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    AEEResult ret = FadasIface_FadasRegBufSafe(
            handle, (FadasIface_FadasBufType_e) FADAS_BUF_TYPE_IN_NSP, fd, 1024, 0, 1, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// fd not mapped → ptr is null → AEE_EFAILED (covers nullptr==ptr branch)
TEST_F( FadasIfaceCoreTest, RegBuf_Fail_NullPtr )
{
    remote_handle64 handle = 1;
    int32_t fd = 99;   // not mapped

    AEEResult ret = FadasIface_FadasRegBufSafe(
            handle, (FadasIface_FadasBufType_e) FADAS_BUF_TYPE_IN_NSP, fd, 1024, 0, 1, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// FadasRegBuf returns error → AEE_EFAILED (covers FADAS_ERROR_NONE!=retVal branch)
TEST_F( FadasIfaceCoreTest, RegBuf_Fail_FadasRegBuf )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REG_BUF, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_FadasRegBufSafe( handle, (FadasIface_FadasBufType_e) FADAS_BUF_TYPE_IN_NSP, fd,
                                  1024, 0, 1, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// DeregBuf: entire function was uncovered
TEST_F( FadasIfaceCoreTest, DeregBuf_Success )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_FadasDeregBufSafe( handle, fd, 1024, 0, 1, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// CRC validation failure in DeregBuf
TEST_F( FadasIfaceCoreTest, DeregBuf_Fail_CRC )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    AEEResult ret = FadasIface_FadasDeregBufSafe( handle, fd, 1024, 0, 1, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// fd not mapped → ptr is null → AEE_EFAILED
TEST_F( FadasIfaceCoreTest, DeregBuf_Fail_NullPtr )
{
    remote_handle64 handle = 1;
    int32_t fd = 99;   // not mapped

    AEEResult ret = FadasIface_FadasDeregBufSafe( handle, fd, 1024, 0, 1, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// FadasDeregBuf returns error → AEE_EFAILED
TEST_F( FadasIfaceCoreTest, DeregBuf_Fail_FadasDeregBuf )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_DEREG_BUF, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_FadasDeregBufSafe( handle, fd, 1024, 0, 1, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// ============================================================
// PointPillar tests
// ============================================================

TEST_F( FadasIfaceCoreTest, PointPillar_Create_Success )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_PointPillarDestroySafe( handle, ph, 0 );
}

// CRC validation failure in PointPillarCreate
TEST_F( FadasIfaceCoreTest, PointPillar_Create_Fail_CRC )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// pPlrSize=nullptr → AEE_EFAILED (covers nullptr==pPlrSize branch, line 1031)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_NullPPlrSize )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, nullptr, &min, &max, 100, 4, 10, 10, 4,
                                                  &ph, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// pMinRange=nullptr → AEE_EFAILED (covers nullptr==pMinRange branch, line 1031)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_NullPMinRange )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, nullptr, &max, 100, 4, 10, 10, 4,
                                                  &ph, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// pMaxRange=nullptr → AEE_EFAILED (covers nullptr==pMaxRange branch, line 1031)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_NullPMaxRange )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, nullptr, 100, 4, 10, 10, 4,
                                                  &ph, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// phPreProc=nullptr → AEE_EFAILED (covers nullptr==phPreProc branch, line 1032)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_NullPhPreProc )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4,
                                                  nullptr, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// FadasVM_PointPillar_Create returns null → *phPreProc=0 → AEE_EFAILED (line 1046)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_Fail_CreateReturnsNull )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_CREATE, MOCK_CONTROL_RETURN, nullptr );

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch, line 1054)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;
    uint32_t crcRx = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_PointPillarDestroySafe( handle, ph, 0 );
}

TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_NullPtr )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;

    // Don't map FDs, GetBufPtr will fail (return null)

    AEEResult ret = FadasIface_PointPillarRunSafe( handle, ph, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100,
                                               &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// CRC validation failure in PointPillarRun
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_CRC )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;

    AEEResult ret = FadasIface_PointPillarRunSafe( handle, ph, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100,
                                               &numOut, 0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// handle=0 → dspContext=null → AEE_EFAILED (covers nullptr==dspContext branch, line 1135)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_NullDspContext )
{
    remote_handle64 handle = 0;   // null context
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( (remote_handle64) 1, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( (remote_handle64) 1, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( (remote_handle64) 1, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( (remote_handle64) 1, fdIn, 1024, 0 );
    FadasIface_munmapSafe( (remote_handle64) 1, fdOutPlrs, 1024, 0 );
    FadasIface_munmapSafe( (remote_handle64) 1, fdOutFeat, 1024, 0 );
}

// fdOutPlrs not mapped → pOutPlrsData=null → AEE_EFAILED (covers nullptr==pOutPlrsData branch)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_NullOutPlrs )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutFeat = 3;
    // fdOutPlrs=2 intentionally NOT mapped

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, 2, 0, 100, fdOutFeat, 0, 100,
                                     &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
}

// fdOutFeature not mapped → pOutFeatureData=null → AEE_EFAILED
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_NullOutFeature )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutPlrs = 2;
    // fdOutFeat=3 intentionally NOT mapped

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, 3, 0, 100,
                                     &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
}

// hPreProc=0 → AEE_EFAILED (covers 0==hPreProc branch, line 1136)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_ZeroHandle )
{
    remote_handle64 handle = 1;
    uint64_t ph = 0;   // zero handle
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
}

TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_Run )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_RUN, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EOFFSET + FADAS_ERROR_FAIL, ret );

    ret = FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// All valid → AEE_SUCCESS (covers false branch of FADAS_ERROR_NONE!=error, line 1152)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Success )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch, line 1167)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    uint32_t crcRx = 0;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// CRC validation failure in PointPillarDestroy
TEST_F( FadasIfaceCoreTest, PointPillar_Destroy_Fail_CRC )
{
    remote_handle64 handle = 1;
    AEEResult ret = FadasIface_PointPillarDestroySafe( handle, 1, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// FadasVM_PointPillar_Destroy returns error → AEE_EFAILED (covers FADAS_ERROR_NONE!=error branch)
TEST_F( FadasIfaceCoreTest, PointPillar_Destroy_Fail_DestroyError )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_DESTROY, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_PointPillarDestroySafe( handle, ph, 0 );
    ASSERT_EQ( AEE_EOFFSET + FADAS_ERROR_FAIL, ret );
}

// ============================================================
// ExtractBBox tests
// ============================================================

TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_Success )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_ExtractBBoxDestroySafe( handle, ph, 0 );
}

// CRC validation failure in ExtractBBoxCreate
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_Fail_CRC )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;

    AEEResult ret =
            FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0, 10, 10,
                                          10, nullptr, 0, 0, &ph, 0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// pGrid=nullptr → AEE_EFAILED (covers nullptr==pGrid branch, line 1297)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_NullPGrid )
{
    remote_handle64 handle = 1;
    uint64_t ph = 0;

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, nullptr, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// phPostProc=nullptr → AEE_EFAILED (covers nullptr==phPostProc branch, line 1297)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_NullPhPostProc )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, nullptr, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// labelSelect non-null and labelSelectLen>0 → T&&T branch (line 1277) and nullptr!=labelSelect
// branch (line 1318)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_WithLabelSelect )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    uint8_t labelSelect[4] = { 1, 0, 1, 0 };

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 4, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, labelSelect, 4, 4, &ph, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_ExtractBBoxDestroySafe( handle, ph, 0 );
}

// FadasVM_ExtractBBox_Create returns null → *phPostProc=0 → AEE_EFAILED (line 1331)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_Fail_CreateReturnsNull )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    void *nullPtr = nullptr;

    MockApi_Control( MOCK_API_FADAS_VM_EXTRACTBBOX_CREATE, MOCK_CONTROL_RETURN, &nullPtr );

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch, line 1345)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    uint32_t crcRx = 0;

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_ExtractBBoxDestroySafe( handle, ph, 0 );
}

TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_Run )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret;
    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_EXTRACTBBOX_RUN, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0, 0,
                                     &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EOFFSET + FADAS_ERROR_FAIL, ret );

    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }
}

// CRC validation failure in ExtractBBoxRun
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_CRC )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0xdeadbeef, nullptr );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// fds=null → AEE_EFAILED (covers nullptr==fds branch, line 1427)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_NullFds )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, nullptr, 10, offsets, 10, sizes, 10,
                                               0, 0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// offsets=null → AEE_EFAILED (covers nullptr==offsets branch, line 1428)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_NullOffsets )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, nullptr, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// sizes=null → AEE_EFAILED (covers nullptr==sizes branch, line 1429)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_NullSizes )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, nullptr, 10,
                                               0, 0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// Some fd not mapped → null ptr → AEE_EFAILED (covers nullptr==pInPts etc. branch, line 1449)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullPtrs )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    // Only map fds[0..8], leave fds[9] unmapped → pMetadataOut will be null
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret;
    for ( int i = 0; i < 9; i++ )
    {
        ret = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }
    // fds[9]=10 is NOT mapped → pMetadataOut = null

    ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0, 0,
                                     &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 9; i++ )
    {
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// All valid → AEE_SUCCESS (covers false branch of FADAS_ERROR_NONE!=error, line 1503)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Success )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret;
    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }

    ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0, 0,
                                     &numOut, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );

    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }
}

// crcRx valid → crc32_generate_scatter succeeds (covers false branch, line 1527)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Success_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    uint32_t crcRx = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret;
    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }

    ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0, 0,
                                     &numOut, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );

    for ( int i = 0; i < 10; i++ )
    {
        ret = FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, ret );
    }
}

// CRC validation failure in ExtractBBoxDestroy
TEST_F( FadasIfaceCoreTest, ExtractBBox_Destroy_Fail_CRC )
{
    remote_handle64 handle = 1;
    AEEResult ret = FadasIface_ExtractBBoxDestroySafe( handle, 1, 0xdeadbeef );
    ASSERT_EQ( AEE_EBADPARM, ret );
}

// FadasVM_ExtractBBox_Destroy returns error → AEE_EFAILED (covers FADAS_ERROR_NONE!=error branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Destroy_Fail_DestroyError )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5, 0.5, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_EXTRACTBBOX_DESTROY, MOCK_CONTROL_RETURN, &fail );

    ret = FadasIface_ExtractBBoxDestroySafe( handle, ph, 0 );
    ASSERT_EQ( AEE_EOFFSET + FADAS_ERROR_FAIL, ret );
}

// malloc returns NULL → *handle = 0 → AEE_EFAILED (covers 0==*handle branch, line 153)
TEST_F( FadasIfaceCoreTest, Open_Fail_NullHandle )
{
    remote_handle64 handle = 0;
    MockC_MallocCtrl( 1 );
    AEEResult ret = FadasIface_open( "test_uri", &handle );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// ver_int=nullptr, crcRx non-null → T&&F for (crcRx!=nullptr && ver_int!=nullptr) (line 232)
// strlcpy with n=0 does not write to dst, so nullptr dst is safe here
TEST_F( FadasIfaceCoreTest, Version_NullVerInt_WithCrcRx )
{
    remote_handle64 handle = 1;
    uint32_t crcRx = 0;
    AEEResult ret = FadasIface_FadasVersionSafe( handle, nullptr, 0, 0, &crcRx );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// dstROIs non-null but dstROIsLen=0 → T&&F for (dstROIs && dstROIsLen>0) in CRC build (line 609)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_DstROIsNonNull_LenZero )
{
    remote_handle64 handle = 1;
    int32_t dstFd = 2;
    FadasIface_FadasROI_t dstROI = { 0, 0, 640, 480 };   // non-null pointer
    FadasIface_FadasImgProps_t dstProps = {
            640,
            480,
            (FadasIface_FadasImageFormat_e) FADAS_IMAGE_FORMAT_RGB888_NSP,
            { 640 * 3, 0, 0, 0 },
            1,
            { 480, 0, 0, 0 } };

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // dstROIs non-null, dstROIsLen=0 → T&&F; srcFdsLen=0 so inner loop skipped
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0,
                                       nullptr, 0, dstFd, 640 * 480 * 3, &dstProps, &dstROI, 0,
                                       nullptr, 0, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 640 * 480 * 3, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// dstFd mapped (dst!=nullptr), srcFdsLen=1, offsetsLen=2, srcPropsLen=1
// → srcFdsLen!=offsetsLen is TRUE → T||_ branch (line 652)
TEST_F( FadasIfaceCoreTest, Remap_RunMT_Fail_OffsetLenMismatch )
{
    remote_handle64 handle = 1;
    int32_t srcFd = 1;
    uint32_t offsets[2] = { 0, 0 };
    FadasIface_FadasImgProps_t srcProps = { 0 };
    int32_t dstFd = 2;

    AEEResult ret = FadasIface_mmapSafe( handle, dstFd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    // srcFdsLen=1, offsetsLen=2 → srcFdsLen != offsetsLen TRUE → T||_
    ret = FadasIface_FadasRemap_RunMTSafe( handle, nullptr, 0, nullptr, 0, &srcFd, 1, offsets, 2,
                                       &srcProps, 1, dstFd, 1024, nullptr, nullptr, 0, nullptr, 0,
                                       0 );
    ASSERT_EQ( AEE_EFAILED, ret );

    ret = FadasIface_munmapSafe( handle, dstFd, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
}

// HAP_mmap returns nullptr → nullptr==buf check (line 794)
TEST_F( FadasIfaceCoreTest, Mmap_Fail_HapMmapNullPtr )
{
    remote_handle64 handle = 1;
    int32_t fd = 1;
    void *nullPtr = nullptr;
    MockApi_Control( MOCK_API_HAP_MMAP, MOCK_CONTROL_RETURN, &nullPtr );

    AEEResult ret = FadasIface_mmapSafe( handle, fd, 1024, 0 );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx=0xdeadbeef → crc32_generate_scatter returns CRC_ERROR → AEE_EFAILED (line 1060)
TEST_F( FadasIfaceCoreTest, PointPillar_Create_Fail_CrcGenerate )
{
    remote_handle64 handle = 1;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, min = { 0, 0, 0 }, max = { 10, 10, 10 };
    uint64_t ph = 0;
    uint32_t crcRx = 0xdeadbeef;

    AEEResult ret = FadasIface_PointPillarCreateSafe( handle, &sz, &min, &max, 100, 4, 10, 10, 4, &ph,
                                                  0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// crcRx=0xdeadbeef with all fds mapped → crc32_generate_scatter fails → AEE_EFAILED (line 1172)
TEST_F( FadasIfaceCoreTest, PointPillar_Run_Fail_CrcGenerate )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    uint32_t crcRx = 0xdeadbeef;
    int32_t fdIn = 1, fdOutPlrs = 2, fdOutFeat = 3;

    AEEResult ret = FadasIface_mmapSafe( handle, fdIn, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutPlrs, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ret = FadasIface_mmapSafe( handle, fdOutFeat, 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, ret );

    ret = FadasIface_PointPillarRunSafe( handle, ph, 100, fdIn, 0, 100, fdOutPlrs, 0, 100, fdOutFeat, 0,
                                     100, &numOut, 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( handle, fdIn, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutPlrs, 1024, 0 );
    FadasIface_munmapSafe( handle, fdOutFeat, 1024, 0 );
}

// labelSelect non-null but labelSelectLen=0 → T&&F for (labelSelect && labelSelectLen>0) (line
// 1277)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_LabelSelectLenZero )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    uint8_t labelSelect = 1;   // non-null pointer

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0, 0,
                                                  10, 10, 10, &labelSelect, 0, 0, &ph, 0, nullptr );
    ASSERT_EQ( AEE_SUCCESS, ret );
    ASSERT_NE( 0, ph );

    if ( ph ) FadasIface_ExtractBBoxDestroySafe( handle, ph, 0 );
}

// crcRx=0xdeadbeef → crc32_generate_scatter fails → AEE_EFAILED (line 1350)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Create_Fail_CrcGenerate )
{
    remote_handle64 handle = 1;
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    uint32_t crcRx = 0xdeadbeef;

    AEEResult ret = FadasIface_ExtractBBoxCreateSafe( handle, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0, 0,
                                                  10, 10, 10, nullptr, 0, 0, &ph, 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// handle=0 → dspContext=nullptr → AEE_EFAILED (covers nullptr==dspContext branch, line 1427)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullDspContext )
{
    remote_handle64 handle = 0;   // dspContext = nullptr
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// fdsLen=9 → PLRPOST_NUM_INPUTS!=fdsLen → AEE_EFAILED (covers 10!=fdsLen branch, line 1430)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_FdsLenNot10 )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 9, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// offsetsLen=9 → PLRPOST_NUM_INPUTS!=offsetsLen → AEE_EFAILED (covers 10!=offsetsLen branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_OffsetsLenNot10 )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 9, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// sizesLen=9 → PLRPOST_NUM_INPUTS!=sizesLen → AEE_EFAILED (covers 10!=sizesLen branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_SizesLenNot10 )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 9, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// pNumDetOut=nullptr → AEE_EFAILED (covers nullptr==pNumDetOut branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullNumDetOut )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, nullptr, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );
}

// fds[0] not mapped → pInPts=nullptr → AEE_EFAILED (covers nullptr==pInPts branch, line 1449)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullInPts )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    // Map fds[1..9], leave fds[0] unmapped → pInPts = nullptr
    for ( int i = 1; i < 10; i++ )
    {
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 1; i < 10; i++ )
    {
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[1] not mapped → pHeatmap=nullptr → AEE_EFAILED (covers nullptr==pHeatmap branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullHeatmap )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    // Map fds[0] and fds[2..9], leave fds[1] unmapped → pHeatmap = nullptr
    AEEResult r = FadasIface_mmapSafe( handle, fds[0], 1024, 0 );
    ASSERT_EQ( AEE_SUCCESS, r );
    for ( int i = 2; i < 10; i++ )
    {
        r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    FadasIface_munmapSafe( handle, fds[0], 1024, 0 );
    for ( int i = 2; i < 10; i++ )
    {
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[2] not mapped → pXY=nullptr → AEE_EFAILED (covers nullptr==pXY branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullXY )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    // Map fds[0..1] and fds[3..9], leave fds[2] unmapped → pXY = nullptr
    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 2 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 2 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[3] not mapped → pZ=nullptr → AEE_EFAILED (covers nullptr==pZ branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullZ )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 3 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 3 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[4] not mapped → pSize=nullptr → AEE_EFAILED (covers nullptr==pSize branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullSize )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 4 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 4 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[5] not mapped → pTheta=nullptr → AEE_EFAILED (covers nullptr==pTheta branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullTheta )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 5 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 5 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[6] not mapped → pBBoxList=nullptr → AEE_EFAILED (covers nullptr==pBBoxList branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullBBoxList )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 6 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 6 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[7] not mapped → pLabelsOut=nullptr → AEE_EFAILED (covers nullptr==pLabelsOut branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullLabelsOut )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 7 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 7 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// fds[8] not mapped → pScoresOut=nullptr → AEE_EFAILED (covers nullptr==pScoresOut branch)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_NullScoresOut )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 8 ) continue;
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, nullptr );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        if ( i == 8 ) continue;
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
    }
}

// crcRx=0xdeadbeef with all fds mapped → crc32_generate_scatter fails → AEE_EFAILED (line 1532)
TEST_F( FadasIfaceCoreTest, ExtractBBox_Run_Fail_CrcGenerate )
{
    remote_handle64 handle = 1;
    uint64_t ph = 1;
    uint32_t numOut = 0;
    uint32_t crcRx = 0xdeadbeef;
    int32_t fds[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    uint32_t offsets[10] = { 0 };
    uint32_t sizes[10] = { 100 };

    for ( int i = 0; i < 10; i++ )
    {
        AEEResult r = FadasIface_mmapSafe( handle, fds[i], 1024, 0 );
        ASSERT_EQ( AEE_SUCCESS, r );
    }

    AEEResult ret = FadasIface_ExtractBBoxRunSafe( handle, ph, 100, fds, 10, offsets, 10, sizes, 10, 0,
                                               0, &numOut, 0, &crcRx );
    ASSERT_EQ( AEE_EFAILED, ret );

    for ( int i = 0; i < 10; i++ )
    {
        FadasIface_munmapSafe( handle, fds[i], 1024, 0 );
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
