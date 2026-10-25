// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_MEMORY_UTILS_BASE_HPP
#define QC_MEMORY_UTILS_BASE_HPP

#include "QC/Infras/Memory/Ifs/QCMemoryUtilsIfs.hpp"
#include <apdf.h>

namespace QC
{
namespace Memory
{

/**
 * @brief Returns the number of bytes per pixel for an uncompressed image format.
 *
 * @return The bytes per pixel, or zero when the format is unsupported or compressed.
 */
constexpr uint32_t GetBytesPerPixel( QCImageFormat_e format ) noexcept
{
    uint32_t bytesPerPixel = 0U;

    switch ( format )
    {
        case QC_IMAGE_FORMAT_RGB888:
        case QC_IMAGE_FORMAT_BGR888:
            bytesPerPixel = 3U;
            break;
        case QC_IMAGE_FORMAT_UYVY:
        case QC_IMAGE_FORMAT_P010:
        case QC_IMAGE_FORMAT_TP10_UBWC:
            bytesPerPixel = 2U;
            break;
        case QC_IMAGE_FORMAT_NV12:
        case QC_IMAGE_FORMAT_NV12_UBWC:
            bytesPerPixel = 1U;
            break;
        default:
            break;
    }

    return bytesPerPixel;
}

constexpr const char *GetImageFormatString( QCImageFormat_e format ) noexcept
{
    const char *formatString = "UNKNOWN";

    switch ( format )
    {
        case QC_IMAGE_FORMAT_RGB888:
            formatString = "RGB888";
            break;
        case QC_IMAGE_FORMAT_BGR888:
            formatString = "BGR888";
            break;
        case QC_IMAGE_FORMAT_UYVY:
            formatString = "UYVY";
            break;
        case QC_IMAGE_FORMAT_NV12:
            formatString = "NV12";
            break;
        case QC_IMAGE_FORMAT_P010:
            formatString = "P010";
            break;
        case QC_IMAGE_FORMAT_NV12_UBWC:
            formatString = "NV12_UBWC";
            break;
        case QC_IMAGE_FORMAT_TP10_UBWC:
            formatString = "TP_UBWC";
            break;
        default:
            break;
    }

    return formatString;
}

constexpr PDColorFormat_e GetApdfFormat( QCImageFormat_e format ) noexcept
{
    PDColorFormat_e apdfFormat = PD_FORMAT_MAX;

    switch ( format )
    {
        case QC_IMAGE_FORMAT_RGB888:
        case QC_IMAGE_FORMAT_BGR888:
            apdfFormat = PD_FORMAT_RGB888;
            break;
        case QC_IMAGE_FORMAT_UYVY:
            apdfFormat = PD_FORMAT_UYVY;
            break;
        case QC_IMAGE_FORMAT_NV12:
        case QC_IMAGE_FORMAT_NV12_UBWC:
            apdfFormat = PD_FORMAT_NV12;
            break;
        case QC_IMAGE_FORMAT_P010:
            apdfFormat = PD_FORMAT_P010;
            break;
        case QC_IMAGE_FORMAT_TP10_UBWC:
            apdfFormat = PD_FORMAT_TP10;
            break;
        default:
            break;
    }

    return apdfFormat;
}

constexpr uint32_t GetTensorTypeDataSize( QCTensorType_e type ) noexcept
{
    uint32_t dataSize = 0U;

    switch ( type )
    {
        case QC_TENSOR_TYPE_INT_8:
        case QC_TENSOR_TYPE_UINT_8:
        case QC_TENSOR_TYPE_SFIXED_POINT_8:
        case QC_TENSOR_TYPE_UFIXED_POINT_8:
        case QC_TENSOR_TYPE_BOOL_8:
            dataSize = 1U;
            break;
        case QC_TENSOR_TYPE_INT_16:
        case QC_TENSOR_TYPE_UINT_16:
        case QC_TENSOR_TYPE_FLOAT_16:
        case QC_TENSOR_TYPE_SFIXED_POINT_16:
        case QC_TENSOR_TYPE_UFIXED_POINT_16:
            dataSize = 2U;
            break;
        case QC_TENSOR_TYPE_INT_32:
        case QC_TENSOR_TYPE_UINT_32:
        case QC_TENSOR_TYPE_FLOAT_32:
        case QC_TENSOR_TYPE_SFIXED_POINT_32:
        case QC_TENSOR_TYPE_UFIXED_POINT_32:
            dataSize = 4U;
            break;
        case QC_TENSOR_TYPE_INT_64:
        case QC_TENSOR_TYPE_UINT_64:
        case QC_TENSOR_TYPE_FLOAT_64:
            dataSize = 8U;
            break;
        default:
            break;
    }

    return dataSize;
}

constexpr uint32_t GetNumberOfPlanes( QCImageFormat_e format ) noexcept
{
    uint32_t numberOfPlanes = 0U;

    switch ( format )
    {
        case QC_IMAGE_FORMAT_RGB888:
        case QC_IMAGE_FORMAT_BGR888:
        case QC_IMAGE_FORMAT_UYVY:
            numberOfPlanes = 1U;
            break;
        case QC_IMAGE_FORMAT_NV12:
        case QC_IMAGE_FORMAT_P010:
            numberOfPlanes = 2U;
            break;
        case QC_IMAGE_FORMAT_NV12_UBWC:
        case QC_IMAGE_FORMAT_TP10_UBWC:
            numberOfPlanes = 4U;
            break;
        default:
            break;
    }

    return numberOfPlanes;
}

constexpr uint32_t GetPlaneHeightDivider( QCImageFormat_e format, uint32_t planeIndex ) noexcept
{
    uint32_t heightDivider = 0U;

    if ( planeIndex < QC_NUM_IMAGE_PLANES )
    {
        switch ( format )
        {
            case QC_IMAGE_FORMAT_NV12:
            case QC_IMAGE_FORMAT_P010:
                heightDivider = ( 1U == planeIndex ) ? 2U : 1U;
                break;
            case QC_IMAGE_FORMAT_NV12_UBWC:
            case QC_IMAGE_FORMAT_TP10_UBWC:
                heightDivider = ( planeIndex >= 2U ) ? 2U : 1U;
                break;
            case QC_IMAGE_FORMAT_RGB888:
            case QC_IMAGE_FORMAT_BGR888:
            case QC_IMAGE_FORMAT_UYVY:
                heightDivider = 1U;
                break;
            default:
                break;
        }
    }

    return heightDivider;
}

/**
 * @class UtilsBase
 * @brief A concrete implementation of the QCMemoryUtilsIfs interface for PMEM allocation.
 *
 * This class provides methods for allocating and freeing memory from the PMEM software package.
 */
class UtilsBase : public QCMemoryUtilsIfs
{
public:
    /**
     * @brief Default constructor.
     * This constructor Intilizes members which do not require external parameters
     */
    UtilsBase();

    UtilsBase( const UtilsBase & ) = default;
    UtilsBase& operator=( const UtilsBase& ) = default;

    /**
     * @brief Destructor for the UtilsBase class.
     * This destructor releases any resources allocated by the ManagerLocal object.
     */
    virtual ~UtilsBase();

protected:
    explicit UtilsBase( const char *loggerName );

    QC_DECLARE_LOGGER();

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
                                  QCBufferDescriptorBase_t &mapped );

    /**
     * @brief Unmaps a buffer from memory.
     * This method unmaps a buffer from memory and returns a status code indicating success or
     * failure.
     * @param buff The buffer descriptor to unmap from memory.
     * @return The status of the memory unmapping operation.
     */
    virtual QCStatus_e MemoryUnMap( const QCBufferDescriptorBase_t &buff );

    /**
     * @brief Sets tensor descriptorvalues from tensor properties.
     * @param prop The tensor properties.
     * @param desc The tensor descriptor.
     * @return The status of the buffer size calculation operation.
     */
    virtual QCStatus_e SetTensorDescFromTensorProp( TensorProps_t &prop, TensorDescriptor_t &desc );

    /**
     * @brief Sets the buffer descriptor values for an image.
     * @param prop The image basic properties.
     * @param desc The image descriptor.
     * @return The status of the buffer descriptor setting operation.
     */
    virtual QCStatus_e SetImageDescFromImageBasicProp( ImageBasicProps_t &prop,
                                                       ImageDescriptor_t &desc );

    /**
     * @brief Sets the buffer descriptor values for an image.
     * @param prop The image properties.
     * @param desc The image descriptor.
     * @return The status of the buffer descriptor setting operation.
     */
    virtual QCStatus_e SetImageDescFromImageProp( ImageProps_t &prop, ImageDescriptor_t &desc );

    // ------------------------------------------------------------------
    // Memory transition APIs
    // ------------------------------------------------------------------

    /**
     * @brief Populates outTypes with all transition types supported by this implementation.
     *
     * All returned types are guaranteed to succeed when passed to CreateTransition().
     * @param[out] outTypes  Receives the vector of types; valid only on QC_STATUS_OK.
     * @return QC_STATUS_OK on success.
     */
    virtual QCStatus_e GetSupportedTransitionTypes(
            std::vector<QCMemoryTransition_e> &outTypes ) override;

    /**
     * @brief Returns a transition callable for the given type.
     *
     * The callable performs a zero-copy transition from an ImageDescriptor_t to a
     * TensorDescriptor_t, sharing the underlying buffer pointer without any data copy.
     *
     * @param type   The desired transition type.
     * @param outFn  On success, receives the transition callable.
     *               Set to nullptr on failure.
     * @return QC_STATUS_OK on success.
     *         QC_STATUS_UNSUPPORTED if @p type is not in GetSupportedTransitionTypes().
     */
    virtual QCStatus_e CreateTransition( QCMemoryTransition_e type,
                                         QCMemoryTransitionFn_t &outFn ) override;

    /**
     * @brief Populates outJson with a JSON object of supported transition types.
     *
     * Keys are enumerator name strings; values are the corresponding integer values.
     * @param[out] outJson  Receives the JSON string; valid only on QC_STATUS_OK.
     * @return QC_STATUS_OK on success.
     */
    virtual QCStatus_e GetSupportedBufferTransitionTypesJson( std::string &outJson ) override;
};

}   // namespace Memory
}   // namespace QC

#endif   // BASE
