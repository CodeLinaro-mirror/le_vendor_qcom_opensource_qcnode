// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "CL2DFlexImpl.hpp"
#include "QC/Infras/Memory/UtilsBase.hpp"
#include "QC/Node/CL2DFlex.hpp"
#include <unistd.h>

namespace QC
{
namespace Node
{

QCStatus_e CL2DFlexConfig::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;
    std::string name = dt.Get<std::string>( "name", "" );
    if ( "" == name )
    {
        errors += "the name is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    uint32_t id = dt.Get<uint32_t>( "id", UINT32_MAX );
    if ( UINT32_MAX == id )
    {
        errors += "the id is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    /*
     * The OpenCL interface supports the primary GPU (deviceId == 0) on all
     * platforms and the second GPU (deviceId == 1) only on SA8797 (Nordy).
     * QC_CL2DFLEX_DEVICE_ID_MAX reflects the maximum valid deviceId for the
     * current target SOC. Reject any deviceId beyond it early.
     */
    uint32_t deviceId = dt.Get<uint32_t>( "deviceId", 0 );
    if ( QC_CL2DFLEX_DEVICE_ID_MAX < deviceId )
    {
        errors += "the deviceId is invalid, max supported deviceId is " +
                  std::to_string( QC_CL2DFLEX_DEVICE_ID_MAX ) + ", ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( dt.Exists( "bufferIds" ) )
    {
        std::vector<uint32_t> bufferIds = dt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
        if ( 0 == bufferIds.size() )
        {
            errors += "the bufferIds is invalid, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    std::vector<DataTree> inputDts;
    (void) dt.Get( "inputs", inputDts );
    uint32_t numOfInputs = 0;
    for ( DataTree &idt : inputDts )
    {
        numOfInputs++;

        std::string mode = idt.Get<std::string>( "workMode", "resize_nearest" );
        if ( ( "convert" != mode ) && ( "resize_nearest" != mode ) &&
             ( "letterbox_nearest" != mode ) && ( "letterbox_nearest_multiple" != mode ) &&
             ( "resize_nearest_multiple" != mode ) && ( "convert_ubwc" != mode ) &&
             ( "remap_nearest" != mode ) )
        {
            errors += "the mode is invalid, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        if ( ( "letterbox_nearest_multiple" == mode ) || ( "resize_nearest_multiple" == mode ) )
        {
            if ( !dt.Exists( "numOfROIs" ) )
            {
                errors += "multiple work mode without number of ROIs, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else
            {
                uint32_t numOfROIs = dt.Get<uint32_t>( "numOfROIs", UINT32_MAX );
                if ( QC_CL2DFLEX_ROI_NUMBER_MAX < numOfROIs )
                {
                    errors += "ROIs number invalid, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
            }
            if ( !dt.Exists( "ROIsBufferId" ) )
            {
                errors += "multiple work mode without ROIs buffer ID, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
        }
        if ( "remap_nearest" == mode )
        {
            if ( ( !idt.Exists( "mapXBufferId" ) ) || ( !idt.Exists( "mapYBufferId" ) ) )
            {
                errors += "remap work mode without mapX and mapY buffer ID, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
        }
    }

    if ( QC_MAX_INPUTS < numOfInputs )
    {
        errors += "inputs number invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::vector<DataTree> globalBufferIdMap;
    status2 = dt.Get( "globalBufferIdMap", globalBufferIdMap );
    if ( QC_STATUS_OUT_OF_BOUND == status2 )
    {
        /* OK if not configured */
    }
    else if ( QC_STATUS_OK != status2 )
    {
        errors += "the globalBufferIdMap is invalid, ";
        status = status2;
    }
    else
    {
        uint32_t idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            std::string name = gbm.Get<std::string>( "name", "" );
            uint32_t index = gbm.Get<uint32_t>( "id", UINT32_MAX );
            if ( "" == name )
            {
                errors += "the globalIdMap " + std::to_string( idx ) + " name is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( UINT32_MAX == index )
            {
                errors += "the globalIdMap " + std::to_string( idx ) + " id is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            idx++;
        }
    }

    return status;
}

QCStatus_e CL2DFlexConfig::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    CL2DFlexImplConfig_t &config = m_pCL2DFlexImpl->GetConifg();
    status = VerifyStaticConfig( dt, errors );
    if ( QC_STATUS_OK == status )
    {
        config.nodeId.name = dt.Get<std::string>( "name", "" );
        config.nodeId.id = dt.Get<uint8_t>( "id", 255 );
        config.params.outputWidth = dt.Get<uint32_t>( "outputWidth", 1024 );
        config.params.outputHeight = dt.Get<uint32_t>( "outputHeight", 1024 );
        config.params.outputFormat = dt.GetImageFormat( "outputFormat", QC_IMAGE_FORMAT_RGB888 );

        std::string priority = dt.Get<std::string>( "priority", "normal" );
        if ( "normal" == priority )
        {
            config.params.priority = OPENCLIFACE_PERF_NORMAL;
        }
        else if ( "high" == priority )
        {
            config.params.priority = OPENCLIFACE_PERF_HIGH;
        }
        else if ( "low" == priority )
        {
            config.params.priority = OPENCLIFACE_PERF_LOW;
        }
        else
        {
            errors += "the priority is invalid, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        config.params.deviceId = dt.Get<uint32_t>( "deviceId", 0 );

        std::vector<DataTree> inputDts;
        (void) dt.Get( "inputs", inputDts );
        uint32_t inputId = 0;
        for ( DataTree &idt : inputDts )
        {
            config.params.inputWidths[inputId] = idt.Get<uint32_t>( "inputWidth", 1024 );
            config.params.inputHeights[inputId] = idt.Get<uint32_t>( "inputHeight", 1024 );
            config.params.inputFormats[inputId] =
                    idt.GetImageFormat( "inputFormat", QC_IMAGE_FORMAT_NV12 );
            config.params.ROIs[inputId].x = idt.Get<uint32_t>( "roiX", 0 );
            config.params.ROIs[inputId].y = idt.Get<uint32_t>( "roiY", 0 );
            config.params.ROIs[inputId].width = idt.Get<uint32_t>( "roiWidth", 0 );
            config.params.ROIs[inputId].height = idt.Get<uint32_t>( "roiHeight", 0 );
            std::string mode = idt.Get<std::string>( "workMode", "resize_nearest" );
            if ( "convert" == mode )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_CONVERT;
            }
            else if ( "resize_nearest" == mode )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST;
            }
            else if ( "letterbox_nearest" == mode )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST;
            }
            else if ( ( "letterbox_nearest_multiple" == mode ) && ( 0 == inputId ) )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE;
                config.params.numOfROIs = dt.Get<uint32_t>( "numOfROIs", UINT32_MAX );
                config.params.ROIsBufferId = dt.Get<uint32_t>( "ROIsBufferId", UINT32_MAX );
            }
            else if ( ( "resize_nearest_multiple" == mode ) && ( 0 == inputId ) )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_RESIZE_NEAREST_MULTIPLE;
                config.params.numOfROIs = dt.Get<uint32_t>( "numOfROIs", UINT32_MAX );
                config.params.ROIsBufferId = dt.Get<uint32_t>( "ROIsBufferId", UINT32_MAX );
            }
            else if ( "convert_ubwc" == mode )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_CONVERT_UBWC;
            }
            else if ( "remap_nearest" == mode )
            {
                config.params.workModes[inputId] = CL2DFLEX_WORK_MODE_REMAP_NEAREST;
                config.params.remapTable[inputId].mapXBufferId =
                        idt.Get<uint32_t>( "mapXBufferId", UINT32_MAX );
                config.params.remapTable[inputId].mapYBufferId =
                        idt.Get<uint32_t>( "mapYBufferId", UINT32_MAX );
            }
            else
            {
                /* no action required */
            }

            inputId++;
        }
        config.params.numOfInputs = inputId;

        config.bufferIds = dt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

        std::vector<DataTree> globalBufferIdMap;
        (void) dt.Get( "globalBufferIdMap", globalBufferIdMap );
        config.globalBufferIdMap.resize( globalBufferIdMap.size() );
        uint32_t idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            config.globalBufferIdMap[idx].name = gbm.Get<std::string>( "name", "" );
            config.globalBufferIdMap[idx].globalBufferId = gbm.Get<uint32_t>( "id", UINT32_MAX );
            idx++;
        }

        config.bDeRegisterAllBuffersWhenStop =
                dt.Get<bool>( "deRegisterAllBuffersWhenStop", false );

        config.executeMode = QC_EXECUTE_MODE_SYNC;
        config.processor = QC_PROCESSOR_GPU;
        config.coreIds = { config.params.deviceId };
        config.inputs.clear();
        config.outputs.clear();
        config.inputs.reserve( config.params.numOfInputs );
        for ( uint32_t i = 0U; i < config.params.numOfInputs; ++i )
        {
            config.inputs.push_back( { "input" + std::to_string( i ), QC_BUFFER_TYPE_IMAGE,
                                       Memory::ImageBasicProps_t( config.params.inputWidths[i],
                                                                   config.params.inputHeights[i],
                                                                   config.params.inputFormats[i] ) } );
        }

        const bool noPadding = dt.Get<bool>( "noPadding", false );
        const bool cacheable = dt.Get<bool>( "cache", true );
        const QCAllocationCache_e cache = cacheable ? QC_CACHEABLE : QC_CACHEABLE_NON;
        uint32_t outputBatchSize = config.params.numOfInputs;
        if ( ( 1U == config.params.numOfInputs ) &&
             ( ( CL2DFLEX_WORK_MODE_LETTERBOX_NEAREST_MULTIPLE == config.params.workModes[0] ) ||
               ( CL2DFLEX_WORK_MODE_RESIZE_NEAREST_MULTIPLE == config.params.workModes[0] ) ) )
        {
            outputBatchSize = config.params.numOfROIs;
        }

        Memory::ImageBasicProps_t outputBasicProps(
                outputBatchSize, config.params.outputWidth, config.params.outputHeight,
                config.params.outputFormat, QC_MEMORY_ALLOCATOR_DMA_GPU, cache );
        Memory::ImageProps_t outputProps;
        Memory::UtilsBase memoryUtils;
        const QCStatus_e outputPropsStatus =
                memoryUtils.SetImagePropsFromImageBasicProp( outputBasicProps, outputProps,
                                                             noPadding, true );
        if ( QC_STATUS_OK != outputPropsStatus )
        {
            if ( noPadding )
            {
                errors += "no padding is not supported for the output format, ";
            }
            else
            {
                errors += "failed to derive CL2DFlex output buffer properties, ";
            }
            return outputPropsStatus;
        }
        config.outputs.push_back( { "output", QC_BUFFER_TYPE_IMAGE, std::move( outputProps ) } );
    }
    else
    {
        QC_ERROR( "VerifyStaticConfig failed!" );
    }

    return status;
}

QCStatus_e CL2DFlexConfig::VerifyAndSet( const std::string config, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    status = NodeConfigBase::VerifyAndSet( config, errors );
    if ( QC_STATUS_OK == status )
    {
        DataTree dt;
        status = m_dataTree.Get( "static", dt );
        if ( QC_STATUS_OK == status )
        {
            status = ParseStaticConfig( dt, errors );
        }
        else
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "CL2D only support static config" );
        }
    }

    return status;
}

QCStatus_e CL2DFlexConfig::GetOptions( std::string &options )
{
    DataTree dt;
    dt.Set<uint32_t>( "version", QCNODE_CL2DFLEX_VERSION );
    m_options = dt.Dump();

    options = m_options;
    return QC_STATUS_OK;
}

const QCNodeConfigBase_t &CL2DFlexConfig::Get()
{
    return m_pCL2DFlexImpl->GetConifg();
}

}   // namespace Node
}   // namespace QC
