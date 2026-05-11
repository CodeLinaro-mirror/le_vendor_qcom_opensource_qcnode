// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#include "gtest/gtest.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <stdio.h>
#include <string>

#include "QC/Node/CL2DFlex.hpp"
#include "QC/sample/BufferManager.hpp"
#include "md5_utils.hpp"

#include "CL2DFlexImpl.hpp"
#include "kernel/CL2DFlex.cl.h"
#include "pipeline/CL2DPipelineBase.hpp"
#include "pipeline/CL2DPipelineConvert.hpp"
#include "pipeline/CL2DPipelineConvertUBWC.hpp"
#include "pipeline/CL2DPipelineLetterbox.hpp"
#include "pipeline/CL2DPipelineLetterboxMultiple.hpp"
#include "pipeline/CL2DPipelineRemap.hpp"
#include "pipeline/CL2DPipelineResize.hpp"
#include "pipeline/CL2DPipelineResizeMultiple.hpp"

#include <atomic>
#include <dlfcn.h>

// OpenCL clReleaseMemObject mock interposer for coverage
enum MockClReleaseMode
{
    MOCK_CL_RELEASE_NONE = 0,
    MOCK_CL_RELEASE_ALWAYS_FAIL = 1,
    MOCK_CL_RELEASE_FAIL_N_TIMES = 2
};

static std::atomic<int> g_mock_release_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseMemObject( int mode, int count )
{
    g_mock_release_mode.store( mode );
    g_mock_release_count.store( count );
}

extern "C" cl_int clReleaseMemObject( cl_mem memobj )
{
    static cl_int ( *real_clReleaseMemObject )( cl_mem ) = nullptr;
    if ( nullptr == real_clReleaseMemObject )
    {
        real_clReleaseMemObject = (cl_int ( * )( cl_mem )) dlsym( RTLD_NEXT, "clReleaseMemObject" );
    }

    const int mode = g_mock_release_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        cl_int ret = real_clReleaseMemObject( memobj );
        return CL_INVALID_MEM_OBJECT;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_count.store( cnt - 1 );
            return CL_INVALID_MEM_OBJECT;
        }
    }

    if ( real_clReleaseMemObject )
    {
        return real_clReleaseMemObject( memobj );
    }
    // Fallback: if the real symbol cannot be resolved in this environment, behave as success
    // to avoid impacting unrelated tests when mocking is not enabled.
    return CL_SUCCESS;
}

// OpenCL clReleaseKernel mock interposer for coverage
static std::atomic<int> g_mock_release_kernel_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_kernel_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseKernel( int mode, int count )
{
    g_mock_release_kernel_mode.store( mode );
    g_mock_release_kernel_count.store( count );
}

extern "C" cl_int clReleaseKernel( cl_kernel kernel )
{
    static cl_int ( *real_clReleaseKernel )( cl_kernel ) = nullptr;
    if ( nullptr == real_clReleaseKernel )
    {
        real_clReleaseKernel = (cl_int ( * )( cl_kernel )) dlsym( RTLD_NEXT, "clReleaseKernel" );
    }

    const int mode = g_mock_release_kernel_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        return CL_INVALID_KERNEL;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_kernel_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_kernel_count.store( cnt - 1 );
            return CL_INVALID_KERNEL;
        }
    }

    if ( real_clReleaseKernel )
    {
        return real_clReleaseKernel( kernel );
    }
    // Fallback: if the real symbol cannot be resolved in this environment, behave as success
    return CL_SUCCESS;
}

// OpenCL clReleaseProgram mock interposer for coverage
static std::atomic<int> g_mock_release_program_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_program_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseProgram( int mode, int count )
{
    g_mock_release_program_mode.store( mode );
    g_mock_release_program_count.store( count );
}

extern "C" cl_int clReleaseProgram( cl_program program )
{
    static cl_int ( *real_clReleaseProgram )( cl_program ) = nullptr;
    if ( nullptr == real_clReleaseProgram )
    {
        real_clReleaseProgram = (cl_int ( * )( cl_program )) dlsym( RTLD_NEXT, "clReleaseProgram" );
    }

    const int mode = g_mock_release_program_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        return CL_INVALID_PROGRAM;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_program_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_program_count.store( cnt - 1 );
            return CL_INVALID_PROGRAM;
        }
    }

    if ( real_clReleaseProgram )
    {
        return real_clReleaseProgram( program );
    }
    return CL_SUCCESS;
}

// OpenCL clReleaseCommandQueue mock interposer for coverage
static std::atomic<int> g_mock_release_cmdqueue_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_cmdqueue_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseCommandQueue( int mode, int count )
{
    g_mock_release_cmdqueue_mode.store( mode );
    g_mock_release_cmdqueue_count.store( count );
}

extern "C" cl_int clReleaseCommandQueue( cl_command_queue command_queue )
{
    static cl_int ( *real_clReleaseCommandQueue )( cl_command_queue ) = nullptr;
    if ( nullptr == real_clReleaseCommandQueue )
    {
        real_clReleaseCommandQueue =
                (cl_int ( * )( cl_command_queue )) dlsym( RTLD_NEXT, "clReleaseCommandQueue" );
    }

    const int mode = g_mock_release_cmdqueue_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        return CL_INVALID_COMMAND_QUEUE;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_cmdqueue_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_cmdqueue_count.store( cnt - 1 );
            return CL_INVALID_COMMAND_QUEUE;
        }
    }

    if ( real_clReleaseCommandQueue )
    {
        return real_clReleaseCommandQueue( command_queue );
    }
    return CL_SUCCESS;
}

// OpenCL clReleaseContext mock interposer for coverage
static std::atomic<int> g_mock_release_context_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_context_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseContext( int mode, int count )
{
    g_mock_release_context_mode.store( mode );
    g_mock_release_context_count.store( count );
}

extern "C" cl_int clReleaseContext( cl_context context )
{
    static cl_int ( *real_clReleaseContext )( cl_context ) = nullptr;
    if ( nullptr == real_clReleaseContext )
    {
        real_clReleaseContext = (cl_int ( * )( cl_context )) dlsym( RTLD_NEXT, "clReleaseContext" );
    }

    const int mode = g_mock_release_context_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        return CL_INVALID_CONTEXT;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_context_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_context_count.store( cnt - 1 );
            return CL_INVALID_CONTEXT;
        }
    }

    if ( real_clReleaseContext )
    {
        return real_clReleaseContext( context );
    }
    return CL_SUCCESS;
}

// OpenCL clReleaseSampler mock interposer for coverage
static std::atomic<int> g_mock_release_sampler_mode{ MOCK_CL_RELEASE_NONE };
static std::atomic<int> g_mock_release_sampler_count{ 0 };

extern "C" void MockOpenCL_ControlReleaseSampler( int mode, int count )
{
    g_mock_release_sampler_mode.store( mode );
    g_mock_release_sampler_count.store( count );
}

extern "C" cl_int clReleaseSampler( cl_sampler sampler )
{
    static cl_int ( *real_clReleaseSampler )( cl_sampler ) = nullptr;
    if ( nullptr == real_clReleaseSampler )
    {
        real_clReleaseSampler = (cl_int ( * )( cl_sampler )) dlsym( RTLD_NEXT, "clReleaseSampler" );
    }

    const int mode = g_mock_release_sampler_mode.load();
    if ( mode == MOCK_CL_RELEASE_ALWAYS_FAIL )
    {
        return CL_INVALID_SAMPLER;
    }
    if ( mode == MOCK_CL_RELEASE_FAIL_N_TIMES )
    {
        int cnt = g_mock_release_sampler_count.load();
        if ( cnt > 0 )
        {
            g_mock_release_sampler_count.store( cnt - 1 );
            return CL_INVALID_SAMPLER;
        }
    }

    if ( real_clReleaseSampler )
    {
        return real_clReleaseSampler( sampler );
    }
    return CL_SUCCESS;
}

inline const char *InvalidKernels() noexcept
{
    static const char *invalidkernels = KERNELCODE(

            __kernel void Invalid(){ xyz } );
    return invalidkernels;
}
static const char *s_pSourceInvalid = InvalidKernels();

using namespace QC::Node;
using namespace QC::test::utils;
using namespace QC::sample;

QCStatus_e LoadImage( ImageDescriptor_t imageDesc, std::string path )
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

QCStatus_e LoadMap( TensorDescriptor_t buffer, std::string path )
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

void SetConfigCL2D( CL2DFlex_Config_t *pCL2DFlexConfig, DataTree *pdt )
{
    pdt->Set<uint32_t>( "static.outputWidth", pCL2DFlexConfig->outputWidth );
    pdt->Set<uint32_t>( "static.outputHeight", pCL2DFlexConfig->outputHeight );
    pdt->SetImageFormat( "static.outputFormat", pCL2DFlexConfig->outputFormat );

    std::vector<DataTree> inputDts;
    for ( int i = 0; i < pCL2DFlexConfig->numOfInputs; i++ )
    {
        DataTree inputDt;
        inputDt.Set<uint32_t>( "inputWidth", pCL2DFlexConfig->inputWidths[i] );
        inputDt.Set<uint32_t>( "inputHeight", pCL2DFlexConfig->inputHeights[i] );
        inputDt.SetImageFormat( "inputFormat", pCL2DFlexConfig->inputFormats[i] );
        inputDt.Set<uint32_t>( "roiX", pCL2DFlexConfig->ROIs[i].x );
        inputDt.Set<uint32_t>( "roiY", pCL2DFlexConfig->ROIs[i].y );
        inputDt.Set<uint32_t>( "roiWidth", pCL2DFlexConfig->ROIs[i].width );
        inputDt.Set<uint32_t>( "roiHeight", pCL2DFlexConfig->ROIs[i].height );

        if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_CONVERT )
        {
            inputDt.Set<std::string>( "workMode", "convert" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_RESIZE_NEAREST )
        {
            inputDt.Set<std::string>( "workMode", "resize_nearest" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST )
        {
            inputDt.Set<std::string>( "workMode", "letterbox_nearest" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE )
        {
            inputDt.Set<std::string>( "workMode", "letterbox_nearest_multiple" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_RESIZE_NEAREST_MULTIPLE )
        {
            inputDt.Set<std::string>( "workMode", "resize_nearest_multiple" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_CONVERT_UBWC )
        {
            inputDt.Set<std::string>( "workMode", "convert_ubwc" );
        }
        else if ( pCL2DFlexConfig->workModes[i] == CL2DFLEX_WORK_MODE_REMAP_NEAREST )
        {
            inputDt.Set<std::string>( "workMode", "remap_nearest" );
            inputDt.Set<uint32_t>( "mapXBufferId", pCL2DFlexConfig->numOfInputs + 1 );
            inputDt.Set<uint32_t>( "mapYBufferId", pCL2DFlexConfig->numOfInputs + 2 );
        }
        else
        {
            inputDt.Set<std::string>( "workMode", "unknown" );
        }

        inputDts.push_back( inputDt );
    }
    pdt->Set( "static.inputs", inputDts );
}

void Sanity()
{
    QCStatus_e ret;
    std::string errors;
    QCNodeIfs *pCL2DFlex = new QC::Node::CL2DFlex();
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = 2;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;
        CL2DFlexConfig.inputWidths[i] = 128;
        CL2DFlexConfig.inputHeights[i] = 128;
        CL2DFlexConfig.inputFormats[i] = QC_IMAGE_FORMAT_NV12;
        CL2DFlexConfig.ROIs[i].x = 64;
        CL2DFlexConfig.ROIs[i].y = 64;
        CL2DFlexConfig.ROIs[i].width = 64;
        CL2DFlexConfig.ROIs[i].height = 64;
    }
    CL2DFlexConfig.outputWidth = 64;
    CL2DFlexConfig.outputHeight = 64;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;

    DataTree dt;
    dt.Set<std::string>( "static.name", "CL2D" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt );

    QCNodeInit_t config = { dt.Dump() };

    ImageProps_t imgPropInputs[CL2DFlexConfig.numOfInputs];
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].height = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].format = CL2DFlexConfig.inputFormats[i];
        if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].stride[1] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].actualHeight[1] = CL2DFlexConfig.inputHeights[i] / 2;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].planeBufSize[1] = 0;
            imgPropInputs[i].numPlanes = 2;
        }
        else if ( QC_IMAGE_FORMAT_UYVY == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 2;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
        else if ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 3;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = CL2DFlexConfig.numOfInputs;
    imgPropOutput.width = CL2DFlexConfig.outputWidth;
    imgPropOutput.height = CL2DFlexConfig.outputHeight;
    imgPropOutput.format = CL2DFlexConfig.outputFormat;
    if ( ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.outputFormat ) ||
         ( QC_IMAGE_FORMAT_BGR888 == CL2DFlexConfig.outputFormat ) )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth * 3;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.numPlanes = 1;
    }
    else if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.outputFormat )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth;
        imgPropOutput.stride[1] = CL2DFlexConfig.outputWidth;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.actualHeight[1] = CL2DFlexConfig.outputHeight / 2;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.planeBufSize[1] = 0;
        imgPropOutput.numPlanes = 2;
    }

    uint32_t globalIdx = 0;
    NodeFrameDescriptor frameDesc( CL2DFlexConfig.numOfInputs + 1 );
    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( CL2DFlexConfig.numOfInputs );
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
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

    ret = pCL2DFlex->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->DeInitialize();
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

    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex )->~CL2DFlex();
}

void Accuracy( uint32_t inputNumberTest, CL2DFlex_Work_Mode_e modeTest,
               QCImageFormat_e inputFormatTest, QCImageFormat_e outputFormatTest,
               uint32_t inputWidthTest, uint32_t inputHeightTest, uint32_t outputWidthTest,
               uint32_t outputHeightTest, std::string pathTest, std::string goldenPath,
               bool saveOutput )
{
    QCStatus_e ret;
    std::string errors;
    QCNodeIfs *pCL2DFlex = new QC::Node::CL2DFlex();
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );
    uint32_t globalIdx = 0;
    std::vector<uint32_t> bufferIds;
    QCNodeInit_t config;
    config.buffers.clear();

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = inputNumberTest;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = modeTest;
        CL2DFlexConfig.inputWidths[i] = inputWidthTest;
        CL2DFlexConfig.inputHeights[i] = inputHeightTest;
        CL2DFlexConfig.inputFormats[i] = inputFormatTest;
        CL2DFlexConfig.ROIs[i].x = 0;
        CL2DFlexConfig.ROIs[i].y = 0;
        CL2DFlexConfig.ROIs[i].width = inputWidthTest;
        CL2DFlexConfig.ROIs[i].height = inputHeightTest;
    }
    CL2DFlexConfig.outputWidth = outputWidthTest;
    CL2DFlexConfig.outputHeight = outputHeightTest;
    CL2DFlexConfig.outputFormat = outputFormatTest;

    DataTree dt;
    dt.Set<std::string>( "static.name", "CL2D" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt );

    ImageProps_t imgPropInputs[CL2DFlexConfig.numOfInputs];
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].height = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].format = CL2DFlexConfig.inputFormats[i];
        if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].stride[1] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].actualHeight[1] = CL2DFlexConfig.inputHeights[i] / 2;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].planeBufSize[1] = 0;
            imgPropInputs[i].numPlanes = 2;
        }
        else if ( QC_IMAGE_FORMAT_UYVY == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 2;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
        else if ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 3;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = CL2DFlexConfig.numOfInputs;
    imgPropOutput.width = CL2DFlexConfig.outputWidth;
    imgPropOutput.height = CL2DFlexConfig.outputHeight;
    imgPropOutput.format = CL2DFlexConfig.outputFormat;
    if ( ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.outputFormat ) ||
         ( QC_IMAGE_FORMAT_BGR888 == CL2DFlexConfig.outputFormat ) )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth * 3;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.numPlanes = 1;
    }
    else if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.outputFormat )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth;
        imgPropOutput.stride[1] = CL2DFlexConfig.outputWidth;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.actualHeight[1] = CL2DFlexConfig.outputHeight / 2;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.planeBufSize[1] = 0;
        imgPropOutput.numPlanes = 2;
    }

    uint32_t frameIdx = 0;
    NodeFrameDescriptor frameDesc( CL2DFlexConfig.numOfInputs + 1 );

    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( CL2DFlexConfig.numOfInputs );
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        ImageDescriptor_t imageDesc;
        if ( QC_IMAGE_FORMAT_NV12_UBWC == CL2DFlexConfig.inputFormats[i] )
        {
            ret = bufMgr.Allocate( ImageBasicProps_t( CL2DFlexConfig.inputWidths[i],
                                                      CL2DFlexConfig.inputHeights[i],
                                                      CL2DFlexConfig.inputFormats[i] ),
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
    if ( CL2DFLEX_WORK_MODE_REMAP_NEAREST == CL2DFlexConfig.workModes[0] )
    {
        TensorProps_t mapXProp = { QC_TENSOR_TYPE_FLOAT_32,
                                   { CL2DFlexConfig.outputHeight * CL2DFlexConfig.outputWidth } };
        ret = bufMgr.Allocate( mapXProp, mapXBufferDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );

        TensorProps_t mapYProp = { QC_TENSOR_TYPE_FLOAT_32,
                                   { CL2DFlexConfig.outputHeight * CL2DFlexConfig.outputWidth }

        };
        ret = bufMgr.Allocate( mapYProp, mapYBufferDesc );
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

    ret = pCL2DFlex->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        memset( inputs[i].pBuf, 0, inputs[i].size );
        ret = LoadImage( inputs[i], pathTest );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
    memset( outputs[0].pBuf, 0, outputs[0].size );
    if ( CL2DFLEX_WORK_MODE_REMAP_NEAREST == CL2DFlexConfig.workModes[0] )
    {
        ret = LoadMap( mapXBufferDesc, "./data/test/CL2DFlex/mapX.raw" );
        ASSERT_EQ( QC_STATUS_OK, ret );
        ret = LoadMap( mapYBufferDesc, "./data/test/CL2DFlex/mapY.raw" );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    ret = pCL2DFlex->ProcessFrameDescriptor( frameDesc );
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
    LoadImage( golden, goldenPath );

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
        ASSERT_GT( cos, 0.999 );
    }

    ret = pCL2DFlex->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->DeInitialize();
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

    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex )->~CL2DFlex();
}

void MultipleROI( CL2DFlex_Work_Mode_e modeTest, QCImageFormat_e inputFormatTest,
                  QCImageFormat_e outputFormatTest, uint32_t inputWidthTest,
                  uint32_t inputHeightTest, uint32_t outputWidthTest, uint32_t outputHeightTest,
                  std::string pathTest )
{
    QCStatus_e ret;
    std::string errors;
    QCNodeIfs *pCL2DFlex = new QC::Node::CL2DFlex();
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );
    uint32_t globalIdx = 0;
    std::vector<uint32_t> bufferIds;
    QCNodeInit_t config;
    config.buffers.clear();

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = 1;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = modeTest;
        CL2DFlexConfig.inputWidths[i] = inputWidthTest;
        CL2DFlexConfig.inputHeights[i] = inputHeightTest;
        CL2DFlexConfig.inputFormats[i] = inputFormatTest;
        CL2DFlexConfig.ROIs[i].x = 0;
        CL2DFlexConfig.ROIs[i].y = 0;
        CL2DFlexConfig.ROIs[i].width = inputWidthTest;
        CL2DFlexConfig.ROIs[i].height = inputHeightTest;
    }
    CL2DFlexConfig.outputWidth = outputWidthTest;
    CL2DFlexConfig.outputHeight = outputHeightTest;
    CL2DFlexConfig.outputFormat = outputFormatTest;

    DataTree dt;
    dt.Set<std::string>( "static.name", "CL2D" );
    dt.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt );

    ImageProps_t imgPropInputs[CL2DFlexConfig.numOfInputs];
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].height = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].format = CL2DFlexConfig.inputFormats[i];
        if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].stride[1] = CL2DFlexConfig.inputWidths[i];
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].actualHeight[1] = CL2DFlexConfig.inputHeights[i] / 2;
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].planeBufSize[1] = 0;
            imgPropInputs[i].numPlanes = 2;
        }
        else if ( QC_IMAGE_FORMAT_UYVY == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 2;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
        else if ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.inputFormats[i] )
        {
            imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i] * 3;
            imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
            imgPropInputs[i].planeBufSize[0] = 0;
            imgPropInputs[i].numPlanes = 1;
        }
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = CL2DFlexConfig.numOfInputs;
    imgPropOutput.width = CL2DFlexConfig.outputWidth;
    imgPropOutput.height = CL2DFlexConfig.outputHeight;
    imgPropOutput.format = CL2DFlexConfig.outputFormat;
    if ( ( QC_IMAGE_FORMAT_RGB888 == CL2DFlexConfig.outputFormat ) ||
         ( QC_IMAGE_FORMAT_BGR888 == CL2DFlexConfig.outputFormat ) )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth * 3;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.numPlanes = 1;
    }
    else if ( QC_IMAGE_FORMAT_NV12 == CL2DFlexConfig.outputFormat )
    {
        imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth;
        imgPropOutput.stride[1] = CL2DFlexConfig.outputWidth;
        imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
        imgPropOutput.actualHeight[1] = CL2DFlexConfig.outputHeight / 2;
        imgPropOutput.planeBufSize[0] = 0;
        imgPropOutput.planeBufSize[1] = 0;
        imgPropOutput.numPlanes = 2;
    }

    uint32_t frameIdx = 0;
    NodeFrameDescriptor frameDesc( CL2DFlexConfig.numOfInputs + 1 );

    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( CL2DFlexConfig.numOfInputs );
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        ImageDescriptor_t imageDesc;
        if ( QC_IMAGE_FORMAT_NV12_UBWC == CL2DFlexConfig.inputFormats[i] )
        {
            ret = bufMgr.Allocate( ImageBasicProps_t( CL2DFlexConfig.inputWidths[i],
                                                      CL2DFlexConfig.inputHeights[i],
                                                      CL2DFlexConfig.inputFormats[i] ),
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

    uint32_t roiNumber = 100;
    dt.Set<uint32_t>( "static.numOfROIs", roiNumber );
    dt.Set<uint32_t>( "static.ROIsBufferId", CL2DFlexConfig.numOfInputs + 1 );
    TensorDescriptor_t ROIsBufferDesc;
    TensorProps_t ROIProp;
    ROIProp = { QC_TENSOR_TYPE_UINT_32, { roiNumber, 4 } };
    ret = bufMgr.Allocate( ROIProp, ROIsBufferDesc );
    if ( nullptr != ROIsBufferDesc.pBuf )
    {
        config.buffers.push_back( ROIsBufferDesc );
        bufferIds.push_back( globalIdx );
        globalIdx++;
    }
    else
    {
        printf( "ROIsBufferDesc is nullptr\n" );
    }

    for ( uint32_t i = 0; i < roiNumber; i++ )
    {
        uint32_t *pROIsBufferDesc = reinterpret_cast<uint32_t *>( ROIsBufferDesc.pBuf );
        pROIsBufferDesc[i * 4 + 0] = 0;
        pROIsBufferDesc[i * 4 + 1] = 0;
        pROIsBufferDesc[i * 4 + 2] = CL2DFlexConfig.inputWidths[0] / 2;
        pROIsBufferDesc[i * 4 + 3] = CL2DFlexConfig.inputHeights[0] / 2;
    }

    dt.Set<uint32_t>( "static.bufferIds", bufferIds );
    config.config = dt.Dump();
    printf( "config: %s\n", config.config.c_str() );

    ret = pCL2DFlex->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        memset( inputs[i].pBuf, 0, inputs[i].size );
        ret = LoadImage( inputs[i], pathTest );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
    memset( outputs[0].pBuf, 0, outputs[0].size );

    ret = pCL2DFlex->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pCL2DFlex->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( auto imageDesc : inputs )
    {
        ret = bufMgr.Free( imageDesc );
    }

    for ( auto imageDesc : outputs )
    {
        ret = bufMgr.Free( imageDesc );
    }

    (void) bufMgr.Free( ROIsBufferDesc );

    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex )->~CL2DFlex();
}

void Coverage1()
{
    QCStatus_e ret;
    std::string errors;

    // dynamic config
    QCNodeIfs *pCL2DFlex1 = new QC::Node::CL2DFlex();
    DataTree dt1;
    dt1.Set<std::string>( "dynamic.name", "CL2D" );
    QCNodeInit_t config1 = { dt1.Dump() };
    ret = pCL2DFlex1->Initialize( config1 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    QCNodeConfigIfs &configIfs = pCL2DFlex1->GetConfigurationIfs();
    const std::string &options = configIfs.GetOptions();
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex1 )->~CL2DFlex();

    // empty name
    QCNodeIfs *pCL2DFlex2 = new QC::Node::CL2DFlex();
    DataTree dt2;
    dt2.Set<std::string>( "static.name", "" );
    dt2.Set<uint32_t>( "static.id", 0 );
    QCNodeInit_t config2 = { dt2.Dump() };
    ret = pCL2DFlex2->Initialize( config2 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex2 )->~CL2DFlex();

    // empty id
    QCNodeIfs *pCL2DFlex3 = new QC::Node::CL2DFlex();
    DataTree dt3;
    dt3.Set<std::string>( "static.name", "CL2D" );
    QCNodeInit_t config3 = { dt3.Dump() };
    ret = pCL2DFlex3->Initialize( config3 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex3 )->~CL2DFlex();

    // empty bufferIds
    QCNodeIfs *pCL2DFlex4 = new QC::Node::CL2DFlex();
    DataTree dt4;
    dt4.Set<std::string>( "static.name", "CL2D" );
    dt4.Set<uint32_t>( "static.id", 0 );
    std::vector<uint32_t> bufferIds;
    dt4.Set<uint32_t>( "static.bufferIds", bufferIds );
    QCNodeInit_t config4 = { dt4.Dump() };
    ret = pCL2DFlex4->Initialize( config4 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex4 )->~CL2DFlex();

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = 2;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;
        CL2DFlexConfig.inputWidths[i] = 128;
        CL2DFlexConfig.inputHeights[i] = 128;
        CL2DFlexConfig.inputFormats[i] = QC_IMAGE_FORMAT_NV12;
        CL2DFlexConfig.ROIs[i].x = 64;
        CL2DFlexConfig.ROIs[i].y = 64;
        CL2DFlexConfig.ROIs[i].width = 64;
        CL2DFlexConfig.ROIs[i].height = 64;
    }
    CL2DFlexConfig.outputWidth = 64;
    CL2DFlexConfig.outputHeight = 64;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;

    // invalid status and dynamic config
    QCNodeIfs *pCL2DFlex5 = new QC::Node::CL2DFlex();
    DataTree dt5;
    dt5.Set<std::string>( "static.name", "CL2D" );
    dt5.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt5 );
    QCObjectState_e status = pCL2DFlex5->GetState();
    QCNodeInit_t config5 = { dt5.Dump() };
    ret = pCL2DFlex5->DeInitialize();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
    ret = pCL2DFlex5->Stop();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
    ret = pCL2DFlex5->Start();
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
    NodeFrameDescriptor frameDesc( 1 );
    ret = pCL2DFlex5->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_STATE, ret );
    ret = pCL2DFlex5->Initialize( config5 );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = pCL2DFlex5->Initialize( config5 );
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex5 )->~CL2DFlex();

    // invalid workmode
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_MAX;
    QCNodeIfs *pCL2DFlex6 = new QC::Node::CL2DFlex();
    DataTree dt6;
    dt6.Set<std::string>( "static.name", "CL2D" );
    dt6.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt6 );
    QCNodeInit_t config6 = { dt6.Dump() };
    ret = pCL2DFlex6->Initialize( config6 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex6 )->~CL2DFlex();
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;

    // multiple rois with invalid numOfROIs
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE;
    QCNodeIfs *pCL2DFlex7 = new QC::Node::CL2DFlex();
    DataTree dt7;
    dt7.Set<std::string>( "static.name", "CL2D" );
    dt7.Set<uint32_t>( "static.id", 0 );
    dt7.Set<uint32_t>( "static.numOfROIs", 1000 );
    SetConfigCL2D( &CL2DFlexConfig, &dt7 );
    QCNodeInit_t config7 = { dt7.Dump() };
    ret = pCL2DFlex7->Initialize( config7 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex7 )->~CL2DFlex();
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;

    // multiple rois without numOfROIs and ROIsBufferId
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE;
    QCNodeIfs *pCL2DFlex8 = new QC::Node::CL2DFlex();
    DataTree dt8;
    dt8.Set<std::string>( "static.name", "CL2D" );
    dt8.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt8 );
    QCNodeInit_t config8 = { dt8.Dump() };
    ret = pCL2DFlex8->Initialize( config8 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex8 )->~CL2DFlex();
    CL2DFlexConfig.workModes[0] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;

    // remap without mapXBufferId
    std::vector<DataTree> inputDts;
    DataTree inputDt;
    inputDt.Set<std::string>( "workMode", "remap_nearest" );
    inputDts.push_back( inputDt );
    QCNodeIfs *pCL2DFlex9 = new QC::Node::CL2DFlex();
    DataTree dt9;
    dt9.Set<std::string>( "static.name", "CL2D" );
    dt9.Set<uint32_t>( "static.id", 0 );
    dt9.Set( "static.inputs", inputDts );
    QCNodeInit_t config9 = { dt9.Dump() };
    ret = pCL2DFlex9->Initialize( config9 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex9 )->~CL2DFlex();

    // invalid numOfInputs
    CL2DFlexConfig.numOfInputs = QC_MAX_INPUTS + 1;
    QCNodeIfs *pCL2DFlex10 = new QC::Node::CL2DFlex();
    DataTree dt10;
    dt10.Set<std::string>( "static.name", "CL2D" );
    dt10.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt10 );
    QCNodeInit_t config10 = { dt10.Dump() };
    ret = pCL2DFlex10->Initialize( config10 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex10 )->~CL2DFlex();
    CL2DFlexConfig.numOfInputs = 2;

    // invalid globalBufferIdMap with empty name and id
    QCNodeIfs *pCL2DFlex11 = new QC::Node::CL2DFlex();
    DataTree dt11;
    dt11.Set<std::string>( "static.name", "CL2D" );
    dt11.Set<uint32_t>( "static.id", 0 );
    std::vector<DataTree> globalBufferIdMap1;
    DataTree globalBufferId1;
    globalBufferId1.Set<std::string>( "name", "" );
    globalBufferId1.Set<uint32_t>( "id", UINT32_MAX );
    globalBufferIdMap1.push_back( globalBufferId1 );
    dt11.Set( "static.globalBufferIdMap", globalBufferIdMap1 );
    SetConfigCL2D( &CL2DFlexConfig, &dt11 );
    QCNodeInit_t config11 = { dt11.Dump() };
    ret = pCL2DFlex11->Initialize( config11 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex11 )->~CL2DFlex();

    // invalid globalBufferIdMap with incorrect size
    QCNodeIfs *pCL2DFlex12 = new QC::Node::CL2DFlex();
    DataTree dt12;
    dt12.Set<std::string>( "static.name", "CL2D" );
    dt12.Set<uint32_t>( "static.id", 0 );
    std::vector<DataTree> globalBufferIdMap2;
    DataTree globalBufferId2;
    globalBufferId2.Set<std::string>( "name", "CL2D" );
    globalBufferId2.Set<uint32_t>( "id", 0 );
    globalBufferIdMap2.push_back( globalBufferId2 );
    dt12.Set( "static.globalBufferIdMap", globalBufferIdMap2 );
    SetConfigCL2D( &CL2DFlexConfig, &dt12 );
    QCNodeInit_t config12 = { dt12.Dump() };
    ret = pCL2DFlex12->Initialize( config12 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex12 )->~CL2DFlex();

    // high priority
    QCNodeIfs *pCL2DFlex13 = new QC::Node::CL2DFlex();
    DataTree dt13;
    dt13.Set<std::string>( "static.name", "CL2D" );
    dt13.Set<uint32_t>( "static.id", 0 );
    dt13.Set<std::string>( "static.priority", "high" );
    SetConfigCL2D( &CL2DFlexConfig, &dt13 );
    QCNodeInit_t config13 = { dt13.Dump() };
    ret = pCL2DFlex13->Initialize( config13 );
    EXPECT_EQ( QC_STATUS_OK, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex13 )->~CL2DFlex();

    // low priority
    QCNodeIfs *pCL2DFlex14 = new QC::Node::CL2DFlex();
    DataTree dt14;
    dt14.Set<std::string>( "static.name", "CL2D" );
    dt14.Set<uint32_t>( "static.id", 0 );
    dt14.Set<std::string>( "static.priority", "low" );
    SetConfigCL2D( &CL2DFlexConfig, &dt14 );
    QCNodeInit_t config14 = { dt14.Dump() };
    ret = pCL2DFlex14->Initialize( config14 );
    EXPECT_EQ( QC_STATUS_OK, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex14 )->~CL2DFlex();

    // invalid priority
    QCNodeIfs *pCL2DFlex15 = new QC::Node::CL2DFlex();
    DataTree dt15;
    dt15.Set<std::string>( "static.name", "CL2D" );
    dt15.Set<uint32_t>( "static.id", 0 );
    dt15.Set<std::string>( "static.priority", "unknown" );
    SetConfigCL2D( &CL2DFlexConfig, &dt15 );
    QCNodeInit_t config15 = { dt15.Dump() };
    ret = pCL2DFlex15->Initialize( config15 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex15 )->~CL2DFlex();

    // invalid device id
    QCNodeIfs *pCL2DFlex16 = new QC::Node::CL2DFlex();
    DataTree dt16;
    dt16.Set<std::string>( "static.name", "CL2D" );
    dt16.Set<uint32_t>( "static.id", 0 );
    dt16.Set<uint32_t>( "static.deviceId", 100 );
    SetConfigCL2D( &CL2DFlexConfig, &dt16 );
    QCNodeInit_t config16 = { dt16.Dump() };
    ret = pCL2DFlex16->Initialize( config16 );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex16 )->~CL2DFlex();

    // deregister all buffers when stop
    QCNodeIfs *pCL2DFlex17 = new QC::Node::CL2DFlex();
    DataTree dt17;
    dt17.Set<std::string>( "static.name", "CL2D" );
    dt17.Set<uint32_t>( "static.id", 0 );
    dt17.Set<bool>( "static.deRegisterAllBuffersWhenStop", true );
    SetConfigCL2D( &CL2DFlexConfig, &dt17 );
    QCNodeInit_t config17 = { dt17.Dump() };
    ret = pCL2DFlex17->Initialize( config17 );
    ret = pCL2DFlex17->Start();
    EXPECT_EQ( QC_STATUS_OK, ret );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = pCL2DFlex17->Stop();
    EXPECT_EQ( QC_STATUS_OK, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex17 )->~CL2DFlex();

    // wrong bufferIds
    QCNodeIfs *pCL2DFlex18 = new QC::Node::CL2DFlex();
    DataTree dt18;
    dt18.Set<std::string>( "static.name", "CL2D" );
    dt18.Set<uint32_t>( "static.id", 0 );
    uint32_t id = 100;
    bufferIds.push_back( id );
    dt18.Set<uint32_t>( "static.bufferIds", bufferIds );
    QCNodeInit_t config18 = { dt18.Dump() };
    ret = pCL2DFlex18->Initialize( config18 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex18 )->~CL2DFlex();
}

void Coverage2()
{
    QCStatus_e ret;
    std::string errors;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = 2;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;
        CL2DFlexConfig.inputWidths[i] = 128;
        CL2DFlexConfig.inputHeights[i] = 128;
        CL2DFlexConfig.inputFormats[i] = QC_IMAGE_FORMAT_NV12;
        CL2DFlexConfig.ROIs[i].x = 64;
        CL2DFlexConfig.ROIs[i].y = 64;
        CL2DFlexConfig.ROIs[i].width = 64;
        CL2DFlexConfig.ROIs[i].height = 64;
    }
    CL2DFlexConfig.outputWidth = 64;
    CL2DFlexConfig.outputHeight = 64;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;

    ImageProps_t imgPropInputs[CL2DFlexConfig.numOfInputs];
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].height = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].format = CL2DFlexConfig.inputFormats[i];
        imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].stride[1] = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].actualHeight[1] = CL2DFlexConfig.inputHeights[i] / 2;
        imgPropInputs[i].planeBufSize[0] = 0;
        imgPropInputs[i].planeBufSize[1] = 0;
        imgPropInputs[i].numPlanes = 2;
    }

    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = CL2DFlexConfig.numOfInputs;
    imgPropOutput.width = CL2DFlexConfig.outputWidth;
    imgPropOutput.height = CL2DFlexConfig.outputHeight;
    imgPropOutput.format = CL2DFlexConfig.outputFormat;
    imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth * 3;
    imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
    imgPropOutput.planeBufSize[0] = 0;
    imgPropOutput.numPlanes = 1;

    uint32_t globalIdx = 0;
    NodeFrameDescriptor frameDesc( CL2DFlexConfig.numOfInputs + 1 );
    std::vector<ImageDescriptor_t> inputs;
    inputs.reserve( CL2DFlexConfig.numOfInputs );
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        ImageDescriptor_t imageDesc;
        ret = bufMgr.Allocate( imgPropInputs[i], imageDesc );
        EXPECT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( imageDesc );
        ret = frameDesc.SetBuffer( globalIdx, inputs.back() );
        EXPECT_EQ( QC_STATUS_OK, ret );
        globalIdx++;
    }
    std::vector<ImageDescriptor_t> outputs;
    outputs.reserve( 1 );
    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgPropOutput, imageDesc );
    outputs.push_back( imageDesc );
    ret = frameDesc.SetBuffer( globalIdx, outputs.back() );
    EXPECT_EQ( QC_STATUS_OK, ret );
    globalIdx++;

    // output format not match
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_NV12;
    QCNodeIfs *pCL2DFlex1 = new QC::Node::CL2DFlex();
    DataTree dt1;
    dt1.Set<std::string>( "static.name", "CL2D" );
    dt1.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt1 );
    QCNodeInit_t config1 = { dt1.Dump() };
    ret = pCL2DFlex1->Initialize( config1 );
    ret = pCL2DFlex1->Start();
    ret = pCL2DFlex1->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex1 )->~CL2DFlex();
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;

    // output height not match
    CL2DFlexConfig.outputHeight = 32;
    QCNodeIfs *pCL2DFlex2 = new QC::Node::CL2DFlex();
    DataTree dt2;
    dt2.Set<std::string>( "static.name", "CL2D" );
    dt2.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt2 );
    QCNodeInit_t config2 = { dt2.Dump() };
    ret = pCL2DFlex2->Initialize( config2 );
    ret = pCL2DFlex2->Start();
    ret = pCL2DFlex2->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex2 )->~CL2DFlex();
    CL2DFlexConfig.outputHeight = 64;

    // output width not match
    CL2DFlexConfig.outputWidth = 32;
    QCNodeIfs *pCL2DFlex3 = new QC::Node::CL2DFlex();
    DataTree dt3;
    dt3.Set<std::string>( "static.name", "CL2D" );
    dt3.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt3 );
    QCNodeInit_t config3 = { dt3.Dump() };
    ret = pCL2DFlex3->Initialize( config3 );
    ret = pCL2DFlex3->Start();
    ret = pCL2DFlex3->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex3 )->~CL2DFlex();
    CL2DFlexConfig.outputWidth = 64;

    // input format not match
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_RGB888;
    QCNodeIfs *pCL2DFlex4 = new QC::Node::CL2DFlex();
    DataTree dt4;
    dt4.Set<std::string>( "static.name", "CL2D" );
    dt4.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt4 );
    QCNodeInit_t config4 = { dt4.Dump() };
    ret = pCL2DFlex4->Initialize( config4 );
    ret = pCL2DFlex4->Start();
    ret = pCL2DFlex4->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex4 )->~CL2DFlex();
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;

    // input height not match
    CL2DFlexConfig.inputHeights[0] = 64;
    QCNodeIfs *pCL2DFlex5 = new QC::Node::CL2DFlex();
    DataTree dt5;
    dt5.Set<std::string>( "static.name", "CL2D" );
    dt5.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt5 );
    QCNodeInit_t config5 = { dt5.Dump() };
    ret = pCL2DFlex5->Initialize( config5 );
    ret = pCL2DFlex5->Start();
    ret = pCL2DFlex5->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex5 )->~CL2DFlex();
    CL2DFlexConfig.inputHeights[0] = 128;

    // input width not match
    CL2DFlexConfig.inputWidths[0] = 64;
    QCNodeIfs *pCL2DFlex6 = new QC::Node::CL2DFlex();
    DataTree dt6;
    dt6.Set<std::string>( "static.name", "CL2D" );
    dt6.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt6 );
    QCNodeInit_t config6 = { dt6.Dump() };
    ret = pCL2DFlex6->Initialize( config6 );
    ret = pCL2DFlex6->Start();
    ret = pCL2DFlex6->ProcessFrameDescriptor( frameDesc );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    reinterpret_cast<QC::Node::CL2DFlex *>( pCL2DFlex6 )->~CL2DFlex();
    CL2DFlexConfig.inputWidths[0] = 128;

    QCNodeIfs *pCL2DFlex7 = new QC::Node::CL2DFlex();
    DataTree dt7;
    dt7.Set<std::string>( "static.name", "CL2D" );
    dt7.Set<uint32_t>( "static.id", 0 );
    SetConfigCL2D( &CL2DFlexConfig, &dt7 );
    QCNodeInit_t config7 = { dt7.Dump() };
    QCNodeID_t nodeId;
    Logger logger;
    CL2DFlexImpl *pCL2DFlexImpl = new CL2DFlexImpl( nodeId, logger );
    CL2DFlexImplConfig_t &config = pCL2DFlexImpl->GetConifg();
    config.params.deviceId = 0;
    config.params.priority = OPENCLIFACE_PERF_NORMAL;
    config.params.numOfInputs = 1;
    config.params.workModes[0] = CL2DFLEX_WORK_MODE_MAX;
    ret = pCL2DFlexImpl->Initialize( config7.buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    for ( auto imageDesc : inputs )
    {
        ret = bufMgr.Free( imageDesc );
        EXPECT_EQ( QC_STATUS_OK, ret );
    }

    for ( auto imageDesc : outputs )
    {
        ret = bufMgr.Free( imageDesc );
        EXPECT_EQ( QC_STATUS_OK, ret );
    }
}

void Coverage3()
{
    QCStatus_e ret;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    OpenclSrv OpenclSrvObj;
    char pName[20] = "CL2DFlex";

    ret = OpenclSrvObj.Init( pName, LOGGER_LEVEL_ERROR, OPENCLIFACE_PERF_NORMAL,
                             100 );   // init with invalid device id
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    ret = OpenclSrvObj.Init( pName, LOGGER_LEVEL_ERROR, (OpenclIfcae_Perf_e) 100,
                             0 );   // init with invalid performance level
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    ret = OpenclSrvObj.Init( pName, LOGGER_LEVEL_MAX,
                             OPENCLIFACE_PERF_LOW );   // success init OpenclSrv with low priority
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.LoadFromSource( nullptr );   // create program with null source
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    cl_kernel kernel;
    ret = OpenclSrvObj.CreateKernel( &kernel,
                                     "" );   // create kernel with null name
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    ret = OpenclSrvObj.LoadFromBinary(
            (const unsigned char *) "" );   // create program with null binary
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    ret = OpenclSrvObj.LoadFromSource( s_pSourceInvalid );   // create program with null source
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    ret = OpenclSrvObj.RegImage(
            nullptr, 0, (cl_mem *) nullptr, (cl_image_format *) nullptr,
            (cl_image_desc *) nullptr );   // register image with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    ret = OpenclSrvObj.RegPlane(
            nullptr, (cl_mem *) nullptr, (cl_image_format *) nullptr,
            (cl_image_desc *) nullptr );   // register plane with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    QCBufferDescriptorBase_t buffer;
    cl_mem clMem;
    ret = OpenclSrvObj.RegBufferDesc( buffer,
                                      clMem );   // register buffer descriptor with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    ret = OpenclSrvObj.DeregImage( nullptr );   // deregister image with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    ret = OpenclSrvObj.DeregPlane(
            nullptr, (cl_image_format *) nullptr );   // deregister plane with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    ret = OpenclSrvObj.DeregBufferDesc(
            buffer );   // deregister buffer descriptor with null host buffer
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    ImageProps_t imgProp;
    imgProp.batchSize = 2;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_RGB888;
    imgProp.stride[0] = imgProp.width * 3;
    imgProp.actualHeight[0] = imgProp.height;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;
    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgProp, imageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCL;
    ret = OpenclSrvObj.RegBufferDesc( imageDesc, bufferCL );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.RegBufferDesc( imageDesc, bufferCL );   // register bufferdesc twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.DeregBufferDesc( imageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.DeregBufferDesc( imageDesc );   // dereg bufferdesc twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.RegBufferDesc( imageDesc, bufferCL );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLImage;
    cl_image_format inputImageFormat = { 0 };
    cl_image_desc inputImageDesc = { 0 };
    inputImageDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputImageDesc.image_width = 64;
    inputImageDesc.image_height = 64;
    ret = OpenclSrvObj.RegImage( imageDesc.pBuf, 0, &bufferCLImage, &inputImageFormat,
                                 &inputImageDesc );
    EXPECT_EQ( QC_STATUS_FAIL, ret );   // register bufferimage with null format

    inputImageFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12;
    inputImageFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = OpenclSrvObj.RegImage( imageDesc.pBuf, imageDesc.dmaHandle, &bufferCLImage,
                                 &inputImageFormat, &inputImageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.RegImage( imageDesc.pBuf, imageDesc.dmaHandle, &bufferCLImage,
                                 &inputImageFormat,
                                 &inputImageDesc );   // register bufferimage twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLPlane;
    cl_image_format inputYFormat = { 0 };
    cl_image_desc inputYDesc = { 0 };
    inputYDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputYDesc.image_width = 64;
    inputYDesc.image_height = 64;
    inputYDesc.mem_object = bufferCLImage;
    ret = OpenclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat, &inputYDesc );
    EXPECT_EQ( QC_STATUS_FAIL, ret );   // register bufferplane with null format

    inputYFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12_Y;
    inputYFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = OpenclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat, &inputYDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat,
                                 &inputYDesc );   // register bufferplane twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.DeregImage( imageDesc.pBuf );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.DeregImage( imageDesc.pBuf );   // dereg image twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.DeregPlane( imageDesc.pBuf, &inputYFormat );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.DeregPlane( imageDesc.pBuf, &inputYFormat );   // dereg plane twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    inputImageFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12;
    inputImageFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = OpenclSrvObj.RegImage( imageDesc.pBuf, imageDesc.dmaHandle, &bufferCLImage,
                                 &inputImageFormat, &inputImageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );
    inputYFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12_Y;
    inputYFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = OpenclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat, &inputYDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.DeregAllBuffers();   // deregister all buffers
    EXPECT_EQ( QC_STATUS_OK, ret );

    OpenclIfcae_Arg_t OpenclArg;
    OpenclArg.pArg = nullptr;
    OpenclArg.argSize = 0;
    OpenclIface_WorkParams_t OpenclWorkParams;
    ret = OpenclSrvObj.Execute( &kernel, &OpenclArg, 1,
                                &OpenclWorkParams );   // execute with null args
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    OpenclArg.pArg = (void *) &bufferCL;
    OpenclArg.argSize = sizeof( bufferCL );
    OpenclWorkParams.workDim = 0;
    OpenclWorkParams.pGlobalWorkSize = nullptr;
    OpenclWorkParams.pGlobalWorkOffset = nullptr;
    OpenclWorkParams.pLocalWorkSize = nullptr;
    ret = OpenclSrvObj.Execute( &kernel, &OpenclArg, 1,
                                &OpenclWorkParams );   // execute with null work params
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    OpenclArg.pArg = (void *) &bufferCL;
    OpenclArg.argSize = sizeof( bufferCL );
    OpenclWorkParams.workDim = 2;
    size_t globalWorkSize[2] = { 64, 64 };
    OpenclWorkParams.pGlobalWorkSize = globalWorkSize;
    size_t globalWorkOffset[2] = { 0, 0 };
    OpenclWorkParams.pGlobalWorkOffset = globalWorkOffset;
    OpenclWorkParams.pLocalWorkSize = NULL;
    ret = OpenclSrvObj.Execute( &kernel, &OpenclArg, 0,
                                &OpenclWorkParams );   // execute with invalid kernel
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    ret = OpenclSrvObj.LoadFromSource( s_pSourceCL2DFlex );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.CreateKernel( &kernel, "RemapNV12ToRGB" );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.CreateKernel( &kernel, "RemapNV12ToRGB" );   // create kernel twice
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.DeregAllBuffers();   // DeregAllBuffers after init
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.Deinit();   // success to deinit
    EXPECT_EQ( QC_STATUS_OK, ret );

    ret = OpenclSrvObj.Deinit();   // deinit without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.DeregAllBuffers();   // DeregAllBuffers without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.LoadFromSource( nullptr );   // create program without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.CreateKernel( &kernel,
                                     "" );   // create kernel without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.LoadFromBinary(
            (const unsigned char *) "" );   // create program without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.RegImage( nullptr, 0, (cl_mem *) nullptr, (cl_image_format *) nullptr,
                                 (cl_image_desc *) nullptr );   // register image without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.RegPlane( nullptr, (cl_mem *) nullptr, (cl_image_format *) nullptr,
                                 (cl_image_desc *) nullptr );   // register plane without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.RegBufferDesc( buffer,
                                      clMem );   // register buffer descriptor without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.DeregImage( nullptr );   // deregister image without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.DeregPlane( nullptr,
                                   (cl_image_format *) nullptr );   // deregister plane without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.DeregBufferDesc( buffer );   // deregister buffer descriptor without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.Execute( &kernel, &OpenclArg, 1,
                                &OpenclWorkParams );   // execute without init
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    ret = OpenclSrvObj.Init( pName, LOGGER_LEVEL_MAX,
                             OPENCLIFACE_PERF_LOW );   // success init OpenclSrv
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = OpenclSrvObj.Init( pName, LOGGER_LEVEL_MAX,
                             OPENCLIFACE_PERF_LOW );   // init twice
    EXPECT_EQ( QC_STATUS_BAD_STATE, ret );

    (void) bufMgr.Free( imageDesc );
}

void Coverage4()
{
    QCStatus_e ret;
    std::string errors;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    CL2DFlex_Config_t CL2DFlexConfig;
    CL2DFlexConfig.numOfInputs = 1;
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        CL2DFlexConfig.workModes[i] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;
        CL2DFlexConfig.inputWidths[i] = 128;
        CL2DFlexConfig.inputHeights[i] = 128;
        CL2DFlexConfig.inputFormats[i] = QC_IMAGE_FORMAT_RGB888;
        CL2DFlexConfig.ROIs[i].x = 64;
        CL2DFlexConfig.ROIs[i].y = 64;
        CL2DFlexConfig.ROIs[i].width = 64;
        CL2DFlexConfig.ROIs[i].height = 64;
    }
    CL2DFlexConfig.outputWidth = 64;
    CL2DFlexConfig.outputHeight = 64;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_UYVY;

    CL2DPipelineConvert *pCL2DPipelineConvert = nullptr;
    pCL2DPipelineConvert = new CL2DPipelineConvert();
    CL2DPipelineResize *pCL2DPipelineResize = nullptr;
    pCL2DPipelineResize = new CL2DPipelineResize();
    CL2DPipelineLetterbox *pCL2DPipelineLetterbox = nullptr;
    pCL2DPipelineLetterbox = new CL2DPipelineLetterbox();
    CL2DPipelineConvertUBWC *pCL2DPipelineConvertUBWC = nullptr;
    pCL2DPipelineConvertUBWC = new CL2DPipelineConvertUBWC();
    CL2DPipelineLetterboxMultiple *pCL2DPipelineLetterboxMultiple = nullptr;
    pCL2DPipelineLetterboxMultiple = new CL2DPipelineLetterboxMultiple();
    CL2DPipelineResizeMultiple *pCL2DPipelineResizeMultiple = nullptr;
    pCL2DPipelineResizeMultiple = new CL2DPipelineResizeMultiple();
    CL2DPipelineRemap *pCL2DPipelineRemap = nullptr;
    pCL2DPipelineRemap = new CL2DPipelineRemap();

    pCL2DPipelineConvert->InitLogger( "Convert", LOGGER_LEVEL_MAX );   // invalid logger init
    pCL2DPipelineConvert->DeinitLogger();                              // invalid logger deinit

    OpenclSrv openclSrvObj;
    cl_kernel kernel;
    ret = openclSrvObj.Init( "Opencl", LOGGER_LEVEL_ERROR, OPENCLIFACE_PERF_NORMAL, 0 );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = openclSrvObj.LoadFromSource( s_pSourceCL2DFlex );
    EXPECT_EQ( QC_STATUS_OK, ret );
    std::vector<std::reference_wrapper<QCBufferDescriptorBase>> buffers;

    // not support pipeline
    ret = pCL2DPipelineConvert->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineLetterbox->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineConvertUBWC->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineLetterboxMultiple->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj,
                                                buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineResizeMultiple->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineRemap->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    CL2DFlexConfig.outputWidth = 128;
    CL2DFlexConfig.outputHeight = 128;
    CL2DFlexConfig.numOfInputs = 2;
    CL2DFlexConfig.inputFormats[1] = QC_IMAGE_FORMAT_NV12;
    ret = pCL2DPipelineConvert->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineConvertUBWC->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineLetterboxMultiple->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj,
                                                buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineResizeMultiple->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    CL2DFlexConfig.outputWidth = 64;
    CL2DFlexConfig.outputHeight = 64;
    CL2DFlexConfig.numOfInputs = 1;

    ImageProps_t imgPropInputs[CL2DFlexConfig.numOfInputs];
    for ( int i = 0; i < CL2DFlexConfig.numOfInputs; i++ )
    {
        imgPropInputs[i].batchSize = 1;
        imgPropInputs[i].width = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].height = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].format = CL2DFlexConfig.inputFormats[i];
        imgPropInputs[i].stride[0] = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].stride[1] = CL2DFlexConfig.inputWidths[i];
        imgPropInputs[i].actualHeight[0] = CL2DFlexConfig.inputHeights[i];
        imgPropInputs[i].actualHeight[1] = CL2DFlexConfig.inputHeights[i] / 2;
        imgPropInputs[i].planeBufSize[0] = 0;
        imgPropInputs[i].planeBufSize[1] = 0;
        imgPropInputs[i].numPlanes = 2;
    }
    ImageProps_t imgPropOutput;
    imgPropOutput.batchSize = CL2DFlexConfig.numOfInputs;
    imgPropOutput.width = CL2DFlexConfig.outputWidth;
    imgPropOutput.height = CL2DFlexConfig.outputHeight;
    imgPropOutput.format = CL2DFlexConfig.outputFormat;
    imgPropOutput.stride[0] = CL2DFlexConfig.outputWidth * 3;
    imgPropOutput.actualHeight[0] = CL2DFlexConfig.outputHeight;
    imgPropOutput.planeBufSize[0] = 0;
    imgPropOutput.numPlanes = 1;
    ImageDescriptor_t imageDescInput;
    ret = bufMgr.Allocate( imgPropInputs[0], imageDescInput );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ImageDescriptor_t imageDescOutput;
    ret = bufMgr.Allocate( imgPropOutput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_OK, ret );

    // invalid pipeline
    ret = pCL2DPipelineConvert->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineLetterbox->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineConvertUBWC->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineLetterboxMultiple->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineResizeMultiple->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ret = pCL2DPipelineRemap->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    // null input or output pointer
    ImageDescriptor_t imageDescNull;
    ret = pCL2DPipelineConvert->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineLetterbox->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineLetterboxMultiple->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineResizeMultiple->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineRemap->Execute( imageDescInput, imageDescNull );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineConvert->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineResize->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineLetterbox->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineLetterboxMultiple->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineResizeMultiple->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );
    ret = pCL2DPipelineRemap->Execute( imageDescNull, imageDescOutput );
    EXPECT_EQ( QC_STATUS_NULL_PTR, ret );

    // fail to execute convert pipeline
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineConvert->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineConvert->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_UYVY;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineConvert->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineConvert->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_UYVY;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_NV12;
    ret = pCL2DPipelineConvert->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineConvert->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    // fail to execute resize pipeline
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_UYVY;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_UYVY;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_NV12;
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_RGB888;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_NV12;
    ret = pCL2DPipelineResize->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineResize->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    // fail to execute letterbox pipeline
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    ret = pCL2DPipelineLetterbox->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineLetterbox->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    // fail for remap pipeline
    CL2DFlexConfig.inputFormats[0] = QC_IMAGE_FORMAT_NV12;
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_RGB888;
    TensorDescriptor_t mapBufferDesc;
    TensorProps_t mapProp = { QC_TENSOR_TYPE_FLOAT_32,
                              { CL2DFlexConfig.outputHeight * CL2DFlexConfig.outputWidth } };
    ret = bufMgr.Allocate( mapProp, mapBufferDesc );
    ASSERT_EQ( QC_STATUS_OK, ret );
    buffers.push_back( mapBufferDesc );
    CL2DFlexConfig.remapTable[0].mapXBufferId = 0;
    CL2DFlexConfig.remapTable[0].mapYBufferId = 0;
    ret = pCL2DPipelineRemap->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineRemap->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    CL2DFlexConfig.outputFormat = QC_IMAGE_FORMAT_BGR888;
    ret = pCL2DPipelineRemap->Init( 0, &kernel, &CL2DFlexConfig, &openclSrvObj, buffers );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = pCL2DPipelineRemap->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_OK, ret );
    kernel = nullptr;
    ret = pCL2DPipelineRemap->Execute( imageDescInput, imageDescOutput );
    EXPECT_EQ( QC_STATUS_FAIL, ret );

    (void) openclSrvObj.Deinit();
    (void) bufMgr.Free( imageDescInput );
    (void) bufMgr.Free( imageDescOutput );
    (void) bufMgr.Free( mapBufferDesc );
    delete pCL2DPipelineConvert;
    delete pCL2DPipelineResize;
    delete pCL2DPipelineLetterbox;
    delete pCL2DPipelineConvertUBWC;
    delete pCL2DPipelineLetterboxMultiple;
    delete pCL2DPipelineResizeMultiple;
    delete pCL2DPipelineRemap;
}

void CoverageOpenCLReleaseMock_DeregAllBuffers()
{
    QCStatus_e ret;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    OpenclSrv openclSrvObj;
    ret = openclSrvObj.Init( "Opencl", LOGGER_LEVEL_MAX, OPENCLIFACE_PERF_NORMAL, 0 );
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Prepare a registered buffer, image and plane to populate internal maps
    ImageProps_t imgProp;
    imgProp.batchSize = 2;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_RGB888;
    imgProp.stride[0] = imgProp.width * 3;
    imgProp.actualHeight[0] = imgProp.height;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;

    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgProp, imageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCL;
    ret = openclSrvObj.RegBufferDesc( imageDesc, bufferCL );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLImage;
    cl_image_format inputImageFormat = { 0 };
    cl_image_desc inputImageDesc = { 0 };
    inputImageDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputImageDesc.image_width = 64;
    inputImageDesc.image_height = 64;
    inputImageFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12;
    inputImageFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = openclSrvObj.RegImage( imageDesc.pBuf, imageDesc.dmaHandle, &bufferCLImage,
                                 &inputImageFormat, &inputImageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLPlane;
    cl_image_format inputYFormat = { 0 };
    cl_image_desc inputYDesc = { 0 };
    inputYDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputYDesc.image_width = 64;
    inputYDesc.image_height = 64;
    inputYDesc.mem_object = bufferCLImage;
    inputYFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12_Y;
    inputYFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = openclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat, &inputYDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Force clReleaseMemObject to fail to cover error branches in DeregAllBuffers
    MockOpenCL_ControlReleaseMemObject( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    ret = openclSrvObj.DeregAllBuffers();
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    // Reset mock
    MockOpenCL_ControlReleaseMemObject( MOCK_CL_RELEASE_NONE, 0 );
    (void) bufMgr.Free( imageDesc );
}

void CoverageOpenCLReleaseMock_Deinit()
{
    QCStatus_e ret;
    BufferManager bufMgr( { "MANAGER", QC_NODE_TYPE_CL_2D_FLEX, 0 } );

    OpenclSrv openclSrvObj;
    ret = openclSrvObj.Init( "Opencl", LOGGER_LEVEL_MAX, OPENCLIFACE_PERF_NORMAL, 0 );
    EXPECT_EQ( QC_STATUS_OK, ret );
    ret = openclSrvObj.LoadFromSource( s_pSourceCL2DFlex );
    EXPECT_EQ( QC_STATUS_OK, ret );
    cl_kernel kernel;
    ret = openclSrvObj.CreateKernel( &kernel, "RemapNV12ToRGB" );
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Prepare registered objects again
    ImageProps_t imgProp;
    imgProp.batchSize = 2;
    imgProp.width = 64;
    imgProp.height = 64;
    imgProp.format = QC_IMAGE_FORMAT_RGB888;
    imgProp.stride[0] = imgProp.width * 3;
    imgProp.actualHeight[0] = imgProp.height;
    imgProp.planeBufSize[0] = 0;
    imgProp.numPlanes = 1;

    ImageDescriptor_t imageDesc;
    ret = bufMgr.Allocate( imgProp, imageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCL;
    ret = openclSrvObj.RegBufferDesc( imageDesc, bufferCL );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLImage;
    cl_image_format inputImageFormat = { 0 };
    cl_image_desc inputImageDesc = { 0 };
    inputImageDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputImageDesc.image_width = 64;
    inputImageDesc.image_height = 64;
    inputImageFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12;
    inputImageFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = openclSrvObj.RegImage( imageDesc.pBuf, imageDesc.dmaHandle, &bufferCLImage,
                                 &inputImageFormat, &inputImageDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    cl_mem bufferCLPlane;
    cl_image_format inputYFormat = { 0 };
    cl_image_desc inputYDesc = { 0 };
    inputYDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
    inputYDesc.image_width = 64;
    inputYDesc.image_height = 64;
    inputYDesc.mem_object = bufferCLImage;
    inputYFormat.image_channel_order = CL_QCOM_COMPRESSED_NV12_Y;
    inputYFormat.image_channel_data_type = CL_UNORM_INT8;
    ret = openclSrvObj.RegPlane( imageDesc.pBuf, &bufferCLPlane, &inputYFormat, &inputYDesc );
    EXPECT_EQ( QC_STATUS_OK, ret );

    // Force clReleaseMemObject to fail to cover error branches in Deinit
    MockOpenCL_ControlReleaseMemObject( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    // Force clReleaseKernel to fail to cover error branch in Deinit
    MockOpenCL_ControlReleaseKernel( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    // Force clReleaseProgram to fail to cover error branch in Deinit
    MockOpenCL_ControlReleaseProgram( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    // Force clReleaseCommandQueue to fail to cover error branch in Deinit
    MockOpenCL_ControlReleaseCommandQueue( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    // Force clReleaseContext to fail to cover error branch in Deinit
    MockOpenCL_ControlReleaseContext( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );
    // Force clReleaseSampler to fail to cover error branch in Deinit
    MockOpenCL_ControlReleaseSampler( MOCK_CL_RELEASE_ALWAYS_FAIL, 0 );

    ret = openclSrvObj.Deinit();
    EXPECT_EQ( QC_STATUS_FAIL, ret );
    // Reset mock
    MockOpenCL_ControlReleaseMemObject( MOCK_CL_RELEASE_NONE, 0 );
    // Reset kernel release mock
    MockOpenCL_ControlReleaseKernel( MOCK_CL_RELEASE_NONE, 0 );
    // Reset program release mock
    MockOpenCL_ControlReleaseProgram( MOCK_CL_RELEASE_NONE, 0 );
    // Reset command queue release mock
    MockOpenCL_ControlReleaseCommandQueue( MOCK_CL_RELEASE_NONE, 0 );
    // Reset context release mock
    MockOpenCL_ControlReleaseContext( MOCK_CL_RELEASE_NONE, 0 );
    // Reset sampler release mock
    MockOpenCL_ControlReleaseSampler( MOCK_CL_RELEASE_NONE, 0 );

    (void) bufMgr.Free( imageDesc );
}

TEST( NodeCL2D, Sanity )
{
    Sanity();
}

TEST( NodeCL2D, Stress )
{
    uint32_t loopNumber = 100;
    const char *envValue = getenv( "CL2DFLEX_TEST_LOOP_NUMBER" );
    if ( nullptr != envValue )
    {
        loopNumber = (uint32_t) atoi( envValue );
    }
    for ( uint32_t l = 0; l < loopNumber; l++ )
    {
        printf( "\nloopNumber = %d", l );
        Sanity();
    }
}

TEST( NodeCL2D, Coverage1 )
{
    printf( "\ncoverage test 1\n" );
    Coverage1();
}

TEST( NodeCL2D, Coverage2 )
{
    printf( "\ncoverage test 2\n" );
    Coverage2();
}

TEST( NodeCL2D, Coverage3 )
{
    printf( "\ncoverage test 3\n" );
    Coverage3();
}

TEST( NodeCL2D, Coverage4 )
{
    printf( "\ncoverage test 4\n" );
    Coverage4();
}

TEST( NodeCL2D, Coverage5 )
{
    printf( "\ncoverage test 5\n" );
    CoverageOpenCLReleaseMock_DeregAllBuffers();
}

TEST( NodeCL2D, Coverage6 )
{
    printf( "\ncoverage test 6\n" );
    CoverageOpenCLReleaseMock_Deinit();
}

// md5 of 0.nv12 is a1591f4b8c196a47628f0ef6bc3a721c
// md5 of 0.uyvy is 5b1ae2203a9d97aeafe65e997f3beebc
// md5 of 0.ubwc is ce5f81f72f9c0ec0c347b1ee55d13584
// md5 of golden1.rgb is 4b528d54b7c5d5164f66719a89f988be
// md5 of golden2.rgb is f94a6aeae302add0424821660bcd2684
// md5 of golden3.nv12 is 91ed68589443b87bcfff8ae7e69b03b2
// md5 of golden4.rgb is 9382129ca960b8ff22e3c135e3eb12b7
// md5 of golden5.rgb is 520d1039f96107ef4db669e9644250fd
// md5 of golden6.nv12 is 91cdd0def0f40ce3c0fec070c2bccd01
// md5 of golden7.rgb is a906c3bc49c7b91ec25d6474311d8031
// md5 of golden8.nv12 is 43d33b3417b0b53c594269e5df95e035
// md5 of golden9.rgb is cdb33ad92de656964c3be1d67fff25fb
// md5 of golden10.nv12 is e5396625f90a049b625b65bc1fdf8183

TEST( NodeCL2D, Accuracy )
{
    Accuracy( 1, CL2DFLEX_WORK_MODE_CONVERT, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888, 1920,
              1024, 1920, 1024, "./data/test/CL2DFlex/0.nv12", "./data/test/CL2DFlex/golden1.rgb",
              false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_CONVERT, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888, 1920,
              1024, 1920, 1024, "./data/test/CL2DFlex/0.uyvy", "./data/test/CL2DFlex/golden2.rgb",
              false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_CONVERT, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_NV12, 1920, 1024,
              1920, 1024, "./data/test/CL2DFlex/0.uyvy", "./data/test/CL2DFlex/golden3.nv12",
              false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_RESIZE_NEAREST, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12",
              "./data/test/CL2DFlex/golden4.rgb", false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_RESIZE_NEAREST, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_RGB888,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.uyvy",
              "./data/test/CL2DFlex/golden5.rgb", false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_RESIZE_NEAREST, QC_IMAGE_FORMAT_UYVY, QC_IMAGE_FORMAT_NV12,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.uyvy",
              "./data/test/CL2DFlex/golden6.nv12", false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_RESIZE_NEAREST, QC_IMAGE_FORMAT_RGB888, QC_IMAGE_FORMAT_RGB888,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/golden1.rgb",
              "./data/test/CL2DFlex/golden4.rgb", false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_RESIZE_NEAREST, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_NV12,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12",
              "./data/test/CL2DFlex/golden10.nv12", false );
    Accuracy( 1, CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12",
              "./data/test/CL2DFlex/golden7.rgb", false );
}

TEST( NodeCL2D, RemapAccuracy )
{
    Accuracy( 1, CL2DFLEX_WORK_MODE_REMAP_NEAREST, QC_IMAGE_FORMAT_NV12, QC_IMAGE_FORMAT_RGB888,
              1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12",
              "./data/test/CL2DFlex/golden9.rgb", false );
}

TEST( NodeCL2D, UBWCAccuracy )
{
    Accuracy( 1, CL2DFLEX_WORK_MODE_CONVERT_UBWC, QC_IMAGE_FORMAT_NV12_UBWC, QC_IMAGE_FORMAT_NV12,
              3840, 2160, 3840, 2160, "./data/test/CL2DFlex/0.ubwc",
              "./data/test/CL2DFlex/golden8.nv12", false );
}

TEST( NodeCL2D, MultipleROI )
{
    MultipleROI( CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE, QC_IMAGE_FORMAT_NV12,
                 QC_IMAGE_FORMAT_RGB888, 1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12" );
    MultipleROI( CL2DFLEX_WORK_MODE_RESIZE_NEAREST_MULTIPLE, QC_IMAGE_FORMAT_NV12,
                 QC_IMAGE_FORMAT_RGB888, 1920, 1024, 1152, 800, "./data/test/CL2DFlex/0.nv12" );
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
