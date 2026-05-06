// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file OpenCLMock.h
 * @brief Mock declarations for OpenCL C APIs
 *
 * This header provides mock control declarations for OpenCL C APIs used by
 * OpenclIface. It allows controlled failure injection for comprehensive
 * code coverage testing.
 */

#ifndef OPENCL_MOCK_H
#define OPENCL_MOCK_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Actions that can be performed by the mock control mechanism
     */
    typedef enum
    {
        MOCK_CONTROL_CL_NONE,         ///< No mock control, use real implementation
        MOCK_CONTROL_CL_RETURN,       ///< Override return value
        MOCK_CONTROL_CL_OUT_PARAM0,   ///< Override output parameter 0
        MOCK_CONTROL_CL_OUT_PARAM1,   ///< Override output parameter 1
        MOCK_CONTROL_CL_OUT_PARAM2,   ///< Override output parameter 2
    } MockAPI_CL_Action_e;

    /**
     * @brief Identifiers for all mockable OpenCL C APIs
     */
    typedef enum
    {
        // Platform/Device/Context APIs
        MOCK_API_CL_GET_PLATFORM_IDS,
        MOCK_API_CL_GET_DEVICE_IDS,
        MOCK_API_CL_CREATE_CONTEXT,
        MOCK_API_CL_GET_DEVICE_INFO,

        // Command Queue APIs
        MOCK_API_CL_CREATE_COMMAND_QUEUE_WITH_PROPERTIES,

        // Program/Kernel APIs
        MOCK_API_CL_CREATE_PROGRAM_WITH_SOURCE,
        MOCK_API_CL_CREATE_PROGRAM_WITH_BINARY,
        MOCK_API_CL_BUILD_PROGRAM,
        MOCK_API_CL_GET_PROGRAM_BUILD_INFO,
        MOCK_API_CL_CREATE_KERNEL_K1,
        MOCK_API_CL_CREATE_KERNEL_K2,
        MOCK_API_CL_RELEASE_KERNEL,
        MOCK_API_CL_RELEASE_PROGRAM,

        // Memory APIs
        MOCK_API_CL_CREATE_BUFFER_B1,
        MOCK_API_CL_CREATE_BUFFER_B2,
        MOCK_API_CL_CREATE_BUFFER_B3,
        MOCK_API_CL_CREATE_BUFFER_B4,
        MOCK_API_CL_CREATE_BUFFER_B5,
        MOCK_API_CL_CREATE_IMAGE,
        MOCK_API_CL_RELEASE_MEM_OBJECT,

        // Sampler APIs
        MOCK_API_CL_CREATE_SAMPLER_WITH_PROPERTIES,
        MOCK_API_CL_RELEASE_SAMPLER,

        // Execution APIs
        MOCK_API_CL_SET_KERNEL_ARG,
        MOCK_API_CL_ENQUEUE_NDRANGE_KERNEL,
        MOCK_API_CL_FINISH,

        // Cleanup APIs
        MOCK_API_CL_RELEASE_COMMAND_QUEUE,
        MOCK_API_CL_RELEASE_CONTEXT,

        MOCK_API_CL_MAX
    } MockAPI_CL_ID_e;

    /**
     * @brief Control mock behavior for a specific OpenCL C API
     *
     * @param apiId The API to control
     * @param action The action to perform (return override, param override, etc.)
     * @param param Pointer to the control parameter (e.g., return value to inject)
     *
     * @note The mock control is consumed after one use and automatically resets
     *
     * Example:
     *   cl_int failStatus = CL_INVALID_VALUE;
     *   MockApi_CL_Control(MOCK_API_CL_CREATE_CONTEXT, MOCK_CONTROL_CL_RETURN, &failStatus);
     *   // Next call to clCreateContext() will return CL_INVALID_VALUE
     */
    void MockApi_CL_Control( MockAPI_CL_ID_e apiId, MockAPI_CL_Action_e action, void *param );

    /**
     * @brief Reset all OpenCL mock controls to default (no override)
     */
    void MockApi_CL_ResetAll( void );

#ifdef __cplusplus
}
#endif

#endif   // OPENCL_MOCK_H
