// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "MockCLib.hpp"
#include <cstddef>
#include <dlfcn.h>
#include <new>
#include <stdint.h>

#define MOCKC_INIT_START_MAGIC 0x12345678
#define MOCKC_INIT_MAGIC 0xdeadbeef

extern "C"
{
    void *malloc( size_t size );
    void *calloc( size_t nitems, size_t size );
    size_t fread( void *ptr, size_t size, size_t nitems, FILE *stream );
} /* extern "C" */


typedef void *( *MallocFnc_t )( size_t size );
typedef void *( *CallocFnc_t )( size_t nitems, size_t size );
typedef size_t ( *FreadFnc_t )( void *ptr, size_t size, size_t n, FILE *stream );

static MallocFnc_t s_MallocFnc = nullptr;
static CallocFnc_t s_CallocFnc = nullptr;
static FreadFnc_t s_FreadFnc = nullptr;

/* ── legacy single-mode controls ──────────────────────────────────── */
static int s_whenMallocToReturnNull = 0; /* count-only */
static int s_whenCallocToReturnNull = 0;
static size_t s_sizeMallocToReturnNull = 0; /* size-only (one-shot) */
static size_t s_sizeCallocToReturnNull = 0;
static size_t s_sizeFreadToReturnZero = 0;

/* ── combined size+count mode ──────────────────────────────────────
 * Fires null on the (s_sizeAndCountLeft)-th malloc whose size ==
 * s_sizeAndCountSize.  Both fields must be > 0 to enable the mode.
 * After firing both are cleared.                                    */
static size_t s_sizeAndCountSize = 0;
static int s_sizeAndCountLeft = 0;

/* ── size-range+count mode ─────────────────────────────────────────
 * Fires null on the (s_rangeCountLeft)-th malloc whose size is in
 * [s_rangeMinSize, s_rangeMaxSize].  Enables when s_rangeCountLeft > 0.
 * This is the portable primitive for new T[n]: the C++ array cookie is
 * added on top of sizeof(T)*n and its size is ABI-dependent, so a range
 * absorbs it. After firing all fields are cleared.                  */
static size_t s_rangeMinSize = 0;
static size_t s_rangeMaxSize = 0;
static int s_rangeCountLeft = 0;

static uint32_t s_initMagic = 0;

static void MockC_EnsureInit( void )
{
    if ( MOCKC_INIT_MAGIC != s_initMagic )
    {
        s_initMagic = MOCKC_INIT_START_MAGIC;
        s_MallocFnc = (MallocFnc_t) dlsym( RTLD_NEXT, "malloc" );
        s_CallocFnc = (CallocFnc_t) dlsym( RTLD_NEXT, "calloc" );
        s_FreadFnc = (FreadFnc_t) dlsym( RTLD_NEXT, "fread" );
        s_whenMallocToReturnNull = 0;
        s_whenCallocToReturnNull = 0;
        s_sizeMallocToReturnNull = 0;
        s_sizeCallocToReturnNull = 0;
        s_sizeFreadToReturnZero = 0;
        s_sizeAndCountSize = 0;
        s_sizeAndCountLeft = 0;
        s_rangeMinSize = 0;
        s_rangeMaxSize = 0;
        s_rangeCountLeft = 0;
        s_initMagic = MOCKC_INIT_MAGIC;
    }
}

/* ── public API ──────────────────────────────────────────────────── */

void MockC_MallocCtrlSize( size_t size )
{
    s_sizeMallocToReturnNull = size;
}

void MockC_MallocCtrl( int whenToReturnNull )
{
    s_whenMallocToReturnNull = whenToReturnNull;
}

/* Combined mode: return null on the (count)-th malloc of exactly (size) bytes.
 * count=1 → first matching malloc; count=2 → second matching malloc, etc.
 * Passing count=0 or size=0 disables the mode.
 * Legacy MockC_MallocCtrl / MockC_MallocCtrlSize continue to work independently. */
void MockC_MallocCtrlSizeAndCount( size_t size, int count )
{
    s_sizeAndCountSize = size;
    s_sizeAndCountLeft = count;
}

/* Range mode: return null on the (count)-th malloc whose size is in
 * [minSize, maxSize].  count=1 → first match.  Use for new T[n], where the
 * request is sizeof(T)*n + an ABI-dependent array cookie: bracket the window
 * as [sizeof(T)*n, sizeof(T)*n + cookieUpperBound].  count=0 disables. */
void MockC_MallocCtrlSizeRangeAndCount( size_t minSize, size_t maxSize, int count )
{
    s_rangeMinSize = minSize;
    s_rangeMaxSize = maxSize;
    s_rangeCountLeft = count;
}

void MockC_CallocCtrlSize( size_t size )
{
    s_sizeCallocToReturnNull = size;
}

void MockC_CallocCtrl( int whenToReturnNull )
{
    s_whenCallocToReturnNull = whenToReturnNull;
}

void MockC_FreadCtrlSize( size_t size )
{
    s_sizeFreadToReturnZero = size;
}

void MockC_ClearAll( void )
{
    s_whenMallocToReturnNull = 0;
    s_whenCallocToReturnNull = 0;
    s_sizeMallocToReturnNull = 0;
    s_sizeCallocToReturnNull = 0;
    s_sizeFreadToReturnZero = 0;
    s_sizeAndCountSize = 0;
    s_sizeAndCountLeft = 0;
    s_rangeMinSize = 0;
    s_rangeMaxSize = 0;
    s_rangeCountLeft = 0;
}


void *malloc( size_t size )
{
    void *pData = nullptr;
    bool bReturnNull = false;

    if ( MOCKC_INIT_START_MAGIC == s_initMagic )
    {
        /* dlsym may call malloc during initialisation */
        int r = posix_memalign( &pData, 128, size );
        if ( 0 != r )
        {
            pData = nullptr;
        }
        return pData;
    }

    MockC_EnsureInit();

    /* ── legacy count-only mode ──────────────────────────────────── */
    if ( s_whenMallocToReturnNull > 0 )
    {
        s_whenMallocToReturnNull--;
        if ( 0 == s_whenMallocToReturnNull )
        {
            bReturnNull = true;
        }
    }

    /* ── legacy size-only mode (one-shot) ────────────────────────── */
    if ( s_sizeMallocToReturnNull > 0 )
    {
        if ( size == s_sizeMallocToReturnNull )
        {
            bReturnNull = true;
            s_sizeMallocToReturnNull = 0;
        }
    }

    /* ── combined size+count mode ────────────────────────────────────
     * Only decrement when size matches; fire when counter reaches 0. */
    if ( ( s_sizeAndCountSize > 0 ) && ( s_sizeAndCountLeft > 0 ) )
    {
        if ( size == s_sizeAndCountSize )
        {
            s_sizeAndCountLeft--;
            if ( 0 == s_sizeAndCountLeft )
            {
                bReturnNull = true;
                s_sizeAndCountSize = 0;
            }
        }
    }

    /* ── size-range+count mode ────────────────────────────────────────
     * Only decrement when size is in [min,max]; fire when counter hits 0. */
    if ( s_rangeCountLeft > 0 )
    {
        if ( ( size >= s_rangeMinSize ) && ( size <= s_rangeMaxSize ) )
        {
            s_rangeCountLeft--;
            if ( 0 == s_rangeCountLeft )
            {
                bReturnNull = true;
                s_rangeMinSize = 0;
                s_rangeMaxSize = 0;
            }
        }
    }

    if ( true == bReturnNull )
    {
        pData = nullptr;
    }
    else
    {
        pData = s_MallocFnc( size );
    }

    return pData;
}


/* On aarch64 glibc, new(std::nothrow) does NOT route through malloc — it calls
 * the nothrow operator new directly, bypassing our interposed malloc.  Override
 * operator new / new[] (nothrow variants) to route through our malloc so that
 * MockC_MallocCtrlSizeAndCount / MockC_MallocCtrlSizeRangeAndCount work on
 * aarch64 targets the same way they do on x86. */
void *operator new( size_t size, const std::nothrow_t & ) noexcept
{
    return malloc( size );
}

void *operator new[]( size_t size, const std::nothrow_t & ) noexcept
{
    return malloc( size );
}


void *calloc( size_t nitems, size_t size )
{
    void *pData = nullptr;
    bool bReturnNull = false;

    if ( MOCKC_INIT_START_MAGIC == s_initMagic )
    {
        int r = posix_memalign( &pData, 128, nitems * size );
        if ( 0 == r )
        {
            memset( pData, 0, nitems * size );
        }
        else
        {
            pData = nullptr;
        }
        return pData;
    }

    MockC_EnsureInit();

    if ( s_whenCallocToReturnNull > 0 )
    {
        s_whenCallocToReturnNull--;
        if ( 0 == s_whenCallocToReturnNull )
        {
            bReturnNull = true;
        }
    }

    if ( s_sizeCallocToReturnNull > 0 )
    {
        if ( ( size * nitems ) == s_sizeCallocToReturnNull )
        {
            bReturnNull = true;
            s_sizeCallocToReturnNull = 0;
        }
    }

    if ( true == bReturnNull )
    {
        pData = nullptr;
    }
    else
    {
        pData = s_CallocFnc( nitems, size );
    }

    return pData;
}

size_t fread( void *ptr, size_t size, size_t nitems, FILE *stream )
{
    bool bReturnZero = false;
    size_t sizeRead = 0;

    MockC_EnsureInit();

    if ( s_sizeFreadToReturnZero > 0 )
    {
        if ( ( size * nitems ) == s_sizeFreadToReturnZero )
        {
            bReturnZero = true;
            s_sizeFreadToReturnZero = 0;
        }
    }

    if ( true == bReturnZero )
    {
        sizeRead = 0;
    }
    else
    {
        sizeRead = s_FreadFnc( ptr, size, nitems, stream );
    }

    return sizeRead;
}
