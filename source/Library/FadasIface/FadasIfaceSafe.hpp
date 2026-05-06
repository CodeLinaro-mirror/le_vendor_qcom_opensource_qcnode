// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _FADASIFACESAFE_H
#define _FADASIFACESAFE_H

/*
 * FadasIface.h
 *
 * Public C API for the FadasIface wrapper library.
 *
 * Each function in this API:
 *   1. Computes a CRC32 over its input arguments (crcTx) using
 *      crc32_generate_scatter().
 *   2. Forwards the call to the corresponding safe implementation
 *      declared in FadasIface_safe.h (generated from FadasIface_safe.idl).
 *   3. Verifies the returned CRC (crcRx) on output buffers using
 *      crc32_verify_scatter() where applicable.
 *
 * FadasIface_open() and FadasIface_close() are provided directly by the
 * generated stub (FadasIface_safe.h / FadasIface_stub.c) and are not
 * re-wrapped here.
 */

#include <AEEStdDef.h>
#include <remote.h>

#include "FadasIface.h"

#ifdef __cplusplus
extern "C"
{
#endif

    AEEResult FadasIface_FadasInit( remote_handle64 _h, int32_t *status );

    AEEResult FadasIface_FadasVersion( remote_handle64 _h, uint8_t *version, int versionLen );

    AEEResult FadasIface_FadasDeInit( remote_handle64 _h );

    AEEResult FadasIface_FadasRemap_CreateMapFromMap(
            remote_handle64 _h, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
            uint32_t mapWidth, uint32_t mapHeight, int32_t mapXFd, int32_t mapYFd,
            uint32_t mapStride, FadasIface_FadasRemapPipeline_e imgFormat, uint8_t borderConst );

    AEEResult FadasIface_FadasRemap_CreateMapNoUndistortion(
            remote_handle64 _h, uint64 *mapPtr, uint32_t camWidth, uint32_t camHeight,
            uint32_t mapWidth, uint32_t mapHeight, FadasIface_FadasRemapPipeline_e imgFormat,
            uint8_t borderConst );

    AEEResult FadasIface_FadasRemap_DestroyMap( remote_handle64 _h, uint64 mapPtr );

    AEEResult FadasIface_FadasRemap_CreateWorkers( remote_handle64 _h, uint64 *worker_ptr,
                                                   uint32_t nThreads,
                                                   FadasIface_FadasRemapPipeline_e imgFormat );

    AEEResult FadasIface_FadasRemap_DestroyWorkers( remote_handle64 _h, uint64 worker_ptr );

    AEEResult FadasIface_FadasRemap_RunMT(
            remote_handle64 _h, const uint64 *workerPtrs, int workerPtrsLen, const uint64 *mapPtrs,
            int mapPtrsLen, const int32_t *srcFds, int srcFdsLen, const uint32_t *offsets,
            int offsetsLen, const FadasIface_FadasImgProps_t *srcProps, int srcPropsLen,
            int32_t dstFd, uint32_t dstLen, const FadasIface_FadasImgProps_t *dstProps,
            const FadasIface_FadasROI_t *dstROIs, int dstROIsLen,
            const FadasIface_FadasNormlzParams_t *normlz, int normlzLen );

    AEEResult FadasIface_mmap( remote_handle64 _h, int32_t bufFd, uint32_t bufSize );

    AEEResult FadasIface_munmap( remote_handle64 _h, int32_t bufFd, uint32_t bufSize );

    AEEResult FadasIface_FadasRegBuf( remote_handle64 _h, FadasIface_FadasBufType_e bufType,
                                      int32_t bufFd, uint32_t bufSize, uint32_t bufOffset,
                                      uint32_t batchSize );

    AEEResult FadasIface_FadasDeregBuf( remote_handle64 _h, int32_t bufFd, uint32_t bufSize,
                                        uint32_t bufOffset, uint32_t batchSize );

    AEEResult FadasIface_PointPillarCreate( remote_handle64 _h, const FadasIface_Pt3D_t *pPlrSize,
                                            const FadasIface_Pt3D_t *pMinRange,
                                            const FadasIface_Pt3D_t *pMaxRange,
                                            uint32_t maxNumInPts, uint32_t numInFeatureDim,
                                            uint32_t maxNumPlrs, uint32_t maxNumPtsPerPlr,
                                            uint32_t numOutFeatureDim, uint64_t *phPreProc );

    AEEResult FadasIface_PointPillarRun( remote_handle64 _h, uint64_t hPreProc, uint32_t numPts,
                                         int32_t fdInPts, uint32_t inPtsOffset, uint32_t inPtsSize,
                                         int32_t fdOutPlrs, uint32_t outPlrsOffset,
                                         uint32_t outPlrsSize, int32_t fdOutFeature,
                                         uint32_t outFeatureOffset, uint32_t outFeatureSize,
                                         uint32_t *pNumOutPlrs );

    AEEResult FadasIface_PointPillarDestroy( remote_handle64 _h, uint64_t hPreProc );

    AEEResult FadasIface_ExtractBBoxCreate( remote_handle64 _h, uint32_t maxNumInPts,
                                            uint32_t numInFeatureDim, uint32_t maxNumDetOut,
                                            uint32_t numClass, const FadasIface_Grid2D_t *pGrid,
                                            float threshScore, float threshIOU, float minCentreX,
                                            float minCentreY, float minCentreZ, float maxCentreX,
                                            float maxCentreY, float maxCentreZ,
                                            const uint8_t *labelSelect, int labelSelectLen,
                                            uint32_t maxNumFilter, uint64_t *phPostProc );

    AEEResult FadasIface_ExtractBBoxRun( remote_handle64 _h, uint64_t hPostProc, uint32_t numPts,
                                         const int32_t *fds, int fdsLen, const uint32_t *offsets,
                                         int offsetsLen, const uint32_t *sizes, int sizesLen,
                                         uint8_t bMapPtsToBBox, uint8_t bBBoxFilter,
                                         uint32_t *pNumDetOut );

    AEEResult FadasIface_ExtractBBoxDestroy( remote_handle64 _h, uint64_t hPostProc );

#ifdef __cplusplus
}
#endif

#endif /* _FADASIFACESAFE_H */
