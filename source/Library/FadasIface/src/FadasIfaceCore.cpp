// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "AEEStdErr.h"
#include "FadasIface.h"
#include "HAP_farf.h"
#include "HAP_mem.h"
#include "HAP_perf.h"
#include "HAP_power.h"
#include "crc32.h"
#include "qurt.h"
#include "remote.h"
#include <fadas.h>
#include <stdio.h>
#include <string.h>

#define PLRPOST_NUM_INPUTS 10

#define PLRPOST_IN_PTS      0
#define PLRPOST_IN_HEATMAP  1
#define PLRPOST_IN_XY       2
#define PLRPOST_IN_Z        3
#define PLRPOST_IN_SIZE     4
#define PLRPOST_IN_THETA    5
#define PLRPOST_OUT_BBOX    6
#define PLRPOST_OUT_LABELS  7
#define PLRPOST_OUT_SCORES  8
#define PLRPOST_OUT_METADATA 9
#define MAX_INPUTS 64

#define CRC_RET_SUCCESS_VALUE 0
typedef struct
{
    qurt_mutex_t mutex;
} dspContext_t;

// FIXME: Maybe need better way to map those enums
const FadasRemapPipeline_e g_MapImageConversion[FADAS_REMAP_PIPELINE_MAX_NSP] = {
        FADAS_REMAP_PIPELINE_1C8,
        FADAS_REMAP_PIPELINE_1C8_ROISCALE,
        FADAS_REMAP_PIPELINE_3C888,
        FADAS_REMAP_PIPELINE_3C888_ROISCALE,
        FADAS_REMAP_PIPELINE_YUV888_TO_RGB888,
        FADAS_REMAP_PIPELINE_UYVY_TO_RGB888,
        FADAS_REMAP_PIPELINE_VYUY_TO_RGB888,
        FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_ROISCALE,
        FADAS_REMAP_PIPELINE_VYUY_TO_RGB888_ROISCALE,
        FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NORMI8,
        FADAS_REMAP_PIPELINE_UYVY_TO_RGB888_NORMU8,
        FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888,
        FADAS_REMAP_PIPELINE_Y8UV8_TO_BGR888,
        FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NORMI8,
        FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_NORMU8,
        FADAS_REMAP_PIPELINE_Y8UV8_TO_RGB888_ROISCALE,
#if 0
	//supported only for engineering build system 
        FADAS_REMAP_PIPELINE_UYVY_TO_BGR888, 
#endif
};

const FadasImageFormat_e g_MapImageFormat[FADAS_IMAGE_FORMAT_COUNT_NSP] = {
        FADAS_IMAGE_FORMAT_UNKNOWN, FADAS_IMAGE_FORMAT_Y,      FADAS_IMAGE_FORMAT_Y12,
        FADAS_IMAGE_FORMAT_UYVY,    FADAS_IMAGE_FORMAT_UYVY10, FADAS_IMAGE_FORMAT_VYUY,
        FADAS_IMAGE_FORMAT_YUV888,  FADAS_IMAGE_FORMAT_RGB888, FADAS_IMAGE_FORMAT_Y10UV10,
        FADAS_IMAGE_FORMAT_Y8UV8,
};

const FadasBufType_e g_MapBufType[FADAS_BUF_TYPE_MAX_NSP] = {
        FADAS_BUF_TYPE_IN,
        FADAS_BUF_TYPE_OUT,
        FADAS_BUF_TYPE_INOUT,
};

AEEResult SetClocks( remote_handle64 handle )
{
    AEEResult ret = AEE_SUCCESS;
    HAP_power_request_t request;
    memset( &request, 0, sizeof( HAP_power_request_t ) );
    request.type = HAP_power_set_apptype;
    request.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
    void *ctx = (void *) ( handle );
    int retVal = HAP_power_set( ctx, &request );

    if ( 0 != retVal )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        memset( &request, 0, sizeof( HAP_power_request_t ) );
        request.type = HAP_power_set_DCVS_v2;

        request.dcvs_v2.dcvs_enable = TRUE;
        request.dcvs_v2.dcvs_params.target_corner = (HAP_dcvs_voltage_corner_t) 7;

        request.dcvs_v2.dcvs_params.min_corner = request.dcvs_v2.dcvs_params.target_corner;
        request.dcvs_v2.dcvs_params.max_corner = request.dcvs_v2.dcvs_params.target_corner;

        request.dcvs_v2.dcvs_option = HAP_DCVS_V2_PERFORMANCE_MODE;
        request.dcvs_v2.set_dcvs_params = TRUE;
        request.dcvs_v2.set_latency = TRUE;
        request.dcvs_v2.latency = 100;
        retVal = HAP_power_set( ctx, &request );
    }

    if ( 0 != retVal )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        memset( &request, 0, sizeof( HAP_power_request_t ) );
        request.type = HAP_power_set_HVX;
        request.hvx.power_up = TRUE;
        retVal = HAP_power_set( ctx, &request );
    }

    if ( 0 != retVal )
    {
        FARF( ERROR, "Failed to set clocks!" );
        ret = AEE_EFAILED;
    }

    return ret;
}

void *FadasIface_GetBufPtr( int32_t bufFd )
{
    void *bufPtr = nullptr;
    if ( 0 < bufFd )
    {
        AEEResult retVal = HAP_mmap_get( bufFd, (void **) &bufPtr, NULL );
        if ( AEE_SUCCESS != retVal )
        {
            FARF( ERROR, "Failed to get mmap!" );
        }
        else
        {
            HAP_mmap_put( bufFd );
        }
    }
    else
    {
        FARF( ERROR, "bufFd %d!", bufFd );
    }

    return bufPtr;
}

AEEResult FadasIface_open( const char *uri, remote_handle64 *handle )
{
    AEEResult ret = AEE_SUCCESS;
    dspContext_t *dspContext = (dspContext_t *) malloc( sizeof( dspContext_t ) );
    *handle = (remote_handle64) dspContext;
    if ( 0 == *handle )
    {
        FARF( ERROR, "Null handle pointer!" );
        ret = AEE_EFAILED;
    }
    else
    {
        qurt_mutex_init( &dspContext->mutex );
        ret = SetClocks( *handle );
    }

    if ( AEE_SUCCESS != ret )
    {
        FARF( ERROR, "Failed to do FadasIface_open!" );
    }

    return ret;
}

AEEResult FadasIface_close( remote_handle64 handle )
{
    dspContext_t *dspContext = (dspContext_t *) handle;
    if ( NULL == dspContext )
    {
        FARF( ERROR, "Null handle pointer!" );
    }
    else
    {
        free( dspContext );
    }
    HAP_power_destroy( NULL );

    return AEE_SUCCESS;
}

/*--------------------------------------------------------------
 *  FadasInit  (crcTx only)
 *  In-args:  none (status is an out-arg)
 *  Out-args: *status  (no crcRx in safe.h)
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasInitSafe( remote_handle64 handle, int32_t *status, uint32_t* crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    *status = static_cast<int32_t>( FadasInit( nullptr ) );

    /* Generate crcRx from out-arguments */
    if ( crcRx != nullptr && status != nullptr )
    {
        struct scatter_buffer sbRx[1] = {};
        sbRx[0].buf = reinterpret_cast<const char *>( status );
        sbRx[0].len = sizeof( *status );
        if ( CRC_RET_SUCCESS_VALUE !=
             (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
        {
            FARF( ERROR, "CRC generation failed in FadasIface_FadasInitSafe!" );
            ret = AEE_EFAILED;
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasVersion  (crcTx + crcRx)
 *  In-args:  versionLen
 *  Out-args: version[]
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasVersionSafe( remote_handle64 handle, uint8_t *ver_int, int ver_intLen,
                                   uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[1] = {};
        sbTx[0].buf = reinterpret_cast<const char *>( &ver_intLen );
        sbTx[0].len = sizeof( ver_intLen );
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, 1, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasVersionSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        strlcpy( reinterpret_cast<char *>( ver_int ), FadasVersion(), ver_intLen );

        /* Generate crcRx from out-arguments */
        if ( crcRx != nullptr && ver_int != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( ver_int );
            sbRx[0].len = static_cast<sl_size_t>( sizeof( *ver_int ) * ver_intLen );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR, "CRC generation failed in FadasIface_FadasVersionSafe!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasDeInit  (no CRC parameters)
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasDeInitSafe( remote_handle64 handle )
{
    FadasDeInit();

    return AEE_SUCCESS;
}

/*--------------------------------------------------------------
 *  FadasRemap_CreateMapFromMap  (crcTx + crcRx)
 *  In-args:  camWidth, camHeight, mapWidth, mapHeight,
 *            mapXFd, mapYFd, mapStride, imgFormat, borderConst
 *  Out-args: *mapPtr
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_CreateMapFromMapSafe(
        remote_handle64 handle, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
        uint32_t mapWidth, uint32_t mapHeight, int32_t mapXFd, int32_t mapYFd,
        uint32_t mapStride, FadasIface_FadasRemapPipeline_e imgFormat, uint8_t borderConst,
        uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[9] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &camWidth );
        sbTx[sbNum].len = sizeof( camWidth ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &camHeight );
        sbTx[sbNum].len = sizeof( camHeight ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapWidth );
        sbTx[sbNum].len = sizeof( mapWidth ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapHeight );
        sbTx[sbNum].len = sizeof( mapHeight ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapXFd );
        sbTx[sbNum].len = sizeof( mapXFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapYFd );
        sbTx[sbNum].len = sizeof( mapYFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapStride );
        sbTx[sbNum].len = sizeof( mapStride ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &imgFormat );
        sbTx[sbNum].len = sizeof( imgFormat ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &borderConst );
        sbTx[sbNum].len = sizeof( borderConst ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRemap_CreateMapFromMapSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        float *mapX = (float *) FadasIface_GetBufPtr( mapXFd );
        float *mapY = (float *) FadasIface_GetBufPtr( mapYFd );
        FadasRemapMap *map = FadasRemap_CreateMapFromMap(
                camWidth, camHeight, mapWidth, mapHeight, mapStride, mapX, mapY,
                g_MapImageConversion[static_cast<int>( imgFormat )], borderConst );

        if ( nullptr == map )
        {
            FARF( ERROR, "Null map pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            *mapPtr = reinterpret_cast<uint64>( map );
        }

        /* Generate crcRx from out-arguments */
        if ( AEE_SUCCESS == ret && crcRx != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( mapPtr );
            sbRx[0].len = sizeof( *mapPtr );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR, "CRC generation failed in FadasIface_FadasRemap_CreateMapFromMap!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRemap_CreateMapNoUndistortion  (crcTx + crcRx)
 *  In-args:  camWidth, camHeight, mapWidth, mapHeight,
 *            imgFormat, borderConst
 *  Out-args: *mapPtr
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_CreateMapNoUndistortionSafe(
        remote_handle64 handle, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
        uint32_t mapWidth, uint32_t mapHeight, FadasIface_FadasRemapPipeline_e imgFormat,
        uint8_t borderConst, uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[6] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &camWidth );
        sbTx[sbNum].len = sizeof( camWidth ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &camHeight );
        sbTx[sbNum].len = sizeof( camHeight ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapWidth );
        sbTx[sbNum].len = sizeof( mapWidth ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapHeight );
        sbTx[sbNum].len = sizeof( mapHeight ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &imgFormat );
        sbTx[sbNum].len = sizeof( imgFormat ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &borderConst );
        sbTx[sbNum].len = sizeof( borderConst ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR,
                  "CRC validation failed in FadasIface_FadasRemap_CreateMapNoUndistortionSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        FadasRemapMap *map = FadasRemap_CreateMapNoUndistortion(
                camWidth, camHeight, mapWidth, mapHeight,
                g_MapImageConversion[static_cast<int>( imgFormat )], borderConst );
        if ( nullptr == map )
        {
            FARF( ERROR, "Null map pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            *mapPtr = reinterpret_cast<uint64>( map );
        }

        /* Generate crcRx from out-arguments */
        if ( AEE_SUCCESS == ret && crcRx != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( mapPtr );
            sbRx[0].len = sizeof( *mapPtr );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR,
                      "CRC generation failed in FadasIface_FadasRemap_CreateMapNoUndistortionSafe!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRemap_DestroyMap  (crcTx only)
 *  In-args:  mapPtr (value)
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_DestroyMapSafe( remote_handle64 handle, uint64 mapPtr,
                                            uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[1] = {};
        sbTx[0].buf = reinterpret_cast<const char *>( &mapPtr );
        sbTx[0].len = sizeof( mapPtr );
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, 1, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRemap_DestroyMapSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        FadasRemapMap *map = reinterpret_cast<FadasRemapMap *>( mapPtr );
        FadasRemap_DestroyMap( map );
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRemap_CreateWorkers  (crcTx + crcRx)
 *  In-args:  nThreads, imgFormat
 *  Out-args: *worker_ptr
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_CreateWorkersSafe( remote_handle64 handle, uint64 *worker_ptr,
                                               uint32_t nThreads,
                                               FadasIface_FadasRemapPipeline_e imgFormat,
                                               uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[2] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &nThreads );
        sbTx[sbNum].len = sizeof( nThreads ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &imgFormat );
        sbTx[sbNum].len = sizeof( imgFormat ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRemap_CreateWorkersSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        int32_t pThreadsAffinity[] = { 0, 1, 2, 3 };
        void *worker = FadasRemap_CreateWorkers( nThreads, pThreadsAffinity,
                                                 g_MapImageConversion[static_cast<int>( imgFormat )] );
        if ( nullptr == worker )
        {
            FARF( ERROR, "Null worker pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            *worker_ptr = reinterpret_cast<uint64>( worker );
        }

        /* Generate crcRx from out-arguments */
        if ( AEE_SUCCESS == ret && crcRx != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( worker_ptr );
            sbRx[0].len = sizeof( *worker_ptr );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR, "CRC generation failed in FadasIface_FadasRemap_CreateWorkersSafe!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRemap_DestroyWorkers  (crcTx only)
 *  In-args:  worker_ptr (value)
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_DestroyWorkersSafe( remote_handle64 handle, uint64 worker_ptr,
                                                uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[1] = {};
        sbTx[0].buf = reinterpret_cast<const char *>( &worker_ptr );
        sbTx[0].len = sizeof( worker_ptr );
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, 1, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRemap_DestroyWorkersSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        void *worker = reinterpret_cast<void *>( worker_ptr );
        FadasRemap_DestroyWorkers( worker );
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRemap_RunMT  (crcTx only)
 *  In-args:  workerPtrs[], mapPtrs[], srcFds[], offsets[],
 *            srcProps[], dstFd, dstLen, dstProps, dstROIs[],
 *            normlz[]
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRemap_RunMTSafe(
        remote_handle64 handle, const uint64 *workerPtrs, int workerPtrsLen,
        const uint64 *mapPtrs, int mapPtrsLen, const int32_t *srcFds, int srcFdsLen,
        const uint32_t *offsets, int offsetsLen, const FadasIface_FadasImgProps_t *srcProps,
        int srcPropsLen, int32_t dstFd, uint32_t dstLen,
        const FadasIface_FadasImgProps_t *dstProps, const FadasIface_FadasROI_t *dstROIs,
        int dstROIsLen, const FadasIface_FadasNormlzParams_t *normlz, int normlzLen,
        uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[20] = {};
        sl_size_t sbNum = 0;

        if ( workerPtrs && workerPtrsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( workerPtrs );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *workerPtrs ) * workerPtrsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &workerPtrsLen );
        sbTx[sbNum].len = sizeof( workerPtrsLen ); sbNum++;

        if ( mapPtrs && mapPtrsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( mapPtrs );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *mapPtrs ) * mapPtrsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapPtrsLen );
        sbTx[sbNum].len = sizeof( mapPtrsLen ); sbNum++;

        if ( srcFds && srcFdsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( srcFds );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *srcFds ) * srcFdsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &srcFdsLen );
        sbTx[sbNum].len = sizeof( srcFdsLen ); sbNum++;

        if ( offsets && offsetsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( offsets );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *offsets ) * offsetsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &offsetsLen );
        sbTx[sbNum].len = sizeof( offsetsLen ); sbNum++;

        if ( srcProps && srcPropsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( srcProps );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *srcProps ) * srcPropsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &srcPropsLen );
        sbTx[sbNum].len = sizeof( srcPropsLen ); sbNum++;

        sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstFd );
        sbTx[sbNum].len = sizeof( dstFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstLen );
        sbTx[sbNum].len = sizeof( dstLen ); sbNum++;

        if ( dstProps )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( dstProps );
            sbTx[sbNum].len = sizeof( *dstProps ); sbNum++;
        }

        if ( dstROIs && dstROIsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( dstROIs );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *dstROIs ) * dstROIsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstROIsLen );
        sbTx[sbNum].len = sizeof( dstROIsLen ); sbNum++;

        if ( normlz && normlzLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( normlz );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *normlz ) * normlzLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &normlzLen );
        sbTx[sbNum].len = sizeof( normlzLen ); sbNum++;

        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRemap_RunMTSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
    dspContext_t *dspContext = (dspContext_t *) handle;
    if ( srcFdsLen > MAX_INPUTS )
    {
        FARF( ERROR, "Inputs number out of limitation !" );
        ret = AEE_EFAILED;
    }
    else
    {
        const uint8_t *src[MAX_INPUTS];
        uint8_t *dst = (uint8_t *) FadasIface_GetBufPtr( dstFd );
        if ( nullptr == dst )
        {
            FARF( ERROR, "Null dst pointer!" );
            ret = AEE_EFAILED;
        }
        else if ( ( srcFdsLen != offsetsLen ) || ( srcFdsLen != srcPropsLen ) )
        {
            FARF( ERROR, "Fd length not equal to props length" );
            ret = AEE_EFAILED;
        }
        else
        {
            for ( int i = 0; i < srcFdsLen; i++ )
            {
                src[i] = (uint8_t *) FadasIface_GetBufPtr( srcFds[i] );
                if ( nullptr == src[i] )
                {
                    FARF( ERROR, "Null src pointer!" );
                    ret = AEE_EFAILED;
                    break;
                }
                else
                {
                    src[i] += offsets[i];
                }
            }
        }

        if ( AEE_SUCCESS == ret )
        {
            FadasError_e retVal;
            FadasImage_t srcImg = {};
            FadasImage_t dstImg = {};
            dstImg.bAllocated = false;
            dstImg.props.width = static_cast<uint32_t>( dstProps->width );
            dstImg.props.height = static_cast<uint32_t>( dstProps->height );
            dstImg.props.format = g_MapImageFormat[dstProps->format];
            memcpy( dstImg.props.stride, dstProps->stride,
                    dstProps->numPlanes * sizeof( uint32_t ) );
            dstImg.props.numPlanes = dstProps->numPlanes;

            for ( int i = 0; i < srcFdsLen; i++ )
            {
                srcImg.bAllocated = false;
                srcImg.props.width = static_cast<uint32_t>( srcProps[i].width );
                srcImg.props.height = static_cast<uint32_t>( srcProps[i].height );
                srcImg.props.format = g_MapImageFormat[srcProps[i].format];
                memcpy( srcImg.props.stride, srcProps[i].stride,
                        srcProps[i].numPlanes * sizeof( uint32_t ) );
                srcImg.props.numPlanes = srcProps[i].numPlanes;
                void *worker = reinterpret_cast<void *>( workerPtrs[0] );
                if ( i < workerPtrsLen )
                {
                    worker = reinterpret_cast<void *>( workerPtrs[i] );
                }
                FadasRemapMap *map = reinterpret_cast<FadasRemapMap *>( mapPtrs[0] );
                if ( i < mapPtrsLen )
                {
                    map = reinterpret_cast<FadasRemapMap *>( mapPtrs[i] );
                }
                FadasROI_t roiStruct = {};
                roiStruct.x = dstROIs[i].x;
                roiStruct.y = dstROIs[i].y;
                roiStruct.width = dstROIs[i].width;
                roiStruct.height = dstROIs[i].height;
                srcImg.plane[0] = const_cast<uint8_t *>( src[i] );
                if ( FADAS_IMAGE_FORMAT_Y8UV8 == srcImg.props.format )
                {
                    srcImg.plane[1] = const_cast<uint8_t *>(
                            src[i] + srcImg.props.stride[0] * srcProps[i].actualHeight[0] );
                }
                dstImg.plane[0] = dst + i * dstLen;
                uint32_t srcLen = srcImg.props.height * srcImg.props.stride[0];
                qurt_mem_cache_clean( (qurt_addr_t) src[i], srcLen,
                                      QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
                qurt_mutex_lock( &dspContext->mutex );
                if ( 3 == normlzLen )
                {
                    FadasNormlzParams_t normlzParams[3];
                    normlzParams[0].sub = normlz[0].sub;
                    normlzParams[0].mul = normlz[0].mul;
                    normlzParams[0].add = normlz[0].add;
                    normlzParams[1].sub = normlz[1].sub;
                    normlzParams[1].mul = normlz[1].mul;
                    normlzParams[1].add = normlz[1].add;
                    normlzParams[2].sub = normlz[2].sub;
                    normlzParams[2].mul = normlz[2].mul;
                    normlzParams[2].add = normlz[2].add;
                    retVal = FadasRemap_RunMT( worker, map, &srcImg, &dstImg, &roiStruct, 1.0,
                                               normlzParams );
                }
                else
                {
                    retVal = FadasRemap_RunMT( worker, map, &srcImg, &dstImg, &roiStruct );
                }
                qurt_mutex_unlock( &dspContext->mutex );

                if ( FADAS_ERROR_NONE != retVal )
                {
                    FARF( ERROR, "Failed to do FadasRemap_RunMT" );
                    ret = AEE_EOFFSET + retVal;
                    break;
                }
            }
        }
        qurt_mem_cache_clean( (qurt_addr_t) dst, dstLen * srcFdsLen, QURT_MEM_CACHE_FLUSH_ALL,
                              QURT_MEM_DCACHE );
    }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasIface_mmap  (crcTx only)
 *  In-args:  bufFd, bufSize
 *--------------------------------------------------------------*/
AEEResult FadasIface_mmapSafe( remote_handle64 handle, int32_t bufFd, uint32_t bufSize,
                           uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[2] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
        sbTx[sbNum].len = sizeof( bufFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
        sbTx[sbNum].len = sizeof( bufSize ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_mmapSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        void *buf = FadasIface_GetBufPtr( bufFd );
        if ( nullptr != buf )
        {
            FARF( ERROR, "Already used buf pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            int32_t prot = HAP_PROT_READ | HAP_PROT_WRITE;
            int32_t flags = 0;
            buf = HAP_mmap( NULL, bufSize, prot, flags, bufFd, 0 );
            if ( ( ( (void *) 0xFFFFFFFF ) == buf ) || ( nullptr == buf ) )
            {
                FARF( ERROR, "Null buf pointer!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasIface_munmap  (crcTx only)
 *  In-args:  bufFd, bufSize
 *--------------------------------------------------------------*/
AEEResult FadasIface_munmapSafe( remote_handle64 handle, int32_t bufFd, uint32_t bufSize,
                             uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[2] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
        sbTx[sbNum].len = sizeof( bufFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
        sbTx[sbNum].len = sizeof( bufSize ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_munmapSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        void *buf = nullptr;
        ret = HAP_mmap_get( bufFd, (void **) &buf, NULL );
        if ( AEE_SUCCESS == ret )
        {
            int32_t err = -1;
            do
            {
                // decrement user count to 0
                err = HAP_mmap_put( bufFd );
            } while ( 0 == err );
            ret = HAP_munmap( buf, bufSize );
        }

        if ( AEE_SUCCESS != ret )
        {
            FARF( ERROR, "Failed to do HAP_munmap!" );
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasRegBuf  (crcTx only)
 *  In-args:  bufType, bufFd, bufSize, bufOffset, batchSize
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasRegBufSafe( remote_handle64 handle, FadasIface_FadasBufType_e bufType,
                                  int32_t bufFd, uint32_t bufSize, uint32_t bufOffset,
                                  uint32_t batch, uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[5] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufType );
        sbTx[sbNum].len = sizeof( bufType ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
        sbTx[sbNum].len = sizeof( bufFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
        sbTx[sbNum].len = sizeof( bufSize ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufOffset );
        sbTx[sbNum].len = sizeof( bufOffset ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &batch );
        sbTx[sbNum].len = sizeof( batch ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasRegBufSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        uint8_t *ptr = (uint8_t *) FadasIface_GetBufPtr( bufFd );
        uint32_t i;

        if ( nullptr == ptr )
        {
            FARF( ERROR, "Null bufFd pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            FadasError_e retVal;
            ptr += bufOffset;
            for ( i = 0; i < batch; i++ )
            {
                retVal = FadasRegBuf( g_MapBufType[bufType], ptr, bufSize );
                ptr += bufSize;
                if ( FADAS_ERROR_NONE != retVal )
                {
                    FARF( ERROR, "Failed to do FadasRegBuf!" );
                    ret = AEE_EFAILED;
                    break;
                }
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  FadasDeregBuf  (crcTx only)
 *  In-args:  bufFd, bufSize, bufOffset, batchSize
 *--------------------------------------------------------------*/
AEEResult FadasIface_FadasDeregBufSafe( remote_handle64 handle, int32_t bufFd, uint32_t bufSize,
                                    uint32_t bufOffset, uint32_t batch, uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[4] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
        sbTx[sbNum].len = sizeof( bufFd ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
        sbTx[sbNum].len = sizeof( bufSize ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufOffset );
        sbTx[sbNum].len = sizeof( bufOffset ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &batch );
        sbTx[sbNum].len = sizeof( batch ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_FadasDeregBufSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        uint8_t *ptr = (uint8_t *) FadasIface_GetBufPtr( bufFd );
        uint32_t i;

        if ( nullptr == ptr )
        {
            FARF( ERROR, "Null bufFd pointer!" );
            ret = AEE_EFAILED;
        }
        else
        {
            FadasError_e retVal;
            ptr += bufOffset;
            for ( i = 0; i < batch; i++ )
            {
                retVal = FadasDeregBuf( ptr );
                ptr += bufSize;
                if ( FADAS_ERROR_NONE != retVal )
                {
                    FARF( ERROR, "Failed to do FadasDeregBuf!" );
                    ret = AEE_EFAILED;
                    break;
                }
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  PointPillarCreate  (crcTx + crcRx)
 *  In-args:  *pPlrSize, *pMinRange, *pMaxRange,
 *            maxNumInPts, numInFeatureDim, maxNumPlrs,
 *            maxNumPtsPerPlr, numOutFeatureDim
 *  Out-args: *phPreProc
 *--------------------------------------------------------------*/
AEEResult FadasIface_PointPillarCreateSafe( remote_handle64 handle,
                                        const FadasIface_Pt3D_t *pPlrSize,
                                        const FadasIface_Pt3D_t *pMinRange,
                                        const FadasIface_Pt3D_t *pMaxRange,
                                        uint32_t maxNumInPts, uint32_t numInFeatureDim,
                                        uint32_t maxNumPlrs, uint32_t maxNumPtsPerPlr,
                                        uint32_t numOutFeatureDim, uint64_t *phPreProc,
                                        uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[8] = {};
        sl_size_t sbNum = 0;
        if ( pPlrSize )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( pPlrSize );
            sbTx[sbNum].len = sizeof( *pPlrSize ); sbNum++;
        }
        if ( pMinRange )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( pMinRange );
            sbTx[sbNum].len = sizeof( *pMinRange ); sbNum++;
        }
        if ( pMaxRange )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( pMaxRange );
            sbTx[sbNum].len = sizeof( *pMaxRange ); sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumInPts );
        sbTx[sbNum].len = sizeof( maxNumInPts ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numInFeatureDim );
        sbTx[sbNum].len = sizeof( numInFeatureDim ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumPlrs );
        sbTx[sbNum].len = sizeof( maxNumPlrs ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumPtsPerPlr );
        sbTx[sbNum].len = sizeof( maxNumPtsPerPlr ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numOutFeatureDim );
        sbTx[sbNum].len = sizeof( numOutFeatureDim ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_PointPillarCreateSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        if ( ( nullptr == pPlrSize ) || ( nullptr == pMinRange ) || ( nullptr == pMaxRange ) ||
             ( nullptr == phPreProc ) )
        {
            FARF( ERROR, "PointPillarCreate with nullptr" );
            ret = AEE_EFAILED;
        }
        else
        {
            FadasPt_3Df32_t plrSize = { pPlrSize->x, pPlrSize->y, pPlrSize->z };
            FadasPt_3Df32_t minRange = { pMinRange->x, pMinRange->y, pMinRange->z };
            FadasPt_3Df32_t maxRange = { pMaxRange->x, pMaxRange->y, pMaxRange->z };

            *phPreProc = (uint64_t) FadasVM_PointPillar_Create(
                    plrSize, minRange, maxRange, maxNumInPts, numInFeatureDim, maxNumPlrs,
                    maxNumPtsPerPlr, numOutFeatureDim );
            if ( 0 == ( *phPreProc ) )
            {
                FARF( ERROR, "PointPillarCreate failed!" );
                ret = AEE_EFAILED;
            }
        }

        /* Generate crcRx from out-arguments */
        if ( AEE_SUCCESS == ret && crcRx != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( phPreProc );
            sbRx[0].len = sizeof( *phPreProc );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR, "CRC generation failed in FadasIface_PointPillarCreateSafe!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  PointPillarRun  (crcTx + crcRx)
 *  In-args:  hPreProc, numPts, fdInPts, inPtsOffset, inPtsSize,
 *            fdOutPlrs, outPlrsOffset, outPlrsSize,
 *            fdOutFeature, outFeatureOffset, outFeatureSize
 *  Out-args: *pNumOutPlrs
 *--------------------------------------------------------------*/
AEEResult FadasIface_PointPillarRunSafe( remote_handle64 handle, uint64_t hPreProc, uint32_t numPts,
                                     int32_t fdInPts, uint32_t inPtsOffset, uint32_t inPtsSize,
                                     int32_t fdOutPlrs, uint32_t outPlrsOffset,
                                     uint32_t outPlrsSize, int32_t fdOutFeature,
                                     uint32_t outFeatureOffset, uint32_t outFeatureSize,
                                     uint32_t *pNumOutPlrs, uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[11] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPreProc );
        sbTx[sbNum].len = sizeof( hPreProc ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numPts );
        sbTx[sbNum].len = sizeof( numPts ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdInPts );
        sbTx[sbNum].len = sizeof( fdInPts ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &inPtsOffset );
        sbTx[sbNum].len = sizeof( inPtsOffset ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &inPtsSize );
        sbTx[sbNum].len = sizeof( inPtsSize ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdOutPlrs );
        sbTx[sbNum].len = sizeof( fdOutPlrs ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &outPlrsOffset );
        sbTx[sbNum].len = sizeof( outPlrsOffset ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &outPlrsSize );
        sbTx[sbNum].len = sizeof( outPlrsSize ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdOutFeature );
        sbTx[sbNum].len = sizeof( fdOutFeature ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &outFeatureOffset );
        sbTx[sbNum].len = sizeof( outFeatureOffset ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &outFeatureSize );
        sbTx[sbNum].len = sizeof( outFeatureSize ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_PointPillarRunSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        dspContext_t *dspContext = (dspContext_t *) handle;
        const float32_t *pInPtsData = (const float32_t *) FadasIface_GetBufPtr( fdInPts );
        FadasVM_PointPillar_t *pOutPlrsData =
                (FadasVM_PointPillar_t *) FadasIface_GetBufPtr( fdOutPlrs );
        float32_t *pOutFeatureData = (float32_t *) FadasIface_GetBufPtr( fdOutFeature );

        if ( ( nullptr == dspContext ) || ( nullptr == pInPtsData ) || ( nullptr == pOutPlrsData ) ||
             ( nullptr == pOutFeatureData ) || ( 0 == hPreProc ) )
        {
            FARF( ERROR, "FadasIface_PointPillarRun with nullptr" );
            ret = AEE_EFAILED;
        }
        else
        {
            pInPtsData = (const float32_t *) ( ( (uint8_t *) pInPtsData ) + inPtsOffset );
            pOutPlrsData =
                    (FadasVM_PointPillar_t *) ( ( (uint8_t *) pOutPlrsData ) + outPlrsOffset );
            pOutFeatureData = (float32_t *) ( ( (uint8_t *) pOutFeatureData ) + outFeatureOffset );
            qurt_mem_cache_clean( (qurt_addr_t) pInPtsData, inPtsSize,
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mutex_lock( &dspContext->mutex );
            FadasError_e error = FadasVM_PointPillar_Run( (void *) hPreProc, numPts, pInPtsData,
                                                          pOutPlrsData, pOutFeatureData,
                                                          pNumOutPlrs );
            qurt_mutex_unlock( &dspContext->mutex );
            if ( FADAS_ERROR_NONE != error )
            {
                FARF( ERROR, "Failed to do FadasVM_PointPillar_Run: ret=%d", error );
                ret = AEE_EOFFSET + error;
            }
            else
            {
                qurt_mem_cache_clean( (qurt_addr_t) pOutPlrsData, outPlrsSize,
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
                qurt_mem_cache_clean( (qurt_addr_t) pOutFeatureData, outFeatureSize,
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
            }
        }
    }

    /* Generate crcRx from out-arguments */
    if ( AEE_SUCCESS == ret && crcRx != nullptr )
    {
        struct scatter_buffer sbRx[1] = {};
        sbRx[0].buf = reinterpret_cast<const char *>( pNumOutPlrs );
        sbRx[0].len = sizeof( *pNumOutPlrs );
        if ( CRC_RET_SUCCESS_VALUE !=
             (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
        {
            FARF( ERROR, "CRC generation failed in FadasIface_PointPillarRunSafe!" );
            ret = AEE_EFAILED;
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  PointPillarDestroy  (crcTx only)
 *  In-args:  hPreProc
 *--------------------------------------------------------------*/
AEEResult FadasIface_PointPillarDestroySafe( remote_handle64 handle, uint64_t hPreProc,
                                         uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[1] = {};
        sbTx[0].buf = reinterpret_cast<const char *>( &hPreProc );
        sbTx[0].len = sizeof( hPreProc );
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, 1, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_PointPillarDestroySafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        FadasError_e error = FadasVM_PointPillar_Destroy( (void *) hPreProc );
        if ( FADAS_ERROR_NONE != error )
        {
            FARF( ERROR, "Failed to do FadasVM_PointPillar_Destroy: ret=%d", error );
            ret = AEE_EOFFSET + error;
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  ExtractBBoxCreate  (crcTx + crcRx)
 *  In-args:  maxNumInPts, numInFeatureDim, maxNumDetOut,
 *            numClass, *pGrid, threshScore, threshIOU,
 *            minCentreX/Y/Z, maxCentreX/Y/Z,
 *            labelSelect[], labelSelectLen, maxNumFilter
 *  Out-args: *phPostProc
 *--------------------------------------------------------------*/
AEEResult FadasIface_ExtractBBoxCreateSafe( remote_handle64 handle, uint32_t maxNumInPts,
                                        uint32_t numInFeatureDim, uint32_t maxNumDetOut,
                                        uint32_t numClass, const FadasIface_Grid2D_t *pGrid,
                                        float threshScore, float threshIOU, float minCentreX,
                                        float minCentreY, float minCentreZ, float maxCentreX,
                                        float maxCentreY, float maxCentreZ,
                                        const uint8_t *labelSelect, int labelSelectLen,
                                        uint32_t maxNumFilter, uint64_t *phPostProc,
                                        uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[16] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumInPts );
        sbTx[sbNum].len = sizeof( maxNumInPts ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numInFeatureDim );
        sbTx[sbNum].len = sizeof( numInFeatureDim ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumDetOut );
        sbTx[sbNum].len = sizeof( maxNumDetOut ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numClass );
        sbTx[sbNum].len = sizeof( numClass ); sbNum++;
        if ( pGrid )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( pGrid );
            sbTx[sbNum].len = sizeof( *pGrid ); sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &threshScore );
        sbTx[sbNum].len = sizeof( threshScore ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &threshIOU );
        sbTx[sbNum].len = sizeof( threshIOU ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreX );
        sbTx[sbNum].len = sizeof( minCentreX ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreY );
        sbTx[sbNum].len = sizeof( minCentreY ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreZ );
        sbTx[sbNum].len = sizeof( minCentreZ ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreX );
        sbTx[sbNum].len = sizeof( maxCentreX ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreY );
        sbTx[sbNum].len = sizeof( maxCentreY ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreZ );
        sbTx[sbNum].len = sizeof( maxCentreZ ); sbNum++;
        if ( labelSelect && labelSelectLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( labelSelect );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *labelSelect ) * labelSelectLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &labelSelectLen );
        sbTx[sbNum].len = sizeof( labelSelectLen ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumFilter );
        sbTx[sbNum].len = sizeof( maxNumFilter ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_ExtractBBoxCreateSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        if ( ( nullptr == pGrid ) || ( nullptr == phPostProc ) )
        {
            FARF( ERROR, "ExtractBBoxCreate with nullptr" );
            ret = AEE_EFAILED;
        }
        else
        {
            Fadas2DGrid_t grid = { { pGrid->tlX, pGrid->tlY },
                                   { pGrid->brX, pGrid->brY },
                                   pGrid->cellSizeX,
                                   pGrid->cellSizeY };

            Fadas3DBBoxInitParams_t bboxInitParams = { 0 };

            bboxInitParams.strideInPts = numInFeatureDim * sizeof( float32_t );
            bboxInitParams.numClass = numClass;
            bboxInitParams.grid = grid;
            bboxInitParams.maxNumInPts = maxNumInPts;
            bboxInitParams.maxNumDetOut = maxNumDetOut;
            bboxInitParams.threshScore = threshScore;
            bboxInitParams.threshIOU = threshIOU;
            if ( nullptr != labelSelect )
            {
                bboxInitParams.filterParams.minCentre.x = minCentreX;
                bboxInitParams.filterParams.minCentre.y = minCentreY;
                bboxInitParams.filterParams.minCentre.z = minCentreZ;
                bboxInitParams.filterParams.maxCentre.x = maxCentreX;
                bboxInitParams.filterParams.maxCentre.y = maxCentreY;
                bboxInitParams.filterParams.maxCentre.z = maxCentreZ;
                bboxInitParams.filterParams.maxNumFilter = maxNumFilter;
                bboxInitParams.filterParams.labelSelect = (bool *) labelSelect;
            }

            *phPostProc = (uint64_t) FadasVM_ExtractBBox_Create( &bboxInitParams );
            if ( 0 == ( *phPostProc ) )
            {
                FARF( ERROR, "ExtractBBoxCreate failed!" );
                FARF( ERROR,
                      "plrSize=[%.2f %.2f], minRange=[%.2f %.2f], maxRange=[%.2f %.2f], "
                      "pcd=%ux%u, %u class, thresh=[%.2f %.2f], max=%u, numFilter=%d",
                      pGrid->cellSizeX, pGrid->cellSizeY, pGrid->tlX, pGrid->tlY, pGrid->brX,
                      pGrid->brY, maxNumInPts, numInFeatureDim, numClass, threshScore, threshIOU,
                      maxNumDetOut, labelSelectLen );
                ret = AEE_EFAILED;
            }
        }

        /* Generate crcRx from out-arguments */
        if ( AEE_SUCCESS == ret && crcRx != nullptr )
        {
            struct scatter_buffer sbRx[1] = {};
            sbRx[0].buf = reinterpret_cast<const char *>( phPostProc );
            sbRx[0].len = sizeof( *phPostProc );
            if ( CRC_RET_SUCCESS_VALUE !=
                 (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
            {
                FARF( ERROR, "CRC generation failed in FadasIface_ExtractBBoxCreateSafe!" );
                ret = AEE_EFAILED;
            }
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  ExtractBBoxRun  (crcTx + crcRx)
 *  In-args:  hPostProc, numPts, fds[], fdsLen,
 *            offsets[], offsetsLen, sizes[], sizesLen,
 *            bMapPtsToBBox, bBBoxFilter
 *  Out-args: *pNumDetOut
 *--------------------------------------------------------------*/
AEEResult FadasIface_ExtractBBoxRunSafe( remote_handle64 handle, uint64_t hPostProc, uint32_t numPts,
                                     const int32_t *fds, int fdsLen, const uint32_t *offsets,
                                     int offsetsLen, const uint32_t *sizes, int sizesLen,
                                     uint8_t bMapPtsToBBox, uint8_t bBBoxFilter,
                                     uint32_t *pNumDetOut, uint32_t crcTx, uint32_t *crcRx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[10] = {};
        sl_size_t sbNum = 0;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPostProc );
        sbTx[sbNum].len = sizeof( hPostProc ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &numPts );
        sbTx[sbNum].len = sizeof( numPts ); sbNum++;
        if ( fds && fdsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( fds );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *fds ) * fdsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdsLen );
        sbTx[sbNum].len = sizeof( fdsLen ); sbNum++;
        if ( offsets && offsetsLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( offsets );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *offsets ) * offsetsLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &offsetsLen );
        sbTx[sbNum].len = sizeof( offsetsLen ); sbNum++;
        if ( sizes && sizesLen > 0 )
        {
            sbTx[sbNum].buf = reinterpret_cast<const char *>( sizes );
            sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *sizes ) * sizesLen );
            sbNum++;
        }
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &sizesLen );
        sbTx[sbNum].len = sizeof( sizesLen ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bMapPtsToBBox );
        sbTx[sbNum].len = sizeof( bMapPtsToBBox ); sbNum++;
        sbTx[sbNum].buf = reinterpret_cast<const char *>( &bBBoxFilter );
        sbTx[sbNum].len = sizeof( bBBoxFilter ); sbNum++;
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, sbNum, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_ExtractBBoxRunSafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
    dspContext_t *dspContext = (dspContext_t *) handle;
    FadasError_e error = FADAS_ERROR_UNKNOWN;
    if ( ( nullptr == dspContext ) || ( nullptr == fds ) || ( nullptr == offsets ) ||
         ( nullptr == sizes ) || ( PLRPOST_NUM_INPUTS != fdsLen ) ||
         ( PLRPOST_NUM_INPUTS != offsetsLen ) || ( PLRPOST_NUM_INPUTS != sizesLen ) ||
         ( nullptr == pNumDetOut ) )
    {
        FARF( ERROR, "FadasIface_ExtractBBoxRunSafe with bad arguments" );
        ret = AEE_EFAILED;
    }
    else
    {
        float32_t *pInPts = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_PTS] );
        float32_t *pHeatmap = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_HEATMAP] );
        float32_t *pXY = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_XY] );
        float32_t *pZ = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_Z] );
        float32_t *pSize = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_SIZE] );
        float32_t *pTheta = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_IN_THETA] );
        FadasCuboidf32_t *pBBoxList =
                (FadasCuboidf32_t *) FadasIface_GetBufPtr( fds[PLRPOST_OUT_BBOX] );
        uint32_t *pLabelsOut = (uint32_t *) FadasIface_GetBufPtr( fds[PLRPOST_OUT_LABELS] );
        float32_t *pScoresOut = (float32_t *) FadasIface_GetBufPtr( fds[PLRPOST_OUT_SCORES] );
        Fadas3DBBoxMetadata_t *pMetadataOut =
                (Fadas3DBBoxMetadata_t *) FadasIface_GetBufPtr( fds[PLRPOST_OUT_METADATA] );
        if ( ( nullptr == pInPts ) || ( nullptr == pHeatmap ) || ( nullptr == pXY ) ||
             ( nullptr == pZ ) || ( nullptr == pSize ) || ( nullptr == pTheta ) ||
             ( nullptr == pBBoxList ) || ( nullptr == pLabelsOut ) || ( nullptr == pScoresOut ) ||
             ( nullptr == pMetadataOut ) )
        {
            FARF( ERROR, "FadasIface_ExtractBBoxRun with nullptr" );
            ret = AEE_EFAILED;
        }
        else
        {
            pInPts = (float32_t *) ( ( (uint8_t *) pInPts ) + offsets[PLRPOST_IN_PTS] );
            pHeatmap = (float32_t *) ( ( (uint8_t *) pHeatmap ) + offsets[PLRPOST_IN_HEATMAP] );
            pXY = (float32_t *) ( ( (uint8_t *) pXY ) + offsets[PLRPOST_IN_XY] );
            pZ = (float32_t *) ( ( (uint8_t *) pZ ) + offsets[PLRPOST_IN_Z] );
            pSize = (float32_t *) ( ( (uint8_t *) pSize ) + offsets[PLRPOST_IN_SIZE] );
            pTheta = (float32_t *) ( ( (uint8_t *) pTheta ) + offsets[PLRPOST_IN_THETA] );
            pBBoxList = (FadasCuboidf32_t *) ( ( (uint8_t *) pBBoxList ) +
                                               offsets[PLRPOST_OUT_BBOX] );
            pLabelsOut =
                    (uint32_t *) ( ( (uint8_t *) pLabelsOut ) + offsets[PLRPOST_OUT_LABELS] );
            pScoresOut =
                    (float32_t *) ( ( (uint8_t *) pScoresOut ) + offsets[PLRPOST_OUT_SCORES] );
            pMetadataOut = (Fadas3DBBoxMetadata_t *) ( ( (uint8_t *) pMetadataOut ) +
                                                       offsets[PLRPOST_OUT_METADATA] );

            Fadas3DRPNBufs_t rpnBuf;
            Fadas3DBBoxBufs_t outBuf;

            rpnBuf.pHeatmap = pHeatmap;
            rpnBuf.pXY = pXY;
            rpnBuf.pZ = pZ;
            rpnBuf.pSize = pSize;
            rpnBuf.pTheta = pTheta;

            outBuf.pBBoxList = pBBoxList;
            outBuf.pLabels = pLabelsOut;
            outBuf.pScores = pScoresOut;
            outBuf.pMetadata = pMetadataOut;

            qurt_mem_cache_clean( (qurt_addr_t) pInPts, sizes[PLRPOST_IN_PTS],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mem_cache_clean( (qurt_addr_t) pHeatmap, sizes[PLRPOST_IN_HEATMAP],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mem_cache_clean( (qurt_addr_t) pXY, sizes[PLRPOST_IN_XY],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mem_cache_clean( (qurt_addr_t) pZ, sizes[PLRPOST_IN_Z],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mem_cache_clean( (qurt_addr_t) pSize, sizes[PLRPOST_IN_SIZE],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            qurt_mem_cache_clean( (qurt_addr_t) pTheta, sizes[PLRPOST_IN_THETA],
                                  QURT_MEM_CACHE_FLUSH_INVALIDATE_ALL, QURT_MEM_DCACHE );
            *pNumDetOut = 0;
            qurt_mutex_lock( &dspContext->mutex );
            error = FadasVM_ExtractBBox_Run( (void *) hPostProc, numPts, pInPts, rpnBuf, outBuf,
                                             pNumDetOut, (bool) bMapPtsToBBox,
                                             (bool) bBBoxFilter );
            qurt_mutex_unlock( &dspContext->mutex );
            if ( FADAS_ERROR_NONE != error )
            {
                FARF( ERROR, "Failed to do FadasVM_PointPillar_Run: ret=%d", error );
                for ( int i = 0; i < PLRPOST_NUM_INPUTS; i++ )
                {
                    FARF( ERROR, "[%d]: fd=%d size=%u offset=%u", i, fds[i], sizes[i],
                          offsets[i] );
                }
                ret = AEE_EOFFSET + error;
            }
            else
            {
                qurt_mem_cache_clean( (qurt_addr_t) pBBoxList, sizes[PLRPOST_OUT_BBOX],
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
                qurt_mem_cache_clean( (qurt_addr_t) pLabelsOut, sizes[PLRPOST_OUT_LABELS],
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
                qurt_mem_cache_clean( (qurt_addr_t) pScoresOut, sizes[PLRPOST_OUT_SCORES],
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
                qurt_mem_cache_clean( (qurt_addr_t) pMetadataOut, sizes[PLRPOST_OUT_METADATA],
                                      QURT_MEM_CACHE_FLUSH_ALL, QURT_MEM_DCACHE );
            }
        }
    }
    }

    /* Generate crcRx from out-arguments */
    if ( AEE_SUCCESS == ret && crcRx != nullptr )
    {
        struct scatter_buffer sbRx[1] = {};
        sbRx[0].buf = reinterpret_cast<const char *>( pNumDetOut );
        sbRx[0].len = sizeof( *pNumDetOut );
        if ( CRC_RET_SUCCESS_VALUE !=
             (int)crc32_generate_scatter( sbRx, 1, reinterpret_cast<sl_u32_t *>( crcRx ) ) )
        {
            FARF( ERROR, "CRC generation failed in FadasIface_ExtractBBoxRunSafe!" );
            ret = AEE_EFAILED;
        }
    }

    return ret;
}

/*--------------------------------------------------------------
 *  ExtractBBoxDestroy  (crcTx only)
 *  In-args:  hPostProc
 *--------------------------------------------------------------*/
AEEResult FadasIface_ExtractBBoxDestroySafe( remote_handle64 handle, uint64_t hPostProc,
                                         uint32_t crcTx )
{
    AEEResult ret = AEE_SUCCESS;
    /* Validate crcTx against all in-arguments */
    {
        struct scatter_buffer sbTx[1] = {};
        sbTx[0].buf = reinterpret_cast<const char *>( &hPostProc );
        sbTx[0].len = sizeof( hPostProc );
        if ( CRC_RET_SUCCESS_VALUE != (int)crc32_verify_scatter( sbTx, 1, crcTx ) )
        {
            FARF( ERROR, "CRC validation failed in FadasIface_ExtractBBoxDestroySafe!" );
            ret = AEE_EBADPARM;
        }
    }

    if ( AEE_SUCCESS == ret )
    {
        FadasError_e error = FadasVM_ExtractBBox_Destroy( (void *) hPostProc );
        if ( FADAS_ERROR_NONE != error )
        {
            FARF( ERROR, "Failed to do FadasVM_ExtractBBox_Destroy: ret=%d", error );
            ret = AEE_EOFFSET + error;
        }
    }

    return ret;
}
