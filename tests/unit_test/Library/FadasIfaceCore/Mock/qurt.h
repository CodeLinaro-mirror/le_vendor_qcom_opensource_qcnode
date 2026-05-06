// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QURT_H
#define QURT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    int _opaque;
} qurt_mutex_t;

typedef uint64_t qurt_addr_t;

#define QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL 1
#define QURT_MEM_DCACHE 2
#define QURT_MEM_CACHE_FLUSH_ALL 3

void qurt_mutex_init( qurt_mutex_t *mutex );
void qurt_mutex_lock( qurt_mutex_t *mutex );
void qurt_mutex_unlock( qurt_mutex_t *mutex );
void qurt_mem_cache_clean( qurt_addr_t addr, size_t size, int op, int type );

#endif
