// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef HAP_POWER_H
#define HAP_POWER_H

#include "AEEStdErr.h"

typedef enum
{
    HAP_power_set_apptype,
    HAP_power_set_DCVS_v2,
    HAP_power_set_HVX
} HAP_power_request_type;

typedef enum
{
    HAP_POWER_COMPUTE_CLIENT_CLASS
} HAP_power_app_type;

typedef enum
{
    HAP_DCVS_V2_PERFORMANCE_MODE
} HAP_dcvs_v2_option_t;

typedef int HAP_dcvs_voltage_corner_t;

typedef struct
{
    HAP_power_request_type type;
    union
    {
        HAP_power_app_type apptype;
        struct
        {
            int dcvs_enable;
            struct
            {
                HAP_dcvs_voltage_corner_t target_corner;
                HAP_dcvs_voltage_corner_t min_corner;
                HAP_dcvs_voltage_corner_t max_corner;
            } dcvs_params;
            HAP_dcvs_v2_option_t dcvs_option;
            int set_dcvs_params;
            int set_latency;
            int latency;
        } dcvs_v2;
        struct
        {
            int power_up;
        } hvx;
    };
} HAP_power_request_t;

int HAP_power_set( void *ctx, HAP_power_request_t *request );
int HAP_power_destroy( void *ctx );

#endif
