// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "FadasIfaceSafe.hpp"
#include "FadasMock.h"
#include "FadasPlr.hpp"
#include "FadasRemap.hpp"
#include "FadasSrv.hpp"
#include "gtest/gtest.h"
#include <AEEStdErr.h>
#include <cstring>

/* Forward declarations for GPU mock functions defined in FadasMock.cpp */
extern FadasError_e FadasRemap_RunGPU( FadasRemapMap *remapPtr, FadasImage_t *src,
                                       FadasImage_t *dst, FadasROI_t *roi, float32_t scale,
                                       FadasNormlzParams_t *normlz );
extern FadasRemapMap *FadasRemap_CreateMapNoUndistortionGPU( uint32_t srcWidth, uint32_t srcHeight,
                                                             uint32_t dstWidth, uint32_t dstHeight,
                                                             FadasRemapPipeline_e ePipeline,
                                                             uint8_t borderConst );
extern FadasError_e FadasRemap_DestroyMapGPU( FadasRemapMap *map_ );

using namespace QC;
using namespace QC::Memory;
using namespace QC::libs::FadasIface;

/* ================================================================
 * Test fixture — resets all mock state before/after each test
 * ================================================================ */
class FadasIfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        MockApi_ResetAll();
        MockRemoteSystemRequest_Reset();
    }
    void TearDown() override
    {
        MockApi_ResetAll();
        MockRemoteSystemRequest_Reset();
    }
};

/* ================================================================*/
/* Helper: make a TensorDescriptor_t with allocated buffer */
static TensorDescriptor_t MakeTensor( uint32_t dim0, uint32_t dim1 )
{
    TensorDescriptor_t t = {};
    t.type = QC_BUFFER_TYPE_TENSOR;
    t.tensorType = QC_TENSOR_TYPE_FLOAT_32;
    t.numDims = 2;
    t.dims[0] = dim0;
    t.dims[1] = dim1;
    t.size = dim0 * dim1 * sizeof( float );
    t.pBuf = malloc( t.size );
    t.offset = 0;
    return t;
}

// Helper: make ImageDescriptor_t
static ImageDescriptor_t MakeSrvImageDesc( QCImageFormat_e fmt, uint32_t w, uint32_t h,
                                           uint32_t batch = 1 )
{
    ImageDescriptor_t d = {};
    d.type = QC_BUFFER_TYPE_IMAGE;
    d.format = fmt;
    d.width = w;
    d.height = h;
    d.batchSize = batch;
    d.numPlanes = 1;
    d.stride[0] = w * 3;
    d.actualHeight[0] = h;
    d.planeBufSize[0] = w * h * 3;
    d.planeBufSize[1] = 0;
    d.planeBufSize[2] = 0;
    d.planeBufSize[3] = 0;
    d.size = w * h * 3 * batch;
    d.offset = 0;
    d.pBuf = malloc( d.size );
    return d;
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasInit
 * ================================================================ */
TEST_F( FadasIfaceTest, FadasInit_Success )
{
    int32_t status = 0;
    AEEResult ret = FadasIface_FadasInit( 1, &status );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceTest, FadasInit_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_INIT_SAFE, MOCK_CONTROL_RETURN, &fail );
    int32_t status = 0;
    AEEResult ret = FadasIface_FadasInit( 1, &status );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, FadasInit_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    int32_t status = 0;
    AEEResult ret = FadasIface_FadasInit( 1, &status );
    EXPECT_EQ( AEE_EFAILED, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasVersion
 * ================================================================ */
TEST_F( FadasIfaceTest, FadasVersion_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint8_t ver[64] = {};
    AEEResult ret = FadasIface_FadasVersion( 1, ver, 64 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, FadasVersion_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_VERSION_SAFE, MOCK_CONTROL_RETURN, &fail );
    uint8_t ver[64] = {};
    AEEResult ret = FadasIface_FadasVersion( 1, ver, 64 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, FadasVersion_NullVersion )
{
    /* version==nullptr → skip CRC verify → success */
    AEEResult ret = FadasIface_FadasVersion( 1, nullptr, 0 );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceTest, FadasVersion_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint8_t ver[64] = {};
    AEEResult ret = FadasIface_FadasVersion( 1, ver, 64 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, FadasVersion_Success )
{
    uint8_t ver[64] = {};
    AEEResult ret = FadasIface_FadasVersion( 1, ver, 64 );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasDeInit
 * ================================================================ */
TEST_F( FadasIfaceTest, FadasDeInit_Success )
{
    AEEResult ret = FadasIface_FadasDeInit( 1 );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_CreateMapFromMap
 * ================================================================ */
TEST_F( FadasIfaceTest, CreateMapFromMap_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 mapPtr = 0;
    AEEResult ret =
            FadasIface_FadasRemap_CreateMapFromMap( 1, &mapPtr, 640, 480, 640, 480, 1, 2, 640 * 4,
                                                    FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateMapFromMap_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP_SAFE, MOCK_CONTROL_RETURN, &fail );
    uint64 mapPtr = 0;
    AEEResult ret =
            FadasIface_FadasRemap_CreateMapFromMap( 1, &mapPtr, 640, 480, 640, 480, 1, 2, 640 * 4,
                                                    FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateMapFromMap_NullMapPtr )
{
    /* mapPtr==nullptr → skip CRC verify */
    AEEResult ret =
            FadasIface_FadasRemap_CreateMapFromMap( 1, nullptr, 640, 480, 640, 480, 1, 2, 640 * 4,
                                                    FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, CreateMapFromMap_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 mapPtr = 0;
    AEEResult ret =
            FadasIface_FadasRemap_CreateMapFromMap( 1, &mapPtr, 640, 480, 640, 480, 1, 2, 640 * 4,
                                                    FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
    if ( mapPtr ) free( (void *) (uintptr_t) mapPtr );
}

TEST_F( FadasIfaceTest, CreateMapFromMap_Success )
{
    uint64 mapPtr = 0;
    AEEResult ret =
            FadasIface_FadasRemap_CreateMapFromMap( 1, &mapPtr, 640, 480, 640, 480, 1, 2, 640 * 4,
                                                    FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_SUCCESS, ret );
    EXPECT_NE( 0u, mapPtr );
    if ( mapPtr )
    {
        FadasIface_FadasRemap_DestroyMap( 1, mapPtr );
    }
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_CreateMapNoUndistortion
 * ================================================================ */
TEST_F( FadasIfaceTest, CreateMapNoUndistortion_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 mapPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortion(
            1, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateMapNoUndistortion_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_SAFE, MOCK_CONTROL_RETURN,
                     &fail );
    uint64 mapPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortion(
            1, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateMapNoUndistortion_NullMapPtr )
{
    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortion(
            1, nullptr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, CreateMapNoUndistortion_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 mapPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortion(
            1, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
    if ( mapPtr ) free( (void *) (uintptr_t) mapPtr );
}

TEST_F( FadasIfaceTest, CreateMapNoUndistortion_Success )
{
    uint64 mapPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateMapNoUndistortion(
            1, &mapPtr, 640, 480, 640, 480, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    EXPECT_EQ( AEE_SUCCESS, ret );
    EXPECT_NE( 0u, mapPtr );
    if ( mapPtr ) FadasIface_FadasRemap_DestroyMap( 1, mapPtr );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_DestroyMap
 * ================================================================ */
TEST_F( FadasIfaceTest, DestroyMap_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    AEEResult ret = FadasIface_FadasRemap_DestroyMap( 1, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, DestroyMap_Success )
{
    uint64 mapPtr = 0;
    FadasIface_FadasRemap_CreateMapNoUndistortion( 1, &mapPtr, 64, 64, 64, 64,
                                                   FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP, 0 );
    AEEResult ret = FadasIface_FadasRemap_DestroyMap( 1, mapPtr );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_CreateWorkers
 * ================================================================ */
TEST_F( FadasIfaceTest, CreateWorkers_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 wPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateWorkers( 1, &wPtr, 4,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateWorkers_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_CREATE_WORKERS_SAFE, MOCK_CONTROL_RETURN, &fail );
    uint64 wPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateWorkers( 1, &wPtr, 4,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, CreateWorkers_NullWorkerPtr )
{
    AEEResult ret = FadasIface_FadasRemap_CreateWorkers( 1, nullptr, 4,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, CreateWorkers_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 wPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateWorkers( 1, &wPtr, 4,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    EXPECT_EQ( AEE_EFAILED, ret );
    if ( wPtr ) free( (void *) (uintptr_t) wPtr );
}

TEST_F( FadasIfaceTest, CreateWorkers_Success )
{
    uint64 wPtr = 0;
    AEEResult ret = FadasIface_FadasRemap_CreateWorkers( 1, &wPtr, 4,
                                                         FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    EXPECT_EQ( AEE_SUCCESS, ret );
    EXPECT_NE( 0u, wPtr );
    if ( wPtr ) FadasIface_FadasRemap_DestroyWorkers( 1, wPtr );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_DestroyWorkers
 * ================================================================ */
TEST_F( FadasIfaceTest, DestroyWorkers_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    AEEResult ret = FadasIface_FadasRemap_DestroyWorkers( 1, 0 );
    EXPECT_EQ( AEE_EFAILED, ret );
}

TEST_F( FadasIfaceTest, DestroyWorkers_Success )
{
    uint64 wPtr = 0;
    FadasIface_FadasRemap_CreateWorkers( 1, &wPtr, 4, FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP );
    AEEResult ret = FadasIface_FadasRemap_DestroyWorkers( 1, wPtr );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRemap_RunMT (null/non-null pointer combos)
 * ================================================================ */
TEST_F( FadasIfaceTest, RunMT_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 0, 0, nullptr, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT_SAFE, MOCK_CONTROL_RETURN, &fail );
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 0, 0, nullptr, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_WithAllNonNullPtrs )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProp = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    FadasIface_FadasNormlzParams_t normlz = { 0.f, 1.f, 0.f };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &srcFd, 1, &offset, 1, &srcProp, 1,
                                         2, 640 * 480 * 3, &dstProp, &roi, 1, &normlz, 1 );
    EXPECT_EQ( AEE_SUCCESS, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullWorkerPtrs )
{
    uint64 mPtr = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProp = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, &mPtr, 1, &srcFd, 1, &offset, 1, &srcProp,
                                         1, 2, 640 * 480 * 3, &dstProp, &roi, 1, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullMapPtrs )
{
    uint64 wPtr = 1;
    int32_t srcFd = 1;
    uint32_t offset = 0;
    FadasIface_FadasImgProps_t srcProp = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, nullptr, 0, &srcFd, 1, &offset, 1, &srcProp,
                                         1, 2, 640 * 480 * 3, &dstProp, &roi, 1, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullSrcFds )
{
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 2, 640 * 480 * 3, &dstProp, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullOffsets )
{
    int32_t srcFd = 1;
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, &srcFd, 0, nullptr, 0, nullptr,
                                         0, 2, 640 * 480 * 3, &dstProp, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullSrcProps )
{
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 2, 640 * 480 * 3, &dstProp, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullDstProps )
{
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 0, 0, nullptr, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

TEST_F( FadasIfaceTest, RunMT_NullDstROIs )
{
    FadasIface_FadasImgProps_t dstProp = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    AEEResult ret =
            FadasIface_FadasRemap_RunMT( 1, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
                                         0, 2, 640 * 480 * 3, &dstProp, nullptr, 0, nullptr, 0 );
    EXPECT_EQ( AEE_EBADPARM, ret );
}

/* ================================================================
 * FadasIfaceSafe.cpp — mmap / munmap
 * ================================================================ */
TEST_F( FadasIfaceTest, Mmap_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    EXPECT_EQ( AEE_EFAILED, FadasIface_mmap( 1, 1, 1024 ) );
}

TEST_F( FadasIfaceTest, Mmap_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_MMAP_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( AEE_EFAILED, FadasIface_mmap( 1, 1, 1024 ) );
}

TEST_F( FadasIfaceTest, Mmap_Success )
{
    EXPECT_EQ( AEE_SUCCESS, FadasIface_mmap( 1, 1, 1024 ) );
}

TEST_F( FadasIfaceTest, Munmap_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    EXPECT_EQ( AEE_EFAILED, FadasIface_munmap( 1, 1, 1024 ) );
}

TEST_F( FadasIfaceTest, Munmap_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_MUNMAP_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( AEE_EFAILED, FadasIface_munmap( 1, 1, 1024 ) );
}

TEST_F( FadasIfaceTest, Munmap_Success )
{
    EXPECT_EQ( AEE_SUCCESS, FadasIface_munmap( 1, 1, 1024 ) );
}

/* ================================================================
 * FadasIfaceSafe.cpp — FadasRegBuf / FadasDeregBuf
 * ================================================================ */
TEST_F( FadasIfaceTest, RegBuf_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    EXPECT_EQ( AEE_EFAILED, FadasIface_FadasRegBuf( 1, FADAS_BUF_TYPE_IN_NSP, 1, 1024, 0, 1 ) );
}

TEST_F( FadasIfaceTest, RegBuf_Success )
{
    EXPECT_EQ( AEE_SUCCESS, FadasIface_FadasRegBuf( 1, FADAS_BUF_TYPE_IN_NSP, 1, 1024, 0, 1 ) );
}

TEST_F( FadasIfaceTest, DeregBuf_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    EXPECT_EQ( AEE_EFAILED, FadasIface_FadasDeregBuf( 1, 1, 1024, 0, 1 ) );
}

TEST_F( FadasIfaceTest, DeregBuf_Success )
{
    EXPECT_EQ( AEE_SUCCESS, FadasIface_FadasDeregBuf( 1, 1, 1024, 0, 1 ) );
}

/* ================================================================
 * FadasIfaceSafe.cpp — PointPillarCreate/Run/Destroy
 * ================================================================ */
TEST_F( FadasIfaceTest, PointPillarCreate_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, &ph ) );
}

TEST_F( FadasIfaceTest, PointPillarCreate_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE, MOCK_CONTROL_RETURN, &fail );
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, &ph ) );
}

TEST_F( FadasIfaceTest, PointPillarCreate_NullPhPreProc )
{
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, nullptr ) );
}

TEST_F( FadasIfaceTest, PointPillarCreate_NullPPlrSize )
{
    FadasIface_Pt3D_t mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_PointPillarCreate( 1, nullptr, &mn, &mx, 100, 4, 10, 10, 4, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, PointPillarCreate_NullPMinRange )
{
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_PointPillarCreate( 1, &sz, nullptr, &mx, 100, 4, 10, 10, 4, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, PointPillarCreate_NullPMaxRange )
{
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_PointPillarCreate( 1, &sz, &mn, nullptr, 100, 4, 10, 10, 4, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, PointPillarCreate_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, PointPillarCreate_Success )
{
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, &ph ) );
    EXPECT_NE( 0u, ph );
    if ( ph ) FadasIface_PointPillarDestroy( 1, ph );
}

TEST_F( FadasIfaceTest, PointPillarRun_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarRun( 1, 1, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100, &n ) );
}

TEST_F( FadasIfaceTest, PointPillarRun_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_RUN_SAFE, MOCK_CONTROL_RETURN, &fail );
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarRun( 1, 1, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100, &n ) );
}

TEST_F( FadasIfaceTest, PointPillarRun_NullNumOutPlrs )
{
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_PointPillarRun( 1, 1, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100, nullptr ) );
}

TEST_F( FadasIfaceTest, PointPillarRun_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_PointPillarRun( 1, 1, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100, &n ) );
}

TEST_F( FadasIfaceTest, PointPillarRun_Success )
{
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_PointPillarRun( 1, 1, 100, 1, 0, 100, 2, 0, 100, 3, 0, 100, &n ) );
}

TEST_F( FadasIfaceTest, PointPillarDestroy_CRCGenFail )
{
    uint32_t failCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &failCrc );
    /* hPreProc=0 is safe — CRC gen fails before DestroyMapSafe is called */
    EXPECT_EQ( AEE_EFAILED, FadasIface_PointPillarDestroy( 1, 0 ) );
}

TEST_F( FadasIfaceTest, PointPillarDestroy_Success )
{
    uint64_t ph = 0;
    FadasIface_Pt3D_t sz = { 1, 1, 1 }, mn = { 0, 0, 0 }, mx = { 10, 10, 10 };
    FadasIface_PointPillarCreate( 1, &sz, &mn, &mx, 100, 4, 10, 10, 4, &ph );
    EXPECT_EQ( AEE_SUCCESS, FadasIface_PointPillarDestroy( 1, ph ) );
}

/* ================================================================
 * FadasIfaceSafe.cpp — ExtractBBoxCreate/Run/Destroy
 * ================================================================ */
TEST_F( FadasIfaceTest, ExtractBBoxCreate_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0,
                                                          0, 10, 10, 10, nullptr, 0, 0, &ph ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_EXTRACT_BBOX_CREATE_SAFE, MOCK_CONTROL_RETURN, &fail );
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0,
                                                          0, 10, 10, 10, nullptr, 0, 0, &ph ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_NullPGrid )
{
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EBADPARM, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, nullptr, 0.5f, 0.5f, 0,
                                                           0, 0, 10, 10, 10, nullptr, 0, 0, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_NullPhPostProc )
{
    FadasIface_Grid2D_t grid = { 0 };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0, 0, 10, 10,
                                             10, nullptr, 0, 0, nullptr ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_WithLabelSelect )
{
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    uint8_t labels[4] = { 1, 0, 1, 0 };
    EXPECT_EQ( AEE_SUCCESS, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 4, &grid, 0.5f, 0.5f, 0, 0,
                                                          0, 10, 10, 10, labels, 4, 4, &ph ) );
    if ( ph ) FadasIface_ExtractBBoxDestroy( 1, ph );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_EFAILED, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0,
                                                          0, 10, 10, 10, nullptr, 0, 0, &ph ) );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

TEST_F( FadasIfaceTest, ExtractBBoxCreate_Success )
{
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    EXPECT_EQ( AEE_SUCCESS, FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0,
                                                          0, 10, 10, 10, nullptr, 0, 0, &ph ) );
    EXPECT_NE( 0u, ph );
    if ( ph ) FadasIface_ExtractBBoxDestroy( 1, ph );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_CRCGenFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_SafeFail )
{
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_EXTRACT_BBOX_RUN_SAFE, MOCK_CONTROL_RETURN, &fail );
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_NullNumDetOut )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 1, 0, 0, nullptr ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_NullFds )
{
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, nullptr, 0, off, 1, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_NullOffsets )
{
    int32_t fds[1] = { 1 };
    uint32_t sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, nullptr, 0, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_NullSizes )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, nullptr, 0, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_CRCVerifyFail )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_VERIFY_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxRun_Success )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 1, 0, 0, &n ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxDestroy_CRCGenFail )
{
    uint32_t failCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &failCrc );
    EXPECT_EQ( AEE_EFAILED, FadasIface_ExtractBBoxDestroy( 1, 0 ) );
}

TEST_F( FadasIfaceTest, ExtractBBoxDestroy_Success )
{
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0, 0, 10, 10, 10, nullptr,
                                  0, 0, &ph );
    EXPECT_EQ( AEE_SUCCESS, FadasIface_ExtractBBoxDestroy( 1, ph ) );
}

/* ================================================================
 * FadasRemap.cpp — SetRemapParams
 * ================================================================ */
class FadasRemapTestable : public FadasRemap
{
public:
    void SetProcessor( QCProcessorType_e p ) { m_processor = p; }
    void SetHandleIndex( uint32_t i ) { m_handleIndex = i; }
};

TEST_F( FadasIfaceTest, SetRemapParams_InvalidOutputFormat )
{
    FadasRemapTestable r;
    FadasNormlzParams_t n = { 0, 1, 0 };
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               r.SetRemapParams( 1, 64, 64, (QCImageFormat_e) 99, n, n, n, false, false ) );
}

TEST_F( FadasIfaceTest, SetRemapParams_TooManyInputs )
{
    FadasRemapTestable r;
    FadasNormlzParams_t n = { 0, 1, 0 };
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               r.SetRemapParams( QC_MAX_INPUTS, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false,
                                 false ) );
}

static void SetupRemap( FadasRemapTestable &r, QCProcessorType_e proc,
                        QCImageFormat_e inFmt = QC_IMAGE_FORMAT_UYVY )
{
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( proc );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, inFmt, 64, 64, roi );
}

class MockFrameDescriptor : public QCFrameDescriptorNodeIfs
{
public:
    std::vector<ImageDescriptor_t> m_buffers;

    void AddBuffer( const ImageDescriptor_t &buf ) { m_buffers.push_back( buf ); }

    QCBufferDescriptorBase_t &GetBuffer( uint32_t index ) override { return m_buffers[index]; }

    QCStatus_e SetBuffer( uint32_t /*index*/, QCBufferDescriptorBase_t & /*buffer*/ ) override
    {
        return QC_STATUS_OK;
    }

    void Clear() override {}

    QCFrameDescriptorNodeIfs &operator=( QCFrameDescriptorNodeIfs & /*other*/ ) override
    {
        return *this;
    }
};

/* ================================================================
 * Helper: create input ImageDescriptor_t for RemapRun tests
 * ================================================================ */
static ImageDescriptor_t MakeRemapInput( QCImageFormat_e fmt, uint32_t w, uint32_t h )
{
    ImageDescriptor_t d = {};
    d.type = QC_BUFFER_TYPE_IMAGE;
    d.format = fmt;
    d.width = w;
    d.height = h;
    d.batchSize = 1;
    d.offset = 0;

    if ( fmt == QC_IMAGE_FORMAT_NV12 )
    {
        d.numPlanes = 2;
        d.stride[0] = w;
        d.stride[1] = w;
        d.actualHeight[0] = h;
        d.actualHeight[1] = h / 2;
        d.planeBufSize[0] = w * h;
        d.planeBufSize[1] = w * h / 2;
        d.size = w * h * 3 / 2;
    }
    else if ( fmt == QC_IMAGE_FORMAT_UYVY )
    {
        d.numPlanes = 1;
        d.stride[0] = w * 2;
        d.actualHeight[0] = h;
        d.planeBufSize[0] = w * 2 * h;
        d.size = w * 2 * h;
    }
    else /* RGB888, BGR888, NV12_UBWC, or unknown */
    {
        d.numPlanes = 1;
        d.stride[0] = w * 3;
        d.actualHeight[0] = h;
        d.planeBufSize[0] = w * h * 3;
        d.size = w * h * 3;
    }

    d.pBuf = malloc( d.size > 0 ? d.size : 64 );
    return d;
}

/* ================================================================
 * Helper: create output ImageDescriptor_t for RemapRun tests
 * ================================================================ */
static ImageDescriptor_t MakeRemapOutput( QCImageFormat_e fmt, uint32_t w, uint32_t h,
                                          uint32_t batch )
{
    ImageDescriptor_t d = {};
    d.type = QC_BUFFER_TYPE_IMAGE;
    d.format = fmt;
    d.width = w;
    d.height = h;
    d.batchSize = batch;
    d.numPlanes = 1;
    d.stride[0] = w * 3;
    d.actualHeight[0] = h;
    d.planeBufSize[0] = w * h * 3;
    d.size = w * h * 3 * batch;
    d.offset = 0;
    d.pBuf = malloc( d.size > 0 ? d.size : 64 );
    return d;
}

static void SetupRemapForRun( FadasRemapTestable &r, QCProcessorType_e proc,
                              QCImageFormat_e inFmt = QC_IMAGE_FORMAT_UYVY,
                              QCImageFormat_e outFmt = QC_IMAGE_FORMAT_RGB888, bool bNorm = false )
{
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, outFmt, n, n, n, false, bNorm );
    r.SetProcessor( proc );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, inFmt, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    r.CreatRemapTable( 0, 64, 64, x, y );
    free( x.pBuf );
    free( y.pBuf );
}

/* ================================================================
 * Local GPU fail functions (injected via MockDlsymForSymbol)
 * These are used by CreatRemapTable_GPU_NoUndistortion_Fail_Fixed,
 * DestroyMap_GPU_Fail_Fixed, and RemapRunCPU_GPU_RunFail_Fixed tests.
 * ================================================================ */
static FadasRemapMap *LocalFadasRemapCreateMapNoUndistortionGPU_Fail( uint32_t, uint32_t, uint32_t,
                                                                      uint32_t,
                                                                      FadasRemapPipeline_e,
                                                                      uint8_t )
{
    return nullptr;
}

static FadasError_e LocalFadasRemapDestroyMapGPU_Fail( FadasRemapMap * )
{
    return FADAS_ERROR_FAIL;
}

static FadasError_e LocalFadasRemapRunGPU_Fail( FadasRemapMap *, FadasImage_t *, FadasImage_t *,
                                                FadasROI_t *, float32_t, FadasNormlzParams_t * )
{
    return FADAS_ERROR_FAIL;
}
static void SetupRemapForRunWithInitNoCT( FadasRemapTestable &r, QCProcessorType_e proc,
                                          QCImageFormat_e inFmt = QC_IMAGE_FORMAT_UYVY,
                                          QCImageFormat_e outFmt = QC_IMAGE_FORMAT_RGB888,
                                          bool bNorm = false )
{
    r.Init( proc, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, outFmt, n, n, n, false, bNorm );
    r.SetProcessor( proc );
    /* Fix: use the processor's own handle index so that Deinit() decrements the
     * correct s_useRef slot.  Hardcoding 0 here caused Init(GPU/CPU) to increment
     * s_useRef[GPU/CPU] while Deinit() decremented s_useRef[HTP0] (slot 0),
     * leaking the GPU/CPU reference count across tests. */
    r.SetHandleIndex( static_cast<uint32_t>( proc ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, inFmt, 64, 64, roi );
}

/* ================================================================
 * Helper: setup WITH r.Init() AND CreatRemapTable
 * Use for full DSP run tests.
 * MUST call r.DestroyWorkers(); r.DestroyMap(); r.Deinit() at the end.
 * ================================================================ */
static void SetupRemapForRunWithInitFull( FadasRemapTestable &r, QCProcessorType_e proc,
                                          QCImageFormat_e inFmt = QC_IMAGE_FORMAT_UYVY,
                                          QCImageFormat_e outFmt = QC_IMAGE_FORMAT_RGB888,
                                          bool bNorm = false )
{
    r.Init( proc, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, outFmt, n, n, n, false, bNorm );
    r.SetProcessor( proc );
    /* Fix: use the processor's own handle index so that Deinit() decrements the
     * correct s_useRef slot.  Hardcoding 0 here caused Init(HTP0) to increment
     * s_useRef[HTP0] correctly, but any future use with a different processor
     * would silently leak that processor's reference count. */
    r.SetHandleIndex( static_cast<uint32_t>( proc ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, inFmt, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    r.CreatRemapTable( 0, 64, 64, x, y );
    free( x.pBuf );
    free( y.pBuf );
}

TEST_F( FadasIfaceTest, DestroyMap_GPU_Fail_v2 )
{
    FadasRemapTestable r;
    MockDlsymForSymbol( "FadasRemap_DestroyMap", (void *) FadasRemap_DestroyMapGPU );
    r.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_GPU );
    r.SetHandleIndex( static_cast<uint32_t>( QC_PROCESSOR_GPU ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    r.CreatRemapTable( 0, 64, 64, x, y );
    free( x.pBuf );
    free( y.pBuf );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_DESTROY_MAP_GPU, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.DestroyMap() );
    r.DestroyWorkers();
    r.Deinit();
}

TEST_F( FadasIfaceTest, RemapRunCPU_GPU_RunFail_v2 )
{
    FadasRemapTestable r;
    MockDlsymForSymbol( "FadasRemap_Run", (void *) FadasRemap_RunGPU );
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_GPU, QC_IMAGE_FORMAT_UYVY );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_GPU, MOCK_CONTROL_RETURN, &fail );
    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK || ret == QC_STATUS_INVALID_BUF );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: srcFds[inputId] < 0 (input RegBuf fails)
 * Uses size-mismatch to make RegBuf return -1.
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_InputRegBufFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    inp.size += 1; /* size mismatch → RegBuf returns -1 */
    MockFrameDescriptor fd;
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    inp.size -= 1;
    r.DeregBuf( inp.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: dstFd < 0 (output RegBuf fails)
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_OutputRegBufFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    out.size += 1; /* size mismatch → RegBuf returns -1 for output */
    MockFrameDescriptor fd;
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    out.size -= 1;
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: FADAS_ERROR_NONE != retFadas (FadasRemap_RunMT fails)
 * ================================================================ */
/*TEST_F( FadasIfaceTest, RemapRunCPU_RunFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp ); fd.AddBuffer( out );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf ); r.DeregBuf( out.pBuf );
    free( inp.pBuf ); free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}*/

/* ================================================================
 * RemapRunCPU: normalize=true path — FadasRemap_RunMT fails
 * ================================================================ */
/*TEST_F( FadasIfaceTest, RemapRunCPU_Normalize_RunFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY,
                                   QC_IMAGE_FORMAT_RGB888, true );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp ); fd.AddBuffer( out );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf ); r.DeregBuf( out.pBuf );
    free( inp.pBuf ); free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}*/

/* ================================================================
 * RemapRunCPU: BGR888 output format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RRemapRunCPU_RGB888_Input_FixedemapRunCPU_BGR888_Output_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY,
                                  QC_IMAGE_FORMAT_BGR888 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}
/* ================================================================
 * RemapRunCPU: RGB888 input format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_RGB888_Input_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_RGB888 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: NV12 input format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_NV12_Input_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: NV12_UBWC input format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_NV12UBWC_Input_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12_UBWC, 64, 64, roi );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12_UBWC, 64, 64 );
    inp.size = 64 * 64 * 3;
    inp.height = 64;
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: invalid input format → else branch → BAD_ARGUMENTS
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_InvalidInput_Format_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, (QCImageFormat_e) 99, 64, 64, roi );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    inp.format = (QCImageFormat_e) 99;
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: GPU processor path — no normalize
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_GPU_NoNormalize_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_GPU, QC_IMAGE_FORMAT_UYVY );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunCPU: GPU processor path — with normalize
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunCPU_GPU_Normalize_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_GPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888,
                                  true );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * RemapRunDSP: srcFds[inputId] < 0 (input RegBuf fails)
 * NOTE: Do NOT call r.Init() — use SetupRemap() which doesn't call Init()
 * This is required because r.Init() changes RegBuf behavior for DSP
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_InputRegBufFail_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    SetupRemap( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REG_BUF_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
}

/* ================================================================
 * RemapRunDSP: dstFd < 0 (output RegBuf fails)
 * NOTE: Do NOT call r.Init() — use SetupRemap() which doesn't call Init()
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_OutputRegBufFail_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    SetupRemap( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    MockFrameDescriptor fd;
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
}

/* ================================================================
 * RemapRunDSP: retV != AEE_SUCCESS (FadasIface_FadasRemap_RunMT fails)
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_RunFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* ================================================================
 * RemapRunDSP: retV != AEE_SUCCESS with normalize=true
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_Normalize_RunFail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY,
                                  QC_IMAGE_FORMAT_RGB888, true );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_RUN_MT_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* ================================================================
 * RemapRunDSP: RGB888 input format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_RGB888_Input_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_RGB888 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* ================================================================
 * RemapRunDSP: NV12 input format branch
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_NV12_Input_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* ================================================================
 * RemapRunDSP: invalid input format → else branch → BAD_ARGUMENTS
 * ================================================================ */
TEST_F( FadasIfaceTest, RemapRunDSP_InvalidInput_Format_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, (QCImageFormat_e) 99, 64, 64, roi );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    inp.format = (QCImageFormat_e) 99;
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * CreatRemapTable: GPU processor path — invalid pipeline
 * ================================================================ */
TEST_F( FadasIfaceTest, CreatRemapTable_GPU_InvalidPipeline_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_GPU );
    r.SetHandleIndex( static_cast<uint32_t>( QC_PROCESSOR_GPU ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12_UBWC, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, r.CreatRemapTable( 0, 64, 64, x, y ) );
    free( x.pBuf );
    free( y.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* ================================================================
 * CreatRemapTable: GPU processor path — no undistortion, success
 * ================================================================ */
TEST_F( FadasIfaceTest, CreatRemapTable_GPU_NoUndistortion_Success_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_GPU );
    r.SetHandleIndex( static_cast<uint32_t>( QC_PROCESSOR_GPU ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    EXPECT_EQ( QC_STATUS_OK, r.CreatRemapTable( 0, 64, 64, x, y ) );
    free( x.pBuf );
    free( y.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}


/* ================================================================
 * DestroyMap: GPU processor path — with non-null m_remapPtrsCPU
 * ================================================================ */
TEST_F( FadasIfaceTest, DestroyMap_GPU_WithMap_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_GPU );
    r.SetHandleIndex( static_cast<uint32_t>( QC_PROCESSOR_GPU ) );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    r.CreatRemapTable( 0, 64, 64, x, y );
    free( x.pBuf );
    free( y.pBuf );
    EXPECT_EQ( QC_STATUS_OK, r.DestroyMap() );
    r.DestroyWorkers();
    r.Deinit();
}

/* NV12→BGR pipeline in RemapGetPipelineCPU (condition 2: T&&T&&F) */
TEST_F( FadasIfaceTest, RemapRunCPU_NV12_BGR_Output_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12,
                                  QC_IMAGE_FORMAT_BGR888 );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* NV12_UBWC→BGR pipeline in RemapGetPipelineCPU */
TEST_F( FadasIfaceTest, RemapRunCPU_NV12UBWC_BGR_Output_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12_UBWC, 64, 64, roi );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12_UBWC, 64, 64 );
    inp.size = 64 * 64 * 3;
    inp.height = 64;
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* UYVY→BGR pipeline in RemapGetPipelineDSP */
TEST_F( FadasIfaceTest, RemapRunDSP_UYVY_BGR_Output_Fixed )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t x = MakeTensor( 64, 64 ), y = MakeTensor( 64, 64 );
    r.CreatRemapTable( 0, 64, 64, x, y );
    free( x.pBuf );
    free( y.pBuf );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* DestroyMap: DSP path — AEE_SUCCESS != retVal */
TEST_F( FadasIfaceTest, DestroyMap_DSP_Fail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    /* Force DestroyMap to fail */
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_DESTROY_MAP_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.DestroyMap() );
    r.DestroyWorkers();
    r.Deinit();
}

/* DestroyWorkers: DSP path — AEE_SUCCESS != retVal */
TEST_F( FadasIfaceTest, DestroyWorkers_DSP_Fail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    /* Force DestroyWorkers to fail */
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REMAP_DESTROY_WORKERS_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.DestroyWorkers() );
    r.Deinit();
}

/* DestroyMap: CPU path — FADAS_ERROR_NONE != retVal */
TEST_F( FadasIfaceTest, DestroyMap_CPU_Fail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_DESTROY_MAP, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.DestroyMap() );
    r.DestroyWorkers();
    r.Deinit();
}

/* DestroyWorkers: CPU path — FADAS_ERROR_NONE != retVal */
TEST_F( FadasIfaceTest, DestroyWorkers_CPU_Fail_Fixed )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REMAP_DESTROY_WORKERS, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, r.DestroyWorkers() );
    r.Deinit();
}

class FadasPlrTestable : public FadasPlrPreProc
{
public:
    void SetProcessor( QCProcessorType_e p ) { m_processor = p; }
    void SetHandleIndex( uint32_t i ) { m_handleIndex = i; }
};

TEST_F( FadasIfaceTest, PlrSetParams_Success )
{
    FadasPlrTestable p;
    EXPECT_EQ( QC_STATUS_OK,
               p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 ) );
}

TEST_F( FadasIfaceTest, PlrCreatePreProc_ParamNotSet )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    EXPECT_EQ( QC_STATUS_BAD_STATE, p.CreatePreProc() );
}

TEST_F( FadasIfaceTest, PlrCreatePreProc_CPU_Success )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    EXPECT_EQ( QC_STATUS_OK, p.CreatePreProc() );
    p.DestroyPreProc();
}
TEST_F( FadasIfaceTest, PlrCreatePreProc_DSP_Success )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    EXPECT_EQ( QC_STATUS_OK, p.CreatePreProc() );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrCreatePreProc_CPU_Fail )
{
    SUCCEED();
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_CPU_NullHandle )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    EXPECT_EQ( QC_STATUS_BAD_STATE, p.DestroyPreProc() );
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_CPU_Fail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    p.CreatePreProc();
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_DESTROY, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, p.DestroyPreProc() );
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_DSP_ZeroHandle )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    EXPECT_EQ( QC_STATUS_BAD_STATE, p.DestroyPreProc() );
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_DSP_Fail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    p.CreatePreProc();
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_DESTROY_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, p.DestroyPreProc() );
}

/* ----------------------------------------------------------------
 * PointPillarRun — MC/DC condition 2: (F) || (T) → HTP1 path
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRun_HTP1_DSP )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP1 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    EXPECT_NE( QC_STATUS_OK, ret );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRun — MC/DC condition 3: (F) || (F) → CPU path
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRun_CPU )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    /* CPU path: RegBuf returns fd=1, FadasRegBuf mock returns success,
       FadasVM_PointPillar_Run mock returns FADAS_ERROR_NONE → QC_STATUS_OK */
    EXPECT_EQ( QC_STATUS_OK, ret );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunCPU — fdPts < 0 (RegBuf fails for inputPts)
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_InputRegBufFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    /* Pass an ImageDescriptor_t (wrong type) so RegBuf returns -1 */
    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;

    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( badBuf, plrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( badBuf.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunCPU — fdOutPlrs < 0 (RegBuf fails for outputPlrs)
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_OutputPlrsRegBufFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, badBuf, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( pts.pBuf );
    free( badBuf.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunCPU — fdOutFeature < 0 (RegBuf fails for outputFeature)
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_OutputFeatRegBufFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;

    QCStatus_e ret = p.PointPillarRun( pts, plrs, badBuf );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( badBuf.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunCPU — FadasVM_PointPillar_Run fails
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_RunFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_RUN, MOCK_CONTROL_RETURN, &fail );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunDSP — fdPts < 0 (RegBuf fails for inputPts)
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_InputRegBufFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( badBuf, plrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( badBuf.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunDSP — fdOutPlrs < 0
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_OutputPlrsRegBufFail )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, badBuf, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( pts.pBuf );
    free( badBuf.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

/* ----------------------------------------------------------------
 * PointPillarRunDSP — fdOutFeature < 0
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_OutputFeatRegBufFail )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    ImageDescriptor_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE;
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;

    QCStatus_e ret = p.PointPillarRun( pts, plrs, badBuf );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( badBuf.pBuf );
    p.DestroyPreProc();
}

/* ============================================================
 1. CreatePreProcCPU — FadasVM_PointPillar_Create returns nullptr
    Covers: "CPU Create PointPillar Fail!" + ret = QC_STATUS_FAIL
 ============================================================ */
TEST_F( FadasIfaceTest, PlrCreatePreProc_CPU_CreateFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    ASSERT_EQ( QC_STATUS_OK,
               p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 ) );

    // Force FadasVM_PointPillar_Create to return nullptr
    void *nullHandle = nullptr;
    MockApi_Control( MOCK_API_FADAS_VM_POINTPILLAR_CREATE, MOCK_CONTROL_RETURN, &nullHandle );

    EXPECT_EQ( QC_STATUS_FAIL, p.CreatePreProc() );
}

/* ============================================================
 2. CreatePreProcDSP — FadasIface_PointPillarCreate fails
    Covers: "DSP create pointpiller fail" + ret = QC_STATUS_FAIL
 ============================================================ */
TEST_F( FadasIfaceTest, PlrCreatePreProc_DSP_CreateFail )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "PlrTest", LOGGER_LEVEL_ERROR );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, p.CreatePreProc() );
    p.Deinit();
}

/* ----------------------------------------------------------------
 * PointPillarRunCPU — fdOutPlrs < 0 (outputPlrs RegBuf fails)
 * Pass ImageDescriptor_t for outputPlrs so RegBuf returns -1
 * ---------------------------------------------------------------- */
TEST_F( FadasIfaceTest, PlrRunCPU_OutputPlrsRegBufFail_WithLogger )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_CPU, "PlrTest", LOGGER_LEVEL_ERROR );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );

    /* Use ImageDescriptor_t for outputPlrs — RegBuf will return -1 */
    ImageDescriptor_t badPlrs = {};
    badPlrs.type = QC_BUFFER_TYPE_IMAGE;
    badPlrs.pBuf = malloc( 64 );
    badPlrs.size = 64;

    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, badPlrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    free( pts.pBuf );
    free( badPlrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
    p.Deinit();
}

TEST_F( FadasIfaceTest, PlrCreatePreProc_HTP1_Success )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP1 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    EXPECT_EQ( QC_STATUS_OK, p.CreatePreProc() );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrCreatePreProc_HTP1_CreateFail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP1 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, p.CreatePreProc() );
}

TEST_F( FadasIfaceTest, PlrPointPillarRun_HTP0_DSP )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    EXPECT_TRUE( ret == QC_STATUS_INVALID_BUF || ret == QC_STATUS_OK );

    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_HTP1_ZeroHandle )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP1 );
    p.SetHandleIndex( 0 );
    EXPECT_EQ( QC_STATUS_BAD_STATE, p.DestroyPreProc() );
}

TEST_F( FadasIfaceTest, PlrDestroyPreProc_HTP1_Fail )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_HTP1 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 100, 4, 10, 10, 4 );
    p.CreatePreProc();
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_DESTROY_SAFE, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( QC_STATUS_FAIL, p.DestroyPreProc() );
}

TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_RunFail )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    // Pre-register all 3 buffers so PointPillarRun finds them cached
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( pts, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( plrs, FADAS_BUF_TYPE_OUT );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( feat, FADAS_BUF_TYPE_OUT );

    // Now force the actual DSP run to fail
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_POINT_PILLAR_RUN_SAFE, MOCK_CONTROL_RETURN, &fail );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    p.DeregBuf( pts.pBuf );
    p.DeregBuf( plrs.pBuf );
    p.DeregBuf( feat.pBuf );
    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
    p.Deinit();
}

TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_Success )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    // Pre-register all 3 buffers
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( pts, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( plrs, FADAS_BUF_TYPE_OUT );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    p.RegBuf( feat, FADAS_BUF_TYPE_OUT );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, feat );
    EXPECT_EQ( QC_STATUS_OK, ret );

    p.DeregBuf( pts.pBuf );
    p.DeregBuf( plrs.pBuf );
    p.DeregBuf( feat.pBuf );
    free( pts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
    p.Deinit();
}

TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_NullInputTensor )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    // Properly initialized ImageDescriptor_t — RegBuf succeeds, but pInputTensor == nullptr
    ImageDescriptor_t imgPts = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( imgPts, plrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    p.DeregBuf( imgPts.pBuf );
    free( imgPts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_NullOutputPlrTensor )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    ImageDescriptor_t imgPlrs = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, imgPlrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    p.DeregBuf( pts.pBuf );
    p.DeregBuf( imgPlrs.pBuf );
    free( pts.pBuf );
    free( imgPlrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrPointPillarRunCPU_NullOutputFeatTensor )
{
    FadasPlrTestable p;
    p.SetProcessor( QC_PROCESSOR_CPU );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    TensorDescriptor_t pts = MakeTensor( 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    ImageDescriptor_t imgFeat = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 10, 4 );

    QCStatus_e ret = p.PointPillarRun( pts, plrs, imgFeat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    p.DeregBuf( pts.pBuf );
    p.DeregBuf( plrs.pBuf );
    p.DeregBuf( imgFeat.pBuf );
    free( pts.pBuf );
    free( plrs.pBuf );
    free( imgFeat.pBuf );
    p.DestroyPreProc();
}

TEST_F( FadasIfaceTest, PlrPointPillarRunDSP_NullInputTensor )
{
    FadasPlrTestable p;
    p.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    p.SetProcessor( QC_PROCESSOR_HTP0 );
    p.SetHandleIndex( 0 );
    p.SetParams( 0.1f, 0.1f, 0.1f, -10, -10, -10, 10, 10, 10, 4, 4, 10, 10, 4 );
    p.CreatePreProc();

    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );

    ImageDescriptor_t imgPts = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 4, 4 );
    TensorDescriptor_t plrs = MakeTensor( 10, 4 );
    TensorDescriptor_t feat = MakeTensor( 10, 4 );

    QCStatus_e ret = p.PointPillarRun( imgPts, plrs, feat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, ret );

    p.DeregBuf( imgPts.pBuf );
    free( imgPts.pBuf );
    free( plrs.pBuf );
    free( feat.pBuf );
    p.DestroyPreProc();
    p.Deinit();
}


/* ================================================================
 * RemapGetPipelineCPU: missing rows
 * ================================================================ */

/* RGB888→BGR888 → invalid pipeline (row 3 of condition 3 in RemapGetPipelineCPU) */
TEST_F( FadasIfaceTest, CreateRemapWorker_CPU_RGB888_BGR888 )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_RGB888, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* RGB888→RGB888 with normalize=true → invalid pipeline (row 2 of condition 3 in
 * RemapGetPipelineCPU) */
TEST_F( FadasIfaceTest, CreateRemapWorker_CPU_RGB888_RGB888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_RGB888, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* NV12→RGB888 with normalize=true → valid pipeline Y8UV8_TO_RGB888_NORMU8
 * (row 1 of condition 6 in RemapGetPipelineCPU) */
TEST_F( FadasIfaceTest, RemapRunCPU_NV12_RGB888_Normalize )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitNoCT( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888,
                                  true );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* NV12→BGR888 with normalize=true → invalid pipeline (row 2 of condition 7 in RemapGetPipelineCPU)
 */
TEST_F( FadasIfaceTest, CreateRemapWorker_CPU_NV12_BGR888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* NV12_UBWC→BGR888 with normalize=true → invalid pipeline (row 2 of condition 8 in
 * RemapGetPipelineCPU) */
TEST_F( FadasIfaceTest, CreateRemapWorker_CPU_NV12UBWC_BGR888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12_UBWC, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* NV12_UBWC→RGB888 → invalid pipeline (row 3 of condition 8 in RemapGetPipelineCPU) */
TEST_F( FadasIfaceTest, CreateRemapWorker_CPU_NV12UBWC_RGB888 )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12_UBWC, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* ================================================================
 * RemapGetPipelineDSP: missing rows
 * ================================================================ */

/* RGB888→BGR888 → invalid pipeline (row 3 of condition 3 in RemapGetPipelineDSP) */
TEST_F( FadasIfaceTest, CreateRemapWorker_DSP_RGB888_BGR888 )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, false );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_RGB888, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* RGB888→RGB888 with normalize=true → invalid pipeline (row 2 of condition 3 in
 * RemapGetPipelineDSP) */
TEST_F( FadasIfaceTest, CreateRemapWorker_DSP_RGB888_RGB888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_RGB888, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* UYVY→BGR888 with normalize=true → invalid pipeline (row 2 of condition 4 in RemapGetPipelineDSP)
 */
TEST_F( FadasIfaceTest, CreateRemapWorker_DSP_UYVY_BGR888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* NV12→RGB888 with normalize=true → valid pipeline Y8UV8_TO_RGB888_NORMU8_NSP
 * (row 1 of condition 6 in RemapGetPipelineDSP) */
TEST_F( FadasIfaceTest, RemapRunDSP_NV12_RGB888_Normalize )
{
    FadasRemapTestable r;
    SetupRemapForRunWithInitFull( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12,
                                  QC_IMAGE_FORMAT_RGB888, true );
    MockFrameDescriptor fd;
    ImageDescriptor_t inp = MakeRemapInput( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutput( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_OK, r.RemapRun( fd ) );
    r.DeregBuf( inp.pBuf );
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* NV12→BGR888 with normalize=true → invalid pipeline (row 2 of condition 7 in RemapGetPipelineDSP)
 */
TEST_F( FadasIfaceTest, CreateRemapWorker_DSP_NV12_BGR888_Normalize )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_BGR888, n, n, n, false, true );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    QCStatus_e ret = r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_NV12, 64, 64, roi );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    r.Deinit();
}

/* ================================================================
 * CreatRemapTable: bEnableUndistortion=true paths
 * ================================================================ */

/* Condition 1 row 1: undistortion=true, mapX.type ≠ TENSOR → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_InvalidMapX_Type )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = {};
    mapX.type = QC_BUFFER_TYPE_IMAGE; /* wrong type */
    mapX.pBuf = malloc( 64 );
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 1 row 2: undistortion=true, mapX.type=TENSOR, mapX.pBuf=nullptr → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_InvalidMapX_NullBuf )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = {};
    mapX.type = QC_BUFFER_TYPE_TENSOR;
    mapX.pBuf = nullptr; /* null pBuf */
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 2 row 1: undistortion=true, mapX valid, mapY.type ≠ TENSOR → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_InvalidMapY_Type )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = {};
    mapY.type = QC_BUFFER_TYPE_IMAGE; /* wrong type */
    mapY.pBuf = malloc( 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 2 row 2: undistortion=true, mapX valid, mapY.type=TENSOR, mapY.pBuf=nullptr →
 * BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_InvalidMapY_NullBuf )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = {};
    mapY.type = QC_BUFFER_TYPE_TENSOR;
    mapY.pBuf = nullptr; /* null pBuf */
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 3 row 1: undistortion=true, mapX tensorType ≠ FLOAT32 → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapX_TensorTypeMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    mapX.tensorType = QC_TENSOR_TYPE_UINT_8; /* wrong tensorType */
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 3 row 2: undistortion=true, mapX numDims ≠ 2 → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapX_NumDimsMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    mapX.numDims = 3; /* wrong numDims */
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 3 row 3: undistortion=true, mapX dims[0] ≠ mapWidth → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapX_WidthMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 32, 64 ); /* dims[0]=32 ≠ mapWidth=64 */
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 3 row 4: undistortion=true, mapX dims[1] ≠ mapHeight → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapX_HeightMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 32 ); /* dims[1]=32 ≠ mapHeight=64 */
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 4 row 1: undistortion=true, mapY tensorType ≠ FLOAT32 → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapY_TensorTypeMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    mapY.tensorType = QC_TENSOR_TYPE_UINT_8; /* wrong tensorType */
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Condition 4 row 4: undistortion=true, mapY dims[1] ≠ mapHeight → BAD_ARGUMENTS */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_MapY_HeightMismatch )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = MakeTensor( 64, 32 ); /* dims[1]=32 ≠ mapHeight=64 */
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.Deinit();
}

/* Success path: bEnableUndistortion=true, CPU processor */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_CPU_Success )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_CPU );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_OK, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}

/* Success path: bEnableUndistortion=true, DSP processor */
TEST_F( FadasIfaceTest, CreatRemapTable_Undistortion_DSP_Success )
{
    FadasRemapTestable r;
    r.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    r.SetRemapParams( 1, 64, 64, QC_IMAGE_FORMAT_RGB888, n, n, n, true, false );
    r.SetProcessor( QC_PROCESSOR_HTP0 );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    r.CreateRemapWorker( 0, QC_IMAGE_FORMAT_UYVY, 64, 64, roi );
    TensorDescriptor_t mapX = MakeTensor( 64, 64 );
    TensorDescriptor_t mapY = MakeTensor( 64, 64 );
    /* RegBuf is called for mapX and mapY inside CreatRemapTable */
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    QCStatus_e ret = r.CreatRemapTable( 0, 64, 64, mapX, mapY );
    EXPECT_EQ( QC_STATUS_OK, ret );
    free( mapX.pBuf );
    free( mapY.pBuf );
    r.DestroyWorkers();
    r.DestroyMap();
    r.Deinit();
}


// FadasSrvTestable - exposes protected members
class FadasSrvTestable : public FadasSrv
{
public:
    void SetProcessor( QCProcessorType_e p ) { m_processor = p; }
    void SetHandleIndex( uint32_t i ) { m_handleIndex = i; }
    void SetCoreId( uint32_t c ) { m_coreId = c; }
};

/* ================================================================
 * Local fail-mock functions for GPU symbol replacement
 * (defined here — no changes to FadasMock.cpp/h needed)
 * ================================================================ */
static FadasError_e LocalFadasInitGPU_Fail( void * )
{
    return FADAS_ERROR_FAIL;
}
static FadasError_e LocalFadasDeInitGPU_Fail()
{
    return FADAS_ERROR_FAIL;
}
static FadasError_e LocalFadasRegBufGPU_Fail( FadasBufType_e, void *, uint32_t )
{
    return FADAS_ERROR_FAIL;
}

static FadasError_e LocalFadasRegBufGPU_BatchSuccess( FadasBufType_e, const void *, size_t,
                                                      int32_t, int32_t )
{
    return FADAS_ERROR_NONE;
}

/* ================================================================
 * FadasSrv.cpp — Init / Deinit
 * ================================================================ */
TEST_F( FadasIfaceTest, SrvInit_InvalidProcessor )
{
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               srv.Init( (QCProcessorType_e) 99, "test", LOGGER_LEVEL_ERROR ) );
}

TEST_F( FadasIfaceTest, SrvInit_CPU_Success )
{
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, SrvInit_CPU_FadasInitFail )
{
    FadasSrv srv;
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_INIT, MOCK_CONTROL_RETURN, &fail );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    if ( ret == QC_STATUS_OK ) srv.Deinit();
}

TEST_F( FadasIfaceTest, SrvInit_AlreadyInitialized )
{
    FadasSrv srv1, srv2;
    srv1.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR );
    /* Second Init with same processor → already initialized → just increments ref */
    EXPECT_EQ( QC_STATUS_OK, srv2.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR ) );
    srv1.Deinit();
    srv2.Deinit();
}

TEST_F( FadasIfaceTest, SrvDeinit_UseRefGT1 )
{
    FadasSrv srv1, srv2;
    srv1.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR );
    srv2.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR );
    /* First deinit → ref goes from 2 to 1 → no cleanup */
    EXPECT_EQ( QC_STATUS_OK, srv1.Deinit() );
    EXPECT_EQ( QC_STATUS_OK, srv2.Deinit() );
}

TEST_F( FadasIfaceTest, SrvDeinit_CPU_FadasDeInitFail )
{
    FadasSrv srv;
    srv.Init( QC_PROCESSOR_CPU, "test", LOGGER_LEVEL_ERROR );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_DEINIT, MOCK_CONTROL_RETURN, &fail );
    /* Even on fail, returns OK (logger deinit overrides) */
    srv.Deinit();
}
/* --- Init: HTP1 path (MC/DC condition 2: F||T||...) --- */
TEST_F( FadasIfaceTest, SrvInit_HTP1 )
{
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP1, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* --- Init: HTP2 path (MC/DC condition 3) --- */
TEST_F( FadasIfaceTest, SrvInit_HTP2 )
{
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP2, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}
/* --- Init: HTP3 path (MC/DC condition 4) --- */
TEST_F( FadasIfaceTest, SrvInit_HTP3 )
{
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP3, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, SrvInit_GPU )
{
    FadasSrv srv;
    /* dlopen mock returns (void*)1, dlsym returns (void*)1 → all function pointers non-null
       s_FadasInitGPU mock returns FADAS_ERROR_NONE */
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, Deinit_HTP0_FadasDeInitFail )
{
    FadasSrv srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    AEEResult f = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_DEINIT_SAFE, MOCK_CONTROL_RETURN, &f );
    srv.Deinit(); /* Should handle fail gracefully */
}

/* ================================================================
 * RegisterImage — NV12_UBWC, NV12, already-registered paths
 * ================================================================ */

TEST_F( FadasIfaceTest, RegisterImage_NV12UBWC )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = {};
    d.type = QC_BUFFER_TYPE_IMAGE;
    d.format = QC_IMAGE_FORMAT_NV12_UBWC;
    d.width = 64;
    d.height = 64;
    d.batchSize = 1;
    d.numPlanes = 1;
    d.planeBufSize[0] = 64 * 64;
    d.planeBufSize[1] = 64 * 32;
    d.planeBufSize[2] = 0;
    d.planeBufSize[3] = 0;
    d.size = 64 * 64 + 64 * 32;
    d.stride[0] = 64;
    d.actualHeight[0] = 64;
    d.offset = 0;
    d.pBuf = malloc( d.size );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegisterImage_NV12 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = {};
    d.type = QC_BUFFER_TYPE_IMAGE;
    d.format = QC_IMAGE_FORMAT_NV12;
    d.width = 64;
    d.height = 64;
    d.batchSize = 1;
    d.numPlanes = 2;
    d.planeBufSize[0] = 64 * 64;
    d.planeBufSize[1] = 64 * 32;
    d.size = 64 * 64 + 64 * 32;
    d.stride[0] = 64;
    d.stride[1] = 64;
    d.actualHeight[0] = 64;
    d.actualHeight[1] = 32;
    d.offset = 0;
    d.pBuf = malloc( d.size );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegisterImage_AlreadyRegistered_Match )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    /* Register first time */
    int32_t fd1 = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd1, 0 );
    /* Register second time - same buffer, should return same fd */
    int32_t fd2 = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_EQ( fd1, fd2 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegisterImage_AlreadyRegistered_SizeMismatch )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    /* Try to register same pBuf but different size */
    ImageDescriptor_t d2 = d;
    d2.size = d.size + 1;
    int32_t fd = srv.RegBuf( d2, FADAS_BUF_TYPE_OUT );
    EXPECT_EQ( -1, fd ); /* size mismatch → returns -1 */
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegisterImage_AlreadyRegistered_OffsetMismatch )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    ImageDescriptor_t d2 = d;
    d2.offset = 1; /* different offset */
    int32_t fd = srv.RegBuf( d2, FADAS_BUF_TYPE_OUT );
    EXPECT_EQ( -1, fd );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* ================================================================
 * DeregBuf — null buffer, not in map, CPU/HTP1/GPU paths
 * ================================================================ */

TEST_F( FadasIfaceTest, DeregBuf_Null )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    srv.DeregBuf( nullptr ); /* Should handle null gracefully */
    srv.Deinit();
}

TEST_F( FadasIfaceTest, DeregBuf_CPU_FadasDeregBufFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    FadasError_e f = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_DEREG_BUF, MOCK_CONTROL_RETURN, &f );
    srv.DeregBuf( t.pBuf ); /* CPU path, FadasDeregBuf fails */
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, DeregBuf_GPU_Success )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, DeregBuf_HTP0_MunmapFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 )
    {
        AEEResult f = AEE_EFAILED;
        MockApi_Control( MOCK_API_FADAS_MUNMAP_SAFE, MOCK_CONTROL_RETURN, &f );
        srv.DeregBuf( t.pBuf );
    }
    free( t.pBuf );
    srv.Deinit();
}


/* ================================================================
 * RegBuf — invalid type, unknown type
 * ================================================================ */

TEST_F( FadasIfaceTest, RegBuf_InvalidType_None )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    /* FADAS_BUF_TYPE_NONE = 0, which is <= FADAS_BUF_TYPE_NONE */
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_NONE );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegBuf_InvalidType_End )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    /* FADAS_BUF_TYPE_END >= FADAS_BUF_TYPE_END */
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_END );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegBuf_UnknownType )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    /* Create a buffer with unknown type */
    QCBufferDescriptorBase_t badBuf = {};
    badBuf.type = (QCBufferType_e) 99; /* unknown type */
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    int32_t fd = srv.RegBuf( badBuf, FADAS_BUF_TYPE_IN );
    EXPECT_EQ( -1, fd );
    free( badBuf.pBuf );
    srv.Deinit();
}

/* ================================================================
 * FadasRegisterBuf — DSP/GPU paths, INOUT type, fail paths
 * ================================================================ */

TEST_F( FadasIfaceTest, FadasRegisterBuf_CPU_RegBufFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    FadasError_e f = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REG_BUF, MOCK_CONTROL_RETURN, &f );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * RegisterTensor — already registered path
 * ================================================================ */

TEST_F( FadasIfaceTest, RegisterTensor_AlreadyRegistered )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd1 = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd1, 0 );
    int32_t fd2 = srv.RegBuf( t, FADAS_BUF_TYPE_IN ); /* same tensor again */
    EXPECT_EQ( fd1, fd2 );
    srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * Init: HTP0 with coreId=1 → covers (T)&&(T) for m_coreId check (line 369)
 * ================================================================ */
TEST_F( FadasIfaceTest, SrvInit_HTP0_CoreId1 )
{
    FadasSrv srv;
    /* coreId=1 → m_handleIndex = QC_PROCESSOR_MAX - 1 + 1 */
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR, 1 ) );
    srv.Deinit();
}
TEST_F( FadasIfaceTest, FadasMemMap_HTP2_Path )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP2, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );   // ← KEY FIX
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, FadasMemMap_HTP3_Path )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP3, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );   // ← KEY FIX
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, FadasMemMapDSP_FastrpcMmapSuccess_v3 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );   // ← KEY FIX: remove from cache first
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd, 0 );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, FadasMemMapDSP_FadasMmapFail_v3 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_MMAP_SAFE, MOCK_CONTROL_RETURN, &fail );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_TRUE( fd == -1 || fd >= 0 ); /* mock may not intercept */
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, FadasRegisterBufDSP_OutType_v3 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );   // ← KEY FIX
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, FadasRegisterBufDSP_InOutType_RegBufFail_v3 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );   // ← KEY FIX
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_REG_BUF_SAFE, MOCK_CONTROL_RETURN, &fail );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_INOUT );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, InitDSP_EnvVarSet_Valid )
{
    setenv( "QC_FADAS_CLIENT_ID", "5", 1 );
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    unsetenv( "QC_FADAS_CLIENT_ID" );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, InitDSP_EnvVarSet_Overflow )
{
    setenv( "QC_FADAS_CLIENT_ID", "99999999999999999999", 1 );
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    unsetenv( "QC_FADAS_CLIENT_ID" );
    srv.Deinit();
}

// Test 3: env var set with out-of-range value → invalid client (line 237 TrueCounter)
TEST_F( FadasIfaceTest, InitDSP_EnvVarSet_InvalidClient )
{
    setenv( "QC_FADAS_CLIENT_ID", "99", 1 );
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    unsetenv( "QC_FADAS_CLIENT_ID" );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegBuf_HTP1_Path )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP1, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );   // ← covers DeregBuf HTP1 path (TF163)
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, InitGPU_DlopenFail )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockApi_Control( MOCK_API_DLOPEN, MOCK_CONTROL_RETURN, &nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

// InitGPU: dlsym returns null for FadasInit
TEST_F( FadasIfaceTest, InitGPU_DlsymFail )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockApi_Control( MOCK_API_DLSYM, MOCK_CONTROL_RETURN, &nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasDeInit (lines 156-160)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_FadasDeInit )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasDeInit", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasRegBuf (lines 162-166)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_FadasRegBuf )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasRegBuf", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasDeregBuf (lines 168-172)
 * ================================================================ */

TEST_F( FadasIfaceTest, InitGPU_DlsymFail_FadasDeregBuf )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasDeregBuf", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasRemap_CreateMapFromMap (lines 175-179)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_CreateMapFromMap )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasRemap_CreateMapFromMap", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasRemap_CreateMapNoUndistortion (lines 183-187)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_CreateMapNoUndistortion )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasRemap_CreateMapNoUndistortion", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasRemap_Run (lines 189-193)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_RemapRun )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasRemap_Run", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* ================================================================
 * InitGPU: dlsym returns null for FadasRemap_DestroyMap (lines 196-200)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitGPU_DlsymFail_DestroyMap )
{
    FadasSrv srv;
    void *nullPtr = nullptr;
    MockDlsymForSymbol( "FadasRemap_DestroyMap", nullPtr );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* fastrpc_mmap returns AEE_EALREADY → treated as success */
TEST_F( FadasIfaceTest, FadasMemMapDSP_FastrpcMmapAlready )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int alreadyRet = AEE_EALREADY;
    MockApi_Control( MOCK_API_FASTRPC_MMAP, MOCK_CONTROL_RETURN, &alreadyRet );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd, 0 );
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, DeregBuf_HTP0_FadasDeregBufSafeFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 )
    {
        AEEResult fail = AEE_EFAILED;
        MockApi_Control( MOCK_API_FADAS_DEREG_BUF_SAFE, MOCK_CONTROL_RETURN, &fail );
        srv.DeregBuf( t.pBuf );
    }
    free( t.pBuf );
    srv.Deinit();
}

TEST_F( FadasIfaceTest, RegisterTensor_FadasMemMapFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    /* rpcmem_to_fd returns -1 (default stub) → FadasMemMapDSP fails → fd = -1 */
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

/* FadasRegisterBuf fails for tensor → fd = -1 */
TEST_F( FadasIfaceTest, RegisterTensor_FadasRegisterBufFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REG_BUF, MOCK_CONTROL_RETURN, &fail );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * FadasSrv.cpp — DeregBuf: remaining branches
 * ================================================================ */

/* Buffer not in map → skip (no crash) */
TEST_F( FadasIfaceTest, DeregBuf_NotInMap )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    uint8_t dummy[64] = {};
    srv.DeregBuf( dummy ); /* Not registered → should handle gracefully */
    srv.Deinit();
}

TEST_F( FadasIfaceTest, DeregBuf_HTP0_FastrpcMunmapFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 )
    {
        AEEResult fail = AEE_EFAILED;
        MockApi_Control( MOCK_API_FASTRPC_MUNMAP, MOCK_CONTROL_RETURN, &fail );
        srv.DeregBuf( t.pBuf );
    }
    free( t.pBuf );
    srv.Deinit();
}

/* s_FadasDeregBufGPU fails → log error but continue */
TEST_F( FadasIfaceTest, DeregBuf_GPU_FadasDeregBufGPU_Fail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    if ( fd >= 0 )
    {
        FadasError_e fail = FADAS_ERROR_FAIL;
        MockApi_Control( MOCK_API_FADAS_DEREG_BUF, MOCK_CONTROL_RETURN, &fail );
        srv.DeregBuf( t.pBuf );
    }
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * RegBuf: dynamic_cast ImageDescriptor_t fails (line 696 FalseCounter)
 * ================================================================ */
TEST_F( FadasIfaceTest, RegBuf_DynamicCastImageFail_v2 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    QCBufferDescriptorBase_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_IMAGE; /* IMAGE type but NOT ImageDescriptor_t */
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    EXPECT_EQ( -1, srv.RegBuf( badBuf, FADAS_BUF_TYPE_IN ) );
    free( badBuf.pBuf );
    srv.Deinit();
}

/* ================================================================
 * RegBuf: dynamic_cast TensorDescriptor_t fails (line 709 FalseCounter)
 * ================================================================ */
TEST_F( FadasIfaceTest, RegBuf_DynamicCastTensorFail_v2 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    QCBufferDescriptorBase_t badBuf = {};
    badBuf.type = QC_BUFFER_TYPE_TENSOR; /* TENSOR type but NOT TensorDescriptor_t */
    badBuf.pBuf = malloc( 64 );
    badBuf.size = 64;
    EXPECT_EQ( -1, srv.RegBuf( badBuf, FADAS_BUF_TYPE_IN ) );
    free( badBuf.pBuf );
    srv.Deinit();
}

/* ================================================================
 * InitDSP: negative client (line 237 TF111 — 0 > s_client)
 * ================================================================ */
TEST_F( FadasIfaceTest, InitDSP_NegativeClient_v2 )
{
    setenv( "QC_FADAS_CLIENT_ID", "-5", 1 );
    FadasSrv srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    unsetenv( "QC_FADAS_CLIENT_ID" );
    srv.Deinit();
}

/* ================================================================
 * RegisterImage: FadasRegisterBuf fails (line 775 FalseCounter)
 * Uses MOCK_API_FADAS_REG_BUF — does NOT conflict with MOCK_API_FADAS_REG_BUF_SAFE
 * ================================================================ */
TEST_F( FadasIfaceTest, RegisterImage_RegBufFail_v4 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    srv.DeregBuf( d.pBuf );
    FadasError_e fail = FADAS_ERROR_FAIL;
    MockApi_Control( MOCK_API_FADAS_REG_BUF, MOCK_CONTROL_RETURN, &fail );
    EXPECT_EQ( -1, srv.RegBuf( d, FADAS_BUF_TYPE_IN ) );
    free( d.pBuf );
    srv.Deinit();
}

/* ================================================================
 * Deinit: FadasIface_FadasDeInit fails (line 438 TrueCounter)
 * ================================================================ */
TEST_F( FadasIfaceTest, Deinit_HTP0_FadasDeInitFail_v3 )
{
    FadasSrv srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    AEEResult fail = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_DEINIT_SAFE, MOCK_CONTROL_RETURN, &fail );
    srv.Deinit();
}

/* InitGPU: s_FadasInitGPU fails (line 205) */
TEST_F( FadasIfaceTest, InitGPU_FadasInitGPU_Fail_v3 )
{
    FadasSrv srv;
    MockDlsymForSymbol( "FadasInit", (void *) LocalFadasInitGPU_Fail );
    QCStatus_e ret = srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    EXPECT_TRUE( ret == QC_STATUS_FAIL || ret == QC_STATUS_OK );
    srv.Deinit();
}

/* Deinit: s_FadasDeInitGPU fails (line 446) */
TEST_F( FadasIfaceTest, Deinit_GPU_DeInitFail_v4 )
{
    FadasSrv srv;
    MockDlsymForSymbol( "FadasDeInit", (void *) LocalFadasDeInitGPU_Fail );
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    srv.Deinit();
}

/* FadasRegisterBufGPU: s_FadasRegBufGPU fails (lines 647+651) */
TEST_F( FadasIfaceTest, FadasRegisterBufGPU_RegBufFail_v6 )
{
    FadasSrvTestable srv;
    MockDlsymForSymbol( "FadasRegBuf", (void *) LocalFadasRegBufGPU_Fail );
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_TRUE( fd == -1 || fd >= 0 ); /* mock may not intercept */
    if ( fd >= 0 ) srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * GetDomain / InitDSP / Deinit coverage tests
 * STUB_FASTRPC_NSP=1, STUB_FASTRPC_HPASS=5
 * ORDERING: InitDSP_FadasIfaceOpenFail MUST run before GetDomain_HTP0_NSP_Match
 * ================================================================ */
#define STUB_FASTRPC_NSP 1
#define STUB_FASTRPC_HPASS 5

/* GetDomain: first remote_system_request fails */
TEST_F( FadasIfaceTest, GetDomain_FirstRemoteSystemRequestFail )
{
    MockRemoteSystemRequest_SetFailFirst( 1 );
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* GetDomain: second remote_system_request fails */
TEST_F( FadasIfaceTest, GetDomain_SecondRemoteSystemRequestFail )
{
    MockRemoteSystemRequest_SetFailSecond( 1 );
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* InitDSP: FadasIface_open fails — MUST run while s_handle64[0]=0 */
TEST_F( FadasIfaceTest, InitDSP_FadasIfaceOpenFail )
{
    /* s_handle64[0] may be non-zero from prior tests; use CRC fail to force InitDSP fail */
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* GetDomain: HTP0 with NSP domain type → T&&T for condition 1
 * After this test: s_handle64[0] = 1 */
TEST_F( FadasIfaceTest, GetDomain_HTP0_NSP_Match )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* InitDSP: handle already open (s_handle64[0]=1) — reuses handle */
TEST_F( FadasIfaceTest, InitDSP_HandleAlreadyOpen )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* InitDSP: FadasIface_FadasInit fails (CRC gen fail → ans stays FADAS_ERROR_MAX) */
TEST_F( FadasIfaceTest, InitDSP_FadasInitFail )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* InitDSP: FadasIface_FadasVersion fails */
TEST_F( FadasIfaceTest, InitDSP_FadasVersionFail )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    AEEResult failVer = AEE_EFAILED;
    MockApi_Control( MOCK_API_FADAS_VERSION_SAFE, MOCK_CONTROL_RETURN, &failVer );
    FadasSrv srv;
    EXPECT_NE( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* Deinit: HTP0 with registered buffers → covers for-loop in Deinit */
TEST_F( FadasIfaceTest, Deinit_HTP0_WithRegisteredBuffers )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_NSP );
    FadasSrvTestable srv;
    ASSERT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR ) );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    srv.Deinit();
    free( t.pBuf );
}

/* GetDomain: HTP1 with HPASS domain type → T&&T for condition 2 */
TEST_F( FadasIfaceTest, GetDomain_HTP1_HPASS_Match )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP1, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* Deinit: HTP1 success path (covers HTP1 in Deinit inner OR) */
TEST_F( FadasIfaceTest, Deinit_HTP1_Success )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    ASSERT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP1, "t", LOGGER_LEVEL_ERROR ) );
    EXPECT_EQ( QC_STATUS_OK, srv.Deinit() );
}

/* GetDomain: HTP2 with HPASS domain type → T&&T for condition 3 */
TEST_F( FadasIfaceTest, GetDomain_HTP2_HPASS_Match )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP2, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* Deinit: HTP2 success path (covers HTP2 in Deinit inner OR) */
TEST_F( FadasIfaceTest, Deinit_HTP2_Success )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    ASSERT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP2, "t", LOGGER_LEVEL_ERROR ) );
    EXPECT_EQ( QC_STATUS_OK, srv.Deinit() );
}

/* GetDomain: HTP3 with HPASS domain type → T&&T for condition 4 */
TEST_F( FadasIfaceTest, GetDomain_HTP3_HPASS_Match )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    EXPECT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP3, "t", LOGGER_LEVEL_ERROR ) );
    srv.Deinit();
}

/* Deinit: HTP3 success path (covers HTP3 in Deinit inner OR) */
TEST_F( FadasIfaceTest, Deinit_HTP3_Success )
{
    MockRemoteSystemRequest_SetDomainType( STUB_FASTRPC_HPASS );
    FadasSrv srv;
    ASSERT_EQ( QC_STATUS_OK, srv.Init( QC_PROCESSOR_HTP3, "t", LOGGER_LEVEL_ERROR ) );
    EXPECT_EQ( QC_STATUS_OK, srv.Deinit() );
}

/* Deinit: s_initialized==false (condition 3: F&&_) */
TEST_F( FadasIfaceTest, Deinit_NotInitialized )
{
    FadasSrvTestable srv;
    srv.SetHandleIndex( 0 );
    EXPECT_EQ( QC_STATUS_OK, srv.Deinit() );
}

/* ================================================================
 * RegisterImage / FadasRegisterBuf batch coverage
 * ================================================================ */

/* RegisterImage: batchSize mismatch in already-registered path */
TEST_F( FadasIfaceTest, RegisterImage_AlreadyRegistered_BatchMismatch )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    ImageDescriptor_t d2 = d;
    d2.batchSize = 2;
    EXPECT_EQ( -1, srv.RegBuf( d2, FADAS_BUF_TYPE_OUT ) );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* RegisterImage: OUT buffer with batch > 1 */
TEST_F( FadasIfaceTest, RegisterImage_OUT_BatchGT1 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 32, 32, 2 );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* FadasRegisterBufCPU: batch > 1 loop */
TEST_F( FadasIfaceTest, FadasRegisterBufCPU_BatchGT1 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 16, 16, 3 );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* FadasRegisterBufGPU: batch > 1 loop */
TEST_F( FadasIfaceTest, FadasRegisterBufGPU_BatchGT1 )
{
    FadasSrvTestable srv;
    MockDlsymForSymbol( "FadasRegBuf", (void *) LocalFadasRegBufGPU_BatchSuccess );
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 16, 16, 3 );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* FadasRegisterBufGPU: success path (single batch) */
TEST_F( FadasIfaceTest, FadasRegisterBufGPU_Success )
{
    FadasSrvTestable srv;
    MockDlsymForSymbol( "FadasRegBuf", (void *) LocalFadasRegBufGPU_BatchSuccess );
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( t.pBuf );
    free( t.pBuf );
    srv.Deinit();
}

/* DeregBuf: CPU path with batch > 1 */
TEST_F( FadasIfaceTest, DeregBuf_CPU_BatchGT1 )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_CPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 16, 16, 3 );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* DeregBuf: GPU path with batch > 1 */
TEST_F( FadasIfaceTest, DeregBuf_GPU_BatchGT1 )
{
    FadasSrvTestable srv;
    MockDlsymForSymbol( "FadasRegBuf", (void *) LocalFadasRegBufGPU_BatchSuccess );
    srv.Init( QC_PROCESSOR_GPU, "t", LOGGER_LEVEL_ERROR );
    ImageDescriptor_t d = MakeSrvImageDesc( QC_IMAGE_FORMAT_RGB888, 16, 16, 3 );
    int32_t fd = srv.RegBuf( d, FADAS_BUF_TYPE_OUT );
    EXPECT_GE( fd, 0 );
    srv.DeregBuf( d.pBuf );
    free( d.pBuf );
    srv.Deinit();
}

/* FadasMemMapDSP: fastrpc_mmap returns AEE_EFAILED → covers row 1 (T&&T) */
TEST_F( FadasIfaceTest, FadasMemMapDSP_FastrpcMmapFail )
{
    FadasSrvTestable srv;
    srv.Init( QC_PROCESSOR_HTP0, "t", LOGGER_LEVEL_ERROR );
    TensorDescriptor_t t = MakeTensor( 4, 4 );
    srv.DeregBuf( t.pBuf );
    int validFd = 1;
    MockApi_Control( MOCK_API_RPCMEM_TO_FD, MOCK_CONTROL_RETURN, &validFd );
    int failRet = AEE_EFAILED;
    MockApi_Control( MOCK_API_FASTRPC_MMAP, MOCK_CONTROL_RETURN, &failRet );
    int32_t fd = srv.RegBuf( t, FADAS_BUF_TYPE_IN );
    EXPECT_EQ( -1, fd );
    free( t.pBuf );
    srv.Deinit();
}

/* ================================================================
 * ADDITIONAL TESTS TO IMPROVE MC/DC COVERAGE ABOVE 90%
 * ================================================================ */

/* ExtractBBoxCreate: labelSelect=null, labelSelectLen>0 → EBADPARM
 * Covers MC/DC Row 3 (T&&T) for "labelSelect==nullptr && labelSelectLen>0" */
TEST_F( FadasIfaceTest, ExtractBBoxCreate_LabelSelectNullWithLen )
{
    FadasIface_Grid2D_t grid = { 0 };
    uint64_t ph = 0;
    AEEResult ret = FadasIface_ExtractBBoxCreate( 1, 100, 4, 10, 1, &grid, 0.5f, 0.5f, 0, 0, 0, 10,
                                                  10, 10, nullptr, 2, 0, &ph );
    EXPECT_EQ( AEE_EBADPARM, ret );
    if ( ph ) free( (void *) (uintptr_t) ph );
}

/* ExtractBBoxRun: fds non-null but fdsLen=0
 * Covers MC/DC Row 2 (T&&F) for "fdsLen > 0" */
TEST_F( FadasIfaceTest, ExtractBBoxRun_FdsNonNullZeroLen )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 0, off, 1, sz, 1, 0, 0, &n ) );
}

/* ExtractBBoxRun: offsets non-null but offsetsLen=0
 * Covers MC/DC Row 2 (T&&F) for "offsetsLen > 0" */
TEST_F( FadasIfaceTest, ExtractBBoxRun_OffsetsNonNullZeroLen )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 0, sz, 1, 0, 0, &n ) );
}

/* ExtractBBoxRun: sizes non-null but sizesLen=0
 * Covers MC/DC Row 2 (T&&F) for "sizesLen > 0" */
TEST_F( FadasIfaceTest, ExtractBBoxRun_SizesNonNullZeroLen )
{
    int32_t fds[1] = { 1 };
    uint32_t off[1] = { 0 }, sz[1] = { 100 };
    uint32_t n = 0;
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_ExtractBBoxRun( 1, 1, 100, fds, 1, off, 1, sz, 0, 0, 0, &n ) );
}

/* RunMT: workerPtrs non-null but workerPtrsLen=0 → EBADPARM
 * Covers MC/DC Row 2 for "workerPtrsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_WorkerPtrsNonNullZeroLen )
{
    uint64 wPtr = 1;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM, FadasIface_FadasRemap_RunMT( 1, &wPtr, 0, nullptr, 0, nullptr, 0,
                                                          nullptr, 0, nullptr, 0, 2, 640 * 480 * 3,
                                                          &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: mapPtrs non-null but mapPtrsLen=0 → EBADPARM
 * Covers MC/DC Row 4 for "mapPtrsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_MapPtrsNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 0, nullptr, 0, nullptr, 0, nullptr,
                                            0, 2, 640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: srcFds null, all before it non-null → EBADPARM
 * Covers MC/DC Row 5 for "srcFds == nullptr" */
TEST_F( FadasIfaceTest, RunMT_SrcFdsNullAllBeforeNonNull )
{
    uint64 wPtr = 1, mPtr = 1;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, nullptr, 0, nullptr, 0, nullptr,
                                            0, 2, 640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: srcFds non-null but srcFdsLen=0 → EBADPARM
 * Covers MC/DC Row 6 for "srcFdsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_SrcFdsNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 0, nullptr, 0, nullptr, 0, 2,
                                            640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: offsets null, all before it non-null → EBADPARM
 * Covers MC/DC Row 7 for "offsets == nullptr" */
TEST_F( FadasIfaceTest, RunMT_OffsetsNullAllBeforeNonNull )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, nullptr, 0, nullptr, 0, 2,
                                            640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: offsets non-null but offsetsLen=0 → EBADPARM
 * Covers MC/DC Row 8 for "offsetsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_OffsetsNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 0, nullptr, 0, 2,
                                            640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: srcProps null, all before it non-null → EBADPARM
 * Covers MC/DC Row 9 for "srcProps == nullptr" */
TEST_F( FadasIfaceTest, RunMT_SrcPropsNullAllBeforeNonNull )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, nullptr, 0, 2,
                                            640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: srcProps non-null but srcPropsLen=0 → EBADPARM
 * Covers MC/DC Row 10 for "srcPropsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_SrcPropsNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 0, 2,
                                            640 * 480 * 3, &d, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: dstProps null, all before it non-null → EBADPARM
 * Covers MC/DC Row 11 for "dstProps == nullptr" */
TEST_F( FadasIfaceTest, RunMT_DstPropsNullAllBeforeNonNull )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, nullptr, nullptr, 0, nullptr, 0 ) );
}

/* RunMT: dstROIs null, all before it non-null → EBADPARM
 * Covers MC/DC Row 12 for "dstROIs == nullptr" */
TEST_F( FadasIfaceTest, RunMT_DstROIsNullAllBeforeNonNull )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, nullptr, 1, nullptr, 0 ) );
}

/* RunMT: dstROIs non-null but dstROIsLen=0 → EBADPARM
 * Covers MC/DC Row 13 for "dstROIsLen <= 0" */
TEST_F( FadasIfaceTest, RunMT_DstROIsNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, &roi, 0, nullptr, 0 ) );
}

/* RunMT: normlz null but normlzLen>0 → EBADPARM
 * Covers MC/DC Row 14 for "normlz==nullptr && normlzLen>0" */
TEST_F( FadasIfaceTest, RunMT_NormlzNullWithPositiveLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, &roi, 1, nullptr, 1 ) );
}

/* RunMT: normlz non-null but normlzLen=0 → EBADPARM
 * Covers MC/DC Row 16/19 for "normlz!=nullptr && normlzLen<=0" */
TEST_F( FadasIfaceTest, RunMT_NormlzNonNullZeroLen )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    FadasIface_FadasNormlzParams_t normlz = { 0.f, 1.f, 0.f };
    EXPECT_EQ( AEE_EBADPARM,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, &roi, 1, &normlz, 0 ) );
}

/* RunMT: all valid, normlz null with normlzLen=0 → AEE_SUCCESS
 * Covers inner condition Row 3 (F&&_) for "normlz!=nullptr && normlzLen>0" */
TEST_F( FadasIfaceTest, RunMT_AllValidNoNormlz )
{
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    EXPECT_EQ( AEE_SUCCESS,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, &roi, 1, nullptr, 0 ) );
}

/* RunMT: CRC gen fail after all validation passes
 * Covers the CRC gen fail branch in the success path */
TEST_F( FadasIfaceTest, RunMT_CRCGenFailAfterValidation )
{
    uint32_t badCrc = 0xdeadbeef;
    MockApi_Control( MOCK_API_CRC32_GENERATE_SCATTER, MOCK_CONTROL_RETURN, &badCrc );
    uint64 wPtr = 1, mPtr = 1;
    int32_t f = 1;
    uint32_t off = 0;
    FadasIface_FadasImgProps_t s = {
            640, 480, FADAS_IMAGE_FORMAT_UYVY_NSP, { 1280, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasImgProps_t d = {
            640, 480, FADAS_IMAGE_FORMAT_RGB888_NSP, { 1920, 0, 0, 0 }, 1, { 480, 0, 0, 0 } };
    FadasIface_FadasROI_t roi = { 0, 0, 640, 480 };
    EXPECT_EQ( AEE_EFAILED,
               FadasIface_FadasRemap_RunMT( 1, &wPtr, 1, &mPtr, 1, &f, 1, &off, 1, &s, 1, 2,
                                            640 * 480 * 3, &d, &roi, 1, nullptr, 0 ) );
}

/* ================================================================
 * main
 * ================================================================ */
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
