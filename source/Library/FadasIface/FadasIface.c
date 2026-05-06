// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _FADASIFACE_STUB_H
#define _FADASIFACE_STUB_H
#include "FadasIface.h"
#include <string.h>
#ifndef _WIN32
#include <inttypes.h>
#endif   //_WIN32 for HAP_farf
#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <stdint.h>
#include <stdlib.h>

#define _QAIC_FARF(level, fmt, ...) \
    do { \
        printf(fmt, ##__VA_ARGS__); \
    } while (0)

typedef struct _heap _heap;
struct _heap
{
    _heap *pPrev;
    const char *loc;
    uint64_t buf;
};

typedef struct _allocator
{
    _heap *pheap;
    uint8_t *stack;
    uint8_t *stackEnd;
    int nSize;
} _allocator;

_ATTRIBUTE_UNUSED
static __inline int _heap_alloc( _heap **ppa, const char *loc, int size, void **ppbuf )
{
    _heap *pn = 0;
    pn = MALLOC( (size_t) size + sizeof( _heap ) - sizeof( uint64_t ) );
    if ( pn != 0 )
    {
        pn->pPrev = *ppa;
        pn->loc = loc;
        *ppa = pn;
        *ppbuf = (void *) &( pn->buf );
        return 0;
    }
    else
    {
        return -1;
    }
}
#define _ALIGN_SIZE( x, y ) ( ( ( x ) + ( y - 1 ) ) & ~( y - 1 ) )

_ATTRIBUTE_UNUSED
static __inline int _allocator_alloc( _allocator *me, const char *loc, int size, unsigned int al,
                                      void **ppbuf )
{
    if ( size < 0 )
    {
        return -1;
    }
    else if ( size == 0 )
    {
        *ppbuf = 0;
        return 0;
    }
    if ( ( _ALIGN_SIZE( (uintptr_t) me->stackEnd, al ) + (size_t) size ) <
         (uintptr_t) me->stack + (size_t) me->nSize )
    {
        *ppbuf = (uint8_t *) _ALIGN_SIZE( (uintptr_t) me->stackEnd, al );
        me->stackEnd = (uint8_t *) _ALIGN_SIZE( (uintptr_t) me->stackEnd, al ) + size;
        return 0;
    }
    else
    {
        return _heap_alloc( &me->pheap, loc, size, ppbuf );
    }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_deinit( _allocator *me )
{
    _heap *pa = me->pheap;
    while ( pa != 0 )
    {
        _heap *pn = pa;
        const char *loc = pn->loc;
        (void) loc;
        pa = pn->pPrev;
        FREE( pn );
    }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_init( _allocator *me, uint8_t *stack, int stackSize )
{
    me->stack = stack;
    me->stackEnd = stack + stackSize;
    me->nSize = stackSize;
    me->pheap = 0;
}


#endif   // _ALLOCATOR_H

#ifndef SLIM_H
#define SLIM_H

#include <stdint.h>

// a C data structure for the idl types that can be used to implement
// static and dynamic language bindings fairly efficiently.
//
// the goal is to have a minimal ROM and RAM footprint and without
// doing too many allocations.  A good way to package these things seemed
// like the module boundary, so all the idls within  one module can share
// all the type references.


#define PARAMETER_IN 0x0
#define PARAMETER_OUT 0x1
#define PARAMETER_INOUT 0x2
#define PARAMETER_ROUT 0x3
#define PARAMETER_INROUT 0x4

// the types that we get from idl
#define TYPE_OBJECT 0x0
#define TYPE_INTERFACE 0x1
#define TYPE_PRIMITIVE 0x2
#define TYPE_ENUM 0x3
#define TYPE_STRING 0x4
#define TYPE_WSTRING 0x5
#define TYPE_STRUCTURE 0x6
#define TYPE_UNION 0x7
#define TYPE_ARRAY 0x8
#define TYPE_SEQUENCE 0x9

// these require the pack/unpack to recurse
// so it's a hint to those languages that can optimize in cases where
// recursion isn't necessary.
#define TYPE_COMPLEX_STRUCTURE ( 0x10 | TYPE_STRUCTURE )
#define TYPE_COMPLEX_UNION ( 0x10 | TYPE_UNION )
#define TYPE_COMPLEX_ARRAY ( 0x10 | TYPE_ARRAY )
#define TYPE_COMPLEX_SEQUENCE ( 0x10 | TYPE_SEQUENCE )


typedef struct Type Type;

#define INHERIT_TYPE                                                                               \
    int32_t nativeSize; /*in the simple case its the same as wire size and alignment*/             \
    union                                                                                          \
    {                                                                                              \
        struct                                                                                     \
        {                                                                                          \
            const uintptr_t p1;                                                                    \
            const uintptr_t p2;                                                                    \
        } _cast;                                                                                   \
        struct                                                                                     \
        {                                                                                          \
            uint32_t iid;                                                                          \
            uint32_t bNotNil;                                                                      \
        } object;                                                                                  \
        struct                                                                                     \
        {                                                                                          \
            const Type *arrayType;                                                                 \
            int32_t nItems;                                                                        \
        } array;                                                                                   \
        struct                                                                                     \
        {                                                                                          \
            const Type *seqType;                                                                   \
            int32_t nMaxLen;                                                                       \
        } seqSimple;                                                                               \
        struct                                                                                     \
        {                                                                                          \
            uint32_t bFloating;                                                                    \
            uint32_t bSigned;                                                                      \
        } prim;                                                                                    \
        const SequenceType *seqComplex;                                                            \
        const UnionType *unionType;                                                                \
        const StructType *structType;                                                              \
        int32_t stringMaxLen;                                                                      \
        uint8_t bInterfaceNotNil;                                                                  \
    } param;                                                                                       \
    uint8_t type;                                                                                  \
    uint8_t nativeAlignment

typedef struct UnionType UnionType;
typedef struct StructType StructType;
typedef struct SequenceType SequenceType;
struct Type
{
    INHERIT_TYPE;
};

struct SequenceType
{
    const Type *seqType;
    uint32_t nMaxLen;
    uint32_t inSize;
    uint32_t routSizePrimIn;
    uint32_t routSizePrimROut;
};

// byte offset from the start of the case values for
// this unions case value array.  it MUST be aligned
// at the alignment requrements for the descriptor
//
// if negative it means that the unions cases are
// simple enumerators, so the value read from the descriptor
// can be used directly to find the correct case
typedef union CaseValuePtr CaseValuePtr;
union CaseValuePtr
{
    const uint8_t *value8s;
    const uint16_t *value16s;
    const uint32_t *value32s;
    const uint64_t *value64s;
};

// these are only used in complex cases
// so I pulled them out of the type definition as references to make
// the type smaller
struct UnionType
{
    const Type *descriptor;
    uint32_t nCases;
    const CaseValuePtr caseValues;
    const Type *const *cases;
    int32_t inSize;
    int32_t routSizePrimIn;
    int32_t routSizePrimROut;
    uint8_t inAlignment;
    uint8_t routAlignmentPrimIn;
    uint8_t routAlignmentPrimROut;
    uint8_t inCaseAlignment;
    uint8_t routCaseAlignmentPrimIn;
    uint8_t routCaseAlignmentPrimROut;
    uint8_t nativeCaseAlignment;
    uint8_t bDefaultCase;
};

struct StructType
{
    uint32_t nMembers;
    const Type *const *members;
    int32_t inSize;
    int32_t routSizePrimIn;
    int32_t routSizePrimROut;
    uint8_t inAlignment;
    uint8_t routAlignmentPrimIn;
    uint8_t routAlignmentPrimROut;
};

typedef struct Parameter Parameter;
struct Parameter
{
    INHERIT_TYPE;
    uint8_t mode;
    uint8_t bNotNil;
};

#define SLIM_IFPTR32( is32, is64 ) ( sizeof( uintptr_t ) == 4 ? ( is32 ) : ( is64 ) )
#define SLIM_SCALARS_IS_DYNAMIC( u ) ( ( (u) &0x00ffffff ) == 0x00ffffff )

typedef struct Method Method;
struct Method
{
    uint32_t uScalars;   // no method index
    int32_t primInSize;
    int32_t primROutSize;
    int maxArgs;
    int numParams;
    const Parameter *const *params;
    uint8_t primInAlignment;
    uint8_t primROutAlignment;
};

typedef struct Interface Interface;

struct Interface
{
    int nMethods;
    const Method *const *methodArray;
    int nIIds;
    const uint32_t *iids;
    const uint16_t *methodStringArray;
    const uint16_t *methodStrings;
    const char *strings;
};


#endif   // SLIM_H


#ifndef _FADASIFACE_SLIM_H
#define _FADASIFACE_SLIM_H
#include <stdint.h>

#ifndef __QAIC_SLIM
#define __QAIC_SLIM( ff ) ff
#endif
#ifndef __QAIC_SLIM_EXPORT
#define __QAIC_SLIM_EXPORT
#endif

static const Type types[10];
static const Type *const typeArrays[16] = {
        &( types[9] ), &( types[9] ), &( types[9] ), &( types[9] ), &( types[9] ), &( types[9] ),
        &( types[3] ), &( types[3] ), &( types[5] ), &( types[6] ), &( types[3] ), &( types[6] ),
        &( types[3] ), &( types[3] ), &( types[3] ), &( types[3] ) };
static const StructType structTypes[4] = {
        { 0x6, &( typeArrays[6] ), 0x30, 0x0, 0x30, 0x4, 0x1, 0x4 },
        { 0x3, &( typeArrays[0] ), 0xc, 0x0, 0xc, 0x4, 0x1, 0x4 },
        { 0x6, &( typeArrays[0] ), 0x18, 0x0, 0x18, 0x4, 0x1, 0x4 },
        { 0x4, &( typeArrays[12] ), 0x10, 0x0, 0x10, 0x4, 0x1, 0x4 } };
static const Type types[10] = {
        { 0x1, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x1 },
        { 0x8, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x8 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4 },
        { 0x30, { { ( const uintptr_t ) & ( structTypes[0] ), 0 } }, 6, 0x4 },
        { 0x4, { { 0, 0 } }, 3, 0x4 },
        { 0x10, { { ( const uintptr_t ) & ( types[3] ), (const uintptr_t) 0x4 } }, 8, 0x4 },
        { 0x10, { { ( const uintptr_t ) & ( structTypes[3] ), 0 } }, 6, 0x4 },
        { 0xc, { { ( const uintptr_t ) & ( structTypes[1] ), 0 } }, 6, 0x4 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4 } };
static const Parameter parameters[26] = {
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { (const uintptr_t) 0x0, 0 } },
          4,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { SLIM_IFPTR32( 0x4, 0x8 ),
          { { (const uintptr_t) 0xdeadc0de, (const uintptr_t) 0 } },
          0,
          SLIM_IFPTR32( 0x4, 0x8 ),
          3,
          0 },
        { SLIM_IFPTR32( 0x4, 0x8 ),
          { { (const uintptr_t) 0xdeadc0de, (const uintptr_t) 0 } },
          0,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4, 3, 0 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4, 3, 0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[0] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          3,
          0 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4, 0, 0 },
        { 0x8, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x8, 4, 0 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4, 0, 0 },
        { 0x4, { { 0, 0 } }, 3, 0x4, 0, 0 },
        { 0x1, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x1, 0, 0 },
        { 0x8, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x8, 0, 0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[1] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[2] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[3] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[4] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { 0x30, { { ( const uintptr_t ) & ( structTypes[0] ), 0 } }, 6, 0x4, 0, 0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[7] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[8] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 },
        { 0x4, { { 0, 0 } }, 3, 0x4, 0, 0 },
        { 0xc, { { ( const uintptr_t ) & ( structTypes[1] ), 0 } }, 6, 0x4, 0, 0 },
        { 0x8, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x8, 3, 0 },
        { 0x8, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x8, 0, 0 },
        { 0x18, { { ( const uintptr_t ) & ( structTypes[2] ), 0 } }, 6, 0x4, 0, 0 },
        { 0x4, { { (const uintptr_t) 0, (const uintptr_t) 1 } }, 2, 0x4, 0, 0 },
        { SLIM_IFPTR32( 0x8, 0x10 ),
          { { ( const uintptr_t ) & ( types[0] ), (const uintptr_t) 0x0 } },
          9,
          SLIM_IFPTR32( 0x4, 0x8 ),
          0,
          0 } };
static const Parameter *const parameterArrays[106] = {
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[23] ) ), ( &( parameters[24] ) ),
        ( &( parameters[24] ) ), ( &( parameters[24] ) ), ( &( parameters[24] ) ),
        ( &( parameters[24] ) ), ( &( parameters[24] ) ), ( &( parameters[24] ) ),
        ( &( parameters[24] ) ), ( &( parameters[25] ) ), ( &( parameters[6] ) ),
        ( &( parameters[21] ) ), ( &( parameters[6] ) ),  ( &( parameters[4] ) ),
        ( &( parameters[22] ) ), ( &( parameters[6] ) ),  ( &( parameters[8] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[8] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[8] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[4] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[4] ) ),  ( &( parameters[7] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[8] ) ),  ( &( parameters[8] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[9] ) ),  ( &( parameters[10] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[4] ) ),  ( &( parameters[20] ) ),
        ( &( parameters[20] ) ), ( &( parameters[20] ) ), ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[21] ) ), ( &( parameters[6] ) ),
        ( &( parameters[4] ) ),  ( &( parameters[12] ) ), ( &( parameters[12] ) ),
        ( &( parameters[13] ) ), ( &( parameters[14] ) ), ( &( parameters[15] ) ),
        ( &( parameters[8] ) ),  ( &( parameters[6] ) ),  ( &( parameters[16] ) ),
        ( &( parameters[17] ) ), ( &( parameters[18] ) ), ( &( parameters[6] ) ),
        ( &( parameters[22] ) ), ( &( parameters[6] ) ),  ( &( parameters[13] ) ),
        ( &( parameters[14] ) ), ( &( parameters[14] ) ), ( &( parameters[10] ) ),
        ( &( parameters[10] ) ), ( &( parameters[4] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[4] ) ),  ( &( parameters[7] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[9] ) ),  ( &( parameters[10] ) ), ( &( parameters[6] ) ),
        ( &( parameters[4] ) ),  ( &( parameters[19] ) ), ( &( parameters[8] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[6] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[6] ) ),  ( &( parameters[7] ) ),  ( &( parameters[6] ) ),
        ( &( parameters[9] ) ),  ( &( parameters[6] ) ),  ( &( parameters[4] ) ),
        ( &( parameters[5] ) ),  ( &( parameters[6] ) ),  ( &( parameters[4] ) ),
        ( &( parameters[11] ) ), ( &( parameters[6] ) ),  ( &( parameters[3] ) ),
        ( &( parameters[4] ) ),  ( &( parameters[0] ) ),  ( &( parameters[1] ) ),
        ( &( parameters[2] ) ) };
static const Method methods[18] = {
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x2, 0x0, 0x0, 0x1 ), 0x4, 0x0, 2, 2,
          ( &( parameterArrays[103] ) ), 0x4, 0x1 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x0, 0x0, 0x1, 0x0 ), 0x0, 0x0, 1, 1,
          ( &( parameterArrays[105] ) ), 0x1, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x0, 0x1, 0x0, 0x0 ), 0x0, 0x8, 2, 2,
          ( &( parameterArrays[101] ) ), 0x1, 0x4 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x2, 0x0, 0x0 ), 0x8, 0x4, 5, 3,
          ( &( parameterArrays[96] ) ), 0x4, 0x4 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x0, 0x0, 0x0, 0x0 ), 0x0, 0x0, 0, 0, 0, 0x0, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x1, 0x0, 0x0 ), 0x30, 0xc, 12, 12,
          ( &( parameterArrays[32] ) ), 0x8, 0x8 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x1, 0x0, 0x0 ), 0x24, 0xc, 9, 9,
          ( &( parameterArrays[76] ) ), 0x8, 0x8 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x0, 0x0, 0x0 ), 0xc, 0x0, 4, 2,
          ( &( parameterArrays[99] ) ), 0x8, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x1, 0x0, 0x0 ), 0x14, 0xc, 5, 5,
          ( &( parameterArrays[91] ) ), 0x8, 0x8 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x8, 0x0, 0x0, 0x0 ), 0x58, 0x0, 18, 11,
          ( &( parameterArrays[55] ) ), 0x4, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x0, 0x0, 0x0 ), 0xc, 0x0, 3, 3,
          ( &( parameterArrays[20] ) ), 0x4, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x0, 0x0, 0x0 ), 0x18, 0x0, 6, 6,
          ( &( parameterArrays[85] ) ), 0x4, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x0, 0x0, 0x0 ), 0x14, 0x0, 5, 5,
          ( &( parameterArrays[86] ) ), 0x4, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x1, 0x0, 0x0 ), 0x3c, 0xc, 11, 11,
          ( &( parameterArrays[44] ) ), 0x4, 0x8 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x1, 0x0, 0x0 ), 0x34, 0x8, 16, 14,
          ( &( parameterArrays[18] ) ), 0x8, 0x4 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x1, 0x0, 0x0, 0x0 ), 0xc, 0x0, 4, 2,
          ( &( parameterArrays[18] ) ), 0x8, 0x0 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x2, 0x1, 0x0, 0x0 ), 0x54, 0xc, 19, 18,
          ( &( parameterArrays[0] ) ), 0x4, 0x8 },
        { REMOTE_SCALARS_MAKEX( 0, 0, 0x4, 0x1, 0x0, 0x0 ), 0x20, 0x8, 15, 10,
          ( &( parameterArrays[66] ) ), 0x8, 0x4 } };
static const Method *const methodArrays[21] = {
        &( methods[0] ),  &( methods[1] ),  &( methods[2] ),  &( methods[3] ),  &( methods[4] ),
        &( methods[5] ),  &( methods[6] ),  &( methods[7] ),  &( methods[8] ),  &( methods[7] ),
        &( methods[9] ),  &( methods[10] ), &( methods[10] ), &( methods[11] ), &( methods[12] ),
        &( methods[13] ), &( methods[14] ), &( methods[15] ), &( methods[16] ), &( methods[17] ),
        &( methods[15] ) };
static const char strings[1222] =
        "FadasRemap_CreateMapNoUndistortionSafe\0FadasRemap_CreateMapFromMapSafe\0FadasRemap_"
        "DestroyWorkersSafe\0FadasRemap_CreateWorkersSafe\0FadasRemap_"
        "DestroyMapSafe\0ExtractBBoxDestroySafe\0PointPillarDestroySafe\0ExtractBBoxCreateSafe\0Poi"
        "ntPillarCreateSafe\0FadasRemap_"
        "RunMTSafe\0ExtractBBoxRunSafe\0PointPillarRunSafe\0FadasDeregBufSafe\0outFeatureOffset\0nu"
        "mOutFeatureDim\0FadasVersionSafe\0maxNumPtsPerPlr\0numInFeatureDim\0FadasRegBufSafe\0Fadas"
        "DeInitSafe\0outFeatureSize\0bMapPtsToBBox\0outPlrsOffset\0FadasInitSafe\0maxNumFilter\0max"
        "NumDetOut\0fdOutFeature\0actualHeight\0bBBoxFilter\0labelSelect\0threshScore\0pNumOutPlrs"
        "\0outPlrsSize\0inPtsOffset\0maxNumInPts\0borderConst\0pNumDetOut\0phPostProc\0maxCentreZ\0"
        "maxCentreY\0maxCentreX\0minCentreZ\0minCentreY\0minCentreX\0maxNumPlrs\0munmapSafe\0worker"
        "Ptrs\0worker_"
        "ptr\0threshIOU\0cellSizeY\0cellSizeX\0fdOutPlrs\0inPtsSize\0phPreProc\0pMaxRange\0pMinRang"
        "e\0batchSize\0bufOffset\0numPlanes\0imgFormat\0mapStride\0mapHeight\0camHeight\0numClass\0"
        "pPlrSize\0mmapSafe\0dstProps\0srcProps\0nThreads\0mapWidth\0camWidth\0fdInPts\0bufType\0bu"
        "fSize\0dstROIs\0offsets\0mapPtrs\0version\0numPts\0normlz\0dstLen\0stride\0format\0height"
        "\0srcFds\0mapYFd\0mapXFd\0mapPtr\0status\0sizes\0pGrid\0bufFd\0dstFd\0width\0crcTx\0crcRx"
        "\0close\0open\0fds\0brY\0brX\0tlY\0tlX\0add\0mul\0sub\0uri\0y\0";
static const uint16_t methodStrings[178] = {
        246,  753,  1037, 1095, 1029, 961,  1154, 1088, 1081, 1074, 875,  534,  1148, 1067, 952,
        1154, 1088, 1081, 1074, 875,  534,  1021, 465,  1219, 1154, 1088, 1060, 1211, 1207, 1203,
        1160, 202,  619,  390,  508,  925,  1136, 1199, 1195, 1191, 1187, 795,  785,  571,  775,
        720,  709,  698,  687,  676,  665,  559,  495,  654,  1160, 1166, 224,  934,  465,  1219,
        1065, 845,  465,  1219, 1065, 835,  465,  1219, 1065, 619,  390,  731,  374,  340,  825,
        1160, 1166, 286,  826,  1053, 997,  607,  815,  805,  467,  595,  521,  323,  438,  583,
        1160, 1166, 39,   1116, 988,  915,  979,  905,  1109, 1102, 895,  885,  631,  1160, 1166,
        267,  655,  1053, 1183, 1029, 1130, 453,  547,  643,  1160, 1166, 0,    1116, 988,  915,
        979,  905,  885,  631,  1160, 1166, 406,  1005, 1142, 1013, 865,  855,  1160, 305,  1142,
        1013, 865,  855,  1160, 101,  764,  970,  885,  1160, 1166, 742,  1142, 1013, 1160, 943,
        1142, 1013, 1160, 357,  1045, 1160, 1166, 156,  655,  1160, 179,  826,  1160, 71,   764,
        1160, 130,  1116, 1160, 481,  1123, 1166, 1178, 1215, 986,  1172, 986,  422 };
static const uint16_t methodStringsArrays[21] = { 172, 175, 169, 153, 177, 92,  116,
                                                  166, 139, 163, 0,   149, 145, 126,
                                                  133, 56,  77,  160, 31,  105, 157 };
__QAIC_SLIM_EXPORT const Interface __QAIC_SLIM( FadasIface_slim ) = {
        21, &( methodArrays[0] ), 0, 0, &( methodStringsArrays[0] ), methodStrings, strings };
#endif   //_FADASIFACE_SLIM_H


#ifdef __cplusplus
extern "C"
{
#endif
    __QAIC_STUB_EXPORT int
    __QAIC_STUB( FadasIface_open )( const char *uri, remote_handle64 *h ) __QAIC_STUB_ATTRIBUTE
    {
        return __QAIC_REMOTE( remote_handle64_open )( uri, h );
    }
    __QAIC_STUB_EXPORT int
    __QAIC_STUB( FadasIface_close )( remote_handle64 h ) __QAIC_STUB_ATTRIBUTE
    {
        return __QAIC_REMOTE( remote_handle64_close )( h );
    }
    static __inline int _stub_method( remote_handle64 _handle, uint32_t _mid, uint32_t _rout0[1],
                                      uint32_t _rout1[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[1] = { 0 };
        uint32_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[( _numIn[0] + 0 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 0 )].buf.nLen = sizeof( _primROut );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 0, 1, 0, 0 ), _pra ) );
        _COPY( _rout0, 0, _primROut, 0, 4 );
        _COPY( _rout1, 0, _primROut, 4, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 0, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasInitSafe )(
            remote_handle64 _handle, int32_t *status, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 2;
        return _stub_method( _handle, _mid, (uint32_t *) status, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_1( remote_handle64 _handle, uint32_t _mid, char *_rout0[1],
                                        uint32_t _rout0Len[1], uint32_t _in1[1],
                                        uint32_t _rout2[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[3] = { 0 };
        uint32_t _primIn[2] = { 0 };
        uint32_t _primROut[1] = { 0 };
        remote_arg *_praIn = 0;
        remote_arg *_praROut = 0;
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _rout0Len, 0, 4 );
        _praIn = ( _pra + 1 );
        _praROut = ( _praIn + _numIn[0] + 1 );
        _praROut[0].buf.pv = _rout0[0];
        _praROut[0].buf.nLen = ( 1 * _rout0Len[0] );
        _COPY( _primIn, 4, _in1, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 2, 0, 0 ), _pra ) );
        _COPY( _rout2, 0, _primROut, 0, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 2, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasVersionSafe )(
            remote_handle64 _handle, uint8_t *version, int versionLen, uint32_t crcTx,
            uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 3;
        return _stub_method_1( _handle, _mid, (char **) &version, (uint32_t *) &versionLen,
                               (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_2( remote_handle64 _handle, uint32_t _mid )
    {
        remote_arg *_pra = 0;
        int _nErr = 0;
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 0, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 0, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult
    __QAIC_STUB( FadasIface_FadasDeInitSafe )( remote_handle64 _handle ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 4;
        return _stub_method_2( _handle, _mid );
    }
    static __inline int _stub_method_3( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                        uint64_t _rout0[1], uint32_t _in1[1], uint32_t _in2[1],
                                        uint32_t _in3[1], uint32_t _in4[1], uint32_t _in5[1],
                                        uint32_t _in6[1], uint32_t _in7[1], uint32_t _in8[1],
                                        uint8_t _in9[1], uint32_t _in10[1], uint32_t _rout11[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[2] = { 0 };
        uint64_t _primIn[6] = { 0 };
        uint64_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _COPY( _primIn, 12, _in2, 0, 4 );
        _COPY( _primIn, 16, _in3, 0, 4 );
        _COPY( _primIn, 20, _in4, 0, 4 );
        _COPY( _primIn, 24, _in5, 0, 4 );
        _COPY( _primIn, 28, _in6, 0, 4 );
        _COPY( _primIn, 32, _in7, 0, 4 );
        _COPY( _primIn, 36, _in8, 0, 4 );
        _COPY( _primIn, 40, _in9, 0, 1 );
        _COPY( _primIn, 44, _in10, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _pra ) );
        _COPY( _rout0, 0, _primROut, 0, 8 );
        _COPY( _rout11, 0, _primROut, 8, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_CreateMapFromMapSafe )(
            remote_handle64 _handle, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
            uint32_t mapWidth, uint32_t mapHeight, int32_t mapXFd, int32_t mapYFd,
            uint32_t mapStride, FadasIface_FadasRemapPipeline_e imgFormat, uint8_t borderConst,
            uint32_t crcTx, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 5;
        return _stub_method_3( _handle, _mid, (uint64_t *) mapPtr, (uint64_t *) mapPtr,
                               (uint32_t *) &camWidth, (uint32_t *) &camHeight,
                               (uint32_t *) &mapWidth, (uint32_t *) &mapHeight,
                               (uint32_t *) &mapXFd, (uint32_t *) &mapYFd, (uint32_t *) &mapStride,
                               (uint32_t *) &imgFormat, (uint8_t *) &borderConst,
                               (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_4( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                        uint64_t _rout0[1], uint32_t _in1[1], uint32_t _in2[1],
                                        uint32_t _in3[1], uint32_t _in4[1], uint32_t _in5[1],
                                        uint8_t _in6[1], uint32_t _in7[1], uint32_t _rout8[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[2] = { 0 };
        uint64_t _primIn[5] = { 0 };
        uint64_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _COPY( _primIn, 12, _in2, 0, 4 );
        _COPY( _primIn, 16, _in3, 0, 4 );
        _COPY( _primIn, 20, _in4, 0, 4 );
        _COPY( _primIn, 24, _in5, 0, 4 );
        _COPY( _primIn, 28, _in6, 0, 1 );
        _COPY( _primIn, 32, _in7, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _pra ) );
        _COPY( _rout0, 0, _primROut, 0, 8 );
        _COPY( _rout8, 0, _primROut, 8, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_CreateMapNoUndistortionSafe )(
            remote_handle64 _handle, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
            uint32_t mapWidth, uint32_t mapHeight, FadasIface_FadasRemapPipeline_e imgFormat,
            uint8_t borderConst, uint32_t crcTx, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 6;
        return _stub_method_4( _handle, _mid, (uint64_t *) mapPtr, (uint64_t *) mapPtr,
                               (uint32_t *) &camWidth, (uint32_t *) &camHeight,
                               (uint32_t *) &mapWidth, (uint32_t *) &mapHeight,
                               (uint32_t *) &imgFormat, (uint8_t *) &borderConst,
                               (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_5( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                        uint32_t _in1[1] )
    {
        remote_arg _pra[1] = { 0 };
        uint64_t _primIn[2] = { 0 };
        int _nErr = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_DestroyMapSafe )(
            remote_handle64 _handle, uint64 mapPtr, uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 7;
        return _stub_method_5( _handle, _mid, (uint64_t *) &mapPtr, (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_6( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                        uint64_t _rout0[1], uint32_t _in1[1], uint32_t _in2[1],
                                        uint32_t _in3[1], uint32_t _rout4[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[2] = { 0 };
        uint64_t _primIn[3] = { 0 };
        uint64_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _COPY( _primIn, 12, _in2, 0, 4 );
        _COPY( _primIn, 16, _in3, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _pra ) );
        _COPY( _rout0, 0, _primROut, 0, 8 );
        _COPY( _rout4, 0, _primROut, 8, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_CreateWorkersSafe )(
            remote_handle64 _handle, uint64 *worker_ptr, uint32_t nThreads,
            FadasIface_FadasRemapPipeline_e imgFormat, uint32_t crcTx,
            uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 8;
        return _stub_method_6( _handle, _mid, (uint64_t *) worker_ptr, (uint64_t *) worker_ptr,
                               (uint32_t *) &nThreads, (uint32_t *) &imgFormat, (uint32_t *) &crcTx,
                               (uint32_t *) crcRx );
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_DestroyWorkersSafe )(
            remote_handle64 _handle, uint64 worker_ptr, uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 9;
        return _stub_method_5( _handle, _mid, (uint64_t *) &worker_ptr, (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_7( remote_handle64 _handle, uint32_t _mid, char *_in0[1],
                                        uint32_t _in0Len[1], char *_in1[1], uint32_t _in1Len[1],
                                        char *_in2[1], uint32_t _in2Len[1], char *_in3[1],
                                        uint32_t _in3Len[1], char *_in4[1], uint32_t _in4Len[1],
                                        uint32_t _in5[1], uint32_t _in6[1], uint32_t _in7[12],
                                        char *_in8[1], uint32_t _in8Len[1], char *_in9[1],
                                        uint32_t _in9Len[1], uint32_t _in10[1] )
    {
        remote_arg _pra[8] = { 0 };
        uint32_t _primIn[22] = { 0 };
        remote_arg *_praIn = 0;
        int _nErr = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _COPY( _primIn, 0, _in0Len, 0, 4 );
        _praIn = ( _pra + 1 );
        _praIn[0].buf.pv = (void *) _in0[0];
        _praIn[0].buf.nLen = ( 8 * _in0Len[0] );
        _COPY( _primIn, 4, _in1Len, 0, 4 );
        _praIn[1].buf.pv = (void *) _in1[0];
        _praIn[1].buf.nLen = ( 8 * _in1Len[0] );
        _COPY( _primIn, 8, _in2Len, 0, 4 );
        _praIn[2].buf.pv = (void *) _in2[0];
        _praIn[2].buf.nLen = ( 4 * _in2Len[0] );
        _COPY( _primIn, 12, _in3Len, 0, 4 );
        _praIn[3].buf.pv = (void *) _in3[0];
        _praIn[3].buf.nLen = ( 4 * _in3Len[0] );
        _COPY( _primIn, 16, _in4Len, 0, 4 );
        _praIn[4].buf.pv = (void *) _in4[0];
        _praIn[4].buf.nLen = ( 48 * _in4Len[0] );
        _COPY( _primIn, 20, _in5, 0, 4 );
        _COPY( _primIn, 24, _in6, 0, 4 );
        _COPY( _primIn, 28, _in7, 0, 48 );
        _COPY( _primIn, 76, _in8Len, 0, 4 );
        _praIn[5].buf.pv = (void *) _in8[0];
        _praIn[5].buf.nLen = ( 16 * _in8Len[0] );
        _COPY( _primIn, 80, _in9Len, 0, 4 );
        _praIn[6].buf.pv = (void *) _in9[0];
        _praIn[6].buf.nLen = ( 12 * _in9Len[0] );
        _COPY( _primIn, 84, _in10, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 8, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 8, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRemap_RunMTSafe )(
            remote_handle64 _handle, const uint64 *workerPtrs, int workerPtrsLen,
            const uint64 *mapPtrs, int mapPtrsLen, const int32_t *srcFds, int srcFdsLen,
            const uint32_t *offsets, int offsetsLen, const FadasIface_FadasImgProps_t *srcProps,
            int srcPropsLen, int32_t dstFd, uint32_t dstLen,
            const FadasIface_FadasImgProps_t *dstProps, const FadasIface_FadasROI_t *dstROIs,
            int dstROIsLen, const FadasIface_FadasNormlzParams_t *normlz, int normlzLen,
            uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 10;
        return _stub_method_7( _handle, _mid, (char **) &workerPtrs, (uint32_t *) &workerPtrsLen,
                               (char **) &mapPtrs, (uint32_t *) &mapPtrsLen, (char **) &srcFds,
                               (uint32_t *) &srcFdsLen, (char **) &offsets,
                               (uint32_t *) &offsetsLen, (char **) &srcProps,
                               (uint32_t *) &srcPropsLen, (uint32_t *) &dstFd, (uint32_t *) &dstLen,
                               (uint32_t *) dstProps, (char **) &dstROIs, (uint32_t *) &dstROIsLen,
                               (char **) &normlz, (uint32_t *) &normlzLen, (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_8( remote_handle64 _handle, uint32_t _mid, uint32_t _in0[1],
                                        uint32_t _in1[1], uint32_t _in2[1] )
    {
        remote_arg _pra[1] = { 0 };
        uint32_t _primIn[3] = { 0 };
        int _nErr = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _COPY( _primIn, 0, _in0, 0, 4 );
        _COPY( _primIn, 4, _in1, 0, 4 );
        _COPY( _primIn, 8, _in2, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult
    __QAIC_STUB( FadasIface_mmapSafe )( remote_handle64 _handle, int32_t bufFd, uint32_t bufSize,
                                        uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 11;
        return _stub_method_8( _handle, _mid, (uint32_t *) &bufFd, (uint32_t *) &bufSize,
                               (uint32_t *) &crcTx );
    }
    __QAIC_STUB_EXPORT AEEResult
    __QAIC_STUB( FadasIface_munmapSafe )( remote_handle64 _handle, int32_t bufFd, uint32_t bufSize,
                                          uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 12;
        return _stub_method_8( _handle, _mid, (uint32_t *) &bufFd, (uint32_t *) &bufSize,
                               (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_9( remote_handle64 _handle, uint32_t _mid, uint32_t _in0[1],
                                        uint32_t _in1[1], uint32_t _in2[1], uint32_t _in3[1],
                                        uint32_t _in4[1], uint32_t _in5[1] )
    {
        remote_arg _pra[1] = { 0 };
        uint32_t _primIn[6] = { 0 };
        int _nErr = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _COPY( _primIn, 0, _in0, 0, 4 );
        _COPY( _primIn, 4, _in1, 0, 4 );
        _COPY( _primIn, 8, _in2, 0, 4 );
        _COPY( _primIn, 12, _in3, 0, 4 );
        _COPY( _primIn, 16, _in4, 0, 4 );
        _COPY( _primIn, 20, _in5, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasRegBufSafe )(
            remote_handle64 _handle, FadasIface_FadasBufType_e bufType, int32_t bufFd,
            uint32_t bufSize, uint32_t bufOffset, uint32_t batchSize,
            uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 13;
        return _stub_method_9( _handle, _mid, (uint32_t *) &bufType, (uint32_t *) &bufFd,
                               (uint32_t *) &bufSize, (uint32_t *) &bufOffset,
                               (uint32_t *) &batchSize, (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_10( remote_handle64 _handle, uint32_t _mid, uint32_t _in0[1],
                                         uint32_t _in1[1], uint32_t _in2[1], uint32_t _in3[1],
                                         uint32_t _in4[1] )
    {
        remote_arg _pra[1] = { 0 };
        uint32_t _primIn[5] = { 0 };
        int _nErr = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _COPY( _primIn, 0, _in0, 0, 4 );
        _COPY( _primIn, 4, _in1, 0, 4 );
        _COPY( _primIn, 8, _in2, 0, 4 );
        _COPY( _primIn, 12, _in3, 0, 4 );
        _COPY( _primIn, 16, _in4, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _pra ) );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 0, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_FadasDeregBufSafe )(
            remote_handle64 _handle, int32_t bufFd, uint32_t bufSize, uint32_t bufOffset,
            uint32_t batchSize, uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 14;
        return _stub_method_10( _handle, _mid, (uint32_t *) &bufFd, (uint32_t *) &bufSize,
                                (uint32_t *) &bufOffset, (uint32_t *) &batchSize,
                                (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_11( remote_handle64 _handle, uint32_t _mid, uint32_t _in0[3],
                                         uint32_t _in1[3], uint32_t _in2[3], uint32_t _in3[1],
                                         uint32_t _in4[1], uint32_t _in5[1], uint32_t _in6[1],
                                         uint32_t _in7[1], uint64_t _rout8[1], uint32_t _in9[1],
                                         uint32_t _rout10[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[2] = { 0 };
        uint32_t _primIn[15] = { 0 };
        uint64_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 12 );
        _COPY( _primIn, 12, _in1, 0, 12 );
        _COPY( _primIn, 24, _in2, 0, 12 );
        _COPY( _primIn, 36, _in3, 0, 4 );
        _COPY( _primIn, 40, _in4, 0, 4 );
        _COPY( _primIn, 44, _in5, 0, 4 );
        _COPY( _primIn, 48, _in6, 0, 4 );
        _COPY( _primIn, 52, _in7, 0, 4 );
        _COPY( _primIn, 56, _in9, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _pra ) );
        _COPY( _rout8, 0, _primROut, 0, 8 );
        _COPY( _rout10, 0, _primROut, 8, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_PointPillarCreateSafe )(
            remote_handle64 _handle, const FadasIface_Pt3D_t *pPlrSize,
            const FadasIface_Pt3D_t *pMinRange, const FadasIface_Pt3D_t *pMaxRange,
            uint32_t maxNumInPts, uint32_t numInFeatureDim, uint32_t maxNumPlrs,
            uint32_t maxNumPtsPerPlr, uint32_t numOutFeatureDim, uint64_t *phPreProc,
            uint32_t crcTx, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 15;
        return _stub_method_11( _handle, _mid, (uint32_t *) pPlrSize, (uint32_t *) pMinRange,
                                (uint32_t *) pMaxRange, (uint32_t *) &maxNumInPts,
                                (uint32_t *) &numInFeatureDim, (uint32_t *) &maxNumPlrs,
                                (uint32_t *) &maxNumPtsPerPlr, (uint32_t *) &numOutFeatureDim,
                                (uint64_t *) phPreProc, (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_12( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                         uint32_t _in1[1], uint32_t _in2[1], uint32_t _in3[1],
                                         uint32_t _in4[1], uint32_t _in5[1], uint32_t _in6[1],
                                         uint32_t _in7[1], uint32_t _in8[1], uint32_t _in9[1],
                                         uint32_t _in10[1], uint32_t _rout11[1], uint32_t _in12[1],
                                         uint32_t _rout13[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[2] = { 0 };
        uint64_t _primIn[7] = { 0 };
        uint32_t _primROut[2] = { 0 };
        int _nErr = 0;
        _numIn[0] = 0;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _COPY( _primIn, 12, _in2, 0, 4 );
        _COPY( _primIn, 16, _in3, 0, 4 );
        _COPY( _primIn, 20, _in4, 0, 4 );
        _COPY( _primIn, 24, _in5, 0, 4 );
        _COPY( _primIn, 28, _in6, 0, 4 );
        _COPY( _primIn, 32, _in7, 0, 4 );
        _COPY( _primIn, 36, _in8, 0, 4 );
        _COPY( _primIn, 40, _in9, 0, 4 );
        _COPY( _primIn, 44, _in10, 0, 4 );
        _COPY( _primIn, 48, _in12, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _pra ) );
        _COPY( _rout11, 0, _primROut, 0, 4 );
        _COPY( _rout13, 0, _primROut, 4, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 1, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_PointPillarRunSafe )(
            remote_handle64 _handle, uint64_t hPreProc, uint32_t numPts, int32_t fdInPts,
            uint32_t inPtsOffset, uint32_t inPtsSize, int32_t fdOutPlrs, uint32_t outPlrsOffset,
            uint32_t outPlrsSize, int32_t fdOutFeature, uint32_t outFeatureOffset,
            uint32_t outFeatureSize, uint32_t *pNumOutPlrs, uint32_t crcTx,
            uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 16;
        return _stub_method_12(
                _handle, _mid, (uint64_t *) &hPreProc, (uint32_t *) &numPts, (uint32_t *) &fdInPts,
                (uint32_t *) &inPtsOffset, (uint32_t *) &inPtsSize, (uint32_t *) &fdOutPlrs,
                (uint32_t *) &outPlrsOffset, (uint32_t *) &outPlrsSize, (uint32_t *) &fdOutFeature,
                (uint32_t *) &outFeatureOffset, (uint32_t *) &outFeatureSize,
                (uint32_t *) pNumOutPlrs, (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_PointPillarDestroySafe )(
            remote_handle64 _handle, uint64_t hPreProc, uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 17;
        return _stub_method_5( _handle, _mid, (uint64_t *) &hPreProc, (uint32_t *) &crcTx );
    }
    static __inline int _stub_method_13( remote_handle64 _handle, uint32_t _mid, uint32_t _in0[1],
                                         uint32_t _in1[1], uint32_t _in2[1], uint32_t _in3[1],
                                         uint32_t _in4[6], float _in5[1], float _in6[1],
                                         float _in7[1], float _in8[1], float _in9[1],
                                         float _in10[1], float _in11[1], float _in12[1],
                                         char *_in13[1], uint32_t _in13Len[1], uint32_t _in14[1],
                                         uint64_t _rout15[1], uint32_t _in16[1],
                                         uint32_t _rout17[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[3] = { 0 };
        uint32_t _primIn[21] = { 0 };
        uint64_t _primROut[2] = { 0 };
        remote_arg *_praIn = 0;
        int _nErr = 0;
        _numIn[0] = 1;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 4 );
        _COPY( _primIn, 4, _in1, 0, 4 );
        _COPY( _primIn, 8, _in2, 0, 4 );
        _COPY( _primIn, 12, _in3, 0, 4 );
        _COPY( _primIn, 16, _in4, 0, 24 );
        _COPY( _primIn, 40, _in5, 0, 4 );
        _COPY( _primIn, 44, _in6, 0, 4 );
        _COPY( _primIn, 48, _in7, 0, 4 );
        _COPY( _primIn, 52, _in8, 0, 4 );
        _COPY( _primIn, 56, _in9, 0, 4 );
        _COPY( _primIn, 60, _in10, 0, 4 );
        _COPY( _primIn, 64, _in11, 0, 4 );
        _COPY( _primIn, 68, _in12, 0, 4 );
        _COPY( _primIn, 72, _in13Len, 0, 4 );
        _praIn = ( _pra + 1 );
        _praIn[0].buf.pv = (void *) _in13[0];
        _praIn[0].buf.nLen = ( 1 * _in13Len[0] );
        _COPY( _primIn, 76, _in14, 0, 4 );
        _COPY( _primIn, 80, _in16, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 2, 1, 0, 0 ), _pra ) );
        _COPY( _rout15, 0, _primROut, 0, 8 );
        _COPY( _rout17, 0, _primROut, 8, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 2, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_ExtractBBoxCreateSafe )(
            remote_handle64 _handle, uint32_t maxNumInPts, uint32_t numInFeatureDim,
            uint32_t maxNumDetOut, uint32_t numClass, const FadasIface_Grid2D_t *pGrid,
            float threshScore, float threshIOU, float minCentreX, float minCentreY,
            float minCentreZ, float maxCentreX, float maxCentreY, float maxCentreZ,
            const uint8_t *labelSelect, int labelSelectLen, uint32_t maxNumFilter,
            uint64_t *phPostProc, uint32_t crcTx, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 18;
        return _stub_method_13( _handle, _mid, (uint32_t *) &maxNumInPts,
                                (uint32_t *) &numInFeatureDim, (uint32_t *) &maxNumDetOut,
                                (uint32_t *) &numClass, (uint32_t *) pGrid, (float *) &threshScore,
                                (float *) &threshIOU, (float *) &minCentreX, (float *) &minCentreY,
                                (float *) &minCentreZ, (float *) &maxCentreX, (float *) &maxCentreY,
                                (float *) &maxCentreZ, (char **) &labelSelect,
                                (uint32_t *) &labelSelectLen, (uint32_t *) &maxNumFilter,
                                (uint64_t *) phPostProc, (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    static __inline int _stub_method_14( remote_handle64 _handle, uint32_t _mid, uint64_t _in0[1],
                                         uint32_t _in1[1], char *_in2[1], uint32_t _in2Len[1],
                                         char *_in3[1], uint32_t _in3Len[1], char *_in4[1],
                                         uint32_t _in4Len[1], uint8_t _in5[1], uint8_t _in6[1],
                                         uint32_t _rout7[1], uint32_t _in8[1], uint32_t _rout9[1] )
    {
        int _numIn[1] = { 0 };
        remote_arg _pra[5] = { 0 };
        uint64_t _primIn[4] = { 0 };
        uint32_t _primROut[2] = { 0 };
        remote_arg *_praIn = 0;
        int _nErr = 0;
        _numIn[0] = 3;
        _pra[0].buf.pv = (void *) _primIn;
        _pra[0].buf.nLen = sizeof( _primIn );
        _pra[( _numIn[0] + 1 )].buf.pv = (void *) _primROut;
        _pra[( _numIn[0] + 1 )].buf.nLen = sizeof( _primROut );
        _COPY( _primIn, 0, _in0, 0, 8 );
        _COPY( _primIn, 8, _in1, 0, 4 );
        _COPY( _primIn, 12, _in2Len, 0, 4 );
        _praIn = ( _pra + 1 );
        _praIn[0].buf.pv = (void *) _in2[0];
        _praIn[0].buf.nLen = ( 4 * _in2Len[0] );
        _COPY( _primIn, 16, _in3Len, 0, 4 );
        _praIn[1].buf.pv = (void *) _in3[0];
        _praIn[1].buf.nLen = ( 4 * _in3Len[0] );
        _COPY( _primIn, 20, _in4Len, 0, 4 );
        _praIn[2].buf.pv = (void *) _in4[0];
        _praIn[2].buf.nLen = ( 4 * _in4Len[0] );
        _COPY( _primIn, 24, _in5, 0, 1 );
        _COPY( _primIn, 25, _in6, 0, 1 );
        _COPY( _primIn, 28, _in8, 0, 4 );
        _TRY_FARF( _nErr, __QAIC_REMOTE( remote_handle64_invoke )(
                                  _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 4, 1, 0, 0 ), _pra ) );
        _COPY( _rout7, 0, _primROut, 0, 4 );
        _COPY( _rout9, 0, _primROut, 4, 4 );
        _CATCH_FARF( _nErr )
        {
            _QAIC_FARF( RUNTIME_ERROR,
                        "ERROR 0x%x: handle=0x%" PRIx64 ", scalar=0x%x, method ID=%d: %s failed\n",
                        _nErr, _handle, REMOTE_SCALARS_MAKEX( 0, _mid, 4, 1, 0, 0 ), _mid,
                        __func__ );
        }
        return _nErr;
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_ExtractBBoxRunSafe )(
            remote_handle64 _handle, uint64_t hPostProc, uint32_t numPts, const int32_t *fds,
            int fdsLen, const uint32_t *offsets, int offsetsLen, const uint32_t *sizes,
            int sizesLen, uint8_t bMapPtsToBBox, uint8_t bBBoxFilter, uint32_t *pNumDetOut,
            uint32_t crcTx, uint32_t *crcRx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 19;
        return _stub_method_14( _handle, _mid, (uint64_t *) &hPostProc, (uint32_t *) &numPts,
                                (char **) &fds, (uint32_t *) &fdsLen, (char **) &offsets,
                                (uint32_t *) &offsetsLen, (char **) &sizes, (uint32_t *) &sizesLen,
                                (uint8_t *) &bMapPtsToBBox, (uint8_t *) &bBBoxFilter,
                                (uint32_t *) pNumDetOut, (uint32_t *) &crcTx, (uint32_t *) crcRx );
    }
    __QAIC_STUB_EXPORT AEEResult __QAIC_STUB( FadasIface_ExtractBBoxDestroySafe )(
            remote_handle64 _handle, uint64_t hPostProc, uint32_t crcTx ) __QAIC_STUB_ATTRIBUTE
    {
        uint32_t _mid = 20;
        return _stub_method_5( _handle, _mid, (uint64_t *) &hPostProc, (uint32_t *) &crcTx );
    }
#ifdef __cplusplus
}
#endif
#endif   //_FADASIFACE_STUB_H
