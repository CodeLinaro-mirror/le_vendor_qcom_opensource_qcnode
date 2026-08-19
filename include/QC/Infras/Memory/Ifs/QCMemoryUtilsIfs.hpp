
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_MEMORY_UTILS_IFS_HPP
#define QC_MEMORY_UTILS_IFS_HPP

#include <functional>
#include <string>
#include <vector>

#include "QC/Common/Types.hpp"
#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/Ifs/QCBufferDescriptorBase.hpp"
#include "QC/Infras/Memory/ImageDescriptor.hpp"
#include "QC/Infras/Memory/TensorDescriptor.hpp"

namespace QC
{
namespace Memory
{

/**
 * @enum QCMemoryTransition_e
 * @brief Enumerates the zero-copy memory transition types between buffer descriptor types.
 *
 * Each enumerator describes a specific source image format and the target descriptor
 * (always a TensorDescriptor_t) that the transition produces.  For dual-plane formats
 * (NV12, P010) the caller invokes CreateTransition() twice — once for the luma plane
 * and once for the chroma plane — using the appropriate enumerator.
 */
typedef enum
{
    // Single-plane image → single tensor
    QC_MEMORY_TRANSITION_RGB_TO_TENSOR = 0, /**< QC_IMAGE_FORMAT_RGB888  → TensorDescriptor_t */
    QC_MEMORY_TRANSITION_BGR_TO_TENSOR,     /**< QC_IMAGE_FORMAT_BGR888  → TensorDescriptor_t */
    QC_MEMORY_TRANSITION_UYVY_TO_TENSOR,    /**< QC_IMAGE_FORMAT_UYVY    → TensorDescriptor_t */

    // Dual-plane image → single tensor (luma / Y-plane only)
    QC_MEMORY_TRANSITION_NV12_TO_GRAY, /**< QC_IMAGE_FORMAT_NV12 Y-plane  → TensorDescriptor_t */
    QC_MEMORY_TRANSITION_P010_TO_GRAY, /**< QC_IMAGE_FORMAT_P010 Y-plane  → TensorDescriptor_t */

    // Dual-plane image → single tensor (chroma / UV-plane only)
    QC_MEMORY_TRANSITION_NV12_TO_CHROMA, /**< QC_IMAGE_FORMAT_NV12 UV-plane → TensorDescriptor_t */
    QC_MEMORY_TRANSITION_P010_TO_CHROMA, /**< QC_IMAGE_FORMAT_P010 UV-plane → TensorDescriptor_t */

    QC_MEMORY_TRANSITION_LAST,
    QC_MEMORY_TRANSITION_MAX = UINT32_MAX
} QCMemoryTransition_e;

/**
 * @typedef QCMemoryTransitionFn_t
 * @brief Callable type returned by QCMemoryUtilsIfs::CreateTransition().
 *
 * Performs a zero-copy transition from @p src (an ImageDescriptor_t) to @p dst
 * (a TensorDescriptor_t).  The underlying DMA memory is shared; no data is copied.
 *
 * @param src  Source buffer descriptor.  Must be an ImageDescriptor_t whose format
 *             matches the transition type used when the callable was created.
 * @param dst  Destination buffer descriptor.  Must be a TensorDescriptor_t.
 * @return QC_STATUS_OK on success; QC_STATUS_BAD_ARGUMENTS if the descriptor types
 *         or image format do not match; other status codes on failure.
 */
using QCMemoryTransitionFn_t = std::function<QCStatus_e( const QCBufferDescriptorBase_t &src,
                                                         QCBufferDescriptorBase_t &dst )>;

/**
 * @class QCMemoryUtilsIfs
 * @brief Interface for memory utility classes.
 * This class provides a common interface for memory utilities,
 * including methods for memory mapping, unmapping, and calculating buffer sizes for tensors and
 * images.
 */
class QCMemoryUtilsIfs
{
public:
    /**
     * @brief Maps a buffer into memory.
     * This method maps a buffer into memory and returns a status code indicating success or
     * failure.
     * @param orig The buffer descriptor to map into memory.
     * @param mapped The buffer descriptor mapped into memory.
     * @return The status of the memory mapping operation.
     */
    virtual QCStatus_e MemoryMap( const QCBufferDescriptorBase_t &orig,
                                  QCBufferDescriptorBase_t &mapped ) = 0;

    /**
     * @brief Unmaps a buffer from memory.
     * This method unmaps a buffer from memory and returns a status code indicating success or
     * failure.
     * @param buff The buffer descriptor to unmap from memory.
     * @return The status of the memory unmapping operation.
     */
    virtual QCStatus_e MemoryUnMap( const QCBufferDescriptorBase_t &buff ) = 0;

    /**
     * @brief Sets tensor descriptorvalues from tensor properties.
     * @param prop The tensor properties.
     * @param desc The tensor descriptor.
     * @return The status of the buffer size calculation operation.
     */
    virtual QCStatus_e SetTensorDescFromTensorProp( TensorProps_t &prop,
                                                    TensorDescriptor_t &desc ) = 0;

    /**
     * @brief Sets the buffer descriptor values for an image.
     * @param prop The image basic properties.
     * @param desc The image descriptor.
     * @return The status of the buffer descriptor setting operation.
     */
    virtual QCStatus_e SetImageDescFromImageBasicProp( ImageBasicProps_t &prop,
                                                       ImageDescriptor_t &desc ) = 0;

    /**
     * @brief Sets the buffer descriptor values for an image.
     * @param prop The image properties.
     * @param desc The image descriptor.
     * @return The status of the buffer descriptor setting operation.
     */
    virtual QCStatus_e SetImageDescFromImageProp( ImageProps_t &prop, ImageDescriptor_t &desc ) = 0;

    // ------------------------------------------------------------------
    // Memory transition APIs
    // ------------------------------------------------------------------

    /**
     * @brief Returns all transition types supported by this utils implementation.
     *
     * All returned types are guaranteed to succeed when passed to CreateTransition().
     * @return Const reference to a static vector of supported QCMemoryTransition_e values.
     */
    virtual const std::vector<QCMemoryTransition_e> &GetSupportedTransitionTypes() = 0;

    /**
     * @brief Returns a transition callable for the given type.
     *
     * The callable performs a zero-copy transition from an ImageDescriptor_t to a
     * TensorDescriptor_t, reusing the underlying DMA buffer without any data copy.
     *
     * @param type   The desired transition type.
     * @param outFn  On success, receives the transition callable.
     *               Set to nullptr on failure.
     * @return QC_STATUS_OK on success.
     *         QC_STATUS_UNSUPPORTED if @p type is not in GetSupportedTransitionTypes().
     */
    virtual QCStatus_e CreateTransition( QCMemoryTransition_e type,
                                         QCMemoryTransitionFn_t &outFn ) = 0;

    /**
     * @brief Returns the supported transition types as a JSON object.
     *
     * The returned string is a JSON object whose keys are the enumerator name strings
     * and whose values are the corresponding integer values, e.g.:
     * @code
     * {"QC_MEMORY_TRANSITION_RGB_TO_TENSOR":0,"QC_MEMORY_TRANSITION_BGR_TO_TENSOR":1,...}
     * @endcode
     * @return JSON string of supported transition types.
     */
    virtual std::string GetSupportedBufferTransitionTypesJson() = 0;

protected:
    QCMemoryUtilsIfs() = default;
    QCMemoryUtilsIfs( const QCMemoryUtilsIfs & ) = default;
    QCMemoryUtilsIfs& operator=( const QCMemoryUtilsIfs& ) = default;
    ~QCMemoryUtilsIfs() = default;
};

}   // namespace Memory
}   // namespace QC

#endif   // QC_MEMORY_UTILS_IFS_HPP
