// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef MOCK_C_LIB_HPP
#define MOCK_C_LIB_HPP

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void MockC_MallocCtrlSize( size_t size );
void MockC_MallocCtrl( int whenToReturnNull );

/* Combined: return null only when BOTH size matches AND the Nth matching call
 * is reached.  count=1 means "the first malloc of exactly this size".
 * count=0 disables the combined check.
 * All existing single-mode APIs continue to work as before. */
void MockC_MallocCtrlSizeAndCount( size_t size, int count );

/* Range mode: return null on the (count)-th malloc whose size is in
 * [minSize, maxSize].  count=1 → first match; count=0 disables.
 *
 * This is the portable way to fault a C++ `new` / `new[]` allocation: both
 * new T and new(std::nothrow) T bottom out in malloc, and new T[n] requests
 * sizeof(T)*n + an ABI-dependent array cookie.  Bracketing the size with a
 * range absorbs that cookie so the same test fires on QMock x86, QNX, and
 * HGY Linux.  Feed MOCK_NEW_ARRAY_RANGE(T, n) for arrays; for a single
 * object use MockC_MallocCtrlSizeAndCount(sizeof(T), count) (no cookie).
 *
 * NOTE: we fault at the malloc layer (not by replacing operator new) on
 * purpose — replacing the global operator new/new[] diverts EVERY allocation
 * in the process, including internal library allocations (e.g. the memory
 * manager's own registry vectors on QNX), which corrupts state that only
 * manifests on the target. Interposing malloc leaves operator new intact. */
void MockC_MallocCtrlSizeRangeAndCount( size_t minSize, size_t maxSize, int count );

void MockC_CallocCtrlSize( size_t size );
void MockC_CallocCtrl( int whenToReturnNull );

void MockC_FreadCtrlSize( size_t size );

/* Disarm every malloc/calloc/fread control at once (all size, count, range,
 * and size+count modes). Call after a guarded allocation so a leftover trigger
 * can't fire on an unrelated later allocation. */
void MockC_ClearAll( void );

/* Size window for a `new T[n]` malloc without predicting the exact array
 * cookie.  new T[n] requests sizeof(T)*n + cookie, where the cookie is 0 for a
 * trivially-destructible T, else an ABI prefix (max(alignof(T), sizeof(size_t))
 * on the Itanium ABI used by x86-64 g++ and QNX 8 aarch64 g++).  Rather than
 * branch on the destructor (which needs <type_traits> and can trip CTC++), we
 * bracket the request from sizeof(T)*n up to sizeof(T)*n + a cookie upper bound
 * of 2*max(alignof(T), sizeof(size_t)) — headroom that covers the cookie on
 * every mainstream ABI while staying far below sizeof(T) for any real
 * descriptor type, so the window cannot collide with a new[] of a neighbouring
 * element count.  alignof is a core-language operator (no header), valid on
 * x86, QNX, and Linux.  Expands to two args (min, max) — pass straight into
 * MockC_MallocCtrlSizeRangeAndCount:
 *     MockC_MallocCtrlSizeRangeAndCount( MOCK_NEW_ARRAY_RANGE( T, n ), count ); */
#define MOCK_NEW_ARRAY_COOKIE( T )                                                                 \
    ( 2 * ( ( alignof( T ) > sizeof( size_t ) ) ? alignof( T ) : sizeof( size_t ) ) )
#define MOCK_NEW_ARRAY_RANGE( T, n )                                                               \
    ( sizeof( T ) * ( n ) ), ( sizeof( T ) * ( n ) + MOCK_NEW_ARRAY_COOKIE( T ) )

#endif /* MOCK_C_LIB_HPP */
