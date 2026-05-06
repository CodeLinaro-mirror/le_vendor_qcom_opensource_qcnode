// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <vidc_ioctl.h>
#include <vidc_types.h>

#ifndef _VIDC_LRH_LINUX_
#include <ioctlClient.h>
#else
#include <cstring>
#include <vidc_client.h>
#endif

// Matches production signatures used by VidcDrvClient
extern void* __mockup_device_open( const char *path, ioctl_callback_t *cb );
extern int __mockup_device_close( ioctl_session_t *handle );
extern int __mockup_device_ioctl( ioctl_session_t *handle, unsigned int cmd,
                                  unsigned char *in,
                                  unsigned int in_sz,
                                  unsigned char *out,
                                  unsigned int out_sz );
extern int __mockup_MM_Timer_Sleep( unsigned int ms );

#ifdef __cplusplus
}
#endif
