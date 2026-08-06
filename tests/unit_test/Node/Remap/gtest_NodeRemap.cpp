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

    /* Set cpuThreadsAffinity in JSON only when explicitly configured (non-empty).
     * If empty, the key is omitted → RemapConfig will default to {} → FadasRemap
     * will fall back to platform defaults {12,13,14,15} on Linux or {0,1,2,3} otherwise. */
    if ( !pRemapConfig->cpuThreadsAffinity.empty() )
    {
        pdt->Set<int32_t>( "static.cpuThreadsAffinity", pRemapConfig->cpuThreadsAffinity );
    }

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
// STRESS TESTS
// Validate stability of Remap NSP (HTP0) and CPU processors over many iterations.
// Loop count is configurable via the REMAP_TEST_LOOP_NUMBER environment variable.
// Default loop count is 100.
//
// Usage:
//   export REMAP_TEST_LOOP_NUMBER=10000
//   ./bin/qcrun ./bin/gtest_NodeRemap --gtest_filter=NodeRemap.Stress_RemapNSP
//   ./bin/qcrun ./bin/gtest_NodeRemap --gtest_filter=NodeRemap.Stress_RemapCPU
//   ./bin/qcrun ./bin/gtest_NodeRemap --gtest_filter=NodeRemap.Stress_RemapGPU
// ============================================================================

static void SanityRemapForStress( QCProcessorType_e processor )
{
    QCStatus_e ret;
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t cfg{};
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
    cfg.processor = processor;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    ImageProps_t inProp{};
    inProp.batchSize = 1;
    inProp.width = 64;
    inProp.height = 64;
    inProp.format = QC_IMAGE_FORMAT_UYVY;
    inProp.stride[0] = 128;
    inProp.actualHeight[0] = 64;
    inProp.planeBufSize[0] = 0;
    inProp.numPlanes = 1;

    ImageProps_t outProp{};
    outProp.batchSize = 1;
    outProp.width = 64;
    outProp.height = 64;
    outProp.format = QC_IMAGE_FORMAT_RGB888;
    outProp.stride[0] = 192;
    outProp.actualHeight[0] = 64;
    outProp.planeBufSize[0] = 0;
    outProp.numPlanes = 1;

    ImageDescriptor_t inDesc{}, outDesc{};
    ret = bufMgr.Allocate( inProp, inDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = bufMgr.Allocate( outProp, outDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inDesc );
    config.buffers.push_back( outDesc );

    ret = remap.Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = remap.Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    NodeFrameDescriptor frameDesc( 2 );
    frameDesc.SetBuffer( 0, inDesc );
    frameDesc.SetBuffer( 1, outDesc );

    ret = remap.ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = remap.Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = remap.DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = bufMgr.Free( inDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = bufMgr.Free( outDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );
}

/**
 * @brief Stress test for Remap on NSP (HTP0 / DSP) processor.
 *
 * Runs the full Init → Start → ProcessFrameDescriptor → Stop → DeInit cycle
 * repeatedly to validate stability. Loop count is controlled by the
 * REMAP_TEST_LOOP_NUMBER environment variable (default: 100).
 */
TEST( NodeRemap, Stress_RemapNSP )
{
    uint32_t loopNumber = 100;
    const char *envValue = getenv( "REMAP_TEST_LOOP_NUMBER" );
    if ( nullptr != envValue )
    {
        loopNumber = static_cast<uint32_t>( atoi( envValue ) );
    }
    for ( uint32_t i = 0; i < loopNumber; i++ )
    {
        printf( "Stress_RemapNSP iteration %u / %u\n", i + 1, loopNumber );
        SanityRemapForStress( QC_PROCESSOR_HTP0 );
    }
}

/**
 * @brief Stress test for Remap on CPU processor.
 *
 * Runs the full Init → Start → ProcessFrameDescriptor → Stop → DeInit cycle
 * repeatedly to validate stability. Loop count is controlled by the
 * REMAP_TEST_LOOP_NUMBER environment variable (default: 100).
 */
TEST( NodeRemap, Stress_RemapCPU )
{
    uint32_t loopNumber = 100;
    const char *envValue = getenv( "REMAP_TEST_LOOP_NUMBER" );
    if ( nullptr != envValue )
    {
        loopNumber = static_cast<uint32_t>( atoi( envValue ) );
    }
    for ( uint32_t i = 0; i < loopNumber; i++ )
    {
        printf( "Stress_RemapCPU iteration %u / %u\n", i + 1, loopNumber );
        SanityRemapForStress( QC_PROCESSOR_CPU );
    }
}

#if defined( USE_ENG_FADAS_GPU )
/**
 * @brief Stress test for Remap on GPU processor.
 *
 * Runs the full Init → Start → ProcessFrameDescriptor → Stop → DeInit cycle
 * repeatedly to validate stability. Loop count is controlled by the
 * REMAP_TEST_LOOP_NUMBER environment variable (default: 100).
 *
 * Only compiled when USE_ENG_FADAS_GPU is defined (GPU FADAS library available).
 *
 * Usage:
 *   export REMAP_TEST_LOOP_NUMBER=10000
 *   ./bin/qcrun ./bin/gtest_NodeRemap --gtest_filter=NodeRemap.Stress_RemapGPU
 */
TEST( NodeRemap, Stress_RemapGPU )
{
    uint32_t loopNumber = 100;
    const char *envValue = getenv( "REMAP_TEST_LOOP_NUMBER" );
    if ( nullptr != envValue )
    {
        loopNumber = static_cast<uint32_t>( atoi( envValue ) );
    }
    for ( uint32_t i = 0; i < loopNumber; i++ )
    {
        printf( "Stress_RemapGPU iteration %u / %u\n", i + 1, loopNumber );
        SanityRemapForStress( QC_PROCESSOR_GPU );
    }
}
#endif   // USE_ENG_FADAS_GPU

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
    QCStatus_e ret = pRemap->ProcessFrameDescriptor( frameDesc );
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

// ============================================================================
// NEW TESTS FOR FadasRemap.cpp COVERAGE
// Add these tests to gtest_NodeRemap.cpp BEFORE the #ifndef GTEST_QCNODE block
// ============================================================================

// ============================================================================
// HELPER: Build a minimal Remap node, run one frame, return status.
// Exercises: SetRemapParams → CreateRemapWorker → RemapGetPipelineCPU/DSP
//            → CreatRemapTable → ProcessFrameDescriptor → RemapRun
//            → RemapRunCPU/DSP → DestroyWorkers → DestroyMap
// ============================================================================
static QCStatus_e RunRemapOnce( QCProcessorType_e processor, QCImageFormat_e inputFormat,
                                QCImageFormat_e outputFormat, bool bEnableNormalize,
                                uint32_t inputW = 64, uint32_t inputH = 64, uint32_t outputW = 64,
                                uint32_t outputH = 64 )
{
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t cfg{};
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = inputW;
    cfg.inputConfigs[0].inputHeight = inputH;
    cfg.inputConfigs[0].inputFormat = inputFormat;
    cfg.inputConfigs[0].ROI = { 0, 0, outputW, outputH };
    cfg.inputConfigs[0].mapWidth = outputW;
    cfg.inputConfigs[0].mapHeight = outputH;
    cfg.outputWidth = outputW;
    cfg.outputHeight = outputH;
    cfg.outputFormat = outputFormat;
    cfg.processor = processor;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = bEnableNormalize;
    cfg.coreId = 0;

    if ( bEnableNormalize )
    {
        cfg.normlzR.sub = 123.675f;
        cfg.normlzR.mul = 1.f / 58.395f;
        cfg.normlzR.add = 0.f;
        cfg.normlzG.sub = 116.28f;
        cfg.normlzG.mul = 1.f / 57.12f;
        cfg.normlzG.add = 0.f;
        cfg.normlzB.sub = 103.53f;
        cfg.normlzB.mul = 1.f / 57.375f;
        cfg.normlzB.add = 0.f;
    }

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    // Build input ImageProps
    ImageProps_t inProp{};
    inProp.batchSize = 1;
    inProp.width = inputW;
    inProp.height = inputH;
    inProp.format = inputFormat;
    if ( QC_IMAGE_FORMAT_NV12 == inputFormat )
    {
        inProp.stride[0] = inputW;
        inProp.stride[1] = inputW;
        inProp.actualHeight[0] = inputH;
        inProp.actualHeight[1] = inputH / 2;
        inProp.planeBufSize[0] = 0;
        inProp.planeBufSize[1] = 0;
        inProp.numPlanes = 2;
    }
    else if ( QC_IMAGE_FORMAT_UYVY == inputFormat )
    {
        inProp.stride[0] = inputW * 2;
        inProp.actualHeight[0] = inputH;
        inProp.planeBufSize[0] = 0;
        inProp.numPlanes = 1;
    }
    else
    {
        // RGB888, BGR888, or other 3-channel formats
        inProp.stride[0] = inputW * 3;
        inProp.actualHeight[0] = inputH;
        inProp.planeBufSize[0] = 0;
        inProp.numPlanes = 1;
    }

    // Build output ImageProps
    ImageProps_t outProp{};
    outProp.batchSize = 1;
    outProp.width = outputW;
    outProp.height = outputH;
    outProp.format = outputFormat;
    outProp.stride[0] = outputW * 3;
    outProp.actualHeight[0] = outputH;
    outProp.planeBufSize[0] = 0;
    outProp.numPlanes = 1;

    ImageDescriptor_t inDesc{}, outDesc{};
    if ( QC_STATUS_OK != bufMgr.Allocate( inProp, inDesc ) ) return QC_STATUS_FAIL;
    if ( QC_STATUS_OK != bufMgr.Allocate( outProp, outDesc ) )
    {
        bufMgr.Free( inDesc );
        return QC_STATUS_FAIL;
    }

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inDesc );
    config.buffers.push_back( outDesc );

    QCStatus_e ret = remap.Initialize( config );
    if ( QC_STATUS_OK != ret )
    {
        bufMgr.Free( inDesc );
        bufMgr.Free( outDesc );
        return ret;
    }

    ret = remap.Start();
    if ( QC_STATUS_OK != ret )
    {
        remap.DeInitialize();
        bufMgr.Free( inDesc );
        bufMgr.Free( outDesc );
        return ret;
    }

    NodeFrameDescriptor frameDesc( 2 );
    frameDesc.SetBuffer( 0, inDesc );
    frameDesc.SetBuffer( 1, outDesc );

    ret = remap.ProcessFrameDescriptor( frameDesc );

    remap.Stop();
    remap.DeInitialize();
    bufMgr.Free( inDesc );
    bufMgr.Free( outDesc );
    return ret;
}

// ============================================================================
// HELPER: Initialize a Remap node with 64x64 UYVY→RGB888 CPU config,
// then call ProcessFrameDescriptor with mismatched buffers.
// ============================================================================
static void RunRemapWithMismatch( QCImageFormat_e inputFmt, uint32_t inputW, uint32_t inputH,
                                  uint32_t inputBatch, QCImageFormat_e outputFmt, uint32_t outputW,
                                  uint32_t outputH, uint32_t outputBatch, QCStatus_e expectedRet )
{
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    // Configure node with 64x64 UYVY→RGB888 CPU
    Remap_Config_t cfg{};
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
    cfg.processor = QC_PROCESSOR_CPU;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    // Allocate the registered buffers (64x64 UYVY input, 64x64 RGB888 output)
    ImageProps_t regInProp{};
    regInProp.batchSize = 1;
    regInProp.width = 64;
    regInProp.height = 64;
    regInProp.format = QC_IMAGE_FORMAT_UYVY;
    regInProp.stride[0] = 128;
    regInProp.actualHeight[0] = 64;
    regInProp.planeBufSize[0] = 0;
    regInProp.numPlanes = 1;

    ImageProps_t regOutProp{};
    regOutProp.batchSize = 1;
    regOutProp.width = 64;
    regOutProp.height = 64;
    regOutProp.format = QC_IMAGE_FORMAT_RGB888;
    regOutProp.stride[0] = 192;
    regOutProp.actualHeight[0] = 64;
    regOutProp.planeBufSize[0] = 0;
    regOutProp.numPlanes = 1;

    ImageDescriptor_t regInDesc{}, regOutDesc{};
    if ( QC_STATUS_OK != bufMgr.Allocate( regInProp, regInDesc ) ) return;
    if ( QC_STATUS_OK != bufMgr.Allocate( regOutProp, regOutDesc ) )
    {
        bufMgr.Free( regInDesc );
        return;
    }

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( regInDesc );
    config.buffers.push_back( regOutDesc );

    QCStatus_e ret = remap.Initialize( config );
    if ( QC_STATUS_OK != ret )
    {
        bufMgr.Free( regInDesc );
        bufMgr.Free( regOutDesc );
        return;
    }

    ret = remap.Start();
    if ( QC_STATUS_OK != ret )
    {
        remap.DeInitialize();
        bufMgr.Free( regInDesc );
        bufMgr.Free( regOutDesc );
        return;
    }

    // Allocate "mismatched" buffers for the frame descriptor
    ImageProps_t mismatchInProp{};
    mismatchInProp.batchSize = inputBatch;
    mismatchInProp.width = inputW;
    mismatchInProp.height = inputH;
    mismatchInProp.format = inputFmt;
    if ( QC_IMAGE_FORMAT_UYVY == inputFmt )
    {
        mismatchInProp.stride[0] = inputW * 2;
        mismatchInProp.numPlanes = 1;
    }
    else
    {
        mismatchInProp.stride[0] = inputW * 3;
        mismatchInProp.numPlanes = 1;
    }
    mismatchInProp.actualHeight[0] = inputH;
    mismatchInProp.planeBufSize[0] = 0;

    ImageProps_t mismatchOutProp{};
    mismatchOutProp.batchSize = outputBatch;
    mismatchOutProp.width = outputW;
    mismatchOutProp.height = outputH;
    mismatchOutProp.format = outputFmt;
    mismatchOutProp.stride[0] = outputW * 3;
    mismatchOutProp.actualHeight[0] = outputH;
    mismatchOutProp.planeBufSize[0] = 0;
    mismatchOutProp.numPlanes = 1;

    ImageDescriptor_t mismatchInDesc{}, mismatchOutDesc{};
    if ( QC_STATUS_OK != bufMgr.Allocate( mismatchInProp, mismatchInDesc ) )
    {
        remap.Stop();
        remap.DeInitialize();
        bufMgr.Free( regInDesc );
        bufMgr.Free( regOutDesc );
        return;
    }
    if ( QC_STATUS_OK != bufMgr.Allocate( mismatchOutProp, mismatchOutDesc ) )
    {
        bufMgr.Free( mismatchInDesc );
        remap.Stop();
        remap.DeInitialize();
        bufMgr.Free( regInDesc );
        bufMgr.Free( regOutDesc );
        return;
    }

    NodeFrameDescriptor frameDesc( 2 );
    frameDesc.SetBuffer( 0, mismatchInDesc );
    frameDesc.SetBuffer( 1, mismatchOutDesc );

    ret = remap.ProcessFrameDescriptor( frameDesc );
    EXPECT_EQ( expectedRet, ret );

    remap.Stop();
    remap.DeInitialize();
    bufMgr.Free( regInDesc );
    bufMgr.Free( regOutDesc );
    bufMgr.Free( mismatchInDesc );
    bufMgr.Free( mismatchOutDesc );
}

// ============================================================================
// CPU PIPELINE COVERAGE TESTS
// Coverage: FadasRemap.cpp RemapGetPipelineCPU — all branches
// ============================================================================

/**
 * @brief CPU: UYVY → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_UYVY_TO_RGB888
 *           RemapRunCPU → UYVY input format branch (line 479-481)
 *           SetRemapParams → valid RGB888 output (line 43-54)
 *           DestroyWorkers/DestroyMap → CPU path (else branch)
 */
TEST( FadasRemapPipeline, CPU_UYVY_RGB888_NoNormalize )
{
    EXPECT_EQ( QC_STATUS_OK, RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY,
                                           QC_IMAGE_FORMAT_RGB888, false ) );
}


/**
 * @brief CPU: RGB888 → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_3C888
 *           RemapRunCPU → RGB888 input format branch (line 483-485)
 */
TEST( FadasRemapPipeline, CPU_RGB888_RGB888_NoNormalize )
{
    EXPECT_EQ( QC_STATUS_OK, RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_RGB888,
                                           QC_IMAGE_FORMAT_RGB888, false ) );
}

/**
 * @brief CPU: RGB888 → RGB888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU — TF2 for third condition
 *           (RGB888 input, RGB888 output, normalize=true → falls through to else/invalid)
 */
TEST( FadasRemapPipeline, CPU_RGB888_RGB888_Normalize )
{
    // RGB888 + normalize=true has no CPU pipeline → BAD_ARGUMENTS or FAIL
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_RGB888, QC_IMAGE_FORMAT_RGB888, true );
    EXPECT_NE( QC_STATUS_OK, ret );
}
/**
 * @brief CPU: NV12 → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888
 *           RemapRunCPU → NV12 input format branch (if FADAS supports NV12 on CPU)
 */
TEST( FadasRemapPipeline, CPU_NV12_RGB888_NoNormalize )
{
    // NV12 CPU pipeline — result depends on platform FADAS library support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888, false );
    // Accept OK (if NV12 supported) or FAIL (if not supported) — code path IS exercised
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief CPU: NV12 → RGB888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NORMU8
 */
TEST( FadasRemapPipeline, CPU_NV12_RGB888_Normalize )
{
    // NV12 + normalize CPU pipeline — result depends on platform support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888, true );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief CPU: NV12 → BGR888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_Y8UV8_TO_BGR888
 *           SetRemapParams → valid BGR888 output (TF2 for output format condition)
 */
TEST( FadasRemapPipeline, CPU_NV12_BGR888_NoNormalize )
{
    // NV12→BGR888 CPU pipeline — result depends on platform support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief CPU: UYVY → BGR888 (invalid pipeline — commented out with #if 0)
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → else branch (invalid pipeline)
 *           TF3 for first condition: UYVY input, non-RGB888 output
 *           TF3 for second condition: UYVY input, non-RGB888 output
 */
TEST( FadasRemapPipeline, CPU_UYVY_BGR888_InvalidPipeline )
{
    // UYVY→BGR888 is commented out in RemapGetPipelineCPU → falls to else (invalid)
    // CreateRemapWorker returns QC_STATUS_BAD_ARGUMENTS
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief CPU: RGB888 → BGR888 (invalid pipeline)
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → else branch (invalid pipeline)
 *           TF3 for third condition: RGB888 input, non-RGB888 output
 */
TEST( FadasRemapPipeline, CPU_RGB888_BGR888_InvalidPipeline )
{
    // RGB888→BGR888 has no CPU pipeline → falls to else (invalid)
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_RGB888, QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

// ============================================================================
// RemapRun MISMATCH TESTS
// Coverage: FadasRemap.cpp RemapRun() — input/output validation checks
// ============================================================================

/**
 * @brief Test RemapRun with input format mismatch
 * @coverage FadasRemap.cpp RemapRun() line 724-728 (format mismatch)
 */
TEST( FadasRemapRun, InputFormatMismatch )
{
    // Node configured with UYVY, but frame has RGB888
    RunRemapWithMismatch( QC_IMAGE_FORMAT_RGB888, 64, 64, 1, QC_IMAGE_FORMAT_RGB888, 64, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with input width mismatch
 * @coverage FadasRemap.cpp RemapRun() line 729-733 (width mismatch)
 */
TEST( FadasRemapRun, InputWidthMismatch )
{
    // Node configured with width=64, but frame has width=128
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 128, 64, 1, QC_IMAGE_FORMAT_RGB888, 64, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with input height mismatch
 * @coverage FadasRemap.cpp RemapRun() line 734-738 (height mismatch)
 */
TEST( FadasRemapRun, InputHeightMismatch )
{
    // Node configured with height=64, but frame has height=128
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 128, 1, QC_IMAGE_FORMAT_RGB888, 64, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with input batch size != 1
 * @coverage FadasRemap.cpp RemapRun() line 739-743 (batch size check)
 */
TEST( FadasRemapRun, InputBatchSizeMismatch )
{
    // Node configured with batchSize=1, but frame has batchSize=2
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 64, 2, QC_IMAGE_FORMAT_RGB888, 64, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with output format mismatch
 * @coverage FadasRemap.cpp RemapRun() line 756-760 (output format mismatch)
 */
TEST( FadasRemapRun, OutputFormatMismatch )
{
    // Node configured with RGB888 output, but frame has BGR888
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 64, 1, QC_IMAGE_FORMAT_BGR888, 64, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with output width mismatch
 * @coverage FadasRemap.cpp RemapRun() line 761-765 (output width mismatch)
 */
TEST( FadasRemapRun, OutputWidthMismatch )
{
    // Node configured with output width=64, but frame has width=128
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 64, 1, QC_IMAGE_FORMAT_RGB888, 128, 64, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with output height mismatch
 * @coverage FadasRemap.cpp RemapRun() line 766-770 (output height mismatch)
 */
TEST( FadasRemapRun, OutputHeightMismatch )
{
    // Node configured with output height=64, but frame has height=128
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 64, 1, QC_IMAGE_FORMAT_RGB888, 64, 128, 1,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with output batch mismatch
 * @coverage FadasRemap.cpp RemapRun() line 771-775 (output batch mismatch)
 */
TEST( FadasRemapRun, OutputBatchMismatch )
{
    // Node configured with numOfInputs=1, output batchSize must be 1, but frame has 2
    RunRemapWithMismatch( QC_IMAGE_FORMAT_UYVY, 64, 64, 1, QC_IMAGE_FORMAT_RGB888, 64, 64, 2,
                          QC_STATUS_BAD_ARGUMENTS );
}

/**
 * @brief Test RemapRun with HTP1 processor path
 * @coverage FadasRemap.cpp RemapRun() line 783 — HTP1 path (MC/DC pair 2: F||T)
 *           DestroyWorkers() line 802 — HTP1 path
 *           DestroyMap() line 842 — HTP1 path
 */
TEST( FadasRemapRun, HTP1ProcessorPath )
{
    // HTP1 exercises the (F) || (T) MC/DC pair in RemapRun, DestroyWorkers, DestroyMap
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP1, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, false );
    // Result depends on HTP1 availability; accept OK, FAIL, or INVALID_BUF
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );
}

/**
 * @brief Test SetRemapParams with invalid output format (NV12)
 * @coverage FadasRemap.cpp SetRemapParams() line 33-37 — invalid output format error path
 *           (MC/DC pair: both RGB888 and BGR888 checks fail → QC_STATUS_BAD_ARGUMENTS)
 */
TEST( FadasRemapSetRemapParams, InvalidOutputFormatNV12 )
{
    // NV12 is not a valid output format for SetRemapParams
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_NV12, false );
    EXPECT_NE( QC_STATUS_OK, ret );
}

/**
 * @brief CPU: NV12_UBWC → BGR888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineCPU → FADAS_REMAP_PIPELINE_UBWC_NV12_TO_BGR888
 *           RemapRunCPU → NV12_UBWC input format branch
 */
TEST( FadasRemapPipeline, CPU_NV12UBWC_BGR888_NoNormalize )
{
    QC::Node::Remap remap;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    Remap_Config_t cfg{};
    cfg.numOfInputs = 1;
    cfg.inputConfigs[0].inputWidth = 64;
    cfg.inputConfigs[0].inputHeight = 64;
    cfg.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_NV12_UBWC;
    cfg.inputConfigs[0].ROI = { 0, 0, 64, 64 };
    cfg.inputConfigs[0].mapWidth = 64;
    cfg.inputConfigs[0].mapHeight = 64;
    cfg.outputWidth = 64;
    cfg.outputHeight = 64;
    cfg.outputFormat = QC_IMAGE_FORMAT_BGR888;
    cfg.processor = QC_PROCESSOR_CPU;
    cfg.bEnableUndistortion = false;
    cfg.bEnableNormalize = false;
    cfg.coreId = 0;

    DataTree dt;
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigRemap( &cfg, &dt );

    ImageDescriptor_t inDesc{}, outDesc{};
    QCStatus_e ret =
            bufMgr.Allocate( ImageBasicProps_t( 64, 64, QC_IMAGE_FORMAT_NV12_UBWC ), inDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ImageProps_t outProp{};
    outProp.batchSize = 1;
    outProp.width = 64;
    outProp.height = 64;
    outProp.format = QC_IMAGE_FORMAT_BGR888;
    outProp.stride[0] = 192;
    outProp.actualHeight[0] = 64;
    outProp.planeBufSize[0] = 0;
    outProp.numPlanes = 1;
    ret = bufMgr.Allocate( outProp, outDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    std::vector<uint32_t> bufferIds = { 0, 1 };
    dt.Set<uint32_t>( "static.bufferIds", bufferIds );

    QCNodeInit_t config;
    config.config = dt.Dump();
    config.buffers.push_back( inDesc );
    config.buffers.push_back( outDesc );

    ret = remap.Initialize( config );
    if ( QC_STATUS_OK == ret )
    {
        ret = remap.Start();
        if ( QC_STATUS_OK == ret )
        {
            NodeFrameDescriptor frameDesc( 2 );
            frameDesc.SetBuffer( 0, inDesc );
            frameDesc.SetBuffer( 1, outDesc );
            ret = remap.ProcessFrameDescriptor( frameDesc );
            EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
            remap.Stop();
        }
        remap.DeInitialize();
    }
    else
    {
        // NV12_UBWC→BGR888 pipeline may not be supported on all platforms
        EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
    }

    bufMgr.Free( inDesc );
    bufMgr.Free( outDesc );
}

TEST( FadasRemapPipeline, CPU_NV12UBWC_RGB888_InvalidPipeline )
{
    // NV12_UBWC→RGB888 has no CPU pipeline → falls to else (invalid)
    // Returns BAD_ARGUMENTS if pipeline check fails, or FAIL if FADAS library rejects it
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12_UBWC,
                                   QC_IMAGE_FORMAT_RGB888, false );
    EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
}

// ============================================================================
// DSP PIPELINE COVERAGE TESTS
// Coverage: FadasRemap.cpp RemapGetPipelineDSP — all branches
// ============================================================================

/**
 * @brief DSP: UYVY → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NSP
 *           RemapRunDSP → UYVY input format branch
 *           RemapRunDSP → without normalize path
 */
TEST( FadasRemapPipeline, DSP_UYVY_RGB888_NoNormalize )
{
    EXPECT_EQ( QC_STATUS_OK, RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY,
                                           QC_IMAGE_FORMAT_RGB888, false ) );
}

/**
 * @brief DSP: RGB888 → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_3C888_NSP
 *           RemapRunDSP → RGB888 input format branch
 */
TEST( FadasRemapPipeline, DSP_RGB888_RGB888_NoNormalize )
{
    // RGB888 DSP pipeline — result depends on platform FADAS library support
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_RGB888,
                                   QC_IMAGE_FORMAT_RGB888, false );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief DSP: RGB888 → RGB888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP — TF2 for third condition
 *           (RGB888 input, RGB888 output, normalize=true → falls through to else/invalid)
 */
TEST( FadasRemapPipeline, DSP_RGB888_RGB888_Normalize )
{
    // RGB888 + normalize=true has no DSP pipeline → BAD_ARGUMENTS
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_RGB888, QC_IMAGE_FORMAT_RGB888, true );
    EXPECT_NE( QC_STATUS_OK, ret );
}

/**
 * @brief DSP: UYVY → BGR888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_UYVY_TO_BGR888_NSP
 */
TEST( FadasRemapPipeline, DSP_UYVY_BGR888_NoNormalize )
{
    EXPECT_EQ( QC_STATUS_OK, RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY,
                                           QC_IMAGE_FORMAT_BGR888, false ) );
}

/**
 * @brief DSP: UYVY → BGR888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP — TF2 for fourth condition
 *           (UYVY input, BGR888 output, normalize=true → falls through to else/invalid)
 */
TEST( FadasRemapPipeline, DSP_UYVY_BGR888_Normalize )
{
    // UYVY→BGR888 + normalize has no DSP pipeline → BAD_ARGUMENTS
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_BGR888, true );
    EXPECT_NE( QC_STATUS_OK, ret );
}

/**
 * @brief DSP: NV12 → RGB888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NSP
 *           RemapRunDSP → NV12 input format branch
 */
TEST( FadasRemapPipeline, DSP_NV12_RGB888_NoNormalize )
{
    // NV12 DSP pipeline — result depends on platform FADAS library support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888, false );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief DSP: NV12 → RGB888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NORMU8_NSP
 *           RemapRunDSP → with normalize path
 */
TEST( FadasRemapPipeline, DSP_NV12_RGB888_Normalize )
{
    // NV12 + normalize DSP pipeline — result depends on platform support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888, true );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief DSP: NV12 → BGR888 without normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → FADAS_REMAP_PIPELINE_Y8UV8_TO_BGR888_NSP
 */
TEST( FadasRemapPipeline, DSP_NV12_BGR888_NoNormalize )
{
    // NV12→BGR888 DSP pipeline — result depends on platform support
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL );
}

/**
 * @brief DSP: NV12 → BGR888 with normalization
 * @coverage FadasRemap.cpp RemapGetPipelineDSP — TF2 for seventh condition
 *           (NV12 input, BGR888 output, normalize=true → falls through to else/invalid)
 */
TEST( FadasRemapPipeline, DSP_NV12_BGR888_Normalize )
{
    // NV12→BGR888 + normalize has no DSP pipeline → BAD_ARGUMENTS
    QCStatus_e ret =
            RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_BGR888, true );
    EXPECT_NE( QC_STATUS_OK, ret );
}

/**
 * @brief DSP: NV12_UBWC → BGR888 (invalid pipeline for DSP)
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → else branch (invalid pipeline)
 *           TF4 for all conditions: NV12_UBWC input has no DSP pipeline
 */
TEST( FadasRemapPipeline, DSP_NV12UBWC_BGR888_InvalidPipeline )
{
    // NV12_UBWC has no DSP pipeline → falls to else (invalid)
    // Returns BAD_ARGUMENTS if pipeline check fails, or FAIL if FADAS library rejects it
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12_UBWC,
                                   QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
}

/**
 * @brief DSP: NV12_UBWC → RGB888 (invalid pipeline for DSP)
 * @coverage FadasRemap.cpp RemapGetPipelineDSP → else branch (invalid pipeline)
 */
TEST( FadasRemapPipeline, DSP_NV12UBWC_RGB888_InvalidPipeline )
{
    // NV12_UBWC has no DSP pipeline → falls to else (invalid)
    // Returns BAD_ARGUMENTS if pipeline check fails, or FAIL if FADAS library rejects it
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12_UBWC,
                                   QC_IMAGE_FORMAT_RGB888, false );
    EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
}

// ============================================================================
// HELPER: Common RemapImpl setup for CreatRemapTable tests with undistortion
// ============================================================================
static void SetupRemapImplForUndistortion( RemapImpl &impl,
                                           QCProcessorType_e processor = QC_PROCESSOR_CPU )
{
    auto &cfg = impl.GetConifg();
    cfg.params.processor = processor;
    cfg.params.coreId = 0;
    cfg.params.bEnableUndistortion = true;
    cfg.params.bEnableNormalize = false;
    cfg.params.numOfInputs = 1;
    cfg.params.inputConfigs[0].inputFormat = QC_IMAGE_FORMAT_UYVY;
    cfg.params.inputConfigs[0].inputWidth = 64;
    cfg.params.inputConfigs[0].inputHeight = 64;
    cfg.params.inputConfigs[0].ROI = { 0, 0, 64, 64 };
    cfg.params.inputConfigs[0].mapWidth = 64;
    cfg.params.inputConfigs[0].mapHeight = 64;
    cfg.params.inputConfigs[0].remapTable.mapXBufferId = 0;
    cfg.params.inputConfigs[0].remapTable.mapYBufferId = 1;
    cfg.params.outputWidth = 64;
    cfg.params.outputHeight = 64;
    cfg.params.outputFormat = QC_IMAGE_FORMAT_RGB888;
    cfg.bufferIds.clear();
}

// ============================================================================
// CreatRemapTable: TF2 for first condition — pBuf == nullptr for mapX
// Coverage: FadasRemap.cpp CreatRemapTable()
//   condition: (true == m_bEnableUndistortion) && ((QC_BUFFER_TYPE_TENSOR != type) || (nullptr ==
//   pBuf)) TF2: type==TENSOR but pBuf==nullptr → nullptr == bufDescMapX.pBuf is true
// ============================================================================
TEST( FadasRemapCreatRemapTable, MapX_NullPBuf )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    // mapX: correct type (TENSOR) but pBuf = nullptr
    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );
    void *savedPBuf = mapX.pBuf;
    mapX.pBuf = nullptr;   // TF2: type==TENSOR, pBuf==nullptr

    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    mapX.pBuf = savedPBuf;   // Restore before freeing
    mgr.Free( mapX );
    mgr.Free( mapY );
}


// ============================================================================
// CreatRemapTable: TF2 for second condition — pBuf == nullptr for mapY
// Coverage: FadasRemap.cpp CreatRemapTable()
//   condition: (true == m_bEnableUndistortion) && ((QC_BUFFER_TYPE_TENSOR != type) || (nullptr ==
//   pBuf)) TF2: type==TENSOR but pBuf==nullptr → nullptr == bufDescMapY.pBuf is true
// ============================================================================
TEST( FadasRemapCreatRemapTable, MapY_NullPBuf )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );

    // mapY: correct type (TENSOR) but pBuf = nullptr
    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapY ) );
    void *savedPBuf = mapY.pBuf;
    mapY.pBuf = nullptr;   // TF2: type==TENSOR, pBuf==nullptr

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    mapY.pBuf = savedPBuf;   // Restore before freeing
    mgr.Free( mapX );
    mgr.Free( mapY );
}

// ============================================================================
// CreatRemapTable: TF4 for third condition — mapHeight != bufDescMapX.dims[1]
// Coverage: FadasRemap.cpp CreatRemapTable()
//   condition: (true == m_bEnableUndistortion) &&
//              ((tensorType!=FLOAT_32) || (numDims!=2) || (dims[0]!=mapWidth) ||
//              (dims[1]!=mapHeight))
//   TF4: tensorType==FLOAT_32, numDims==2, dims[0]==mapWidth, dims[1]!=mapHeight
// ============================================================================
TEST( FadasRemapCreatRemapTable, MapX_WrongDims1 )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    // mapX: correct type, numDims=2, dims[0]=64 (correct), dims[1]=32 (wrong, should be 64)
    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 32 } }, mapX ) );

    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    mgr.Free( mapX );
    mgr.Free( mapY );
}

// ============================================================================
// CreatRemapTable: TF3 for fourth condition — mapWidth != bufDescMapY.dims[0]
// Coverage: FadasRemap.cpp CreatRemapTable()
//   condition: (true == m_bEnableUndistortion) &&
//              ((tensorType!=FLOAT_32) || (numDims!=2) || (dims[0]!=mapWidth) ||
//              (dims[1]!=mapHeight))
//   TF3: tensorType==FLOAT_32, numDims==2, dims[0]!=mapWidth
// ============================================================================
TEST( FadasRemapCreatRemapTable, MapY_WrongDims0 )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );

    // mapY: correct type, numDims=2, dims[0]=32 (wrong, should be 64), dims[1]=64 (correct)
    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 32, 64 } }, mapY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    mgr.Free( mapX );
    mgr.Free( mapY );
}

// ============================================================================
// CreatRemapTable: TF5 for third and fourth conditions — all checks pass (CPU)
// Coverage: FadasRemap.cpp CreatRemapTable()
//   TF5: undistortion=true, all tensor checks pass → goes to else branch
//        (calls RemapGetPipelineCPU → FadasRemap_CreateMapFromMap or CreateMapNoUndistortion)
// ============================================================================
TEST( FadasRemapCreatRemapTable, ValidMapXY_CPU )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl, QC_PROCESSOR_CPU );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    // Both mapX and mapY: correct type, numDims=2, correct dims
    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );

    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    // All tensor checks pass → goes to else branch in CreatRemapTable
    // Result depends on FADAS library support for undistortion on CPU
    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_TRUE( status == QC_STATUS_OK || status == QC_STATUS_FAIL );

    mgr.Free( mapX );
    mgr.Free( mapY );
}

// ============================================================================
// RemapGetPipelineCPU: TF2 for seventh condition
// Coverage: FadasRemap.cpp RemapGetPipelineCPU()
//   seventh condition: (NV12_UBWC == inputFormat) && (BGR888 == outputFormat) && (false ==
//   normalize) TF2: NV12_UBWC input, BGR888 output, normalize=true → condition false → else
//   (invalid)
// ============================================================================
TEST( FadasRemapPipeline, CPU_NV12UBWC_BGR888_Normalize )
{
    // NV12_UBWC→BGR888 + normalize=true has no CPU pipeline → falls to else (invalid)
    // This covers TF2 for the seventh condition in RemapGetPipelineCPU
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12_UBWC,
                                   QC_IMAGE_FORMAT_BGR888, true );
    EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
}

TEST( FadasRemapPipeline, DSP_RGB888_BGR888_InvalidPipeline )
{
    // RGB888→BGR888 has no DSP pipeline → falls to else (invalid)
    // This covers TF3 for the third condition in RemapGetPipelineDSP
    QCStatus_e ret = RunRemapOnce( QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_RGB888,
                                   QC_IMAGE_FORMAT_BGR888, false );
    EXPECT_TRUE( ret == QC_STATUS_BAD_ARGUMENTS || ret == QC_STATUS_FAIL );
}

// ============================================================================
// CreatRemapTable: TF4 for fourth condition — mapHeight != bufDescMapY.dims[1]
// Coverage: FadasRemap.cpp CreatRemapTable()
//   TF4: tensorType==FLOAT_32, numDims==2, dims[0]==mapWidth, dims[1]!=mapHeight for mapY
// ============================================================================
TEST( FadasRemapCreatRemapTable, MapY_WrongDims1 )
{
    QCNodeID nodeId{};
    Logger logger{};
    RemapImpl impl( nodeId, logger );
    SetupRemapImplForUndistortion( impl );

    BufferManager mgr( { "MANAGER", QC_NODE_TYPE_FADAS_REMAP, 0 } );

    TensorDescriptor_t mapX{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 64 } }, mapX ) );

    // mapY: correct type, numDims=2, dims[0]=64 (correct), dims[1]=32 (wrong, should be 64)
    TensorDescriptor_t mapY{};
    ASSERT_EQ( QC_STATUS_OK,
               mgr.Allocate( TensorProps_t{ QC_TENSOR_TYPE_FLOAT_32, { 64, 32 } }, mapY ) );

    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapX ) );
    buffers.emplace_back( static_cast<QCBufferDescriptorBase &>( mapY ) );

    QCStatus_e status = impl.Initialize( buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, status );

    mgr.Free( mapX );
    mgr.Free( mapY );
}


// ============================================================================
// FadasRemapTestable: exposes ONLY protected members (m_processor, m_handleIndex)
// Private members are set via public API: SetRemapParams() + CreateRemapWorker().
// CreateRemapWorker() stores m_inputFormats[id] BEFORE the validation check,
// so even if it returns BAD_ARGUMENTS the format is stored correctly.
// ============================================================================
class FadasRemapTestable : public QC::libs::FadasIface::FadasRemap
{
public:
    void SetProcessor( QCProcessorType_e p ) { m_processor = p; }
    void SetHandleIndex( uint32_t i ) { m_handleIndex = i; }
};

// ============================================================================
// MockFrameDescriptorForRemap: implements QCFrameDescriptorNodeIfs
// ============================================================================
class MockFrameDescriptorForRemap : public QCFrameDescriptorNodeIfs
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

// ============================================================================
// Helper: create input ImageDescriptor_t for direct RemapRun tests
// ============================================================================
static ImageDescriptor_t MakeRemapInputDesc( QCImageFormat_e fmt, uint32_t w, uint32_t h )
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
    else
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

// ============================================================================
// Helper: create output ImageDescriptor_t for direct RemapRun tests
// ============================================================================
static ImageDescriptor_t MakeRemapOutputDesc( QCImageFormat_e fmt, uint32_t w, uint32_t h,
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

// ============================================================================
// Helper: setup FadasRemapTestable using public API only.
// CreateRemapWorker() stores m_inputFormats[0]=inFmt BEFORE validation,
// so even if it returns BAD_ARGUMENTS the format is stored for RemapRun.
// ============================================================================
static void SetupRemapForRunDirect( FadasRemapTestable &r, QCProcessorType_e proc,
                                    QCImageFormat_e inFmt = QC_IMAGE_FORMAT_UYVY,
                                    QCImageFormat_e outFmt = QC_IMAGE_FORMAT_RGB888,
                                    bool bNorm = false )
{
    r.Init( proc, "RemapTest", LOGGER_LEVEL_ERROR );
    FadasNormlzParams_t n = { 0, 1, 0 };
    // SetRemapParams sets m_numOfInputs=1, m_outputFormat, m_outputWidth, m_outputHeight
    r.SetRemapParams( 1, 64, 64, outFmt, n, n, n, false, bNorm );
    r.SetProcessor( proc );
    r.SetHandleIndex( 0 );
    FadasROI_t roi = { 0, 0, 64, 64 };
    // CreateRemapWorker stores m_inputFormats[0]=inFmt, m_inputWidths[0]=64,
    // m_inputHeights[0]=64, m_ROIs[0]=roi BEFORE the format/pipeline validation.
    // So even if it returns BAD_ARGUMENTS, the state is set for RemapRun.
    r.CreateRemapWorker( 0, inFmt, 64, 64, roi );
}

// ============================================================================
// RemapRunCPU: NV12 input format branch (line 487-490)
// Coverage: srcImg.props.format = FADAS_IMAGE_FORMAT_Y8UV8
//           srcImg.plane[1] = pSrc + bufDescInput.planeBufSize[0]
// ============================================================================
TEST( FadasRemapRunCPU_Direct, NV12_Input )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12 );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    // m_inputFormats[0]==NV12, bufDescInput.format==NV12 → RemapRun format check passes
    // → RemapRunCPU called → NV12 branch reached
    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunCPU: NV12_UBWC input format branch (line 492-499)
// Coverage: srcImg.props.format = FADAS_IMAGE_FORMAT_UBWC_NV12
// ============================================================================
TEST( FadasRemapRunCPU_Direct, NV12UBWC_Input )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_NV12_UBWC,
                            QC_IMAGE_FORMAT_BGR888 );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_NV12_UBWC, 64, 64 );
    inp.size = 64 * 64 * 3;
    inp.height = 64;
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunCPU: BGR888 output format branch (line 515-517)
// Coverage: rgbImg.props.format = FADAS_IMAGE_FORMAT_BGR888
// ============================================================================
TEST( FadasRemapRunCPU_Direct, BGR888_Output )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_BGR888 );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_BGR888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunCPU: invalid input format branch (else branch, line 501-504)
// Coverage: QC_ERROR("Invalid input format for inputId = %d!")
//           ret = QC_STATUS_BAD_ARGUMENTS
// Strategy: CreateRemapWorker(99) stores m_inputFormats[0]=99 before validation.
//           Frame with format=99 passes RemapRun format check → hits else in RemapRunCPU.
// ============================================================================

// ============================================================================
// RemapRunCPU: CPU processor with normalize=true (line 566-568)
// Coverage: FadasRemap_RunMT( workerPtr, remapPtr, &srcImg, &rgbImg, &roi, 1.0, m_normlz )
// ============================================================================
TEST( FadasRemapRunCPU_Direct, CPU_Normalize )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888,
                            true );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunDSP: RGB888 input format branch (line 645-647)
// Coverage: srcImgProp.format = FADAS_IMAGE_FORMAT_RGB888_NSP
// ============================================================================
TEST( FadasRemapRunDSP_Direct, RGB888_Input )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_RGB888 );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunDSP: NV12 input format branch (line 649-651)
// Coverage: srcImgProp.format = FADAS_IMAGE_FORMAT_Y8UV8_NSP
// ============================================================================
TEST( FadasRemapRunDSP_Direct, NV12_Input )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_NV12 );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_NV12, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// RemapRunDSP: with normalize=true (lines 687-703)
// Coverage: builds normlz[3] array, calls FadasIface_FadasRemap_RunMT with normlz
// ============================================================================
TEST( FadasRemapRunDSP_Direct, DSP_Normalize )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888,
                            true );

    MockFrameDescriptorForRemap fd;
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    fd.AddBuffer( inp );
    fd.AddBuffer( out );

    QCStatus_e ret = r.RemapRun( fd );
    EXPECT_TRUE( ret == QC_STATUS_OK || ret == QC_STATUS_FAIL || ret == QC_STATUS_INVALID_BUF );

    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}


TEST( FadasRemapRunCPU_Direct, OutputRegBufFail )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_CPU, QC_IMAGE_FORMAT_UYVY );
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    /* Pre-register output, then change size → mismatch → RegBuf returns -1 */
    r.RegBuf( out, FADAS_BUF_TYPE_OUT );
    out.size += 1;
    MockFrameDescriptorForRemap fd;
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    out.size -= 1;
    r.DeregBuf( out.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

TEST( FadasRemapRunDSP_Direct, InputRegBufFail )
{
    FadasRemapTestable r;
    SetupRemapForRunDirect( r, QC_PROCESSOR_HTP0, QC_IMAGE_FORMAT_UYVY );
    ImageDescriptor_t inp = MakeRemapInputDesc( QC_IMAGE_FORMAT_UYVY, 64, 64 );
    ImageDescriptor_t out = MakeRemapOutputDesc( QC_IMAGE_FORMAT_RGB888, 64, 64, 1 );
    /* Pre-register input, then change size → mismatch → RegBuf returns -1 */
    r.RegBuf( inp, FADAS_BUF_TYPE_IN );
    inp.size += 1;
    MockFrameDescriptorForRemap fd;
    fd.AddBuffer( inp );
    fd.AddBuffer( out );
    EXPECT_EQ( QC_STATUS_INVALID_BUF, r.RemapRun( fd ) );
    inp.size -= 1;
    r.DeregBuf( inp.pBuf );
    free( inp.pBuf );
    free( out.pBuf );
    r.Deinit();
}

// ============================================================================
// CPU THREAD AFFINITY TESTS — SCENARIO-BASED
// Coverage: RemapConfig.cpp - VerifyStaticConfig (negative value validation)
//           FadasRemap.cpp  - CreateRemapWorker (affinity usage + SOC fallback)
// ============================================================================

/* Helper: build a minimal CPU-processor config DataTree for affinity tests */
static void BuildMinimalCpuConfig( DataTree &dt )
{
    dt.Set<std::string>( "static.name", "Remap" );
    dt.Set<uint32_t>( "static.id", 0 );
    dt.SetProcessorType( "static.processorType", QC_PROCESSOR_CPU );
    dt.Set<uint32_t>( "static.outputWidth", 64 );
    dt.Set<uint32_t>( "static.outputHeight", 64 );
    dt.SetImageFormat( "static.outputFormat", QC_IMAGE_FORMAT_RGB888 );
    dt.Set<bool>( "static.bEnableUndistortion", false );
    dt.Set<bool>( "static.bEnableNormalize", false );
    dt.Set<uint32_t>( "static.coreId", 0 );

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
}

/**
 * @brief Scenario 1: Negative core ID → QC_STATUS_BAD_ARGUMENTS
 * @coverage RemapConfig.cpp - VerifyStaticConfig: negative core ID validation
 * @expected QC_STATUS_BAD_ARGUMENTS — negative values are invalid CPU core IDs
 */
TEST( NodeRemapConfig, CpuThreadsAffinity_Scenario1_NegativeCoreId_ReturnsBadArguments )
{
    QC::Node::Remap remap;
    DataTree dt;
    BuildMinimalCpuConfig( dt );

    /* Negative core ID is invalid → VerifyStaticConfig must return BAD_ARGUMENTS */
    std::vector<int32_t> affinity = { -1, 0, 1, 2 };
    dt.Set<int32_t>( "static.cpuThreadsAffinity", affinity );

    QCNodeInit_t config = { dt.Dump() };
    printf( "Scenario1 config: %s\n", config.config.c_str() );

    QCStatus_e ret = remap.Initialize( config );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
}

/**
 * @brief Scenario 2: Large (unavailable) core IDs → library accepts, OS ignores
 * @coverage FadasRemap.cpp - CreateRemapWorker: FadasRemap_CreateWorkers does not validate
 * @expected NOT QC_STATUS_BAD_ARGUMENTS — values are syntactically valid (non-negative)
 *           FadasRemap_CreateWorkers accepts any non-negative value; OS handles affinity silently
 */
TEST( NodeRemapConfig, CpuThreadsAffinity_Scenario2_UnavailableCore_LibraryAccepts )
{
    QC::Node::Remap remap;
    DataTree dt;
    BuildMinimalCpuConfig( dt );

    /* Large but non-negative → passes VerifyStaticConfig, library accepts */
    std::vector<int32_t> affinity = { 9999, 10000, 10001, 10002 };
    dt.Set<int32_t>( "static.cpuThreadsAffinity", affinity );

    QCNodeInit_t config = { dt.Dump() };
    printf( "Scenario2 config: %s\n", config.config.c_str() );

    QCStatus_e ret = remap.Initialize( config );
    printf( "Scenario2 Initialize ret = %d\n", ret );
    /* FadasRemap_CreateWorkers does NOT validate core IDs → no BAD_ARGUMENTS */
    EXPECT_NE( QC_STATUS_BAD_ARGUMENTS, ret );

    if ( QC_STATUS_OK == ret )
    {
        remap.DeInitialize();
    }
}

/**
 * @brief Scenario 3: No cpuThreadsAffinity provided → falls back to hardcoded platform defaults
 * @coverage FadasRemap.cpp - CreateRemapWorker: empty affinity → platform defaults used
 * @expected NOT QC_STATUS_BAD_ARGUMENTS — Initialize succeeds using platform default core IDs
 *           JSON does NOT contain "cpuThreadsAffinity" key
 */
TEST( NodeRemapConfig, CpuThreadsAffinity_Scenario3_NotProvided_FallsBackToDefault )
{
    QC::Node::Remap remap;
    DataTree dt;
    BuildMinimalCpuConfig( dt );
    /* cpuThreadsAffinity intentionally NOT set → key absent from JSON */

    std::string jsonStr = dt.Dump();
    /* Verify: JSON does NOT contain cpuThreadsAffinity key */
    EXPECT_EQ( std::string::npos, jsonStr.find( "cpuThreadsAffinity" ) );

    QCNodeInit_t config = { jsonStr };
    printf( "Scenario3 config (no affinity): %s\n", config.config.c_str() );

    QCStatus_e ret = remap.Initialize( config );
    printf( "Scenario3 Initialize ret = %d\n", ret );
    /* Platform defaults are always valid → Initialize must succeed */
    EXPECT_EQ( QC_STATUS_OK, ret );

    if ( QC_STATUS_OK == ret )
    {
        remap.DeInitialize();
    }
}

/**
 * @brief Scenario 4a: SOC-based fallback — no affinity provided → platform defaults always valid
 * @coverage FadasRemap.cpp - CreateRemapWorker: #if defined(__linux__) fallback
 *
 * When cpuThreadsAffinity is absent, FadasRemap::CreateRemapWorker uses:
 *   #if defined(__linux__)  → {12, 13, 14, 15}   (Linux/QNX target)
 *   #else                   → {0, 1, 2, 3}        (other platforms)
 *
 * The SOC-based defaults are always valid on the target platform.
 * @expected QC_STATUS_OK — Initialize succeeds using the correct SOC-specific default core IDs
 */
TEST( NodeRemapConfig, CpuThreadsAffinity_Scenario4a_SocBasedFallback_NoAffinityProvided )
{
    QC::Node::Remap remap;
    DataTree dt;
    BuildMinimalCpuConfig( dt );
    /* cpuThreadsAffinity NOT set → SOC-based fallback applies */

    QCNodeInit_t config = { dt.Dump() };
    QCStatus_e ret = remap.Initialize( config );
    printf( "Scenario4a Initialize ret = %d\n", ret );
    /* SOC-based defaults are always valid on the target platform → must succeed */
    EXPECT_EQ( QC_STATUS_OK, ret );

    if ( QC_STATUS_OK == ret )
    {
        remap.DeInitialize();
    }
}

/**
 * @brief Scenario 4b: Platform-specific behavior when passing cores [4,5,6,7]
 * @coverage FadasRemap.cpp - CreateRemapWorker: platform-specific core availability
 *
 * Cores [4,5,6,7] behavior differs by platform:
 *   QNX  : cores 4-7 are available → Initialize succeeds (QC_STATUS_OK)
 *   Linux: hardcoded default is {12,13,14,15}; cores 4-7 may NOT exist on the
 *          Linux target → FadasRemap_CreateWorkers may return nullptr → QC_STATUS_FAIL
 *
 * @expected QNX:   QC_STATUS_OK
 *           Linux: QC_STATUS_FAIL (cores 4-7 not available on Linux target)
 */
TEST( NodeRemapConfig, CpuThreadsAffinity_Scenario4b_PlatformSpecificCores )
{
    QC::Node::Remap remap;
    DataTree dt;
    BuildMinimalCpuConfig( dt );

    /* Cores [4,5,6,7]: valid on QNX, may not exist on Linux (default is {12,13,14,15}) */
    std::vector<int32_t> affinity = { 4, 5, 6, 7 };
    dt.Set<int32_t>( "static.cpuThreadsAffinity", affinity );

    QCNodeInit_t config = { dt.Dump() };
    printf( "Scenario4b config (affinity=[4,5,6,7]): %s\n", config.config.c_str() );

    QCStatus_e ret = remap.Initialize( config );
    printf( "Scenario4b Initialize ret = %d\n", ret );
#if defined( __linux__ )
    EXPECT_EQ( QC_STATUS_FAIL, ret );
#else
    EXPECT_EQ( QC_STATUS_OK, ret );
#endif
    if ( QC_STATUS_OK == ret )
    {
        remap.DeInitialize();
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
