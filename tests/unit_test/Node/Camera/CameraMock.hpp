// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef CAMERA_MOCK_HPP
#define CAMERA_MOCK_HPP

#include "qcarcam.h"
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <string>

typedef enum
{
    MOCK_CONTROL_API_NONE,
    MOCK_CONTROL_API_RETURN,
    MOCK_CONTROL_API_OUT_PARAM0,
    MOCK_CONTROL_API_OUT_PARAM1,
    MOCK_CONTROL_API_OUT_PARAM2,
    MOCK_CONTROL_API_OUT_PARAM3,
    MOCK_CONTROL_API_OUT_PARAM4,
    MOCK_CONTROL_API_OUT_PARAM5,
    MOCK_CONTROL_API_OUT_PARAM6,
    MOCK_CONTROL_API_OUT_PARAM7,
    MOCK_CONTROL_API_OUT_PARAM8,
} MockAPI_Action_e;

typedef enum
{
    MOCK_API_QCARCAM_INITIALIZE,
    MOCK_API_QCARCAM_UNINITIALIZE,
    MOCK_API_QCARCAM_QUERY_INPUTS,
    MOCK_API_QCARCAM_QUERY_INPUT_MODES,
    MOCK_API_QCARCAM_OPEN,
    MOCK_API_QCARCAM_CLOSE,
    MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK,
    MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK,
    MOCK_API_QCARCAM_SET_PARAM_ISP_USECASE,
    MOCK_API_QCARCAM_SET_PARAM_FRAME_DROP_CONTROL,
    MOCK_API_QCARCAM_RESERVE,
    MOCK_API_QCARCAM_RELEASE,
    MOCK_API_QCARCAM_START,
    MOCK_API_QCARCAM_STOP,
    MOCK_API_QCARCAM_SET_BUFFERS,
    MOCK_API_QCARCAM_GET_BUFFERS,
    MOCK_API_QCARCAM_SUBMIT_REQUEST,
    MOCK_API_QCARCAM_GET_FRAME,
    MOCK_API_QCARCAM_RELEASE_FRAME,
    MOCK_API_MAX
} MockAPI_ID_e;

typedef struct
{
    MockAPI_Action_e action;
    void *param;
} MockControlParam_t;

typedef void ( *MockApi_ControlFnc_t )( MockAPI_ID_e apiId, MockAPI_Action_e action, void *param );
typedef void ( *MockApi_SetErrorPassiveFnc_t )( bool active );
typedef void ( *MockApi_TriggerEventFnc_t )( uint32_t eventId,
                                             const QCarCamEventPayload_t *pPayload,
                                             bool useNullPrivateData );

static inline MockApi_ControlFnc_t MockCamera_GetControlFnc( std::string libraryPath )
{
    MockApi_ControlFnc_t fnc = nullptr;
    void *hDll = dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_GLOBAL );
    if ( nullptr == hDll )
    {
        printf( "Failed to load %s: %s\n", libraryPath.c_str(), dlerror() );
    }
    else
    {
        printf( "Successfully loaded %s\n", libraryPath.c_str() );
    }

    fnc = (MockApi_ControlFnc_t) dlsym( hDll, "MockApi_Control" );
    const char *error = dlerror();
    if ( error != nullptr )
    {
        printf( "Failed to load symbol MockApi_Control: %s\n", error );
        fnc = nullptr;
    }
    else
    {
        printf( "Successfully loaded symbol MockApi_Control\n" );
    }

    return fnc;
}

static inline MockApi_SetErrorPassiveFnc_t
MockCamera_GetSetErrorPassiveFnc( std::string libraryPath )
{
    MockApi_SetErrorPassiveFnc_t fnc = nullptr;
    void *hDll = dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_GLOBAL );
    if ( nullptr == hDll )
    {
        printf( "Failed to load %s: %s\n", libraryPath.c_str(), dlerror() );
    }
    else
    {
        printf( "Successfully loaded %s\n", libraryPath.c_str() );
    }

    fnc = (MockApi_SetErrorPassiveFnc_t) dlsym( hDll, "MockApi_SetErrorPassive" );
    const char *error = dlerror();
    if ( error != nullptr )
    {
        printf( "Failed to load symbol MockApi_SetErrorPassive: %s\n", error );
        fnc = nullptr;
    }
    else
    {
        printf( "Successfully loaded symbol MockApi_SetErrorPassive\n" );
    }

    return fnc;
}

static inline MockApi_TriggerEventFnc_t MockCamera_GetTriggerEventFnc( std::string libraryPath )
{
    MockApi_TriggerEventFnc_t fnc = nullptr;
    void *hDll = dlopen( libraryPath.c_str(), RTLD_NOW | RTLD_GLOBAL );
    if ( nullptr == hDll )
    {
        printf( "Failed to load %s: %s\n", libraryPath.c_str(), dlerror() );
    }
    else
    {
        printf( "Successfully loaded %s\n", libraryPath.c_str() );
    }

    fnc = (MockApi_TriggerEventFnc_t) dlsym( hDll, "MockApi_TriggerEvent" );
    const char *error = dlerror();
    if ( error != nullptr )
    {
        printf( "Failed to load symbol MockApi_TriggerEvent: %s\n", error );
        fnc = nullptr;
    }
    else
    {
        printf( "Successfully loaded symbol MockApi_TriggerEvent\n" );
    }

    return fnc;
}

#endif   // CAMERA_MOCK_HPP