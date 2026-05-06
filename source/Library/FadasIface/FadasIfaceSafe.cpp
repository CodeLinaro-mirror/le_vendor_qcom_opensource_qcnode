// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/*=====================================================================
 *  FadasIface.cpp
 *
 *  Implementation of the public FadasIface API.  Each function:
 *    1. Builds a scatter-buffer list over its INPUT arguments only.
 *    2. Calls crc32_generate_scatter() to produce crcTx.
 *    3. Forwards the call to the corresponding _safe implementation
 *       (generated from FadasIface_safe.idl).
 *    4. Where the safe function returns a crcRx, calls
 *       crc32_verify_scatter() over the OUTPUT buffer(s).
 *
 *  On CRC generation or verification failure the function returns
 *  AEE_EFAILED.  Functions whose safe counterpart has no output CRC
 *  (destroy / unmap / dereg variants) skip step 4 entirely.
 *===================================================================*/

#include "FadasIfaceSafe.hpp"
#include "FadasIface.h"
#include "crc32.h"

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Maximum number of scatter-buffer entries used across all functions. */
#define FADAS_MAX_SB 20U

/*value to be used for crc APIs success case .  */
#define CRC_RET_SUCCESS_VALUE 0

/* ------------------------------------------------------------------ */
/*  FadasInit                                                           */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasInitSafe(rout int32_t status, in uint32_t crcTx)
 *
 * 'status' is an OUTPUT (rout) — it must NOT be included in crcTx.
 * There is no crcRx returned by this function.
 */
AEEResult FadasIface_FadasInit( remote_handle64 _h, int32_t *status )
{
    uint32_t crcRx = 0U;
    AEEResult ret = AEE_SUCCESS;

    ret = FadasIface_FadasInitSafe( _h, status, &crcRx );
    if ( ret == AEE_SUCCESS )
    {
        struct scatter_buffer sbRx[1] = { { 0 } };
        sbRx[0].buf = reinterpret_cast<const char *>( status );
        sbRx[0].len = sizeof( *status );

        error_type crc_retval = crc32_verify_scatter( sbRx, 1, crcRx );
        if ((int)crc_retval != CRC_RET_SUCCESS_VALUE)
        {
            ret = AEE_EFAILED;
        }
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasVersion                                                        */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasVersionSafe(rout sequence<uint8_t> version,
 *                                   in uint32_t crcTx, rout uint32_t crcRx)
 *
 * 'version' is OUTPUT (rout) — exclude from crcTx.
 * 'versionLen' is the buffer capacity passed as input.
 * crcRx covers the returned version bytes.
 */
AEEResult FadasIface_FadasVersion( remote_handle64 _h, uint8_t *version, int versionLen )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    uint32_t sbNum = 0U;
    AEEResult ret = AEE_SUCCESS;

    struct scatter_buffer sbTx[1] = {};
    sbTx[0].buf = reinterpret_cast<const char *>( &versionLen );
    sbTx[0].len = static_cast<sl_size_t>( sizeof( versionLen ) );

    if ( (int)crc32_generate_scatter( sbTx, 1, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasVersionSafe( _h, version, versionLen, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            /* Verify CRC over the returned version bytes. */
            if ( version != nullptr && versionLen > 0 )
            {
                struct scatter_buffer sbRx[1] = {};
                sbRx[0].buf = reinterpret_cast<const char *>( version );
                sbRx[0].len = static_cast<sl_size_t>( sizeof( *version ) * versionLen );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasDeInit                                                         */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasDeInitSafe()   — no CRC parameters at all.
 */
AEEResult FadasIface_FadasDeInit( remote_handle64 _h )
{
    return FadasIface_FadasDeInitSafe( _h );
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_CreateMapFromMap                                         */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_FadasRemap_CreateMapFromMap(
        remote_handle64 _h, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
        uint32_t mapWidth, uint32_t mapHeight, int32_t mapXFd, int32_t mapYFd, uint32_t mapStride,
        FadasIface_FadasRemapPipeline_e imgFormat, uint8_t borderConst )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    struct scatter_buffer sbTx[9] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[0].buf = reinterpret_cast<const char *>( &camWidth );
    sbTx[0].len = sizeof( camWidth );
    sbTx[1].buf = reinterpret_cast<const char *>( &camHeight );
    sbTx[1].len = sizeof( camHeight );
    sbTx[2].buf = reinterpret_cast<const char *>( &mapWidth );
    sbTx[2].len = sizeof( mapWidth );
    sbTx[3].buf = reinterpret_cast<const char *>( &mapHeight );
    sbTx[3].len = sizeof( mapHeight );
    sbTx[4].buf = reinterpret_cast<const char *>( &mapXFd );
    sbTx[4].len = sizeof( mapXFd );
    sbTx[5].buf = reinterpret_cast<const char *>( &mapYFd );
    sbTx[5].len = sizeof( mapYFd );
    sbTx[6].buf = reinterpret_cast<const char *>( &mapStride );
    sbTx[6].len = sizeof( mapStride );
    sbTx[7].buf = reinterpret_cast<const char *>( &imgFormat );
    sbTx[7].len = sizeof( imgFormat );
    sbTx[8].buf = reinterpret_cast<const char *>( &borderConst );
    sbTx[8].len = sizeof( borderConst );
    if ( (int)crc32_generate_scatter( sbTx, 9, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_CreateMapFromMapSafe( _h, mapPtr, camWidth, camHeight, mapWidth,
                                                          mapHeight, mapXFd, mapYFd, mapStride,
                                                          imgFormat, borderConst, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            /* Verify CRC over the returned mapPtr value. */
            if ( mapPtr != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                uint32_t rxNum = 0U;
                sbRx[0].buf = reinterpret_cast<const char *>( mapPtr );
                sbRx[0].len = sizeof( *mapPtr );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
        }
        else
        {}
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_CreateMapNoUndistortion                                  */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_FadasRemap_CreateMapNoUndistortion( remote_handle64 _h, uint64 *mapPtr,
                                                         uint32_t camWidth, uint32_t camHeight,
                                                         uint32_t mapWidth, uint32_t mapHeight,
                                                         FadasIface_FadasRemapPipeline_e imgFormat,
                                                         uint8_t borderConst )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    struct scatter_buffer sbTx[6] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[0].buf = reinterpret_cast<const char *>( &camWidth );
    sbTx[0].len = sizeof( camWidth );
    sbTx[1].buf = reinterpret_cast<const char *>( &camHeight );
    sbTx[1].len = sizeof( camHeight );
    sbTx[2].buf = reinterpret_cast<const char *>( &mapWidth );
    sbTx[2].len = sizeof( mapWidth );
    sbTx[3].buf = reinterpret_cast<const char *>( &mapHeight );
    sbTx[3].len = sizeof( mapHeight );
    sbTx[4].buf = reinterpret_cast<const char *>( &imgFormat );
    sbTx[4].len = sizeof( imgFormat );
    sbTx[5].buf = reinterpret_cast<const char *>( &borderConst );
    sbTx[5].len = sizeof( borderConst );

    if ( (int)crc32_generate_scatter( sbTx, 6, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_CreateMapNoUndistortionSafe( _h, mapPtr, camWidth, camHeight,
                                                                 mapWidth, mapHeight, imgFormat,
                                                                 borderConst, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( mapPtr != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                uint32_t rxNum = 0U;
                sbRx[0].buf = reinterpret_cast<const char *>( mapPtr );
                sbRx[0].len = sizeof( *mapPtr );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_DestroyMap                                               */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasRemap_DestroyMapSafe(in uint64 mapPtr, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_FadasRemap_DestroyMap( remote_handle64 _h, uint64 mapPtr )
{
    uint32_t crcTx = 0U;
    struct scatter_buffer sbTx[1] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;
    sbTx[0].buf = reinterpret_cast<const char *>( &mapPtr );
    sbTx[0].len = sizeof( mapPtr );

    if ( (int)crc32_generate_scatter( sbTx, 1, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_DestroyMapSafe( _h, mapPtr, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_CreateWorkers                                            */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_FadasRemap_CreateWorkers( remote_handle64 _h, uint64 *worker_ptr,
                                               uint32_t nThreads,
                                               FadasIface_FadasRemapPipeline_e imgFormat )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    struct scatter_buffer sbTx[2] = {};
    AEEResult ret = AEE_SUCCESS;

    sbTx[0].buf = reinterpret_cast<const char *>( &nThreads );
    sbTx[0].len = sizeof( nThreads );
    sbTx[1].buf = reinterpret_cast<const char *>( &imgFormat );
    sbTx[1].len = sizeof( imgFormat );
    if ( (int)crc32_generate_scatter( sbTx, 2, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_CreateWorkersSafe( _h, worker_ptr, nThreads, imgFormat, crcTx,
                                                       &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( worker_ptr != nullptr )
            {
                struct scatter_buffer sbRx[1] = {};
                sbRx[0].buf = reinterpret_cast<const char *>( worker_ptr );
                sbRx[0].len = sizeof( *worker_ptr );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_DestroyWorkers                                           */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasRemap_DestroyWorkersSafe(in uint64 worker_ptr, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_FadasRemap_DestroyWorkers( remote_handle64 _h, uint64 worker_ptr )
{
    uint32_t crcTx = 0U;
    struct scatter_buffer sbTx[1] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[0].buf = reinterpret_cast<const char *>( &worker_ptr );
    sbTx[0].len = sizeof( worker_ptr );

    if ( (int)crc32_generate_scatter( sbTx, 1, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_DestroyWorkersSafe( _h, worker_ptr, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRemap_RunMT                                                    */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasRemap_RunMTSafe(..., in uint32_t crcTx)
 * No crcRx — all parameters are inputs.
 *
 * Maximum scatter entries: workerPtrs + workerPtrsLen + mapPtrs + mapPtrsLen
 *   + srcFds + srcFdsLen + offsets + offsetsLen + srcProps + srcPropsLen
 *   + dstFd + dstLen + dstProps + dstROIs + dstROIsLen + normlz + normlzLen
 *   = 17 entries.
 */
AEEResult FadasIface_FadasRemap_RunMT( remote_handle64 _h, const uint64 *workerPtrs,
                                       int workerPtrsLen, const uint64 *mapPtrs, int mapPtrsLen,
                                       const int32_t *srcFds, int srcFdsLen,
                                       const uint32_t *offsets, int offsetsLen,
                                       const FadasIface_FadasImgProps_t *srcProps, int srcPropsLen,
                                       int32_t dstFd, uint32_t dstLen,
                                       const FadasIface_FadasImgProps_t *dstProps,
                                       const FadasIface_FadasROI_t *dstROIs, int dstROIsLen,
                                       const FadasIface_FadasNormlzParams_t *normlz, int normlzLen )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[FADAS_MAX_SB] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    if ( workerPtrs != nullptr && workerPtrsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( workerPtrs );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *workerPtrs ) * workerPtrsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &workerPtrsLen );
    sbTx[sbNum].len = sizeof( workerPtrsLen );
    sbNum++;

    if ( mapPtrs != nullptr && mapPtrsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( mapPtrs );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *mapPtrs ) * mapPtrsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &mapPtrsLen );
    sbTx[sbNum].len = sizeof( mapPtrsLen );
    sbNum++;

    if ( srcFds != nullptr && srcFdsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( srcFds );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *srcFds ) * srcFdsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &srcFdsLen );
    sbTx[sbNum].len = sizeof( srcFdsLen );
    sbNum++;

    if ( offsets != nullptr && offsetsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( offsets );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *offsets ) * offsetsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &offsetsLen );
    sbTx[sbNum].len = sizeof( offsetsLen );
    sbNum++;

    if ( srcProps != nullptr && srcPropsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( srcProps );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *srcProps ) * srcPropsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &srcPropsLen );
    sbTx[sbNum].len = sizeof( srcPropsLen );
    sbNum++;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstFd );
    sbTx[sbNum].len = sizeof( dstFd );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstLen );
    sbTx[sbNum].len = sizeof( dstLen );
    sbNum++;

    if ( dstProps != nullptr )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( dstProps );
        sbTx[sbNum].len = sizeof( *dstProps );
        sbNum++;
    }

    if ( dstROIs != nullptr && dstROIsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( dstROIs );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *dstROIs ) * dstROIsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &dstROIsLen );
    sbTx[sbNum].len = sizeof( dstROIsLen );
    sbNum++;

    if ( normlz != nullptr && normlzLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( normlz );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *normlz ) * normlzLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &normlzLen );
    sbTx[sbNum].len = sizeof( normlzLen );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasRemap_RunMTSafe( _h, workerPtrs, workerPtrsLen, mapPtrs, mapPtrsLen,
                                               srcFds, srcFdsLen, offsets, offsetsLen, srcProps,
                                               srcPropsLen, dstFd, dstLen, dstProps, dstROIs,
                                               dstROIsLen, normlz, normlzLen, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  mmap                                                                */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult mmapSafe(in int32_t bufFd, in uint32_t bufSize, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_mmap( remote_handle64 _h, int32_t bufFd, uint32_t bufSize )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[2] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
    sbTx[sbNum].len = sizeof( bufFd );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
    sbTx[sbNum].len = sizeof( bufSize );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_mmapSafe( _h, bufFd, bufSize, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  munmap                                                              */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult munmapSafe(in int32_t bufFd, in uint32_t bufSize, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_munmap( remote_handle64 _h, int32_t bufFd, uint32_t bufSize )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[2] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
    sbTx[sbNum].len = sizeof( bufFd );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
    sbTx[sbNum].len = sizeof( bufSize );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_munmapSafe( _h, bufFd, bufSize, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasRegBuf                                                         */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasRegBufSafe(..., in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_FadasRegBuf( remote_handle64 _h, FadasIface_FadasBufType_e bufType,
                                  int32_t bufFd, uint32_t bufSize, uint32_t bufOffset,
                                  uint32_t batchSize )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[5] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufType );
    sbTx[sbNum].len = sizeof( bufType );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
    sbTx[sbNum].len = sizeof( bufFd );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
    sbTx[sbNum].len = sizeof( bufSize );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufOffset );
    sbTx[sbNum].len = sizeof( bufOffset );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &batchSize );
    sbTx[sbNum].len = sizeof( batchSize );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        FadasIface_FadasRegBufSafe( _h, bufType, bufFd, bufSize, bufOffset, batchSize, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  FadasDeregBuf                                                       */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult FadasDeregBufSafe(..., in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_FadasDeregBuf( remote_handle64 _h, int32_t bufFd, uint32_t bufSize,
                                    uint32_t bufOffset, uint32_t batchSize )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[4] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufFd );
    sbTx[sbNum].len = sizeof( bufFd );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufSize );
    sbTx[sbNum].len = sizeof( bufSize );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bufOffset );
    sbTx[sbNum].len = sizeof( bufOffset );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &batchSize );
    sbTx[sbNum].len = sizeof( batchSize );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_FadasDeregBufSafe( _h, bufFd, bufSize, bufOffset, batchSize, crcTx );
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  PointPillarCreate                                                   */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_PointPillarCreate( remote_handle64 _h, const FadasIface_Pt3D_t *pPlrSize,
                                        const FadasIface_Pt3D_t *pMinRange,
                                        const FadasIface_Pt3D_t *pMaxRange, uint32_t maxNumInPts,
                                        uint32_t numInFeatureDim, uint32_t maxNumPlrs,
                                        uint32_t maxNumPtsPerPlr, uint32_t numOutFeatureDim,
                                        uint64_t *phPreProc )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[8] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;
    if ( pPlrSize != nullptr )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( pPlrSize );
        sbTx[sbNum].len = sizeof( *pPlrSize );
        sbNum++;
    }
    if ( pMinRange != nullptr )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( pMinRange );
        sbTx[sbNum].len = sizeof( *pMinRange );
        sbNum++;
    }
    if ( pMaxRange != nullptr )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( pMaxRange );
        sbTx[sbNum].len = sizeof( *pMaxRange );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumInPts );
    sbTx[sbNum].len = sizeof( maxNumInPts );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numInFeatureDim );
    sbTx[sbNum].len = sizeof( numInFeatureDim );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumPlrs );
    sbTx[sbNum].len = sizeof( maxNumPlrs );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumPtsPerPlr );
    sbTx[sbNum].len = sizeof( maxNumPtsPerPlr );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numOutFeatureDim );
    sbTx[sbNum].len = sizeof( numOutFeatureDim );
    sbNum++;
    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_PointPillarCreateSafe( _h, pPlrSize, pMinRange, pMaxRange, maxNumInPts,
                                                numInFeatureDim, maxNumPlrs, maxNumPtsPerPlr,
                                                numOutFeatureDim, phPreProc, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( phPreProc != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                sbRx[0].buf = reinterpret_cast<const char *>( phPreProc );
                sbRx[0].len = sizeof( *phPreProc );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  PointPillarRun                                                      */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_PointPillarRun( remote_handle64 _h, uint64_t hPreProc, uint32_t numPts,
                                     int32_t fdInPts, uint32_t inPtsOffset, uint32_t inPtsSize,
                                     int32_t fdOutPlrs, uint32_t outPlrsOffset,
                                     uint32_t outPlrsSize, int32_t fdOutFeature,
                                     uint32_t outFeatureOffset, uint32_t outFeatureSize,
                                     uint32_t *pNumOutPlrs )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    uint32_t sbNum = 0U;
    AEEResult ret = AEE_SUCCESS;
    struct scatter_buffer sbTx[11] = { { 0 } };
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPreProc );
    sbTx[sbNum].len = sizeof( hPreProc );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numPts );
    sbTx[sbNum].len = sizeof( numPts );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdInPts );
    sbTx[sbNum].len = sizeof( fdInPts );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &inPtsOffset );
    sbTx[sbNum].len = sizeof( inPtsOffset );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &inPtsSize );
    sbTx[sbNum].len = sizeof( inPtsSize );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdOutPlrs );
    sbTx[sbNum].len = sizeof( fdOutPlrs );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &outPlrsOffset );
    sbTx[sbNum].len = sizeof( outPlrsOffset );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &outPlrsSize );
    sbTx[sbNum].len = sizeof( outPlrsSize );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdOutFeature );
    sbTx[sbNum].len = sizeof( fdOutFeature );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &outFeatureOffset );
    sbTx[sbNum].len = sizeof( outFeatureOffset );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &outFeatureSize );
    sbTx[sbNum].len = sizeof( outFeatureSize );
    sbNum++;
    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_PointPillarRunSafe( _h, hPreProc, numPts, fdInPts, inPtsOffset, inPtsSize,
                                             fdOutPlrs, outPlrsOffset, outPlrsSize, fdOutFeature,
                                             outFeatureOffset, outFeatureSize, pNumOutPlrs, crcTx,
                                             &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( pNumOutPlrs != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                sbRx[0].buf = reinterpret_cast<const char *>( pNumOutPlrs );
                sbRx[0].len = sizeof( *pNumOutPlrs );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  PointPillarDestroy                                                  */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult PointPillarDestroySafe(in uint64_t hPreProc, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_PointPillarDestroy( remote_handle64 _h, uint64_t hPreProc )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[1] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPreProc );
    sbTx[sbNum].len = sizeof( hPreProc );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_PointPillarDestroySafe( _h, hPreProc, crcTx );
    }
    return ret;
}
/* ------------------------------------------------------------------ */
/*  ExtractBBoxCreate                                                   */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_ExtractBBoxCreate( remote_handle64 _h, uint32_t maxNumInPts,
                                        uint32_t numInFeatureDim, uint32_t maxNumDetOut,
                                        uint32_t numClass, const FadasIface_Grid2D_t *pGrid,
                                        float threshScore, float threshIOU, float minCentreX,
                                        float minCentreY, float minCentreZ, float maxCentreX,
                                        float maxCentreY, float maxCentreZ,
                                        const uint8_t *labelSelect, int labelSelectLen,
                                        uint32_t maxNumFilter, uint64_t *phPostProc )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[16] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumInPts );
    sbTx[sbNum].len = sizeof( maxNumInPts );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numInFeatureDim );
    sbTx[sbNum].len = sizeof( numInFeatureDim );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumDetOut );
    sbTx[sbNum].len = sizeof( maxNumDetOut );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numClass );
    sbTx[sbNum].len = sizeof( numClass );
    sbNum++;
    if ( pGrid != nullptr )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( pGrid );
        sbTx[sbNum].len = sizeof( *pGrid );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &threshScore );
    sbTx[sbNum].len = sizeof( threshScore );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &threshIOU );
    sbTx[sbNum].len = sizeof( threshIOU );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreX );
    sbTx[sbNum].len = sizeof( minCentreX );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreY );
    sbTx[sbNum].len = sizeof( minCentreY );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &minCentreZ );
    sbTx[sbNum].len = sizeof( minCentreZ );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreX );
    sbTx[sbNum].len = sizeof( maxCentreX );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreY );
    sbTx[sbNum].len = sizeof( maxCentreY );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxCentreZ );
    sbTx[sbNum].len = sizeof( maxCentreZ );
    sbNum++;
    if ( labelSelect != nullptr && labelSelectLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( labelSelect );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *labelSelect ) * labelSelectLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &labelSelectLen );
    sbTx[sbNum].len = sizeof( labelSelectLen );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &maxNumFilter );
    sbTx[sbNum].len = sizeof( maxNumFilter );
    sbNum++;
    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_ExtractBBoxCreateSafe(
                _h, maxNumInPts, numInFeatureDim, maxNumDetOut, numClass, pGrid, threshScore,
                threshIOU, minCentreX, minCentreY, minCentreZ, maxCentreX, maxCentreY, maxCentreZ,
                labelSelect, labelSelectLen, maxNumFilter, phPostProc, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( phPostProc != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                sbRx[0].buf = reinterpret_cast<const char *>( phPostProc );
                sbRx[0].len = sizeof( *phPostProc );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  ExtractBBoxRun                                                      */
/* ------------------------------------------------------------------ */
AEEResult FadasIface_ExtractBBoxRun( remote_handle64 _h, uint64_t hPostProc, uint32_t numPts,
                                     const int32_t *fds, int fdsLen, const uint32_t *offsets,
                                     int offsetsLen, const uint32_t *sizes, int sizesLen,
                                     uint8_t bMapPtsToBBox, uint8_t bBBoxFilter,
                                     uint32_t *pNumDetOut )
{
    uint32_t crcTx = 0U;
    uint32_t crcRx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[10] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPostProc );
    sbTx[sbNum].len = sizeof( hPostProc );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &numPts );
    sbTx[sbNum].len = sizeof( numPts );
    sbNum++;
    if ( fds != nullptr && fdsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( fds );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *fds ) * fdsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &fdsLen );
    sbTx[sbNum].len = sizeof( fdsLen );
    sbNum++;
    if ( offsets != nullptr && offsetsLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( offsets );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *offsets ) * offsetsLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &offsetsLen );
    sbTx[sbNum].len = sizeof( offsetsLen );
    sbNum++;
    if ( sizes != nullptr && sizesLen > 0 )
    {
        sbTx[sbNum].buf = reinterpret_cast<const char *>( sizes );
        sbTx[sbNum].len = static_cast<sl_size_t>( sizeof( *sizes ) * sizesLen );
        sbNum++;
    }
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &sizesLen );
    sbTx[sbNum].len = sizeof( sizesLen );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bMapPtsToBBox );
    sbTx[sbNum].len = sizeof( bMapPtsToBBox );
    sbNum++;
    sbTx[sbNum].buf = reinterpret_cast<const char *>( &bBBoxFilter );
    sbTx[sbNum].len = sizeof( bBBoxFilter );
    sbNum++;
    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_ExtractBBoxRunSafe( _h, hPostProc, numPts, fds, fdsLen, offsets,
                                             offsetsLen, sizes, sizesLen, bMapPtsToBBox,
                                             bBBoxFilter, pNumDetOut, crcTx, &crcRx );
        if ( ret == AEE_SUCCESS )
        {
            if ( pNumDetOut != nullptr )
            {
                struct scatter_buffer sbRx[1] = { { 0 } };
                sbRx[0].buf = reinterpret_cast<const char *>( pNumDetOut );
                sbRx[0].len = sizeof( *pNumDetOut );
                if ( (int)crc32_verify_scatter( sbRx, 1, crcRx ) != CRC_RET_SUCCESS_VALUE )
                {
                    ret = AEE_EFAILED;
                }
                else
                {}
            }
            else
            {}
        }
        else
        {}
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  ExtractBBoxDestroy                                                  */
/* ------------------------------------------------------------------ */
/*
 * IDL: AEEResult ExtractBBoxDestroySafe(in uint64_t hPostProc, in uint32_t crcTx)
 * No crcRx.
 */
AEEResult FadasIface_ExtractBBoxDestroy( remote_handle64 _h, uint64_t hPostProc )
{
    uint32_t crcTx = 0U;
    uint32_t sbNum = 0U;
    struct scatter_buffer sbTx[1] = { { 0 } };
    AEEResult ret = AEE_SUCCESS;

    sbTx[sbNum].buf = reinterpret_cast<const char *>( &hPostProc );
    sbTx[sbNum].len = sizeof( hPostProc );
    sbNum++;

    if ( (int)crc32_generate_scatter( sbTx, sbNum, reinterpret_cast<sl_u32_t *>( &crcTx ) ) !=
         CRC_RET_SUCCESS_VALUE )
    {
        ret = AEE_EFAILED;
    }
    else
    {
        ret = FadasIface_ExtractBBoxDestroySafe( _h, hPostProc, crcTx );
    }
    return ret;
}
