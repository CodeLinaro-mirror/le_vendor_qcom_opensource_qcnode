// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "FadasMock.h"
#include "HAP_mem.h"
#include "HAP_power.h"
#include "qurt.h"
#include <AEEStdDef.h>
#include <AEEStdErr.h>
#include <crc32.h>
#include <dlfcn.h>
#include <fadas.h>
#include "FadasIface.h"
#include "FadasIfaceSafe.hpp"
#include <map>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#define CRC_RET_SUCCESS_VALUE 0
typedef struct
{
    MockAPI_Action_e action;
    void *param;
} MockControlParam_t;
static MockControlParam_t s_MockParams[MOCK_API_MAX];
// HAP_mem buffer map — declared before MockApi_ResetAll so it can clear it
struct BufferInfo
{
    void *addr;
    size_t size;
    int refCount;
};
static std::map<int32_t, BufferInfo> s_bufferMap;
static std::mutex s_bufferMapMutex;
extern "C" void MockApi_Control( MockAPI_ID_e apiId, MockAPI_Action_e action, void *param )
{
    if ( apiId < MOCK_API_MAX )
    {
        s_MockParams[apiId].action = action;
        s_MockParams[apiId].param = param;
    }
}

extern "C" void MockApi_ResetAll()
{
    for ( int i = 0; i < MOCK_API_MAX; i++ )
    {
        s_MockParams[i].action = MOCK_CONTROL_NONE;
        s_MockParams[i].param = NULL;
    }

    // Free all mapped buffers and clear the map to reset test environment
    std::lock_guard<std::mutex> lock( s_bufferMapMutex );
    for ( auto &entry : s_bufferMap )
    {
        if ( entry.second.addr )
        {
            free( entry.second.addr );
        }
    }
    s_bufferMap.clear();
}


// HAP_power
int HAP_power_set( void *ctx, HAP_power_request_t *request )
{
    int ret = 0;   // Success
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_HAP_POWER_SET].action )
    {
        ret = *(int *) s_MockParams[MOCK_API_HAP_POWER_SET].param;
        s_MockParams[MOCK_API_HAP_POWER_SET].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

int HAP_power_destroy( void *ctx )
{
    return 0;   // Success
}

// HAP_mem
AEEResult HAP_mmap_get( int32_t fd, void **buf, void *data )
{
    AEEResult ret = AEE_SUCCESS;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_HAP_MMAP_GET].action )
    {
        ret = *(AEEResult *) s_MockParams[MOCK_API_HAP_MMAP_GET].param;
        s_MockParams[MOCK_API_HAP_MMAP_GET].action = MOCK_CONTROL_NONE;
        // If forcing failure, return early
        if ( ret != AEE_SUCCESS ) return ret;
    }

    // Default behavior or successful mock override
    std::lock_guard<std::mutex> lock( s_bufferMapMutex );
    auto it = s_bufferMap.find( fd );
    if ( it != s_bufferMap.end() )
    {
        if ( buf )
        {
            *buf = it->second.addr;
            it->second.refCount++;
        }
        return AEE_SUCCESS;
    }
    else
    {
        // Not found, should return error as per requirement "check if fd is in this s_map"
        // But the requirement says "if not allocate memory for it". Wait, that was for HAP_mmap?
        // Requirement: "HAP_mmap to check if fd is in this s_map or not, if not allocate...
        // HAP_mmap_get... to get the addr" So HAP_mmap_get expects it to be there? Usually
        // HAP_mmap_get maps an existing FD. If the user says HAP_mmap allocates, then HAP_mmap must
        // be called first? But FadasIfaceCoreSafe calls HAP_mmap_get first to check if mapped. If
        // HAP_mmap_get returns error, it calls HAP_mmap. So HAP_mmap_get should fail if not in map.
        // But previously I returned success and allocated.
        // If I return fail here, FadasIfaceCoreSafe will call HAP_mmap.
        // HAP_mmap will then allocate.
        // This flow matches FadasIfaceCoreSafe logic.
        return AEE_EFAILED;
    }
}

AEEResult HAP_mmap_put( int32_t fd )
{
    AEEResult ret = AEE_SUCCESS;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_HAP_MMAP_PUT].action )
    {
        ret = *(AEEResult *) s_MockParams[MOCK_API_HAP_MMAP_PUT].param;
        s_MockParams[MOCK_API_HAP_MMAP_PUT].action = MOCK_CONTROL_NONE;
        // If forcing failure, return early
        if ( ret != AEE_SUCCESS ) return ret;
    }

    std::lock_guard<std::mutex> lock( s_bufferMapMutex );
    auto it = s_bufferMap.find( fd );
    if ( it != s_bufferMap.end() )
    {
        if ( it->second.refCount > 0 )
        {
            it->second.refCount--;
            return AEE_SUCCESS;
        }
        else
        {
            return AEE_EFAILED;
        }
    }
    return AEE_EFAILED;   // Error
}

void *HAP_mmap( void *addr, int len, int prot, int flags, int fd, long long offset )
{
    void *ret = NULL;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_HAP_MMAP].action )
    {
        ret = *(void **) s_MockParams[MOCK_API_HAP_MMAP].param;
        s_MockParams[MOCK_API_HAP_MMAP].action = MOCK_CONTROL_NONE;
        return ret;
    }

    std::lock_guard<std::mutex> lock( s_bufferMapMutex );
    auto it = s_bufferMap.find( fd );
    if ( it == s_bufferMap.end() )
    {
        if ( ret == NULL )
        {
            ret = malloc( len );
        }
        if ( ret )
        {
            s_bufferMap[fd] = { ret, (size_t) len, 1 };
        }
    }
    else
    {
        // Already mapped
        ret = it->second.addr;
        it->second.refCount++;   // User didn't specify ref count behavior for mmap existing, assume
                                 // inc
    }
    return ret;
}

AEEResult HAP_munmap( void *addr, int len )
{
    AEEResult ret = AEE_SUCCESS;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_HAP_UNMAP].action )
    {
        ret = *(AEEResult *) s_MockParams[MOCK_API_HAP_UNMAP].param;
        s_MockParams[MOCK_API_HAP_UNMAP].action = MOCK_CONTROL_NONE;
        // If forcing failure, return early
        if ( ret != AEE_SUCCESS ) return ret;
    }
    std::lock_guard<std::mutex> lock( s_bufferMapMutex );
    // Find by addr
    for ( auto it = s_bufferMap.begin(); it != s_bufferMap.end(); ++it )
    {
        if ( it->second.addr == addr )
        {
            free( it->second.addr );
            s_bufferMap.erase( it );
            return 0;
        }
    }
    return 1;   // Not found
}

// qurt
void qurt_mutex_init( qurt_mutex_t *mutex )
{
    // No-op
}

void qurt_mutex_lock( qurt_mutex_t *mutex )
{
    // No-op
}

void qurt_mutex_unlock( qurt_mutex_t *mutex )
{
    // No-op
}

void qurt_mem_cache_clean( qurt_addr_t addr, size_t size, int op, int type )
{
    // No-op
}

// crc32
extern "C" error_type crc32_verify_scatter( const struct scatter_buffer *sb, sl_size_t count,
                                            sl_u32_t crc )
{
    error_type ret = (error_type)CRC_RET_SUCCESS_VALUE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_CRC32_VERIFY_SCATTER].action )
    {
        ret = *(error_type *) s_MockParams[MOCK_API_CRC32_VERIFY_SCATTER].param;
        s_MockParams[MOCK_API_CRC32_VERIFY_SCATTER].action = MOCK_CONTROL_NONE;
	return ret;
    }

    if ( 0xdeadbeef == crc )
    {
        return CRC_ERROR;
    }
    return ret;
}

extern "C" error_type crc32_generate_scatter( const struct scatter_buffer *sb, sl_size_t count,
                                              sl_u32_t *crc )
{
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_CRC32_GENERATE_SCATTER].action )
    {
        error_type ret = *(error_type *) s_MockParams[MOCK_API_CRC32_GENERATE_SCATTER].param;
        s_MockParams[MOCK_API_CRC32_GENERATE_SCATTER].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( crc && 0xdeadbeef == *crc )
    {
        return CRC_ERROR;
    }
    return (error_type)CRC_RET_SUCCESS_VALUE;
}

// Fadas
const char *FadasVersion( void )
{
    return "MOCK_VERSION";
}

FadasError_e FadasInit( const char *licenseKey )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_INIT].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_INIT].param;
        s_MockParams[MOCK_API_FADAS_INIT].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasDeInit( void )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_DEINIT].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_DEINIT].param;
        s_MockParams[MOCK_API_FADAS_DEINIT].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasRegBuf( FadasBufType_e bufType, const void *buf, size_t bufSize, int32_t fd , int32_t offset )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REG_BUF].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REG_BUF].param;
        s_MockParams[MOCK_API_FADAS_REG_BUF].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasRegBuf( FadasBufType_e bufType, const void *buf, size_t bufSize, int32_t fd )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REG_BUF].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REG_BUF].param;
        s_MockParams[MOCK_API_FADAS_REG_BUF].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasRegBuf( FadasBufType_e bufType, const void *buf, size_t bufSize )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REG_BUF].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REG_BUF].param;
        s_MockParams[MOCK_API_FADAS_REG_BUF].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasDeregBuf( const void *buf )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_DEREG_BUF].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_DEREG_BUF].param;
        s_MockParams[MOCK_API_FADAS_DEREG_BUF].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

// Fadas Remap
FadasRemapMap *FadasRemap_CreateMapFromMap( uint32_t camWidth, uint32_t camHeight,
                                            uint32_t mapWidth, uint32_t mapHeight,
                                            uint32_t mapStride, const float32_t *mapX,
                                            const float32_t *mapY, FadasRemapPipeline_e ePipeline,
                                            uint8_t borderConst, uint32_t nThreads )
{
    FadasRemapMap *ret = (FadasRemapMap *) malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP].action )
    {
        if ( ret ) free( ret );
        ret = *(FadasRemapMap **) s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasRemapMap *FadasRemap_CreateMapNoUndistortion( uint32_t srcWidth, uint32_t srcHeight,
                                                   uint32_t dstWidth, uint32_t dstHeight,
                                                   FadasRemapPipeline_e ePipeline,
                                                   uint8_t borderConst )
{
    FadasRemapMap *ret = (FadasRemapMap *) malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN ==
         s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION].action )
    {
        if ( ret ) free( ret );
        ret = *(FadasRemapMap **) s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION]
                       .param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasRemap_DestroyMap( FadasRemapMap *map_ )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP].param;
        s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( map_ ) free( map_ );
    return FADAS_ERROR_NONE;
}

void *FadasRemap_CreateWorkers( uint32_t nThreads, int32_t pThreadsAffinity[],
                                FadasRemapPipeline_e ePipeline )
{
    void *ret = malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS].action )
    {
        if ( ret ) free( ret );
        ret = *(void **) s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasRemap_DestroyWorkers( void *wrkrs )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS].param;
        s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( wrkrs ) free( wrkrs );
    return FADAS_ERROR_NONE;
}

FadasError_e FadasRemap_RunMT( void *wrkrs, FadasRemapMap *map_, FadasImage_t *src,
                               FadasImage_t *dst, FadasROI_t *mapROI, float32_t roiScale,
                               FadasNormlzParams_t *normlz )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT].param;
        s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

// Fadas VM
void *FadasVM_PointPillar_Create( FadasPt_3Df32_t plrSize, FadasPt_3Df32_t minRange,
                                  FadasPt_3Df32_t maxRange, uint32_t maxNumPtsIn,
                                  uint32_t numInFeatureDim, uint32_t maxNumPlrs,
                                  uint32_t maxNumPtsPerPlr, uint32_t numOutFeatureDim )
{
    void *ret = malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].action )
    {
        if ( ret ) free( ret );
        ret = *(void **)s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].param;
        s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

void *FadasVM_PointPillar_Create( FadasPt_3Df32_t plrSize, FadasPt_3Df32_t minRange,
                                  FadasPt_3Df32_t maxRange, uint32_t maxNumPtsIn,
                                  uint32_t numInFeatureDim, uint32_t maxNumPlrs,
                                  uint32_t maxNumPtsPerPlr, uint32_t numOutFeatureDim,
                                  bool bSpreadPointsInPillar )
{
    void *ret = malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].action )
    {
        if ( ret ) free( ret );
        ret = s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].param;
        s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_CREATE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasVM_PointPillar_Run( void *hPtPlr, uint32_t numPts, const float32_t *pInPts,
                                      FadasVM_PointPillar_t *pOutPlrs, float32_t *pOutFeature,
                                      uint32_t *pNumOutPlrs )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( pNumOutPlrs ) *pNumOutPlrs = 0;

    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_RUN].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_RUN].param;
        s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_RUN].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasVM_PointPillar_Destroy( void *hPtPlr )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_DESTROY].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_DESTROY].param;
        s_MockParams[MOCK_API_FADAS_VM_POINTPILLAR_DESTROY].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( hPtPlr ) free( hPtPlr );
    return ret;
}

void *FadasVM_ExtractBBox_Create( const Fadas3DBBoxInitParams_t *pBBoxInitParams )
{
    void *ret = malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_CREATE].action )
    {
        if ( ret ) free( ret );
        ret = *(void **) s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_CREATE].param;
        s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_CREATE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasVM_ExtractBBox_Run( void *hExtractBBox, uint32_t numPtsIn,
                                      const float32_t *pInPtsBuf, Fadas3DRPNBufs_t rpnBuf,
                                      Fadas3DBBoxBufs_t outBuf, uint32_t *pNumDetOut,
                                      bool bMapPtsToBBox, bool bBBoxFilter )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( pNumDetOut ) *pNumDetOut = 0;

    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_RUN].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_RUN].param;
        s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_RUN].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

FadasError_e FadasVM_ExtractBBox_Destroy( void *hExtractBBox )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_DESTROY].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_DESTROY].param;
        s_MockParams[MOCK_API_FADAS_VM_EXTRACTBBOX_DESTROY].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( hExtractBBox ) free( hExtractBBox );
    return ret;
}

/* ---- _Safe stubs (called by FadasIfaceSafe.cpp) ---- */

AEEResult FadasIface_FadasInitSafe(remote_handle64 _h, int32_t *status, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_INIT_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_INIT_SAFE].param;
        s_MockParams[MOCK_API_FADAS_INIT_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (status) *status = 0;
    if (crcRx)  *crcRx  = 0;
    return ret;
}

AEEResult FadasIface_FadasVersionSafe(remote_handle64 _h, uint8_t *version, int versionLen,
                                       uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_VERSION_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_VERSION_SAFE].param;
        s_MockParams[MOCK_API_FADAS_VERSION_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (version && versionLen > 0) version[0] = 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_FadasDeInitSafe(remote_handle64 _h)
{
    return AEE_SUCCESS;
}

AEEResult FadasIface_FadasRemap_CreateMapFromMapSafe(
    remote_handle64 _h, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
    uint32_t mapWidth, uint32_t mapHeight, int32_t mapXFd, int32_t mapYFd,
    uint32_t mapStride, FadasIface_FadasRemapPipeline_e imgFormat, uint8_t borderConst,
    uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_FROM_MAP_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (mapPtr) *mapPtr = (ret == AEE_SUCCESS) ? (uint64)(uintptr_t)malloc(4) : 0;
    if (crcRx)  *crcRx  = 0;
    return ret;
}

AEEResult FadasIface_FadasRemap_CreateMapNoUndistortionSafe(
    remote_handle64 _h, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
    uint32_t mapWidth, uint32_t mapHeight, FadasIface_FadasRemapPipeline_e imgFormat,
    uint8_t borderConst, uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (mapPtr) *mapPtr = (ret == AEE_SUCCESS) ? (uint64)(uintptr_t)malloc(4) : 0;
    if (crcRx)  *crcRx  = 0;
    return ret;
}

AEEResult FadasIface_FadasRemap_DestroyMapSafe(remote_handle64 _h, uint64 mapPtr, uint32_t crcTx)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_SAFE].action) {
        AEEResult ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_SAFE].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if (mapPtr) free((void*)(uintptr_t)mapPtr);
    return AEE_SUCCESS;
}

AEEResult FadasIface_FadasRemap_CreateWorkersSafe(
    remote_handle64 _h, uint64 *worker_ptr, uint32_t nThreads,
    FadasIface_FadasRemapPipeline_e imgFormat, uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_WORKERS_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (worker_ptr) *worker_ptr = (ret == AEE_SUCCESS) ? (uint64)(uintptr_t)malloc(4) : 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_FadasRemap_DestroyWorkersSafe(remote_handle64 _h, uint64 worker_ptr, uint32_t crcTx)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS_SAFE].action) {
        AEEResult ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_WORKERS_SAFE].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if (worker_ptr) free((void*)(uintptr_t)worker_ptr);
    return AEE_SUCCESS;
}

AEEResult FadasIface_FadasRemap_RunMTSafe(
    remote_handle64 _h, const uint64 *workerPtrs, int workerPtrsLen,
    const uint64 *mapPtrs, int mapPtrsLen, const int32_t *srcFds, int srcFdsLen,
    const uint32_t *offsets, int offsetsLen, const FadasIface_FadasImgProps_t *srcProps,
    int srcPropsLen, int32_t dstFd, uint32_t dstLen, const FadasIface_FadasImgProps_t *dstProps,
    const FadasIface_FadasROI_t *dstROIs, int dstROIsLen,
    const FadasIface_FadasNormlzParams_t *normlz, int normlzLen, uint32_t crcTx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REMAP_RUN_MT_SAFE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

AEEResult FadasIface_mmapSafe(remote_handle64 _h, int32_t bufFd, uint32_t bufSize, uint32_t crcTx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_MMAP_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_MMAP_SAFE].param;
        s_MockParams[MOCK_API_FADAS_MMAP_SAFE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

AEEResult FadasIface_munmapSafe(remote_handle64 _h, int32_t bufFd, uint32_t bufSize, uint32_t crcTx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_MUNMAP_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_MUNMAP_SAFE].param;
        s_MockParams[MOCK_API_FADAS_MUNMAP_SAFE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

AEEResult FadasIface_FadasRegBufSafe(remote_handle64 _h, FadasIface_FadasBufType_e bufType,
    int32_t bufFd, uint32_t bufSize, uint32_t bufOffset, uint32_t batchSize, uint32_t crcTx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REG_BUF_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_REG_BUF_SAFE].param;
        s_MockParams[MOCK_API_FADAS_REG_BUF_SAFE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

AEEResult FadasIface_FadasDeregBufSafe(remote_handle64 _h, int32_t bufFd, uint32_t bufSize,
    uint32_t bufOffset, uint32_t batchSize, uint32_t crcTx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_DEREG_BUF_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_DEREG_BUF_SAFE].param;
        s_MockParams[MOCK_API_FADAS_DEREG_BUF_SAFE].action = MOCK_CONTROL_NONE;
    }
    return ret;
}

AEEResult FadasIface_PointPillarCreateSafe(remote_handle64 _h,
    const FadasIface_Pt3D_t *pPlrSize, const FadasIface_Pt3D_t *pMinRange,
    const FadasIface_Pt3D_t *pMaxRange, uint32_t maxNumInPts, uint32_t numInFeatureDim,
    uint32_t maxNumPlrs, uint32_t maxNumPtsPerPlr, uint32_t numOutFeatureDim,
    uint64_t *phPreProc, uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE].param;
        s_MockParams[MOCK_API_FADAS_POINT_PILLAR_CREATE_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (phPreProc) *phPreProc = (ret == AEE_SUCCESS) ? (uint64_t)(uintptr_t)malloc(4) : 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_PointPillarRunSafe(remote_handle64 _h, uint64_t hPreProc, uint32_t numPts,
    int32_t fdInPts, uint32_t inPtsOffset, uint32_t inPtsSize, int32_t fdOutPlrs,
    uint32_t outPlrsOffset, uint32_t outPlrsSize, int32_t fdOutFeature,
    uint32_t outFeatureOffset, uint32_t outFeatureSize, uint32_t *pNumOutPlrs,
    uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_POINT_PILLAR_RUN_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_POINT_PILLAR_RUN_SAFE].param;
        s_MockParams[MOCK_API_FADAS_POINT_PILLAR_RUN_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (pNumOutPlrs) *pNumOutPlrs = 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_PointPillarDestroySafe(remote_handle64 _h, uint64_t hPreProc, uint32_t crcTx)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_POINT_PILLAR_DESTROY_SAFE].action) {
        AEEResult ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_POINT_PILLAR_DESTROY_SAFE].param;
        s_MockParams[MOCK_API_FADAS_POINT_PILLAR_DESTROY_SAFE].action = MOCK_CONTROL_NONE;
        return ret;
    }
    /* Do NOT free hPreProc — it may be a DSP handle, not a heap pointer */
    return AEE_SUCCESS;
}

AEEResult FadasIface_ExtractBBoxCreateSafe(remote_handle64 _h, uint32_t maxNumInPts,
    uint32_t numInFeatureDim, uint32_t maxNumDetOut, uint32_t numClass,
    const FadasIface_Grid2D_t *pGrid, float threshScore, float threshIOU,
    float minCentreX, float minCentreY, float minCentreZ,
    float maxCentreX, float maxCentreY, float maxCentreZ,
    const uint8_t *labelSelect, int labelSelectLen, uint32_t maxNumFilter,
    uint64_t *phPostProc, uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_CREATE_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_CREATE_SAFE].param;
        s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_CREATE_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (phPostProc) *phPostProc = (ret == AEE_SUCCESS) ? (uint64_t)(uintptr_t)malloc(4) : 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_ExtractBBoxRunSafe(remote_handle64 _h, uint64_t hPostProc, uint32_t numPts,
    const int32_t *fds, int fdsLen, const uint32_t *offsets, int offsetsLen,
    const uint32_t *sizes, int sizesLen, uint8_t bMapPtsToBBox, uint8_t bBBoxFilter,
    uint32_t *pNumDetOut, uint32_t crcTx, uint32_t *crcRx)
{
    AEEResult ret = AEE_SUCCESS;
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_RUN_SAFE].action) {
        ret = *(AEEResult*)s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_RUN_SAFE].param;
        s_MockParams[MOCK_API_FADAS_EXTRACT_BBOX_RUN_SAFE].action = MOCK_CONTROL_NONE;
    }
    if (pNumDetOut) *pNumDetOut = 0;
    if (crcRx) *crcRx = 0;
    return ret;
}

AEEResult FadasIface_ExtractBBoxDestroySafe(remote_handle64 _h, uint64_t hPostProc, uint32_t crcTx)
{
    if (hPostProc) free((void*)(uintptr_t)hPostProc);
    return AEE_SUCCESS;
}
FadasError_e FadasRemap_RunGPU( FadasRemapMap *remapPtr, FadasImage_t *src,
                                 FadasImage_t *dst, FadasROI_t *roi,
                                 float32_t scale, FadasNormlzParams_t *normlz )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_RUN_GPU].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REMAP_RUN_GPU].param;
        s_MockParams[MOCK_API_FADAS_REMAP_RUN_GPU].action = MOCK_CONTROL_NONE;
    }
    return ret;
}
FadasRemapMap *FadasRemap_CreateMapNoUndistortionGPU( uint32_t srcWidth, uint32_t srcHeight,
                                                       uint32_t dstWidth, uint32_t dstHeight,
                                                       FadasRemapPipeline_e ePipeline,
                                                       uint8_t borderConst )
{
    FadasRemapMap *ret = (FadasRemapMap *) malloc( sizeof( int ) );
    if ( MOCK_CONTROL_RETURN ==
         s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_GPU].action )
    {
        if ( ret ) free( ret );
        ret = *(FadasRemapMap **) s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_GPU].param;
        s_MockParams[MOCK_API_FADAS_REMAP_CREATE_MAP_NO_UNDISTORTION_GPU].action = MOCK_CONTROL_NONE;
    }
    return ret;
}
FadasError_e FadasRemap_DestroyMapGPU( FadasRemapMap *map_ )
{
    FadasError_e ret = FADAS_ERROR_NONE;
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_GPU].action )
    {
        ret = *(FadasError_e *) s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_GPU].param;
        s_MockParams[MOCK_API_FADAS_REMAP_DESTROY_MAP_GPU].action = MOCK_CONTROL_NONE;
        return ret;
    }
    if ( map_ ) free( map_ );
    return FADAS_ERROR_NONE;
}
/* ---- FastRPC stubs (needed by FadasSrv.cpp) ---- */
extern "C" int rpcmem_to_fd(void *po)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_RPCMEM_TO_FD].action) {
        int r = *(int*)s_MockParams[MOCK_API_RPCMEM_TO_FD].param;
        s_MockParams[MOCK_API_RPCMEM_TO_FD].action = MOCK_CONTROL_NONE;
        return r;
    }
    return -1;
}

extern "C" int fastrpc_mmap(int domain, int fd, void *addr, int offset, size_t length, fastrpc_map_flags flags)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FASTRPC_MMAP].action) {
        int r = *(int*)s_MockParams[MOCK_API_FASTRPC_MMAP].param;
        s_MockParams[MOCK_API_FASTRPC_MMAP].action = MOCK_CONTROL_NONE;
        return r;
    }
    return AEE_SUCCESS;
}

int FadasIface_open(const char *uri, remote_handle64 *h)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_FADAS_IFACE_OPEN].action) {
        int r = *(int*)s_MockParams[MOCK_API_FADAS_IFACE_OPEN].param;
        s_MockParams[MOCK_API_FADAS_IFACE_OPEN].action = MOCK_CONTROL_NONE;
        return r;
    }
    if (h) *h = 1;
    return AEE_SUCCESS;
}

int FadasIface_close(remote_handle64 h) { return AEE_SUCCESS; }

/* dlopen mock */
extern "C" void *__real_dlopen(const char *filename, int flags);
extern "C" void *__wrap_dlopen(const char *filename, int flags)
{
    if (MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_DLOPEN].action) {
        void *r = *(void**)s_MockParams[MOCK_API_DLOPEN].param;
        s_MockParams[MOCK_API_DLOPEN].action = MOCK_CONTROL_NONE;
        return r;
    }
    return __real_dlopen(filename, flags);
}

/* Symbol-aware dlsym mock */
static const char *g_dlsym_mock_symbol = nullptr;
static void       *g_dlsym_mock_return = nullptr;

extern "C" void MockDlsymForSymbol( const char *symbol, void *returnValue )
{
    g_dlsym_mock_symbol = symbol;
    g_dlsym_mock_return = returnValue;
}

extern "C" void *__real_dlsym( void *handle, const char *symbol );
extern "C" void *__wrap_dlsym( void *handle, const char *symbol )
{
    if ( g_dlsym_mock_symbol && symbol &&
         strcmp( symbol, g_dlsym_mock_symbol ) == 0 )
    {
        g_dlsym_mock_symbol = nullptr;
        return g_dlsym_mock_return;
    }
    if ( MOCK_CONTROL_RETURN == s_MockParams[MOCK_API_DLSYM].action )
    {
        void *r = *(void **)s_MockParams[MOCK_API_DLSYM].param;
        s_MockParams[MOCK_API_DLSYM].action = MOCK_CONTROL_NONE;
        return r;
    }
    return __real_dlsym( handle, symbol );
}

