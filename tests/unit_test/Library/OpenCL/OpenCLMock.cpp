// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file OpenCLMock.cpp
 * @brief Mock implementation for OpenCL C APIs used by OpenclIface
 *
 * This file provides mock implementations for OpenCL C APIs from cl.h that are
 * called by the OpenclSrv wrapper class. It allows controlled failure injection
 * for comprehensive code coverage testing.
 *
 * Usage:
 * 1. Link this mock library with your test binary
 * 2. Use MockApi_CL_Control() to inject failures before calling the API under test
 * 3. The mock will override the real implementation behavior for that single call
 *
 * Example:
 *   cl_int failStatus = CL_INVALID_VALUE;
 *   MockApi_CL_Control(MOCK_API_CL_CREATE_CONTEXT, MOCK_CONTROL_CL_RETURN, &failStatus);
 *   // Next call to clCreateContext() will return CL_INVALID_VALUE
 */

#include "OpenCLMock.h"
#include <CL/cl.h>
#include <cstring>
#include <dlfcn.h>
#include <iostream>

// ============================================================================
// MOCK CONTROL STRUCTURE
// ============================================================================

/**
 * @brief Control parameters for mock behavior
 */
typedef struct
{
    MockAPI_CL_Action_e action;   ///< Action to perform
    void *param;                  ///< Parameter for the action
} MockControlParam_CL_t;

static MockControlParam_CL_t s_MockParams_CL[MOCK_API_CL_MAX];

// ============================================================================
// MOCK CONTROL FUNCTIONS
// ============================================================================

static int s_kenerlCnt = 0;
int g_createBffrCall = MOCK_API_CL_CREATE_BUFFER_B1;
extern "C" void MockApi_CL_Control( MockAPI_CL_ID_e apiId, MockAPI_CL_Action_e action, void *param )
{
    if ( apiId < MOCK_API_CL_MAX )
    {
        s_MockParams_CL[apiId].action = action;
        s_MockParams_CL[apiId].param = param;
        s_kenerlCnt = 0;
    }
}

extern "C" void MockApi_CL_ResetAll()
{
    for ( int i = 0; i < MOCK_API_CL_MAX; i++ )
    {
        s_MockParams_CL[i].action = MOCK_CONTROL_CL_NONE;
        s_MockParams_CL[i].param = nullptr;
        s_kenerlCnt = 0;
    }
}

// ============================================================================
// DYNAMIC LIBRARY LOADING
// ============================================================================

static void *s_hOpenCLDll = nullptr;

// Function pointer types for OpenCL C APIs
typedef cl_int ( *clGetPlatformIDs_t )( cl_uint, cl_platform_id *, cl_uint * );
typedef cl_int ( *clGetDeviceIDs_t )( cl_platform_id, cl_device_type, cl_uint, cl_device_id *,
                                      cl_uint * );
typedef cl_context ( *clCreateContext_t )(
        const cl_context_properties *, cl_uint, const cl_device_id *,
        void( CL_CALLBACK * )( const char *, const void *, size_t, void * ), void *, cl_int * );
typedef cl_command_queue ( *clCreateCommandQueueWithProperties_t )( cl_context, cl_device_id,
                                                                    const cl_queue_properties *,
                                                                    cl_int * );
typedef cl_int ( *clGetDeviceInfo_t )( cl_device_id, cl_device_info, size_t, void *, size_t * );
typedef cl_program ( *clCreateProgramWithSource_t )( cl_context, cl_uint, const char **,
                                                     const size_t *, cl_int * );
typedef cl_program ( *clCreateProgramWithBinary_t )( cl_context, cl_uint, const cl_device_id *,
                                                     const size_t *, const unsigned char **,
                                                     cl_int *, cl_int * );
typedef cl_int ( *clBuildProgram_t )( cl_program, cl_uint, const cl_device_id *, const char *,
                                      void( CL_CALLBACK * )( cl_program, void * ), void * );
typedef cl_int ( *clGetProgramBuildInfo_t )( cl_program, cl_device_id, cl_program_build_info,
                                             size_t, void *, size_t * );
typedef cl_kernel ( *clCreateKernel_t )( cl_program, const char *, cl_int * );
typedef cl_int ( *clReleaseKernel_t )( cl_kernel );
typedef cl_int ( *clReleaseProgram_t )( cl_program );
typedef cl_mem ( *clCreateBuffer_t )( cl_context, cl_mem_flags, size_t, void *, cl_int * );
typedef cl_mem ( *clCreateImage_t )( cl_context, cl_mem_flags, const cl_image_format *,
                                     const cl_image_desc *, void *, cl_int * );
typedef cl_int ( *clReleaseMemObject_t )( cl_mem );
typedef cl_sampler ( *clCreateSamplerWithProperties_t )( cl_context, const cl_sampler_properties *,
                                                         cl_int * );
typedef cl_int ( *clReleaseSampler_t )( cl_sampler );
typedef cl_int ( *clSetKernelArg_t )( cl_kernel, cl_uint, size_t, const void * );
typedef cl_int ( *clEnqueueNDRangeKernel_t )( cl_command_queue, cl_kernel, cl_uint, const size_t *,
                                              const size_t *, const size_t *, cl_uint,
                                              const cl_event *, cl_event * );
typedef cl_int ( *clFinish_t )( cl_command_queue );
typedef cl_int ( *clReleaseCommandQueue_t )( cl_command_queue );
typedef cl_int ( *clReleaseContext_t )( cl_context );

// Function pointers for real implementations
static clGetPlatformIDs_t clGetPlatformIDs_real = nullptr;
static clGetDeviceIDs_t clGetDeviceIDs_real = nullptr;
static clCreateContext_t clCreateContext_real = nullptr;
static clCreateCommandQueueWithProperties_t clCreateCommandQueueWithProperties_real = nullptr;
static clGetDeviceInfo_t clGetDeviceInfo_real = nullptr;
static clCreateProgramWithSource_t clCreateProgramWithSource_real = nullptr;
static clCreateProgramWithBinary_t clCreateProgramWithBinary_real = nullptr;
static clBuildProgram_t clBuildProgram_real = nullptr;
static clGetProgramBuildInfo_t clGetProgramBuildInfo_real = nullptr;
static clCreateKernel_t clCreateKernel_real = nullptr;
static clReleaseKernel_t clReleaseKernel_real = nullptr;
static clReleaseProgram_t clReleaseProgram_real = nullptr;
static clCreateBuffer_t clCreateBuffer_real = nullptr;
static clCreateImage_t clCreateImage_real = nullptr;
static clReleaseMemObject_t clReleaseMemObject_real = nullptr;
static clCreateSamplerWithProperties_t clCreateSamplerWithProperties_real = nullptr;
static clReleaseSampler_t clReleaseSampler_real = nullptr;
static clSetKernelArg_t clSetKernelArg_real = nullptr;
static clEnqueueNDRangeKernel_t clEnqueueNDRangeKernel_real = nullptr;
static clFinish_t clFinish_real = nullptr;
static clReleaseCommandQueue_t clReleaseCommandQueue_real = nullptr;
static clReleaseContext_t clReleaseContext_real = nullptr;

// ============================================================================
// LIBRARY LOADER CLASS
// ============================================================================

class OpenCLLibraryLoader
{
public:
    OpenCLLibraryLoader()
    {
        s_hOpenCLDll = dlopen( "libOpenCL.so", RTLD_LAZY );
        if ( nullptr == s_hOpenCLDll )
        {
            std::cerr << "OpenCLMock: Failed to load libOpenCL.so: " << dlerror() << std::endl;
            std::cerr << "OpenCLMock: OpenCL mocks will return default success" << std::endl;
        }
        else
        {
            std::cout << "OpenCLMock: Successfully loaded libOpenCL.so" << std::endl;
            LoadSymbols();
        }
    }

    ~OpenCLLibraryLoader()
    {
        if ( nullptr != s_hOpenCLDll )
        {
            dlclose( s_hOpenCLDll );
            std::cout << "OpenCLMock: OpenCL library unloaded" << std::endl;
        }
    }

private:
    void LoadSymbols()
    {
        if ( nullptr == s_hOpenCLDll ) return;

        clGetPlatformIDs_real = (clGetPlatformIDs_t) dlsym( s_hOpenCLDll, "clGetPlatformIDs" );
        clGetDeviceIDs_real = (clGetDeviceIDs_t) dlsym( s_hOpenCLDll, "clGetDeviceIDs" );
        clCreateContext_real = (clCreateContext_t) dlsym( s_hOpenCLDll, "clCreateContext" );
        clCreateCommandQueueWithProperties_real = (clCreateCommandQueueWithProperties_t) dlsym(
                s_hOpenCLDll, "clCreateCommandQueueWithProperties" );
        clGetDeviceInfo_real = (clGetDeviceInfo_t) dlsym( s_hOpenCLDll, "clGetDeviceInfo" );
        clCreateProgramWithSource_real =
                (clCreateProgramWithSource_t) dlsym( s_hOpenCLDll, "clCreateProgramWithSource" );
        clCreateProgramWithBinary_real =
                (clCreateProgramWithBinary_t) dlsym( s_hOpenCLDll, "clCreateProgramWithBinary" );
        clBuildProgram_real = (clBuildProgram_t) dlsym( s_hOpenCLDll, "clBuildProgram" );
        clGetProgramBuildInfo_real =
                (clGetProgramBuildInfo_t) dlsym( s_hOpenCLDll, "clGetProgramBuildInfo" );
        clCreateKernel_real = (clCreateKernel_t) dlsym( s_hOpenCLDll, "clCreateKernel" );
        clReleaseKernel_real = (clReleaseKernel_t) dlsym( s_hOpenCLDll, "clReleaseKernel" );
        clReleaseProgram_real = (clReleaseProgram_t) dlsym( s_hOpenCLDll, "clReleaseProgram" );
        clCreateBuffer_real = (clCreateBuffer_t) dlsym( s_hOpenCLDll, "clCreateBuffer" );
        clCreateImage_real = (clCreateImage_t) dlsym( s_hOpenCLDll, "clCreateImage" );
        clReleaseMemObject_real =
                (clReleaseMemObject_t) dlsym( s_hOpenCLDll, "clReleaseMemObject" );
        clCreateSamplerWithProperties_real = (clCreateSamplerWithProperties_t) dlsym(
                s_hOpenCLDll, "clCreateSamplerWithProperties" );
        clReleaseSampler_real = (clReleaseSampler_t) dlsym( s_hOpenCLDll, "clReleaseSampler" );
        clSetKernelArg_real = (clSetKernelArg_t) dlsym( s_hOpenCLDll, "clSetKernelArg" );
        clEnqueueNDRangeKernel_real =
                (clEnqueueNDRangeKernel_t) dlsym( s_hOpenCLDll, "clEnqueueNDRangeKernel" );
        clFinish_real = (clFinish_t) dlsym( s_hOpenCLDll, "clFinish" );
        clReleaseCommandQueue_real =
                (clReleaseCommandQueue_t) dlsym( s_hOpenCLDll, "clReleaseCommandQueue" );
        clReleaseContext_real = (clReleaseContext_t) dlsym( s_hOpenCLDll, "clReleaseContext" );

        std::cout << "OpenCLMock: OpenCL symbols loaded" << std::endl;
    }
};

// Global loader instance
static OpenCLLibraryLoader s_openclLoader;

// ============================================================================
// OPENCL C API MOCK WRAPPER FUNCTIONS
// ============================================================================

extern "C" cl_int clGetPlatformIDs( cl_uint num_entries, cl_platform_id *platforms,
                                    cl_uint *num_platforms )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clGetPlatformIDs_real )
    {
        ret = clGetPlatformIDs_real( num_entries, platforms, num_platforms );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_GET_PLATFORM_IDS].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_GET_PLATFORM_IDS].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_GET_PLATFORM_IDS].param;
            std::cout << "OpenCLMock: clGetPlatformIDs return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_GET_PLATFORM_IDS].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clGetDeviceIDs( cl_platform_id platform, cl_device_type device_type,
                                  cl_uint num_entries, cl_device_id *devices, cl_uint *num_devices )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clGetDeviceIDs_real )
    {
        ret = clGetDeviceIDs_real( platform, device_type, num_entries, devices, num_devices );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_GET_DEVICE_IDS].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_GET_DEVICE_IDS].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_GET_DEVICE_IDS].param;
            std::cout << "OpenCLMock: clGetDeviceIDs return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_GET_DEVICE_IDS].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_context clCreateContext( const cl_context_properties *properties, cl_uint num_devices,
                                       const cl_device_id *devices,
                                       void( CL_CALLBACK *pfn_notify )( const char *, const void *,
                                                                        size_t, void * ),
                                       void *user_data, cl_int *errcode_ret )
{
    cl_context context = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateContext_real )
    {
        context = clCreateContext_real( properties, num_devices, devices, pfn_notify, user_data,
                                        errcode_ret );
    }
    else
    {
        context = (cl_context) 0x1;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_CONTEXT].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_CONTEXT].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_CONTEXT].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) context = nullptr;
            std::cout << "OpenCLMock: clCreateContext error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_CONTEXT].action = MOCK_CONTROL_CL_NONE;
    }

    return context;
}

extern "C" cl_command_queue
clCreateCommandQueueWithProperties( cl_context context, cl_device_id device,
                                    const cl_queue_properties *properties, cl_int *errcode_ret )
{
    cl_command_queue queue = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateCommandQueueWithProperties_real )
    {
        queue = clCreateCommandQueueWithProperties_real( context, device, properties, errcode_ret );
    }
    else
    {
        queue = (cl_command_queue) 0x2;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE !=
         s_MockParams_CL[MOCK_API_CL_CREATE_COMMAND_QUEUE_WITH_PROPERTIES].action )
    {
        if ( MOCK_CONTROL_CL_RETURN ==
             s_MockParams_CL[MOCK_API_CL_CREATE_COMMAND_QUEUE_WITH_PROPERTIES].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_COMMAND_QUEUE_WITH_PROPERTIES]
                           .param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) queue = nullptr;
            std::cout << "OpenCLMock: clCreateCommandQueueWithProperties error overridden to "
                      << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_COMMAND_QUEUE_WITH_PROPERTIES].action =
                MOCK_CONTROL_CL_NONE;
    }

    return queue;
}

extern "C" cl_int clGetDeviceInfo( cl_device_id device, cl_device_info param_name,
                                   size_t param_value_size, void *param_value,
                                   size_t *param_value_size_ret )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clGetDeviceInfo_real )
    {
        ret = clGetDeviceInfo_real( device, param_name, param_value_size, param_value,
                                    param_value_size_ret );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_GET_DEVICE_INFO].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_GET_DEVICE_INFO].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_GET_DEVICE_INFO].param;
            std::cout << "OpenCLMock: clGetDeviceInfo return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_GET_DEVICE_INFO].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_program clCreateProgramWithSource( cl_context context, cl_uint count,
                                                 const char **strings, const size_t *lengths,
                                                 cl_int *errcode_ret )
{
    cl_program program = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateProgramWithSource_real )
    {
        program = clCreateProgramWithSource_real( context, count, strings, lengths, errcode_ret );
    }
    else
    {
        program = (cl_program) 0x3;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_SOURCE].action )
    {
        if ( MOCK_CONTROL_CL_RETURN ==
             s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_SOURCE].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_SOURCE].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) program = nullptr;
            std::cout << "OpenCLMock: clCreateProgramWithSource error overridden to " << err
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_SOURCE].action = MOCK_CONTROL_CL_NONE;
    }

    return program;
}

extern "C" cl_program clCreateProgramWithBinary( cl_context context, cl_uint num_devices,
                                                 const cl_device_id *device_list,
                                                 const size_t *lengths,
                                                 const unsigned char **binaries,
                                                 cl_int *binary_status, cl_int *errcode_ret )
{
    cl_program program = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateProgramWithBinary_real )
    {
        program = clCreateProgramWithBinary_real( context, num_devices, device_list, lengths,
                                                  binaries, binary_status, errcode_ret );
    }
    else
    {
        program = (cl_program) 0x3;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_BINARY].action )
    {
        if ( MOCK_CONTROL_CL_RETURN ==
             s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_BINARY].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_BINARY].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) program = nullptr;
            std::cout << "OpenCLMock: clCreateProgramWithBinary error overridden to " << err
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_PROGRAM_WITH_BINARY].action = MOCK_CONTROL_CL_NONE;
    }

    return program;
}

extern "C" cl_int clBuildProgram( cl_program program, cl_uint num_devices,
                                  const cl_device_id *device_list, const char *options,
                                  void( CL_CALLBACK *pfn_notify )( cl_program, void * ),
                                  void *user_data )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clBuildProgram_real )
    {
        ret = clBuildProgram_real( program, num_devices, device_list, options, pfn_notify,
                                   user_data );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_BUILD_PROGRAM].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_BUILD_PROGRAM].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_BUILD_PROGRAM].param;
            std::cout << "OpenCLMock: clBuildProgram return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_BUILD_PROGRAM].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clGetProgramBuildInfo( cl_program program, cl_device_id device,
                                         cl_program_build_info param_name, size_t param_value_size,
                                         void *param_value, size_t *param_value_size_ret )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clGetProgramBuildInfo_real )
    {
        ret = clGetProgramBuildInfo_real( program, device, param_name, param_value_size,
                                          param_value, param_value_size_ret );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_GET_PROGRAM_BUILD_INFO].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_GET_PROGRAM_BUILD_INFO].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_GET_PROGRAM_BUILD_INFO].param;
            std::cout << "OpenCLMock: clGetProgramBuildInfo return overridden to " << ret
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_GET_PROGRAM_BUILD_INFO].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_kernel clCreateKernel( cl_program program, const char *kernel_name,
                                     cl_int *errcode_ret )
{
    cl_kernel kernel = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateKernel_real )
    {
        kernel = clCreateKernel_real( program, kernel_name, errcode_ret );
    }
    else
    {
        kernel = (cl_kernel) 0x4;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    s_kenerlCnt++;

    if ( 1 == s_kenerlCnt and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K1].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K1].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K1].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) kernel = nullptr;
            std::cout << "OpenCLMock: clCreateKernel error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K1].action = MOCK_CONTROL_CL_NONE;
    }


    if ( 2 == s_kenerlCnt and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K2].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K2].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K2].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) kernel = nullptr;
            std::cout << "OpenCLMock: clCreateKernel error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_KERNEL_K2].action = MOCK_CONTROL_CL_NONE;
    }

    return kernel;
}

extern "C" cl_mem clCreateBuffer( cl_context context, cl_mem_flags flags, size_t size,
                                  void *host_ptr, cl_int *errcode_ret )
{
    cl_mem buffer = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateBuffer_real )
    {
        buffer = clCreateBuffer_real( context, flags, size, host_ptr, errcode_ret );
    }
    else
    {
        buffer = (cl_mem) 0x5;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    s_kenerlCnt++;

    if ( s_kenerlCnt == g_createBffrCall and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B1].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B1].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B1].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS )
            {
                if ( nullptr != buffer )
                {
                    clReleaseMemObject( buffer );
                }
                buffer = nullptr;
            }
            std::cout << "OpenCLMock: clCreateBuffer error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B1].action = MOCK_CONTROL_CL_NONE;
    }

    if ( s_kenerlCnt == g_createBffrCall and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B2].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B2].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B2].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS )
            {
                if ( nullptr != buffer )
                {
                    clReleaseMemObject( buffer );
                }
                buffer = nullptr;
            }
            std::cout << "OpenCLMock: clCreateBuffer error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B2].action = MOCK_CONTROL_CL_NONE;
    }

    if ( s_kenerlCnt == g_createBffrCall and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B3].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B3].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B3].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS )
            {
                if ( nullptr != buffer )
                {
                    clReleaseMemObject( buffer );
                }
                buffer = nullptr;
            }
            std::cout << "OpenCLMock: clCreateBuffer error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B3].action = MOCK_CONTROL_CL_NONE;
    }

    if ( s_kenerlCnt == g_createBffrCall and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B4].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B4].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B4].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS )
            {
                if ( nullptr != buffer )
                {
                    clReleaseMemObject( buffer );
                }
                buffer = nullptr;
            }
            std::cout << "OpenCLMock: clCreateBuffer error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B4].action = MOCK_CONTROL_CL_NONE;
    }

    if ( s_kenerlCnt == g_createBffrCall and
         MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B5].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B5].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B5].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS )
            {
                if ( nullptr != buffer )
                {
                    clReleaseMemObject( buffer );
                }
                buffer = nullptr;
            }
            std::cout << "OpenCLMock: clCreateBuffer error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_BUFFER_B5].action = MOCK_CONTROL_CL_NONE;
    }
    return buffer;
}


extern "C" cl_mem clCreateImage( cl_context context, cl_mem_flags flags,
                                 const cl_image_format *image_format,
                                 const cl_image_desc *image_desc, void *host_ptr,
                                 cl_int *errcode_ret )
{
    cl_mem image = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateImage_real )
    {
        image = clCreateImage_real( context, flags, image_format, image_desc, host_ptr,
                                    errcode_ret );
    }
    else
    {
        image = (cl_mem) 0x6;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_CREATE_IMAGE].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_CREATE_IMAGE].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_IMAGE].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) image = nullptr;
            std::cout << "OpenCLMock: clCreateImage error overridden to " << err << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_IMAGE].action = MOCK_CONTROL_CL_NONE;
    }

    return image;
}

extern "C" cl_sampler
clCreateSamplerWithProperties( cl_context context, const cl_sampler_properties *sampler_properties,
                               cl_int *errcode_ret )
{
    cl_sampler sampler = nullptr;
    cl_int err = CL_SUCCESS;

    if ( nullptr != clCreateSamplerWithProperties_real )
    {
        sampler = clCreateSamplerWithProperties_real( context, sampler_properties, errcode_ret );
    }
    else
    {
        sampler = (cl_sampler) 0x7;
        if ( errcode_ret ) *errcode_ret = CL_SUCCESS;
    }

    if ( MOCK_CONTROL_CL_NONE !=
         s_MockParams_CL[MOCK_API_CL_CREATE_SAMPLER_WITH_PROPERTIES].action )
    {
        if ( MOCK_CONTROL_CL_RETURN ==
             s_MockParams_CL[MOCK_API_CL_CREATE_SAMPLER_WITH_PROPERTIES].action )
        {
            err = *(cl_int *) s_MockParams_CL[MOCK_API_CL_CREATE_SAMPLER_WITH_PROPERTIES].param;
            if ( errcode_ret ) *errcode_ret = err;
            if ( err != CL_SUCCESS ) sampler = nullptr;
            std::cout << "OpenCLMock: clCreateSamplerWithProperties error overridden to " << err
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_CREATE_SAMPLER_WITH_PROPERTIES].action = MOCK_CONTROL_CL_NONE;
    }

    return sampler;
}

extern "C" cl_int clSetKernelArg( cl_kernel kernel, cl_uint arg_index, size_t arg_size,
                                  const void *arg_value )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clSetKernelArg_real )
    {
        ret = clSetKernelArg_real( kernel, arg_index, arg_size, arg_value );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_SET_KERNEL_ARG].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_SET_KERNEL_ARG].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_SET_KERNEL_ARG].param;
            std::cout << "OpenCLMock: clSetKernelArg return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_SET_KERNEL_ARG].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clEnqueueNDRangeKernel( cl_command_queue command_queue, cl_kernel kernel,
                                          cl_uint work_dim, const size_t *global_work_offset,
                                          const size_t *global_work_size,
                                          const size_t *local_work_size,
                                          cl_uint num_events_in_wait_list,
                                          const cl_event *event_wait_list, cl_event *event )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clEnqueueNDRangeKernel_real )
    {
        ret = clEnqueueNDRangeKernel_real( command_queue, kernel, work_dim, global_work_offset,
                                           global_work_size, local_work_size,
                                           num_events_in_wait_list, event_wait_list, event );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_ENQUEUE_NDRANGE_KERNEL].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_ENQUEUE_NDRANGE_KERNEL].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_ENQUEUE_NDRANGE_KERNEL].param;
            std::cout << "OpenCLMock: clEnqueueNDRangeKernel return overridden to " << ret
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_ENQUEUE_NDRANGE_KERNEL].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clFinish( cl_command_queue command_queue )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clFinish_real )
    {
        ret = clFinish_real( command_queue );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_FINISH].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_FINISH].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_FINISH].param;
            std::cout << "OpenCLMock: clFinish return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_FINISH].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseMemObject( cl_mem memobj )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseMemObject_real )
    {
        ret = clReleaseMemObject_real( memobj );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_MEM_OBJECT].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_MEM_OBJECT].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_MEM_OBJECT].param;
            std::cout << "OpenCLMock: clReleaseMemObject return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_MEM_OBJECT].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseKernel( cl_kernel kernel )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseKernel_real )
    {
        ret = clReleaseKernel_real( kernel );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_KERNEL].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_KERNEL].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_KERNEL].param;
            std::cout << "OpenCLMock: clReleaseKernel return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_KERNEL].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseProgram( cl_program program )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseProgram_real )
    {
        ret = clReleaseProgram_real( program );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_PROGRAM].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_PROGRAM].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_PROGRAM].param;
            std::cout << "OpenCLMock: clReleaseProgram return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_PROGRAM].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseSampler( cl_sampler sampler )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseSampler_real )
    {
        ret = clReleaseSampler_real( sampler );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_SAMPLER].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_SAMPLER].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_SAMPLER].param;
            std::cout << "OpenCLMock: clReleaseSampler return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_SAMPLER].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseCommandQueue( cl_command_queue command_queue )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseCommandQueue_real )
    {
        ret = clReleaseCommandQueue_real( command_queue );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_COMMAND_QUEUE].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_COMMAND_QUEUE].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_COMMAND_QUEUE].param;
            std::cout << "OpenCLMock: clReleaseCommandQueue return overridden to " << ret
                      << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_COMMAND_QUEUE].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

extern "C" cl_int clReleaseContext( cl_context context )
{
    cl_int ret = CL_SUCCESS;

    if ( nullptr != clReleaseContext_real )
    {
        ret = clReleaseContext_real( context );
    }

    if ( MOCK_CONTROL_CL_NONE != s_MockParams_CL[MOCK_API_CL_RELEASE_CONTEXT].action )
    {
        if ( MOCK_CONTROL_CL_RETURN == s_MockParams_CL[MOCK_API_CL_RELEASE_CONTEXT].action )
        {
            ret = *(cl_int *) s_MockParams_CL[MOCK_API_CL_RELEASE_CONTEXT].param;
            std::cout << "OpenCLMock: clReleaseContext return overridden to " << ret << std::endl;
        }
        s_MockParams_CL[MOCK_API_CL_RELEASE_CONTEXT].action = MOCK_CONTROL_CL_NONE;
    }

    return ret;
}

// ============================================================================
// END OF OPENCL MOCK IMPLEMENTATION
// ============================================================================
