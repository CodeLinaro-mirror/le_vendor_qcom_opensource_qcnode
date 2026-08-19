// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Infras/Memory/UtilsBase.hpp"
#include <algorithm>

#include "gtest/gtest.h"

using namespace QC;
using namespace QC::Memory;

class Test_QCMemoryUtilsBase : public testing::Test
{

protected:
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F( Test_QCMemoryUtilsBase, SANITY )
{

    QCStatus_e status;
    UtilsBase Ifs;
    // QCMemoryUtilsIfs

    ImageDescriptor_t imgDesc;
    TensorDescriptor_t tensorDesc;

    ImageProps_t imageProp;
    ImageBasicProps_t imageBaseProp;
    TensorProps_t tensProp;

    /* testing for UYVY */
    imageBaseProp = ImageBasicProps_t( 3840, 2160, QC_IMAGE_FORMAT_UYVY );
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( imgDesc.GetDataPtr(), imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.size, imgDesc.GetDataSize() );
    ASSERT_LE( 3840 * 2160 * 2, imageBaseProp.size );
    ASSERT_LE( 0, imgDesc.size );
    ASSERT_LE( 3840 * 2160 * 2, imgDesc.planeBufSize[0] );
    ASSERT_EQ( 1, imgDesc.numPlanes );
    ASSERT_EQ( 1, imgDesc.batchSize );
    ASSERT_EQ( QC_IMAGE_FORMAT_UYVY, imgDesc.format );
    ASSERT_LE( 3840 * 2, imgDesc.stride[0] );
    ASSERT_LE( 2160, imgDesc.actualHeight[0] );

    /* testing allocate image for NV12 */
    imageBaseProp = ImageBasicProps_t( 3840, 2160, QC_IMAGE_FORMAT_NV12 );
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.size, imgDesc.GetDataSize() );
    ASSERT_LE( 3840 * 2160 * 3 / 20, imageBaseProp.size );
    ASSERT_LE( 0, imgDesc.size );
    ASSERT_EQ( 2, imgDesc.numPlanes );
    ASSERT_LE( 3840, imgDesc.stride[0] );
    ASSERT_LE( 2160, imgDesc.actualHeight[0] );
    ASSERT_LE( 3840, imgDesc.stride[1] );
    ASSERT_LE( 2160 / 2, imgDesc.actualHeight[1] );

    /* testing for RGB */
    imageBaseProp = ImageBasicProps_t( 1024, 768, QC_IMAGE_FORMAT_RGB888 );
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_LE( 1024 * 768 * 3, imageBaseProp.size );
    ASSERT_EQ( 1, imgDesc.numPlanes );
    ASSERT_LE( 1024, imgDesc.stride[0] );
    ASSERT_LE( 768, imgDesc.actualHeight[0] );
    ASSERT_EQ( QC_STATUS_OK, status );

    /* testing batched for RGB */
    imageBaseProp = ImageBasicProps_t( 7, 1024, 768, QC_IMAGE_FORMAT_RGB888 );
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_LE( 1024 * 768 * 3 * 7, imageBaseProp.size );
    ASSERT_EQ( 1, imgDesc.numPlanes );
    ASSERT_LE( 1024, imgDesc.stride[0] );
    ASSERT_LE( 768, imgDesc.actualHeight[0] );

    BufferProps_t bufProps;
    ImageProps_t imgProps;
    imgProps.size = 1024;
    imgProps.cache = static_cast<QCAllocationCache_e>( 1234 );
    imgProps.alignment = 8096;
    imgProps.allocatorType = static_cast<QCMemoryAllocator_e>( 5678 );
    bufProps = imgProps;
    ASSERT_EQ( 1024, bufProps.size );
    ASSERT_EQ( 1234, bufProps.cache );
    ASSERT_EQ( 8096, bufProps.alignment );
    ASSERT_EQ( 5678, bufProps.allocatorType );

    imageBaseProp.format = QC_IMAGE_FORMAT_NV12_UBWC;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );

    imageBaseProp.format = QC_IMAGE_FORMAT_TP10_UBWC;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );

    imageBaseProp.format = QC_IMAGE_FORMAT_MAX;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageBaseProp.format = (QCImageFormat_e) ( -1 );
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageBaseProp.format = QC_IMAGE_FORMAT_RGB888;
    imageBaseProp.height = 0;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageBaseProp.width = 0;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageBaseProp.batchSize = 0;
    status = Ifs.SetImageDescFromImageBasicProp( imageBaseProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageProp.format = QC_IMAGE_FORMAT_UYVY;
    imageProp.batchSize = 1;
    imageProp.width = 3840;
    imageProp.height = 2160;
    imageProp.stride[0] = 3840 * 2;
    imageProp.actualHeight[0] = 2160;
    imageProp.numPlanes = 1;
    imageProp.planeBufSize[0] = 0;

    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_EQ( 3840 * 2160 * 2, imageProp.size );
    ASSERT_EQ( 3840 * 2160 * 2, imgDesc.planeBufSize[0] );
    ASSERT_EQ( 1, imgDesc.numPlanes );
    ASSERT_EQ( 3840 * 2, imgDesc.stride[0] );
    ASSERT_EQ( 2160, imgDesc.actualHeight[0] );

    imageProp.format = QC_IMAGE_FORMAT_NV12;
    imageProp.batchSize = 1;
    imageProp.width = 1920;
    imageProp.height = 1024;
    imageProp.stride[0] = 1920;
    imageProp.actualHeight[0] = 1024;
    imageProp.stride[1] = 1920;
    imageProp.actualHeight[1] = 512;
    imageProp.numPlanes = 2;
    imageProp.planeBufSize[0] = 0;
    imageProp.planeBufSize[1] = 0;

    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_EQ( 1920 * 1024 * 3 / 2, imageProp.size );
    ASSERT_EQ( 2, imgDesc.numPlanes );
    ASSERT_EQ( 1920 * 1, imgDesc.stride[0] );
    ASSERT_EQ( 1024, imgDesc.actualHeight[0] );

    imageProp.format = QC_IMAGE_FORMAT_RGB888;
    imageProp.batchSize = 3;
    imageProp.width = 1024;
    imageProp.height = 768;
    imageProp.stride[0] = 1024 * 3;
    imageProp.actualHeight[0] = 768;
    imageProp.numPlanes = 1;
    imageProp.planeBufSize[0] = 0;

    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_EQ( 3 * 1024 * 768 * 3, imageProp.size );
    ASSERT_EQ( 1, imgDesc.numPlanes );
    ASSERT_EQ( 3, imgDesc.batchSize );
    ASSERT_EQ( 1024 * 3, imgDesc.stride[0] );
    ASSERT_EQ( 768, imgDesc.actualHeight[0] );

    imageProp.format = QC_IMAGE_FORMAT_COMPRESSED_H265;
    imageProp.batchSize = 1;
    imageProp.width = 3840;
    imageProp.height = 2160;
    imageProp.numPlanes = 1;
    imageProp.planeBufSize[0] = 1024 * 64;

    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, imgDesc.pBuf );
    ASSERT_EQ( 0, imgDesc.offset );
    std::generate( (uint8_t *) imgDesc.pBuf, (uint8_t *) imgDesc.pBuf + imgDesc.size, std::rand );
    ASSERT_EQ( imgDesc.GetDataSize(), imgDesc.size );
    ASSERT_EQ( 0, imgDesc.size );
    ASSERT_EQ( 1, imgDesc.numPlanes );

    imageProp.format = (QCImageFormat_e) ( -1 );
    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageProp.width = 0;
    imageProp.height = 2160;
    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageProp.batchSize = 0;
    imageProp.width = 2160;
    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );
    imageProp.batchSize = 1;

    imageProp.format = QC_IMAGE_FORMAT_MAX;
    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    imageProp.format = QC_IMAGE_FORMAT_COMPRESSED_MAX;
    status = Ifs.SetImageDescFromImageProp( imageProp, imgDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    // Tensor
    tensProp = TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 128, 128, 10 } );
    status = Ifs.SetTensorDescFromTensorProp( tensProp, tensorDesc );
    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( nullptr, tensorDesc.pBuf );
    ASSERT_EQ( 0, tensorDesc.offset );
    std::generate( (uint8_t *) tensorDesc.pBuf, (uint8_t *) tensorDesc.pBuf + tensorDesc.size,
                   std::rand );
    ASSERT_EQ( tensorDesc.GetDataSize(), tensorDesc.size );
    ASSERT_EQ( 1 * 128 * 128 * 10, tensProp.size );
    ASSERT_EQ( 4, tensorDesc.numDims );

    tensProp.dims[0] = 0;
    status = Ifs.SetTensorDescFromTensorProp( tensProp, tensorDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    tensProp.tensorType = QC_TENSOR_TYPE_MAX;
    status = Ifs.SetTensorDescFromTensorProp( tensProp, tensorDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    tensProp.tensorType = (QCTensorType_e) 0;
    status = Ifs.SetTensorDescFromTensorProp( tensProp, tensorDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    tensProp.numDims = 10;
    status = Ifs.SetTensorDescFromTensorProp( tensProp, tensorDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, status );


    QCBufferDescriptorBase_t buff;
    QCBufferDescriptorBase_t mapped;
    status = Ifs.MemoryMap( buff, mapped );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );

    status = Ifs.MemoryUnMap( buff );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
}

// ---------------------------------------------------------------------------
// Helpers for transition tests
// ---------------------------------------------------------------------------

static ImageDescriptor_t MakeSinglePlaneImage( void *buf, size_t bufSize, uint32_t width,
                                               uint32_t height, QCImageFormat_e fmt,
                                               uint32_t bytesPerPixel )
{
    ImageDescriptor_t img;
    img.pBuf = buf;
    img.size = bufSize;
    img.validSize = bufSize;
    img.type = QC_BUFFER_TYPE_IMAGE;
    img.format = fmt;
    img.width = width;
    img.height = height;
    img.batchSize = 1;
    img.numPlanes = 1;
    img.stride[0] = width * bytesPerPixel;
    img.actualHeight[0] = height;
    img.planeBufSize[0] = width * bytesPerPixel * height;
    return img;
}

static ImageDescriptor_t MakeDualPlaneImage( void *buf, size_t bufSize, uint32_t width,
                                             uint32_t height, QCImageFormat_e fmt,
                                             uint32_t bytesPerPixel )
{
    ImageDescriptor_t img;
    img.pBuf = buf;
    img.size = bufSize;
    img.validSize = bufSize;
    img.type = QC_BUFFER_TYPE_IMAGE;
    img.format = fmt;
    img.width = width;
    img.height = height;
    img.batchSize = 1;
    img.numPlanes = 2;
    img.stride[0] = width * bytesPerPixel;
    img.actualHeight[0] = height;
    img.planeBufSize[0] = width * bytesPerPixel * height;
    img.stride[1] = width * bytesPerPixel;
    img.actualHeight[1] = height / 2;
    img.planeBufSize[1] = width * bytesPerPixel * ( height / 2 );
    return img;
}

// ---------------------------------------------------------------------------
// Test GetSupportedTransitionTypes
// ---------------------------------------------------------------------------
TEST_F( Test_QCMemoryUtilsBase, GetSupportedTransitionTypes_NotEmpty )
{
    UtilsBase utils;
    const std::vector<QCMemoryTransition_e> &types = utils.GetSupportedTransitionTypes();
    ASSERT_FALSE( types.empty() );
}

TEST_F( Test_QCMemoryUtilsBase, GetSupportedTransitionTypes_ContainsAllExpectedTypes )
{
    UtilsBase utils;
    const std::vector<QCMemoryTransition_e> &types = utils.GetSupportedTransitionTypes();

    auto contains = [&]( QCMemoryTransition_e t ) {
        return std::find( types.begin(), types.end(), t ) != types.end();
    };

    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_RGB_TO_TENSOR ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_BGR_TO_TENSOR ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_UYVY_TO_TENSOR ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_NV12_TO_GRAY ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_P010_TO_GRAY ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_NV12_TO_CHROMA ) );
    ASSERT_TRUE( contains( QC_MEMORY_TRANSITION_P010_TO_CHROMA ) );
}

TEST_F( Test_QCMemoryUtilsBase, GetSupportedTransitionTypes_DoesNotContainLast )
{
    UtilsBase utils;
    const std::vector<QCMemoryTransition_e> &types = utils.GetSupportedTransitionTypes();
    auto it = std::find( types.begin(), types.end(), QC_MEMORY_TRANSITION_LAST );
    ASSERT_EQ( types.end(), it );
}

// ---------------------------------------------------------------------------
// Test CreateTransition — creation only
// ---------------------------------------------------------------------------
TEST_F( Test_QCMemoryUtilsBase, CreateTransition_UnsupportedType_Last )
{
    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    QCStatus_e status = utils.CreateTransition( QC_MEMORY_TRANSITION_LAST, fn );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, fn );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_UnsupportedType_Invalid )
{
    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    QCStatus_e status = utils.CreateTransition( static_cast<QCMemoryTransition_e>( -1 ), fn );
    ASSERT_EQ( QC_STATUS_UNSUPPORTED, status );
    ASSERT_EQ( nullptr, fn );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_AllSupportedTypes_ReturnOkAndNonNullFn )
{
    UtilsBase utils;
    for ( QCMemoryTransition_e type : utils.GetSupportedTransitionTypes() )
    {
        QCMemoryTransitionFn_t fn;
        QCStatus_e status = utils.CreateTransition( type, fn );
        ASSERT_EQ( QC_STATUS_OK, status ) << "type=" << static_cast<int>( type );
        ASSERT_NE( nullptr, fn ) << "type=" << static_cast<int>( type );
    }
}

// ---------------------------------------------------------------------------
// Test transition callables — single-plane formats
// ---------------------------------------------------------------------------
static void VerifySinglePlaneTensor( QCMemoryTransition_e transitionType, QCImageFormat_e imgFmt,
                                     uint32_t bpp )
{
    constexpr uint32_t W = 4, H = 4;
    uint8_t buf[W * H * 4] = {};

    ImageDescriptor_t img = MakeSinglePlaneImage( buf, W * H * bpp, W, H, imgFmt, bpp );

    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( transitionType, fn ) );

    TensorDescriptor_t tensor;
    QCStatus_e status = fn( img, tensor );

    ASSERT_EQ( QC_STATUS_OK, status );
    ASSERT_EQ( QC_BUFFER_TYPE_TENSOR, tensor.type );
    ASSERT_EQ( 4u, tensor.numDims );
    ASSERT_EQ( 1u, tensor.dims[0] );      // batchSize
    ASSERT_EQ( H, tensor.dims[1] );       // height
    ASSERT_EQ( W, tensor.dims[2] );       // width
    ASSERT_EQ( bpp, tensor.dims[3] );     // channels
    ASSERT_EQ( img.pBuf, tensor.pBuf );   // zero-copy
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_RGB_ToTensor_ValidInput )
{
    VerifySinglePlaneTensor( QC_MEMORY_TRANSITION_RGB_TO_TENSOR, QC_IMAGE_FORMAT_RGB888, 3u );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_BGR_ToTensor_ValidInput )
{
    VerifySinglePlaneTensor( QC_MEMORY_TRANSITION_BGR_TO_TENSOR, QC_IMAGE_FORMAT_BGR888, 3u );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_UYVY_ToTensor_ValidInput )
{
    VerifySinglePlaneTensor( QC_MEMORY_TRANSITION_UYVY_TO_TENSOR, QC_IMAGE_FORMAT_UYVY, 2u );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_RGB_ToTensor_WrongFormat )
{
    constexpr uint32_t W = 4, H = 4;
    uint8_t buf[W * H * 3] = {};

    ImageDescriptor_t img =
            MakeSinglePlaneImage( buf, sizeof( buf ), W, H, QC_IMAGE_FORMAT_BGR888, 3u );

    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( QC_MEMORY_TRANSITION_RGB_TO_TENSOR, fn ) );

    TensorDescriptor_t tensor;
    ASSERT_NE( QC_STATUS_OK, fn( img, tensor ) );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_RGB_ToTensor_WrongSrcType )
{
    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( QC_MEMORY_TRANSITION_RGB_TO_TENSOR, fn ) );

    QCBufferDescriptorBase_t src;
    TensorDescriptor_t tensor;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, fn( src, tensor ) );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_RGB_ToTensor_WrongDstType )
{
    constexpr uint32_t W = 4, H = 4;
    uint8_t buf[W * H * 3] = {};

    ImageDescriptor_t img =
            MakeSinglePlaneImage( buf, sizeof( buf ), W, H, QC_IMAGE_FORMAT_RGB888, 3u );

    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( QC_MEMORY_TRANSITION_RGB_TO_TENSOR, fn ) );

    QCBufferDescriptorBase_t dst;
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, fn( img, dst ) );
}

// ---------------------------------------------------------------------------
// Test transition callables — dual-plane formats (NV12 / P010)
// ---------------------------------------------------------------------------
static void VerifyDualPlaneTensors( QCMemoryTransition_e grayType, QCMemoryTransition_e chromaType,
                                    QCImageFormat_e imgFmt, uint32_t bpp )
{
    constexpr uint32_t W = 4, H = 4;
    const size_t lumaSz = W * H * bpp;
    const size_t chromaSz = W * ( H / 2 ) * bpp;
    std::vector<uint8_t> buf( lumaSz + chromaSz, 0 );

    ImageDescriptor_t img = MakeDualPlaneImage( buf.data(), buf.size(), W, H, imgFmt, bpp );

    UtilsBase utils;

    // luma
    {
        QCMemoryTransitionFn_t fn;
        ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( grayType, fn ) );
        TensorDescriptor_t tensor;
        ASSERT_EQ( QC_STATUS_OK, fn( img, tensor ) );
        ASSERT_EQ( QC_BUFFER_TYPE_TENSOR, tensor.type );
        ASSERT_EQ( 4u, tensor.numDims );
        ASSERT_EQ( 1u, tensor.dims[0] );
        ASSERT_EQ( H, tensor.dims[1] );
        ASSERT_EQ( W, tensor.dims[2] );
        ASSERT_EQ( 1u, tensor.dims[3] );
        ASSERT_EQ( img.pBuf, tensor.pBuf );
        ASSERT_EQ( 0u, tensor.offset );
    }

    // chroma
    {
        QCMemoryTransitionFn_t fn;
        ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( chromaType, fn ) );
        TensorDescriptor_t tensor;
        ASSERT_EQ( QC_STATUS_OK, fn( img, tensor ) );
        ASSERT_EQ( QC_BUFFER_TYPE_TENSOR, tensor.type );
        ASSERT_EQ( 4u, tensor.numDims );
        ASSERT_EQ( 1u, tensor.dims[0] );
        ASSERT_EQ( H / 2, tensor.dims[1] );
        ASSERT_EQ( W / 2, tensor.dims[2] );
        ASSERT_EQ( 2u, tensor.dims[3] );
        ASSERT_EQ( img.pBuf, tensor.pBuf );
        ASSERT_EQ( lumaSz, tensor.offset );
    }
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_NV12_ToGrayAndChroma_ValidInput )
{
    VerifyDualPlaneTensors( QC_MEMORY_TRANSITION_NV12_TO_GRAY, QC_MEMORY_TRANSITION_NV12_TO_CHROMA,
                            QC_IMAGE_FORMAT_NV12, 1u );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_P010_ToGrayAndChroma_ValidInput )
{
    VerifyDualPlaneTensors( QC_MEMORY_TRANSITION_P010_TO_GRAY, QC_MEMORY_TRANSITION_P010_TO_CHROMA,
                            QC_IMAGE_FORMAT_P010, 2u );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_NV12_ToGray_WrongFormat )
{
    constexpr uint32_t W = 4, H = 4;
    std::vector<uint8_t> buf( W * H * 2 + W * H, 0 );

    ImageDescriptor_t img =
            MakeDualPlaneImage( buf.data(), buf.size(), W, H, QC_IMAGE_FORMAT_P010, 2u );

    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( QC_MEMORY_TRANSITION_NV12_TO_GRAY, fn ) );

    TensorDescriptor_t tensor;
    ASSERT_NE( QC_STATUS_OK, fn( img, tensor ) );
}

TEST_F( Test_QCMemoryUtilsBase, CreateTransition_P010_ToChroma_WrongFormat )
{
    constexpr uint32_t W = 4, H = 4;
    std::vector<uint8_t> buf( W * H + W * H / 2, 0 );

    ImageDescriptor_t img =
            MakeDualPlaneImage( buf.data(), buf.size(), W, H, QC_IMAGE_FORMAT_NV12, 1u );

    UtilsBase utils;
    QCMemoryTransitionFn_t fn;
    ASSERT_EQ( QC_STATUS_OK, utils.CreateTransition( QC_MEMORY_TRANSITION_P010_TO_CHROMA, fn ) );

    TensorDescriptor_t tensor;
    ASSERT_NE( QC_STATUS_OK, fn( img, tensor ) );
}

// ---------------------------------------------------------------------------
// Test GetSupportedBufferTransitionTypesJson
// ---------------------------------------------------------------------------
TEST_F( Test_QCMemoryUtilsBase, GetSupportedBufferTransitionTypesJson_NotEmpty )
{
    UtilsBase utils;
    std::string json = utils.GetSupportedBufferTransitionTypesJson();
    ASSERT_FALSE( json.empty() );
}

TEST_F( Test_QCMemoryUtilsBase, GetSupportedBufferTransitionTypesJson_ValidFormat )
{
    UtilsBase utils;
    std::string json = utils.GetSupportedBufferTransitionTypesJson();
    ASSERT_EQ( '{', json.front() );
    ASSERT_EQ( '}', json.back() );
}

TEST_F( Test_QCMemoryUtilsBase, GetSupportedBufferTransitionTypesJson_ContainsAllExpectedKeys )
{
    UtilsBase utils;
    std::string json = utils.GetSupportedBufferTransitionTypesJson();

    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_RGB_TO_TENSOR" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_BGR_TO_TENSOR" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_UYVY_TO_TENSOR" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_NV12_TO_GRAY" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_P010_TO_GRAY" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_NV12_TO_CHROMA" ) );
    ASSERT_NE( std::string::npos, json.find( "QC_MEMORY_TRANSITION_P010_TO_CHROMA" ) );
}

TEST_F( Test_QCMemoryUtilsBase, GetSupportedBufferTransitionTypesJson_MatchesVector )
{
    UtilsBase utils;
    std::string json = utils.GetSupportedBufferTransitionTypesJson();
    const std::vector<QCMemoryTransition_e> &types = utils.GetSupportedTransitionTypes();

    size_t commaCount = static_cast<size_t>( std::count( json.begin(), json.end(), ',' ) );
    ASSERT_EQ( types.size() - 1, commaCount );
}

// ---------------------------------------------------------------------------
// Consistency: every supported type can be created successfully
// ---------------------------------------------------------------------------
TEST_F( Test_QCMemoryUtilsBase, GetSupportedTransitionTypes_Consistency )
{
    UtilsBase utils;
    for ( QCMemoryTransition_e type : utils.GetSupportedTransitionTypes() )
    {
        QCMemoryTransitionFn_t fn;
        QCStatus_e status = utils.CreateTransition( type, fn );
        ASSERT_EQ( QC_STATUS_OK, status )
                << "Failed to create transition for type: " << static_cast<int>( type );
        ASSERT_NE( nullptr, fn ) << "Null callable for type: " << static_cast<int>( type );
    }
}
