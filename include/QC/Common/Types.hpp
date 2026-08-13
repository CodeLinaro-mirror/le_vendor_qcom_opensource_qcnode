// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_TYPES_HPP
#define QC_TYPES_HPP

#include "QC/Common/QCDefs.hpp"
#include <cinttypes>
#include <cstddef>

namespace QC
{

/** @brief The maximum number of a image planes. */
#define QC_NUM_IMAGE_PLANES ( 4 )

/** @brief The maximum number of a tensor planes. */
#define QC_NUM_TENSOR_DIMS ( 8 )

/** @brief The maximum number of inputs of the QC component. */
#ifndef QC_MAX_INPUTS
#define QC_MAX_INPUTS 32
#endif

/** @brief QC Computing Processor Type*/
typedef enum
{
    QC_PROCESSOR_HTP0, /**< do computing on the processor HTP0 */
    QC_PROCESSOR_HTP1, /**< do computing on the processor HTP1 */
    QC_PROCESSOR_HTP2, /**< do computing on the processor HTP2 */
    QC_PROCESSOR_HTP3, /**< do computing on the processor HTP3 */
    QC_PROCESSOR_CPU,  /**< do computing on the processor CPU */
    QC_PROCESSOR_GPU,  /**< do computing on the processor GPU */
    QC_PROCESSOR_MAX
} QCProcessorType_e;

/** @brief The image format. */
typedef enum
{
    /**< Below formats for an image without compression */
    QC_IMAGE_FORMAT_RGB888 = 0,
    QC_IMAGE_FORMAT_BGR888,
    QC_IMAGE_FORMAT_UYVY,
    QC_IMAGE_FORMAT_NV12,
    QC_IMAGE_FORMAT_P010,
    QC_IMAGE_FORMAT_NV12_UBWC,
    QC_IMAGE_FORMAT_TP10_UBWC,
    QC_IMAGE_FORMAT_MAX,
    /**< Below formats for an image with compression, such as by the Video Encoder */
    QC_IMAGE_FORMAT_COMPRESSED_MIN = 100,
    QC_IMAGE_FORMAT_COMPRESSED_H264 = 100,
    QC_IMAGE_FORMAT_COMPRESSED_H265,
    QC_IMAGE_FORMAT_COMPRESSED_MAX,
} QCImageFormat_e;

/** @brief The QC tensor data type. */
typedef enum
{

    QC_TENSOR_TYPE_INT_8,  /**< 8-bit integer type */
    QC_TENSOR_TYPE_INT_16, /**< 16-bit integer type */
    QC_TENSOR_TYPE_INT_32, /**< 32-bit integer type */
    QC_TENSOR_TYPE_INT_64, /** 64-bit integer type */

    QC_TENSOR_TYPE_UINT_8,  /**< 8-bit unsigned integer type */
    QC_TENSOR_TYPE_UINT_16, /**< 16-bit unsigned integer type */
    QC_TENSOR_TYPE_UINT_32, /**< 32-bit unsigned integer type */
    QC_TENSOR_TYPE_UINT_64, /**< 64-bit unsigned integer type */

    QC_TENSOR_TYPE_FLOAT_16, /**< 16-bit float point type */
    QC_TENSOR_TYPE_FLOAT_32, /**< 32-bit float point type */
    QC_TENSOR_TYPE_FLOAT_64, /**< 64-bit float point type */

    QC_TENSOR_TYPE_SFIXED_POINT_8,  /**< 8-bit singed fixed point type */
    QC_TENSOR_TYPE_SFIXED_POINT_16, /**< 16-bit singed fixed point type */
    QC_TENSOR_TYPE_SFIXED_POINT_32, /**< 32-bit singed fixed point type */

    QC_TENSOR_TYPE_UFIXED_POINT_8,  /**< 8-bit unsinged fixed point type */
    QC_TENSOR_TYPE_UFIXED_POINT_16, /**< 16-bit unsinged fixed point type */
    QC_TENSOR_TYPE_UFIXED_POINT_32, /**< 32-bit unsinged fixed point type */

    QC_TENSOR_TYPE_BOOL_8, /**< 8-bit boolean type, 0 = false, any non-zero value = true */

    QC_TENSOR_TYPE_MAX,
} QCTensorType_e;

}   // namespace QC

#endif   // QC_TYPES_HPP
