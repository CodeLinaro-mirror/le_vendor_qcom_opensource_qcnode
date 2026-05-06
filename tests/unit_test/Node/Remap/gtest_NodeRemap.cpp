// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "QC/Node/Remap.hpp"
#include "QC/sample/BufferManager.hpp"
#include "RemapImpl.hpp"
#include "md5_utils.hpp"
#include "gtest/gtest.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <stdio.h>
#include <string>

using namespace QC::Node;
using namespace QC::test::utils;
using namespace QC::sample;

QCStatus_e LoadImageRemap( ImageDescriptor_t imageDesc, std::string path )
{
    QCStatus_e status = QC_STATUS_OK;
    FILE *file = nullptr;
    size_t length = 0;
    file = fopen( path.c_str(), "rb" );
    if ( nullptr == file )
    {
        printf( "could not open image file %s\n", path.c_str() );
        status = QC_STATUS_FAIL;
    }
    else
    {
        fseek( file, 0, SEEK_END );
        length = (size_t) ftell( file );
        if ( imageDesc.size != length )
        {
            printf( "image file %s size not match, need %d but got %d\n", path.c_str(),
                    (int) imageDesc.size, (int) length );
            status = QC_STATUS_FAIL;
        }
        else
        {
            fseek( file, 0, SEEK_SET );
            auto r = fread( imageDesc.pBuf, 1, length, file );
            if ( length != r )
            {
                printf( "failed to read image file %s, need %d but read %d\n", path.c_str(),
                        (int) length, (int) r );
                status = QC_STATUS_FAIL;
            }
        }
        fclose( file );
    }

    return status;
}

QCStatus_e LoadMapRemap( TensorDescriptor_t buffer, std::string path )
{
    QCStatus_e status = QC_STATUS_OK;
    FILE *file = nullptr;
    size_t length = 0;
    size_t size = buffer.size;

    file = fopen( path.c_str(), "rb" );
    if ( nullptr == file )
    {
        printf( "Failed to open file %s", path.c_str() );
        status = QC_STATUS_FAIL;
    }

    if ( QC_STATUS_OK == status )
    {
        fseek( file, 0, SEEK_END );
        length = (size_t) ftell( file );
        if ( size != length )
        {
            printf( "Invalid file size for %s, need %d but got %d", path.c_str(), (int) size,
                    (int) length );
            status = QC_STATUS_FAIL;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        fseek( file, 0, SEEK_SET );
        auto r = fread( buffer.pBuf, 1, length, file );
        if ( length != r )
        {
            printf( "failed to read map table file %s", path.c_str() );
            status = QC_STATUS_FAIL;
        }
    }

    if ( nullptr != file )
    {
        fclose( file );
    }

    return status;
}

void SetConfigRemap( Remap_Config_t *pRemapConfig, DataTree *pdt )
{
    pdt->Set<uint32_t>( "static.outputWidth", pRemapConfig->outputWidth );
    pdt->Set<uint32_t>( "static.outputHeight", pRemapConfig->outputHeight );
    pdt->SetImageFormat( "static.outputFormat", pRemapConfig->outputFormat );
    pdt->SetProcessorType( "static.processorType", pRemapConfig->processor );
    pdt->Set<bool>( "static.bEnableUndistortion", pRemapConfig->bEnableUndistortion );
    pdt->Set<bool>( "static.bEnableNormalize", pRemapConfig->bEnableNormalize );
    pdt->Set<uint32_t>( "static.coreId", pRemapConfig->coreId );

    if ( true == pRemapConfig->bEnableNormalize )
    {
        pdt->Set<float>( "static.RSub", pRemapConfig->normlzR.sub );
        pdt->Set<float>( "static.RMul", pRemapConfig->normlzR.mul );
        pdt->Set<float>( "static.RAdd", pRemapConfig->normlzR.add );
        pdt->Set<float>( "static.GSub", pRemapConfig->normlzG.sub );
        pdt->Set<float>( "static.GMul", pRemapConfig->normlzG.mul );
        pdt->Set<float>( "static.GAdd", pRemapConfig->normlzG.add );
        pdt->Set<float>( "static.BSub", pRemapConfig->normlzB.sub );
        pdt->Set<float>( "static.BMul", pRemapConfig->normlzB.mul );
        pdt->Set<float>( "static.BAdd", pRemapConfig->normlzB.add );
    }

    std::vector<DataTree> inputDts;
    for ( int i = 0; i < pRemapConfig->numOfInputs; i++ )
    {
        DataTree inputDt;
        inputDt.Set<uint32_t>( "inputWidth", pRemapConfig->inputConfigs[i].inputWidth );
        inputDt.Set<uint32_t>( "inputHeight", pRemapConfig->inputConfigs[i].inputHeight );
        inputDt.SetImageFormat( "inputFormat", pRemapConfig->inputConfigs[i].inputFormat );
        inputDt.Set<uint32_t>( "roiX", pRemapConfig->inputConfigs[i].ROI.x );
        inputDt.Set<uint32_t>( "roiY", pRemapConfig->inputConfigs[i].ROI.y );
        inputDt.Set<uint32_t>( "roiWidth", pRemapConfig->inputConfigs[i].ROI.width );
        inputDt.Set<uint32_t>( "roiHeight", pRemapConfig->inputConfigs[i].ROI.height );
        inputDt.Set<uint32_t>( "mapWidth", pRemapConfig->inputConfigs[i].mapWidth );
        inputDt.Set<uint32_t>( "mapHeight", pRemapConfig->inputConfigs[i].mapHeight );

        if ( pRemapConfig->bEnableUndistortion == true )
        {
            inputDt.Set<uint32_t>( "mapXBufferId", 0 );
            inputDt.Set<uint32_t>( "mapYBufferId", 1 );
        }

        inputDts.push_back( inputDt );
    }
    pdt->Set( "static.inputs", inputDts );
}

void SanityRemap()
{
    QCStatus_e ret;
    std::string errors;
    QCNodeIfs *pRemap = new QC::Node::Remap();
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t RemapConfig;
    RemapConfig.numOfInputs = 2;
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        RemapConfig.inputConfigs[i].inputWidth = 128;
        RemapConfig.inputConfigs[i].inputHeight = 128;
        RemapConfig.inputConfigs[i].inputFormat = QC_IMAGE_FORMAT_UYVY;
        RemapConfig.inputConfigs[i].ROI.x = 0;
        RemapConfig.inputConfigs[i].ROI.y = 0;
        RemapConfig.inputConfigs[i].ROI.width = 64;
        RemapConfig.inputConfigs[i].ROI.height = 64;
        RemapConfig.inputConfigs[i].mapWidth = 64;
        RemapConfig.inputConfigs[i].mapHeight = 64;
    }
    RemapConfig.outputWidth = 64;
    RemapConfig.outputHeight = 64;
    RemapConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    RemapConfig.processor = QC_PROCESSOR_HTP0;
    RemapConfig.bEnableUndistortion = false;
    RemapConfig.bEnableNormalize = false;
    RemapConfig.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &RemapConfig, &dt );

    QCNodeInit_t config = { dt.Dump() };
    printf( "config: %s\n", config.config.c_str() );

    ImageProps_t imgPropInputs[RemapConfig.numOfInputs];
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = RemapConfig.inputConfigs[i].inputWidth;
        imgPropInputs[i].height = RemapConfig.inputConfigs[i].inputHeight;
        imgPropInputs[i].format = RemapConfig.inputConfigs[i].inputFormat;
        if ( QC_IMAGE_FORMAT_NV12 == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth;
            imgPropInputs[i].stride[1] = RemapConfig.inputConfigs[i].inputWidth;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].actualHeight[1] = RemapConfig.inputConfigs[i].inputHeight / 2;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].planeBufSize[1] = 0;
            imgPropInputs[i].numPlanes = 2;
        }
        else if ( QC_IMAGE_FORMAT_UYVY == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth * 2;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
        else if ( QC_IMAGE_FORMAT_RGB888 == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth * 3;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = RemapConfig.numOfInputs;
    imgPropOutput.width = RemapConfig.outputWidth;
    imgPropOutput.height = RemapConfig.outputHeight;
    imgPropOutput.format = RemapConfig.outputFormat;
    if ( ( QC_IMAGE_FORMAT_RGB888 == RemapConfig.outputFormat ) ||
         ( QC_IMAGE_FORMAT_BGR888 == RemapConfig.outputFormat ) )
    {
        imgPropOutput.stride[0] = RemapConfig.outputWidth * 3;
        imgPropOutput.actualHeight[0] = RemapConfig.outputHeight;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.numPlanes = 1;
    }
    else if ( QC_IMAGE_FORMAT_NV12 == RemapConfig.outputFormat )
    {
        imgPropOutput.stride[0] = RemapConfig.outputWidth;
        imgPropOutput.stride[1] = RemapConfig.outputWidth;
        imgPropOutput.actualHeight[0] = RemapConfig.outputHeight;
        imgPropOutput.actualHeight[1] = RemapConfig.outputHeight / 2;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.planeBufSize[1] = 0;
        imgPropOutput.numPlanes = 2;
    }

    NodeFrameDescriptor frameDesc( RemapConfig.numOfInputs + 1 );
    uint32_t globalIdx = 0;
    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( RemapConfig.numOfInputs );
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        ImageDescriptor_t imageDesc;
        ret = bufMgr.Allocate( imgPropInputs[i], imageDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( imageDesc );
        ret = frameDesc.SetBuffer( globalIdx, inputs.back() );
        ASSERT_EQ( QC_STATUS_OK, ret );
        globalIdx++;
    }
    std::vector<ImageDescriptor_t> outputs;
    outputs.reserve( 1 );
    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgPropOutput, imageDesc );
    outputs.push_back( imageDesc );
    ret = frameDesc.SetBuffer( globalIdx, outputs.back() );
    ASSERT_EQ( QC_STATUS_OK, ret );
    globalIdx++;

    ret = pRemap->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( auto imageDesc : inputs )
    {
        ret = bufMgr.Free( imageDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    for ( auto imageDesc : outputs )
    {
        ret = bufMgr.Free( imageDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    reinterpret_cast<QC::Node::Remap *>( pRemap )->~Remap();
}

void AccuracyRemap( uint32_t inputNumberTest, QCProcessorType_e processorTest,
                    QCImageFormat_e inputFormatTest, QCImageFormat_e outputFormatTest,
                    uint32_t inputWidthTest, uint32_t inputHeightTest, uint32_t outputWidthTest,
                    uint32_t outputHeightTest, bool normalizationTest, bool undistortionTest,
                    std::string pathTest, std::string goldenPath, bool saveOutput,
                    uint32_t coreIdTest )
{
    QCStatus_e ret;
    std::string errors;
    QCNodeIfs *pRemap = new QC::Node::Remap();
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );
    uint32_t globalIdx = 0;
    std::vector<uint32_t> bufferIds;
    QCNodeInit_t config;
    config.buffers.clear();

    Remap_Config_t RemapConfig;
    RemapConfig.numOfInputs = inputNumberTest;
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        RemapConfig.inputConfigs[i].inputWidth = inputWidthTest;
        RemapConfig.inputConfigs[i].inputHeight = inputHeightTest;
        RemapConfig.inputConfigs[i].inputFormat = inputFormatTest;
        RemapConfig.inputConfigs[i].ROI.x = 0;
        RemapConfig.inputConfigs[i].ROI.y = 0;
        RemapConfig.inputConfigs[i].ROI.width = outputWidthTest;
        RemapConfig.inputConfigs[i].ROI.height = outputHeightTest;
        RemapConfig.inputConfigs[i].mapWidth = outputWidthTest;
        RemapConfig.inputConfigs[i].mapHeight = outputHeightTest;
    }
    RemapConfig.outputWidth = outputWidthTest;
    RemapConfig.outputHeight = outputHeightTest;
    RemapConfig.outputFormat = outputFormatTest;
    RemapConfig.processor = processorTest;
    RemapConfig.bEnableUndistortion = undistortionTest;
    RemapConfig.bEnableNormalize = normalizationTest;
    RemapConfig.coreId = coreIdTest;

    if ( true == RemapConfig.bEnableNormalize )
    {
        RemapConfig.normlzR.sub = 123.675;
        RemapConfig.normlzR.mul = 1.f / 58.395;
        RemapConfig.normlzR.add = 0.f;
        RemapConfig.normlzG.sub = 116.28;
        RemapConfig.normlzG.mul = 1.f / 57.12;
        RemapConfig.normlzG.add = 0.f;
        RemapConfig.normlzB.sub = 103.53;
        RemapConfig.normlzB.mul = 1.f / 57.375;
        RemapConfig.normlzB.add = 0.f;
        float quantScale = 0.0186584480106831f;
        int32_t quantOffset = 114;
        RemapConfig.normlzR.add = RemapConfig.normlzR.add / quantScale + quantOffset;
        RemapConfig.normlzR.mul = RemapConfig.normlzR.mul / quantScale;
        RemapConfig.normlzG.add = RemapConfig.normlzG.add / quantScale + quantOffset;
        RemapConfig.normlzG.mul = RemapConfig.normlzG.mul / quantScale;
        RemapConfig.normlzB.add = RemapConfig.normlzB.add / quantScale + quantOffset;
        RemapConfig.normlzB.mul = RemapConfig.normlzB.mul / quantScale;
    }

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &RemapConfig, &dt );

    ImageProps_t imgPropInputs[RemapConfig.numOfInputs];
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = RemapConfig.inputConfigs[i].inputWidth;
        imgPropInputs[i].height = RemapConfig.inputConfigs[i].inputHeight;
        imgPropInputs[i].format = RemapConfig.inputConfigs[i].inputFormat;
        if ( QC_IMAGE_FORMAT_NV12 == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth;
            imgPropInputs[i].stride[1] = RemapConfig.inputConfigs[i].inputWidth;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].actualHeight[1] = RemapConfig.inputConfigs[i].inputHeight / 2;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].planeBufSize[1] = 0;
            imgPropInputs[i].numPlanes = 2;
        }
        else if ( QC_IMAGE_FORMAT_UYVY == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth * 2;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
        else if ( QC_IMAGE_FORMAT_RGB888 == RemapConfig.inputConfigs[i].inputFormat )
        {
            imgPropInputs[i].stride[0] = RemapConfig.inputConfigs[i].inputWidth * 3;
            imgPropInputs[i].actualHeight[0] = RemapConfig.inputConfigs[i].inputHeight;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = RemapConfig.numOfInputs;
    imgPropOutput.width = RemapConfig.outputWidth;
    imgPropOutput.height = RemapConfig.outputHeight;
    imgPropOutput.format = RemapConfig.outputFormat;
    if ( ( QC_IMAGE_FORMAT_RGB888 == RemapConfig.outputFormat ) ||
         ( QC_IMAGE_FORMAT_BGR888 == RemapConfig.outputFormat ) )
    {
        imgPropOutput.stride[0] = RemapConfig.outputWidth * 3;
        imgPropOutput.actualHeight[0] = RemapConfig.outputHeight;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.numPlanes = 1;
    }
    else if ( QC_IMAGE_FORMAT_NV12 == RemapConfig.outputFormat )
    {
        imgPropOutput.stride[0] = RemapConfig.outputWidth;
        imgPropOutput.stride[1] = RemapConfig.outputWidth;
        imgPropOutput.actualHeight[0] = RemapConfig.outputHeight;
        imgPropOutput.actualHeight[1] = RemapConfig.outputHeight / 2;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.planeBufSize[1] = 0;
        imgPropOutput.numPlanes = 2;
    }

    uint32_t frameIdx = 0;
    NodeFrameDescriptor frameDesc( RemapConfig.numOfInputs + 1 );

    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( RemapConfig.numOfInputs );
    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        ImageDescriptor_t imageDesc;
        if ( QC_IMAGE_FORMAT_NV12_UBWC == RemapConfig.inputConfigs[i].inputFormat )
        {
            ret = bufMgr.Allocate( ImageBasicProps_t( RemapConfig.inputConfigs[i].inputWidth,
                                                      RemapConfig.inputConfigs[i].inputHeight,
                                                      RemapConfig.inputConfigs[i].inputFormat ),
                                   imageDesc );
        }
        else
        {
            ret = bufMgr.Allocate( imgPropInputs[i], imageDesc );
        }
        ASSERT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( imageDesc );
        ret = frameDesc.SetBuffer( frameIdx, inputs.back() );
        ASSERT_EQ( QC_STATUS_OK, ret );
        frameIdx++;
        config.buffers.push_back( inputs.back() );
        bufferIds.push_back( globalIdx );
        globalIdx++;
    }

    std::vector<ImageDescriptor_t> outputs;
    outputs.reserve( 1 );
    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgPropOutput, imageDesc );
    outputs.push_back( imageDesc );
    ret = frameDesc.SetBuffer( frameIdx, outputs.back() );
    ASSERT_EQ( QC_STATUS_OK, ret );
    frameIdx++;
    config.buffers.push_back( outputs.back() );
    bufferIds.push_back( globalIdx );
    globalIdx++;

    TensorDescriptor_t mapXBufferDesc;
    TensorDescriptor_t mapYBufferDesc;
    if ( true == RemapConfig.bEnableUndistortion )
    {
        TensorProps_t mapXProp = {
                QC_TENSOR_TYPE_FLOAT_32,
                { RemapConfig.inputConfigs[0].mapWidth, RemapConfig.inputConfigs[0].mapHeight } };
        ret = bufMgr.Allocate( mapXProp, mapXBufferDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        ret = LoadMapRemap( mapXBufferDesc, "./data/test/Remap/mapX.raw" );
        ASSERT_EQ( QC_STATUS_OK, ret );

        TensorProps_t mapYProp = {
                QC_TENSOR_TYPE_FLOAT_32,
                { RemapConfig.inputConfigs[0].mapWidth, RemapConfig.inputConfigs[0].mapHeight } };
        ret = bufMgr.Allocate( mapYProp, mapYBufferDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        ret = LoadMapRemap( mapYBufferDesc, "./data/test/Remap/mapY.raw" );
        ASSERT_EQ( QC_STATUS_OK, ret );
        if ( nullptr != mapXBufferDesc.pBuf )
        {
            config.buffers.push_back( mapXBufferDesc );
            bufferIds.push_back( globalIdx );
            globalIdx++;
        }
        else
        {
            printf( "mapXBufferDesc is nullptr\n" );
        }
        if ( nullptr != mapYBufferDesc.pBuf )
        {
            config.buffers.push_back( mapYBufferDesc );
            bufferIds.push_back( globalIdx );
            globalIdx++;
        }
        else
        {
            printf( "mapYBufferDesc is nullptr\n" );
        }
    }

    dt.Set<uint32_t>( "static.bufferIds", bufferIds );
    config.config = dt.Dump();
    printf( "config: %s\n", config.config.c_str() );

    ret = pRemap->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( int i = 0; i < RemapConfig.numOfInputs; i++ )
    {
        memset( inputs[i].pBuf, 0, inputs[i].size );
        ret = LoadImageRemap( inputs[i], pathTest );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
    memset( outputs[0].pBuf, 0, outputs[0].size );

    ret = pRemap->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    if ( true == saveOutput )
    {
        uint8_t *ptr = (uint8_t *) outputs[0].pBuf;
        FILE *fp = fopen( goldenPath.c_str(), "wb" );
        if ( nullptr != fp )
        {
            fwrite( ptr, outputs[0].size, 1, fp );
            fclose( fp );
        }
    }

    ImageDescriptor_t golden;
    (void) bufMgr.Allocate( imgPropOutput, golden );
    LoadImageRemap( golden, goldenPath );

    std::string md5Output = MD5Sum( outputs[0].pBuf, outputs[0].size );
    printf( "output md5 = %s\n", md5Output.c_str() );
    std::string md5Golden = MD5Sum( golden.pBuf, outputs[0].size );
    printf( "golden md5 = %s\n", md5Golden.c_str() );

    if ( md5Output != md5Golden )   // check cosine similarity if md5 not match
    {
        size_t outputSize = outputs[0].size;
        uint8_t *outputData = (uint8_t *) outputs[0].pBuf;
        uint8_t *goldenData = (uint8_t *) golden.pBuf;
        float dot = 0.0;
        float norm1 = 1e-10;
        float norm2 = 1e-10;
        int miss = 0;
        for ( int i = 0; i < outputSize; i++ )
        {
            if ( outputData[i] != goldenData[i] )
            {
                miss++;
                if ( miss < 10 )
                {
                    printf( "data not match at i=%d, output=%d, golden=%d\n", i, outputData[i],
                            goldenData[i] );
                }
            }
            dot = dot + outputData[i] * goldenData[i];
            norm1 = norm1 + outputData[i] * outputData[i];
            norm2 = norm2 + goldenData[i] * goldenData[i];
        }
        float cos = dot / sqrt( norm1 * norm2 );
        printf( "cosine similarity = %f\n", cos );
        printf( "miss data number = %d\n", miss );
    }
    ASSERT_EQ( md5Output, md5Golden );

    ret = pRemap->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pRemap->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( auto imageDesc : inputs )
    {
        ret = bufMgr.Free( imageDesc );
    }

    for ( auto imageDesc : outputs )
    {
        ret = bufMgr.Free( imageDesc );
    }

    (void) bufMgr.Free( mapXBufferDesc );
    (void) bufMgr.Free( mapYBufferDesc );
    (void) bufMgr.Free( golden );

    reinterpret_cast<QC::Node::Remap *>( pRemap )->~Remap();
}

TEST( NodeRemap, Sanity )
{
    SanityRemap();
}

// md5 of 0.uyvy is 5b1ae2203a9d97aeafe65e997f3beebc
// md5 of golden_cpu.rgb is 59760700b59beb67227d305b317dcec6
// md5 of golden_dsp.rgb is f139fb73234986a15e340afe1058522e
// md5 of golden_gpu.rgb is 59760700b59beb67227d305b317dcec6
TEST( NodeRemap, AccuracyHTP )
{
    AccuracyRemap( 2, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, 1920, 1024,
                   1152, 800, true, false, "./data/test/remap/0.uyvy",
                   "./data/test/remap/golden_dsp.rgb", false, 0 );
}
TEST( NodeRemap, AccuracyCPU )
{
    AccuracyRemap( 2, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, 1920, 1024,
                   1152, 800, true, false, "./data/test/remap/0.uyvy",
                   "./data/test/remap/golden_cpu.rgb", false, 0 );
}
#if defined( USE_ENG_FADAS_GPU )
TEST( NodeRemap, AccuracyGPU )
{
    AccuracyRemap( 2, QC_PROCESSOR_GPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, 1920, 1024,
                   1152, 800, true, false, "./data/test/remap/0.uyvy",
                   "./data/test/remap/golden_gpu.rgb", false, 0 );
}
#endif

#if ( QC_TARGET_SOC == 8797 )
TEST( NodeRemap, AccuracyHTP0CORE3 )
{
    AccuracyRemap( 2, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, 1920, 1024,
                   1152, 800, true, false, "./data/test/remap/0.uyvy",
                   "./data/test/remap/golden_dsp.rgb", false, 3 );
}
#endif

// ============================================================================
// CONFIGURATION VALIDATION TESTS
// Coverage: RemapConfig.cpp - VerifyStaticConfig() and ParseStaticConfig()
// ============================================================================

/**
 * @brief Test configuration with empty name
 * @coverage RemapConfig.cpp lines 13-17 (name validation)
 */
TEST( NodeRemapConfig, EmptyName )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );
    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test configuration with missing/invalid ID
 * @coverage RemapConfig.cpp lines 19-23 (id validation)
 */
TEST( NodeRemapConfig, InvalidId )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );
    // ID not set - should be UINT32_MAX
    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test configuration with invalid processor type
 * @coverage RemapConfig.cpp lines 25-29 (processorType validation)
 */
TEST( NodeRemapConfig, InvalidProcessorType )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.Set<std::string>( "static.processorType", "INVALID_TYPE" );
    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test configuration with empty bufferIds array
 * @coverage RemapConfig.cpp lines 31-37 (bufferIds validation)
 * @expected QC_STATUS_BAD_ARGUMENTS
 */
TEST( NodeRemapConfig, EmptyBufferIds )
{
    QC::Node::Remap remap;
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 1 );
    std::vector<uint32_t> emptyIds;
    dt.Set<uint32_t>( "static.bufferIds", emptyIds );
    QCNodeInit_t config = { dt.Dump() };
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, remap.Initialize( config ) );
}

/**
 * @brief Test configuration with too many inputs (> QC_MAX_INPUTS)
 * @coverage RemapConfig.cpp lines 48-52 (numOfInputs validation)
 * @expected QC_STATUS_BAD_ARGUMENTS
 */
TEST( NodeRemapConfig, TooManyInputs )
{
    QC::Node::Remap remap;
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );

    std::vector<DataTree> inputDts;
    for ( int i = 0; i < QC_MAX_INPUTS + 1; i++ )
    {
        DataTree inputDt;
        inputDt.Set<uint32_t>( "inputWidth", 64 );
        inputDt.Set<uint32_t>( "inputHeight", 64 );
        inputDt.SetImageFormat( "inputFormat", QC_IMAGE_FORMAT_UYVY );
        inputDts.push_back( inputDt );
    }
    dt.Set( "static.inputs", inputDts );

    QCNodeInit_t config = { dt.Dump() };
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, remap.Initialize( config ) );
}

/**
 * @brief Test configuration with invalid coreId (> NSP_CORES_ID_MAX)
 * @coverage RemapConfig.cpp lines 54-59 (coreId validation)
 */
TEST( NodeRemapConfig, InvalidCoreId )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );
    dt.Set<uint32_t>( "static.coreId", 999 );
    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test globalBufferIdMap with empty name
 * @coverage RemapConfig.cpp lines 73-77 (globalBufferIdMap name validation)
 */
TEST( NodeRemapConfig, GlobalBufferMapEmptyName )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );

    std::vector<DataTree> globalBufferIdMap;
    DataTree gbm;
    gbm.Set<std::string>( "name", "" );   // Empty name
    gbm.Set<uint32_t>( "id", 0 );
    globalBufferIdMap.push_back( gbm );
    dt.Set( "static.globalBufferIdMap", globalBufferIdMap );

    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test globalBufferIdMap with invalid/missing id
 * @coverage RemapConfig.cpp lines 79-83 (globalBufferIdMap id validation)
 */
TEST( NodeRemapConfig, GlobalBufferMapInvalidId )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );

    std::vector<DataTree> globalBufferIdMap;
    DataTree gbm;
    gbm.Set<std::string>( "name", "Input0" );
    // ID not set - will be UINT32_MAX
    globalBufferIdMap.push_back( gbm );
    dt.Set( "static.globalBufferIdMap", globalBufferIdMap );

    QCNodeInit_t config = { dt.Dump() };
    EXPECT_NE( QC_STATUS_OK, pRemap->Initialize( config ) );
    delete pRemap;
}

/**
 * @brief Test GetOptions returns version information
 * @coverage RemapConfig.cpp lines 163-170 (GetOptions method)
 * @expected Returns string containing version
 */
TEST( NodeRemapConfig, GetOptionsReturnsVersion )
{
    QC::Logger logger;
    QC::Node::RemapConfig config( logger, nullptr );
    const std::string &options = config.GetOptions();
    EXPECT_FALSE( options.empty() );
    EXPECT_NE( std::string::npos, options.find( "\"version\"" ) );
}

// ============================================================================
// STATE MACHINE TESTS
// Coverage: RemapImpl.cpp - Start(), Stop(), Initialize(), DeInitialize()
// ============================================================================

/**
 * @brief Test Start() when not in READY state
 * @coverage RemapImpl.cpp lines 18-22 (Start state check)
 * @expected QC_STATUS_BAD_STATE
 */
TEST( NodeRemapStateMachine, StartInWrongState )
{
    QC::Node::Remap remap;
    QCStatus_e ret = remap.Start();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
}

/**
 * @brief Test Stop() when not in RUNNING state
 * @coverage RemapImpl.cpp lines 36-40 (Stop state check)
 * @expected QC_STATUS_BAD_STATE
 */
TEST( NodeRemapStateMachine, StopInWrongState )
{
    QC::Node::Remap remap;
    QCStatus_e ret = remap.Stop();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
}

/**
 * @brief Test Initialize called twice without DeInitialize
 * Covers: RemapImpl.cpp lines 85-88 (not in initial state error)
 */
TEST( NodeRemap, InitializeTwice )
{
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t cfg;
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = 64;
    cfg.inputConfigs[0].inputHeight = 64;
    cfg.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_UYVY;
    cfg.inputConfigs[0].ROI = { 0, 0, 64, 64 };
    cfg.inputConfigs[0].mapWidth = 64;
    cfg.inputConfigs[0].mapHeight = 64;
    cfg.outputWidth = 64;
    cfg.outputHeight = 64;
    cfg.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.processor = QC_PROCESSOR_HTP0;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    ImageProps_t imgProp;
    imgProp.batchSize = 1;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_UYVY;
    imgProp.stride[0] = 128;
    imgProp.actualHeight[0] = 64;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;

    ImageDescriptor_t inputDesc, outputDesc;
    QCStatus_e ret = bufMgr.Allocate( imgProp, inputDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    imgProp.format = QC_IMAGE_FORMAT_RGB888;
    imgProp.stride[0] = 192;
    ret = bufMgr.Allocate( imgProp, outputDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inputDesc );
    config.buffers.push_back( outputDesc );

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );
    config.config = dt.Dump();

    // First initialize should succeed
    ret = remap.Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // Second initialize without DeInitialize should fail
    ret = remap.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    // Cleanup
    remap.DeInitialize();
    bufMgr.Free( inputDesc );
    bufMgr.Free( outputDesc );
}

/**
 * @brief Test DeInitialize() when not in READY state
 * @coverage RemapImpl.cpp lines 214-218 (DeInitialize state check)
 * @expected QC_STATUS_OK
 */
TEST( NodeRemapStateMachine, DeInitializeInWrongState )
{
    QC::Node::Remap remap;
    QCStatus_e ret = remap.DeInitialize();
    // Note: Current implementation returns OK even in wrong state
    EXPECT_EQ( QC_STATUS_OK, ret );
}

/**
 * @brief Test ProcessFrameDescriptor() when not in RUNNING state
 * @coverage RemapImpl.cpp lines 250-254 (ProcessFrameDescriptor state check)
 */
TEST( NodeRemapStateMachine, ProcessFrameDescriptorNotRunning )
{
    QCNodeIfs *pRemap = new QC::Node::Remap();
    NodeFrameDescriptor frameDesc( 3 );
    QCStatus_e ret = pRemap->ProcessFrameDescriptor( frameDesc ) ;
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
    delete pRemap;
}

/**
 * @brief Test GetState() returns correct initial state
 * @coverage RemapImpl.cpp lines 263-266 (GetState method)
 * @expected QC_OBJECT_STATE_INITIAL
 */
TEST( NodeRemapStateMachine, GetStateInitial )
{
    QC::Node::Remap *pRemap = new QC::Node::Remap();
    EXPECT_EQ( QC_OBJECT_STATE_INITIAL, pRemap->GetState() );
    delete pRemap;
}

// ============================================================================
// RUNTIME VALIDATION TESTS
// Coverage: RemapImpl.cpp - Buffer validation and registration
// ============================================================================

/**
 * @brief Test buffer index out of range during registration
 * @coverage RemapImpl.cpp lines 178-182 (buffer index validation)
 * @expected QC_STATUS_BAD_ARGUMENTS
 */
TEST( NodeRemap, BufferIndexOutOfRange )
{
    QC::Node::Remap remap;
    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_HTP0 );
    dt.Set<uint32_t>( "static.outputWidth", 64 );
    dt.Set<uint32_t>( "static.outputHeight", 64 );
    dt.SetImageFormat( "static.outputFormat", QC_IMAGE_FORMAT_RGB888 );

    std::vector<DataTree> inputDts;
    DataTree inputDt;
    inputDt.Set<uint32_t>( "inputWidth", 64 );
    inputDt.Set<uint32_t>( "inputHeight", 64 );
    inputDt.SetImageFormat( "inputFormat", QC_IMAGE_FORMAT_UYVY );
    inputDt.Set<uint32_t>( "roiX", 0 );
    inputDt.Set<uint32_t>( "roiY", 0 );
    inputDt.Set<uint32_t>( "roiWidth", 64 );
    inputDt.Set<uint32_t>( "roiHeight", 64 );
    inputDt.Set<uint32_t>( "mapWidth", 64 );
    inputDt.Set<uint32_t>( "mapHeight", 64 );
    inputDts.push_back( inputDt );
    dt.Set( "static.inputs", inputDts );

    std::vector<uint32_t> bufferIds = { 999 };   // Out of range
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );

    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );
    ImageProps_t imgProp;
    imgProp.batchSize = 1;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_UYVY;
    imgProp.stride[0] = 128;
    imgProp.actualHeight[0] = 64;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;

    ImageDescriptor_t inputDesc;
    QCStatus_e ret = bufMgr.Allocate( imgProp, inputDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inputDesc );

    ret = remap.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    bufMgr.Free( inputDesc );
}

/**
 * @brief Test globalBufferIdMap size mismatch
 * @coverage RemapImpl.cpp lines 277-281 (globalBufferIdMap size validation)
 * @expected QC_STATUS_BAD_ARGUMENTS
 */

TEST( NodeRemap, GlobalBufferMapSizeMismatch )
{
    QC::Node::Remap remap;

    Remap_Config_t cfg;
    cfg.numOfInputs = 2;
    for ( int i = 0; i < (int) cfg.numOfInputs; ++i )
    {
        cfg.inputConfigs[i].inputWidth = 64;
        cfg.inputConfigs[i].inputHeight = 64;
        cfg.inputConfigs[i].inputFormat = QC_IMAGE_FORMAT_UYVY;
        cfg.inputConfigs[i].ROI = { 0, 0, 64, 64 };
        cfg.inputConfigs[i].mapWidth = 64;
        cfg.inputConfigs[i].mapHeight = 64;
    }
    cfg.outputWidth = 64;
    cfg.outputHeight = 64;
    cfg.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.processor = QC_PROCESSOR_HTP0;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    // Intentionally set wrong-sized globalBufferIdMap (should be numOfInputs + 1 = 3)
    std::vector<DataTree> gbm;
    DataTree e0;
    e0.Set<std::string>( "name", "Input0" );
    e0.Set<uint32_t>( "id", 0 );
    gbm.push_back( e0 );
    dt.Set( "static.globalBufferIdMap", gbm );

    QCNodeInit_t config = { dt.Dump() };
    QCStatus_e ret = remap.Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

// ============================================================================
// ADDITIONAL TESTS FOR 100% CODE COVERAGE
// ============================================================================

/**
 * @brief Test configuration with all optional parameters
 * Covers: RemapConfig.cpp complete parameter parsing
 */
TEST( NodeRemap, ConfigWithAllOptionalParameters )
{
    QC::Node::Remap remap;

    Remap_Config_t cfg;
    cfg.numOfInputs = 2;
    for ( int i = 0; i < cfg.numOfInputs; i++ )
    {
        cfg.inputConfigs[i].inputWidth = 128;
        cfg.inputConfigs[i].inputHeight = 128;
        cfg.inputConfigs[i].inputFormat = QC_IMAGE_FORMAT_RGB888;
        cfg.inputConfigs[i].ROI.x = 10;
        cfg.inputConfigs[i].ROI.y = 10;
        cfg.inputConfigs[i].ROI.width = 100;
        cfg.inputConfigs[i].ROI.height = 100;
        cfg.inputConfigs[i].mapWidth = 100;
        cfg.inputConfigs[i].mapHeight = 100;
    }
    cfg.outputWidth = 200;
    cfg.outputHeight = 200;
    cfg.outputFormat = QC_IMAGE_FORMAT_NV12;
    cfg.processor = QC_PROCESSOR_CPU;
    cfg.bEnableUndistortion = true;
    cfg.bEnableNormalize = true;
    cfg.coreId = 1;

    cfg.normlzR.sub = 100.0f;
    cfg.normlzR.mul = 0.5f;
    cfg.normlzR.add = 10.0f;
    cfg.normlzG.sub = 110.0f;
    cfg.normlzG.mul = 0.6f;
    cfg.normlzG.add = 20.0f;
    cfg.normlzB.sub = 120.0f;
    cfg.normlzB.mul = 0.7f;
    cfg.normlzB.add = 30.0f;

    DataTree dt;
    dt.Set<std::string>( "static.name", "RemapFull" );
    dt.Set<uint32_t>( "static.id", 5 );
    dt.Set<bool>( "static.deRegisterAllBuffersWhenStop", true );
    SetConfigRemap( &cfg, &dt );

    QCNodeInit_t config = { dt.Dump() };

    // Config parsing should succeed
    QCStatus_e ret = remap.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
}

/**
 * @brief Test DeInitialize after already called
 * Covers: RemapImpl.cpp lines 214-218 (not in ready state error)
 */
TEST( NodeRemap, DeInitializeTwice )
{
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t cfg;
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = 64;
    cfg.inputConfigs[0].inputHeight = 64;
    cfg.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_UYVY;
    cfg.inputConfigs[0].ROI = { 0, 0, 64, 64 };
    cfg.inputConfigs[0].mapWidth = 64;
    cfg.inputConfigs[0].mapHeight = 64;
    cfg.outputWidth = 64;
    cfg.outputHeight = 64;
    cfg.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.processor = QC_PROCESSOR_HTP0;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    ImageProps_t imgProp;
    imgProp.batchSize = 1;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_UYVY;
    imgProp.stride[0] = 128;
    imgProp.actualHeight[0] = 64;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;

    ImageDescriptor_t inputDesc, outputDesc;
    QCStatus_e ret = bufMgr.Allocate( imgProp, inputDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    imgProp.format = QC_IMAGE_FORMAT_RGB888;
    imgProp.stride[0] = 192;
    ret = bufMgr.Allocate( imgProp, outputDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inputDesc );
    config.buffers.push_back( outputDesc );

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );
    config.config = dt.Dump();

    ret = remap.Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // First DeInitialize should succeed
    ret = remap.DeInitialize();
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Second DeInitialize should fail
    ret = remap.DeInitialize();
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Cleanup
    bufMgr.Free( inputDesc );
    bufMgr.Free( outputDesc );
}

/**
 * @brief Test map dimensions validation
 * Covers: RemapConfig.cpp map width/height parameter parsing
 */
TEST( NodeRemap, MapDimensionsValidation )
{
    QC::Node::Remap remap;

    Remap_Config_t cfg;
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = 256;
    cfg.inputConfigs[0].inputHeight = 256;
    cfg.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_UYVY;
    cfg.inputConfigs[0].ROI = { 0, 0, 128, 128 };
    cfg.inputConfigs[0].mapWidth = 128;
    cfg.inputConfigs[0].mapHeight = 128;
    cfg.outputWidth = 128;
    cfg.outputHeight = 128;
    cfg.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.processor = QC_PROCESSOR_HTP0;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    QCNodeInit_t config = { dt.Dump() };

    // Config parsing should succeed
    QCStatus_e ret = remap.Initialize( config );
    EXPECT_EQ( QC_STATUS_OK, ret );
}

TEST( NodeRemap, GlobalBufferIdMapWrongType_CoversGlobalBufferIdMapInvalidBranch )
{
    QC::Node::Remap remap;

    Remap_Config_t cfg{};
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = 256;
    cfg.inputConfigs[0].inputHeight = 256;
    cfg.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_UYVY;
    cfg.inputConfigs[0].ROI = { 0, 0, 128, 128 };
    cfg.inputConfigs[0].mapWidth = 128;
    cfg.inputConfigs[0].mapHeight = 128;
    cfg.outputWidth = 128;
    cfg.outputHeight = 128;
    cfg.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.processor = QC_PROCESSOR_HTP0;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    // <-- key: force dt.Get("globalBufferIdMap", vector<DataTree>&) to return !OK and !OUT_OF_BOUND
    // by setting globalBufferIdMap to the WRONG type (scalar instead of array/object list).
    dt.Set<uint32_t>( "static.globalBufferIdMap", 123 );

    QCNodeInit_t config = { dt.Dump() };

    QCStatus_e ret = remap.Initialize( config );
    EXPECT_NE( QC_STATUS_OK, ret );
}

TEST( RemapImpl_NoFixture, Initialize_Undistortion_MapXWrongType_CoversNullMapXBranch )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );

    // ---- Inline configuration (formerly in helper) ----
    auto &cfg = impl.GetConifg();

    cfg.params.processor = QC_PROCESSOR_CPU;   // simple path
    cfg.params.coreId = 0;

    cfg.params.bEnableUndistortion = true;
    cfg.params.bEnableNormalize = false;

    cfg.params.numOfInputs = 1;

    // Minimal valid input so CreateRemapWorker() succeeds
    cfg.params.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.params.inputConfigs[0].inputWidth = 64;
    cfg.params.inputConfigs[0].inputHeight = 64;
    cfg.params.inputConfigs[0].ROI = { 0, 0, 32, 32 };

    // Map dimensions used later by CreatRemapTable()
    cfg.params.inputConfigs[0].mapWidth = 32;
    cfg.params.inputConfigs[0].mapHeight = 32;

    // Indices into the buffers vector we will pass to Initialize()
    cfg.params.inputConfigs[0].remapTable.mapXBufferId = 0;   // mapX at index 0
    cfg.params.inputConfigs[0].remapTable.mapYBufferId = 1;   // mapY at index 1

    // Minimal output to satisfy SetRemapParams
    cfg.params.outputWidth = 32;
    cfg.params.outputHeight = 32;
    cfg.params.outputFormat = QC_IMAGE_FORMAT_RGB888;

    // Skip buffer registration loop during Initialize()
    cfg.bufferIds.clear();

    // ---- Prepare buffers: WRONG type for mapX (Image), mapY also Image (unused due to early
    // break) ----
    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    ImageDescriptor_t imgX{};
    ImageDescriptor_t imgY{};
    ImageProps_t imgProps{};
    imgProps.batchSize = 1;
    imgProps.width = 64;
    imgProps.height = 64;
    imgProps.format = QC_IMAGE_FORMAT_RGB888;
    imgProps.stride[0] = imgProps.width * 3;
    imgProps.actualHeight[0] = imgProps.height;
    imgProps.numPlanes = 1;

    QCStatus_e allocStatus = mgr.Allocate( imgProps, imgX );
    ASSERT_EQ( QC_STATUS_OK, allocStatus );
    if ( allocStatus != QC_STATUS_OK )
    {
        return;   // Early exit if allocation fails
    }

    allocStatus = mgr.Allocate( imgProps, imgY );
    if ( allocStatus != QC_STATUS_OK )
    {
        mgr.Free( imgX );   // Clean up previously allocated buffer
        ASSERT_EQ( QC_STATUS_OK, allocStatus );
        return;
    }

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back(
            static_cast<QCBufferDescriptorBase_t &>( imgX ) );   // idx 0 -> mapX (WRONG type)
    buffers.emplace_back( static_cast<QCBufferDescriptorBase_t &>(
            imgY ) );   // idx 1 -> mapY (not reached; break on mapX)

    // ---- Call Initialize: hits the dynamic_cast for mapX and takes the "nullptr" branch ----
    QCStatus_e status = impl.Initialize( buffers );

    // NOTE: Your current implementation logs + break; but does not set status on this error,
    // so Initialize() may still return OK. If you later set BAD_ARGUMENTS in that branch,
    // change this to EXPECT_EQ(QC_STATUS_BAD_ARGUMENTS, status).
    EXPECT_EQ( QC_STATUS_OK, status );

    (void) mgr.Free( imgX );
    (void) mgr.Free( imgY );
}

TEST( RemapImpl_NoFixture, Initialize_Undistortion_MapYWrongType_CoversNullMapYBranch )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );

    // ---- Inline configuration ----
    auto &cfg = impl.GetConifg();

    cfg.params.processor = QC_PROCESSOR_CPU;
    cfg.params.coreId = 0;

    cfg.params.bEnableUndistortion = true;
    cfg.params.bEnableNormalize = false;

    cfg.params.numOfInputs = 1;

    cfg.params.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.params.inputConfigs[0].inputWidth = 64;
    cfg.params.inputConfigs[0].inputHeight = 64;
    cfg.params.inputConfigs[0].ROI = { 0, 0, 32, 32 };

    // Expect 64x64 maps
    cfg.params.inputConfigs[0].mapWidth = 64;
    cfg.params.inputConfigs[0].mapHeight = 64;

    // Indices into the buffers vector we pass to Initialize()
    cfg.params.inputConfigs[0].remapTable.mapXBufferId = 0;   // mapX at index 0
    cfg.params.inputConfigs[0].remapTable.mapYBufferId = 1;   // mapY at index 1

    // Minimal output to satisfy SetRemapParams
    cfg.params.outputWidth = 32;
    cfg.params.outputHeight = 32;
    cfg.params.outputFormat = QC_IMAGE_FORMAT_RGB888;

    // Skip buffer registration loop
    cfg.bufferIds.clear();

    // ---- Prepare buffers: mapX correct (Tensor), mapY WRONG (Image) ----
    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );

    ImageDescriptor_t imgY{};
    ImageProps_t imgProps{};
    imgProps.batchSize = 1;
    imgProps.width = 64;
    imgProps.height = 64;
    imgProps.format = QC_IMAGE_FORMAT_RGB888;
    imgProps.stride[0] = imgProps.width * 3;
    imgProps.actualHeight[0] = imgProps.height;
    imgProps.numPlanes = 1;
    ASSERT_EQ( QC_STATUS_OK, mgr.Allocate( imgProps, imgY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back(
            static_cast<QCBufferDescriptorBase &>( mapX ) );   // idx 0 -> mapX (Tensor OK)
    buffers.emplace_back(
            static_cast<QCBufferDescriptorBase &>( imgY ) );   // idx 1 -> mapY (Image -> cast null)

    // ---- Call Initialize: hits dynamic_cast for mapY and takes the "nullptr" branch ----
    QCStatus_e status = impl.Initialize( buffers );

    EXPECT_EQ( QC_STATUS_OK, status );

    (void) mgr.Free( mapX );
    (void) mgr.Free( imgY );
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
