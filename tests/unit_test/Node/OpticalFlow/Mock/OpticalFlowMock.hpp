// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once

#include <cstdint>

namespace OpticalFlowMock
{
enum ApiId : uint32_t
{
    API_SESSION_CREATE = 0,
    API_LME_CREATE,
    API_LME_SUBMIT_SYNC,
    API_BUFFER_REGISTER,
    API_BUFFER_DEREGISTER,
    API_IMAGE_INFO_QUERY,
    API_SESSION_START,
    API_SESSION_STOP,
    API_SESSION_DESTROY,
    API_CONFIGMAP_SET,   // LME::ConfigMap::Set(...)
    API_MAX
};

enum Action : uint32_t
{
    ACTION_NONE = 0,

    // If Action is ACTION_RETURN_STATUS, param points to an SV::Status value.
    ACTION_RETURN_STATUS,

    // If Action is ACTION_RETURN_NULLPTR, param is ignored.
    ACTION_RETURN_NULLPTR,
};

struct ControlParam
{
    Action action{ ACTION_NONE };
    void*  param{ nullptr };
};

void MockApi_Control( ApiId apiId, Action action, void* param );
void MockApi_ResetAll();

// Fine-grained control for FeatureConfigMap::Set interposition.
// callIndex is 0-based: 0 means the very first Set() call made by the function
// under test, 1 means the second, etc.  After the targeted call fails the mock
// is automatically consumed (reset to "never fail").
void MockApi_ConfigMapSet_FailOnCall( uint32_t callIndex );
void MockApi_ConfigMapSet_Reset();

}   // namespace OpticalFlowMock
