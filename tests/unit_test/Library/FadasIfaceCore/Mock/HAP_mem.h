// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef HAP_MEM_H
#define HAP_MEM_H
#include "AEEStdDef.h"
#include "AEEStdErr.h"
#include <stddef.h>
#include <stdint.h>

#define HAP_PROT_READ 1
#define HAP_PROT_WRITE 2

AEEResult HAP_mmap_get( int32_t fd, void **buf, void *data );
AEEResult HAP_mmap_put( int32_t fd );
void *HAP_mmap( void *addr, int len, int prot, int flags, int fd, long long offset );
AEEResult HAP_munmap( void *addr, int len );

#endif
