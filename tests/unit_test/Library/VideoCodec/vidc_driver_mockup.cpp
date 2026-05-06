// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>
#include <cstring>
#include <atomic>
#include <mutex>
#include <vector>
#include "vidc_driver_mockup.hpp"

// =====================================================================================
// ==== Fake VIDC Driver ===============================================================
// =====================================================================================
// This block replaces the low-level device driver calls used by VidcDrvClient so we can
// deterministically drive success and failure paths without real hardware.
// It implements:
//   - device_open / device_close
//   - device_ioctl (sends back events and property values)
//   - MM_Timer_Sleep (fast)
//
// NOTE: Signatures are kept C-style to match link symbols; we avoid including vendor
// headers in tests and operate on raw buffers. The framework code parses these buffers.
//
// Targets VidcDrvClient flows: OpenDriver, Load/Release, Start/Stop, Get/SetProperty,

extern "C" {

void* __wrap_device_open( const char *path, ioctl_callback_t *cb )
{
    return __mockup_device_open( (char*) path, cb );
}

int __wrap_device_close( ioctl_session_t *handle )
{
    return __mockup_device_close( handle );
}

int __wrap_device_ioctl( ioctl_session_t *handle, unsigned int cmd,
                         unsigned char *in,
                         unsigned int in_sz,
                         unsigned char *out,
                         unsigned int out_sz )
{
    return __mockup_device_ioctl( handle, cmd, in, in_sz, out, out_sz );
}

int __wrap_MM_Timer_Sleep( unsigned int ms )
{
    return __mockup_MM_Timer_Sleep( ms );
}

} // extern "C"
