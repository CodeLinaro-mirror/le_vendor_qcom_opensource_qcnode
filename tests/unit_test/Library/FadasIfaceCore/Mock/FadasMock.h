// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef FADAS_MOCK_H
#define FADAS_MOCK_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        MOCK_CONTROL_NONE,
        MOCK_CONTROL_RETURN,
        MOCK_CONTROL_OUT_PARAM0,
        MOCK_CONTROL_OUT_PARAM1,
    } MockAPI_Action_e;

    typedef enum
    {
        MOCK_API_HAP_POWER_SET,
        MOCK_API_HAP_MMAP_GET,
        MOCK_API_HAP_MMAP_PUT,
        MOCK_API_HAP_MMAP,
        MOCK_API_HAP_UNMAP,
        MOCK_API_FADAS_INIT,
        MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP,
        MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION,
        MOCK_API_FADAS_REMAP_CREATE_WORKERS,
        MOCK_API_FADAS_REMAP_RUN_MT,
        MOCK_API_FADAS_REG_BUF,
        MOCK_API_FADAS_DEREG_BUF,
        MOCK_API_FADAS_VM_POINTPILLAR_CREATE,
        MOCK_API_FADAS_VM_POINTPILLAR_RUN,
        MOCK_API_FADAS_VM_POINTPILLAR_DESTROY,
        MOCK_API_FADAS_VM_EXTRACTBBOX_CREATE,
        MOCK_API_FADAS_VM_EXTRACTBBOX_RUN,
        MOCK_API_FADAS_VM_EXTRACTBBOX_DESTROY,
        MOCK_API_CRC32_VERIFY_SCATTER,
        MOCK_API_CRC32_GENERATE_SCATTER,
        MOCK_API_MAX
    } MockAPI_ID_e;

    void MockApi_Control( MockAPI_ID_e apiId, MockAPI_Action_e action, void *param );
    void MockApi_ResetAll( void );

#ifdef __cplusplus
}
#endif

#endif   // FADAS_MOCK_H
