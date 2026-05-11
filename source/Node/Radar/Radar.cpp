// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Radar.hpp"
#include <unistd.h>

namespace QC
{
namespace Node
{

QCStatus_e RadarConfigIfs::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

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

    uint32_t maxInputBufferSize = dt.Get<uint32_t>( "maxInputBufferSize", 0 );
    if ( 0 == maxInputBufferSize )
    {
        errors += "maxInputBufferSize is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    uint32_t maxOutputBufferSize = dt.Get<uint32_t>( "maxOutputBufferSize", 0 );
    if ( 0 == maxOutputBufferSize )
    {
        errors += "maxOutputBufferSize is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::string serviceName = dt.Get<std::string>( "serviceName", "" );
    if ( "" == serviceName )
    {
        errors += "serviceName is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    uint32_t timeoutMs = dt.Get<uint32_t>( "timeoutMs", 0 );
    if ( 0 == timeoutMs )
    {
        errors += "timeoutMs is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    // Validate globalBufferIdMap if present
    std::vector<DataTree> globalBufferIdMap;
    QCStatus_e status2 = dt.Get( "globalBufferIdMap", globalBufferIdMap );
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

QCStatus_e RadarConfigIfs::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;
    status = VerifyStaticConfig( dt, errors );
    if ( QC_STATUS_OK == status )
    {
        m_config.nodeId.name = dt.Get<std::string>( "name", "" );
        m_config.nodeId.id = dt.Get<uint8_t>( "id", UINT8_MAX );

        m_config.numOfEntries = 2;

        m_config.params.maxInputBufferSize = dt.Get<uint32_t>( "maxInputBufferSize", 0 );
        m_config.params.maxOutputBufferSize = dt.Get<uint32_t>( "maxOutputBufferSize", 0 );

        m_config.params.serviceConfig.serviceName = dt.Get<std::string>( "serviceName", "" );
        m_config.params.serviceConfig.timeoutMs = dt.Get<uint32_t>( "timeoutMs", 5000 );
        m_config.params.serviceConfig.bEnablePerformanceLog =
                dt.Get<bool>( "enablePerformanceLog", false );

        m_config.inputBufferIds = dt.Get<uint32_t>( "inputs", std::vector<uint32_t>{} );
        m_config.outputBufferIds = dt.Get<uint32_t>( "outputs", std::vector<uint32_t>{} );

        std::vector<DataTree> globalBufferIdMap;
        (void) dt.Get( "globalBufferIdMap", globalBufferIdMap );
        m_config.globalBufferIdMap.resize( globalBufferIdMap.size() );
        uint32_t idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            m_config.globalBufferIdMap[idx].name = gbm.Get<std::string>( "name", "" );
            m_config.globalBufferIdMap[idx].globalBufferId = gbm.Get<uint32_t>( "id", UINT32_MAX );
            idx++;
        }

        m_config.bDeRegisterAllBuffersWhenStop =
                dt.Get<bool>( "deRegisterAllBuffersWhenStop", false );
    }
    else
    {
        QC_ERROR( "VerifyStaticConfig failed!" );
    }

    return status;
}

QCStatus_e RadarConfigIfs::VerifyAndSet( const std::string config, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    status = NodeConfigIfs::VerifyAndSet( config, errors );
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
            QC_ERROR( "Radar only support static config" );
        }
    }

    return status;
}

const std::string &RadarConfigIfs::GetOptions()
{
    // Return empty options for now
    return m_options;
}

QCStatus_e Radar::SetupGlobalBufferIdMap( const RadarConfig_t &cfg )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( cfg.globalBufferIdMap.size() > 0 )
    {
        if ( ( m_inputNum + m_outputNum ) != cfg.globalBufferIdMap.size() )
        {
            QC_ERROR( "global buffer map size is not correct: expect %u",
                      m_inputNum + m_outputNum );
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            m_globalBufferIdMap = cfg.globalBufferIdMap;
        }
    }
    else
    {
        // Create a default global buffer index map
        m_globalBufferIdMap.resize( m_inputNum + m_outputNum );
        uint32_t globalBufferId = 0;

        // Input buffer
        m_globalBufferIdMap[globalBufferId].name = "Input";
        m_globalBufferIdMap[globalBufferId].globalBufferId = globalBufferId;
        globalBufferId++;

        // Output buffer
        m_globalBufferIdMap[globalBufferId].name = "Output";
        m_globalBufferIdMap[globalBufferId].globalBufferId = globalBufferId;
        globalBufferId++;
    }
    return status;
}

QCStatus_e Radar::Initialize( QCNodeInit_t &config )
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    const QCNodeConfigBase_t &cfg = m_configIfs.Get();
    const RadarConfig_t *pConfig = dynamic_cast<const RadarConfig_t *>( &cfg );

    bool bNodeBaseInitDone = false;
    bool bRadarInitDone = false;

    status = m_configIfs.VerifyAndSet( config.config, errors );

    if ( QC_STATUS_OK == status )
    {
        status = NodeBase::Init( cfg.nodeId );
    }
    else
    {
        QC_ERROR( "config error: %s", errors.c_str() );
    }

    if ( QC_STATUS_OK == status )
    {
        m_state = QC_OBJECT_STATE_INITIALIZING;
        bNodeBaseInitDone = true;
        m_config = pConfig->params;

        if ( pConfig->params.maxInputBufferSize == 0 || pConfig->params.maxOutputBufferSize == 0 )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Invalid buffer sizes in configuration" );
        }
        else if ( pConfig->params.serviceConfig.serviceName.empty() )
        {
            status = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Service name cannot be empty" );
        }
    }

    if ( QC_STATUS_OK == status )
    {
        status = SetupGlobalBufferIdMap( *pConfig );
    }

    if ( QC_STATUS_OK == status )
    {
        // Register buffers during initialization if specified and available
        if ( config.buffers.size() > 0 )
        {
            // Register input buffers
            for ( uint32_t bufferId : pConfig->inputBufferIds )
            {
                if ( bufferId < config.buffers.size() )
                {
                    QCBufferDescriptorBase_t &bufDesc = config.buffers[bufferId].get();
                    const TensorDescriptor_t *pTensor =
                            dynamic_cast<const TensorDescriptor_t *>( &bufDesc );
                    // Fallback to check if it's just a BufferDescriptor if not TensorDescriptor
                    const BufferDescriptor_t *pBuffer =
                            pTensor ? nullptr
                                    : dynamic_cast<const BufferDescriptor_t *>( &bufDesc );

                    if ( ( nullptr == pTensor ) && ( nullptr == pBuffer ) )
                    {
                        QC_ERROR( "buffer %u is invalid", bufferId );
                        status = QC_STATUS_INVALID_BUF;
                    }
                    else
                    {
                        //
                        // Placeholder for future low level registration/mapping
                        //
                        status = QC_STATUS_OK;
                    }
                }
                else
                {
                    QC_ERROR( "input buffer index out of range" );
                    status = QC_STATUS_BAD_ARGUMENTS;
                }

                if ( status != QC_STATUS_OK )
                {
                    break;
                }
            }

            // Register output buffers
            if ( status == QC_STATUS_OK )
            {
                for ( uint32_t bufferId : pConfig->outputBufferIds )
                {
                    if ( bufferId < config.buffers.size() )
                    {
                        QCBufferDescriptorBase_t &bufDesc = config.buffers[bufferId].get();
                        const TensorDescriptor_t *pTensor =
                                dynamic_cast<const TensorDescriptor_t *>( &bufDesc );
                        const BufferDescriptor_t *pBuffer =
                                pTensor ? nullptr
                                        : dynamic_cast<const BufferDescriptor_t *>( &bufDesc );

                        if ( ( nullptr == pTensor ) && ( nullptr == pBuffer ) )
                        {
                            QC_ERROR( "buffer %u is invalid", bufferId );
                            status = QC_STATUS_INVALID_BUF;
                        }
                        else
                        {
                            //
                            // Placeholder for future low level registration/mapping
                            //
                            status = QC_STATUS_OK;
                        }
                    }
                    else
                    {
                        QC_ERROR( "output buffer index out of range" );
                        status = QC_STATUS_BAD_ARGUMENTS;
                    }

                    if ( status != QC_STATUS_OK )
                    {
                        break;
                    }
                }
            }
        }
        else
        {
            // No buffers provided during initialization - this is acceptable
            // Buffers will be provided later via frame descriptors
            QC_DEBUG( "No buffers provided during initialization, will use frame descriptor "
                      "buffers" );
        }
    }

    if ( QC_STATUS_OK == status )
    {
        status = m_radarIface.Initialize( pConfig->params.serviceConfig.serviceName.c_str(),
                                          pConfig->params.serviceConfig.timeoutMs );
        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to initialize RadarIface with device: %s",
                      pConfig->params.serviceConfig.serviceName.c_str() );
        }
    }

    if ( QC_STATUS_OK == status )
    {
        bRadarInitDone = true;
        m_bDeRegisterAllBuffersWhenStop = pConfig->bDeRegisterAllBuffersWhenStop;
    }

    if ( QC_STATUS_OK != status )
    {
        // Error cleanup
        if ( bRadarInitDone )
        {
            (void) m_radarIface.Deinitialize();
        }
        if ( bNodeBaseInitDone )
        {
            (void) NodeBase::DeInitialize();
        }
    }
    else
    {
        m_state = QC_OBJECT_STATE_READY;
    }

    return status;
}

QCStatus_e Radar::DeInitialize()
{
    QCStatus_e ret = QC_STATUS_OK;
    // The error state arises from the fact that the radar service is not present,
    // and Deinit should proceed in that case to allow reattempts
    if ( QC_OBJECT_STATE_READY == m_state || QC_OBJECT_STATE_ERROR == m_state )
    {
        // Deinitialize RadarIface
        QCStatus_e ret2 = m_radarIface.Deinitialize();
        if ( QC_STATUS_OK != ret2 )
        {
            QC_ERROR( "Failed to deinitialize RadarIface" );
            ret = ret2;
        }

        ret2 = NodeBase::DeInitialize();
        if ( QC_STATUS_OK != ret2 )
        {
            QC_ERROR( "NodeBase::DeInitialize() failed" );
            ret = ret2;
        }
        m_state = QC_OBJECT_STATE_INITIAL;
    }
    else
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Radar component not in ready state for deinitialization" );
    }

    return ret;
}

QCStatus_e Radar::Start()
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( QC_OBJECT_STATE_READY != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Radar component not in ready state" );
    }
    else
    {
        // Check if RadarIface is properly initialized
        if ( !m_radarIface.IsInitialized() )
        {
            ret = QC_STATUS_BAD_STATE;
            QC_ERROR( "RadarIface not initialized" );
        }
        else
        {
            m_state = QC_OBJECT_STATE_RUNNING;
            QC_INFO( "Node Radar started" );
        }
    }

    return ret;
}

QCStatus_e Radar::Stop()
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( QC_OBJECT_STATE_RUNNING != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Radar component not in running state" );
    }
    else
    {
        // Clear registered buffers
        m_registeredInputBuffers.clear();
        m_registeredOutputBuffers.clear();

        m_state = QC_OBJECT_STATE_READY;
        QC_INFO( "Node Radar stopped" );
    }

    return ret;
}

QCStatus_e Radar::ProcessFrameDescriptor( QCFrameDescriptorNodeIfs &frameDesc )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( QC_OBJECT_STATE_RUNNING != m_state )
    {
        status = QC_STATUS_BAD_STATE;
        QC_ERROR( "Radar component not in ready state" );
    }
    else if ( m_globalBufferIdMap.size() < 2 )
    {
        // Ensure we have at least 2 buffers (input and output)
        status = QC_STATUS_BAD_ARGUMENTS;
        QC_ERROR( "Insufficient global buffer map entries: %zu", m_globalBufferIdMap.size() );
    }
    else if ( !m_radarIface.IsInitialized() )
    {
        status = QC_STATUS_BAD_STATE;
        QC_ERROR( "RadarIface not initialized" );
    }
    else
    {
        // Get input buffer (first entry in global buffer map)
        uint32_t inputGlobalBufferId = m_globalBufferIdMap[0].globalBufferId;
        QCBufferDescriptorBase_t &inputBufDesc = frameDesc.GetBuffer( inputGlobalBufferId );
        const TensorDescriptor_t *pInputTensor =
                dynamic_cast<const TensorDescriptor_t *>( &inputBufDesc );
        const BufferDescriptor_t *pInputBuffer =
                dynamic_cast<const BufferDescriptor_t *>( &inputBufDesc );

        if ( ( nullptr == pInputTensor ) && ( nullptr == pInputBuffer ) )
        {
            status = QC_STATUS_INVALID_BUF;
            QC_ERROR( "Input buffer is invalid at global ID %u", inputGlobalBufferId );
        }
        else
        {
            // Get output buffer (second entry in global buffer map)
            uint32_t outputGlobalBufferId = m_globalBufferIdMap[1].globalBufferId;
            QCBufferDescriptorBase_t &outputBufDesc = frameDesc.GetBuffer( outputGlobalBufferId );
            const TensorDescriptor_t *pOutputTensor =
                    dynamic_cast<const TensorDescriptor_t *>( &outputBufDesc );
            const BufferDescriptor_t *pOutputBuffer =
                    dynamic_cast<const BufferDescriptor_t *>( &outputBufDesc );

            if ( ( nullptr == pOutputTensor ) && ( nullptr == pOutputBuffer ) )
            {
                status = QC_STATUS_INVALID_BUF;
                QC_ERROR( "Output buffer is invalid at global ID %u", outputGlobalBufferId );
            }
            else
            {
                // Execute radar processing
                const QCBufferDescriptorBase_t *pInput =
                        pInputTensor
                                ? static_cast<const QCBufferDescriptorBase_t *>( pInputTensor )
                                : static_cast<const QCBufferDescriptorBase_t *>( pInputBuffer );
                const QCBufferDescriptorBase_t *pOutput =
                        pOutputTensor
                                ? static_cast<const QCBufferDescriptorBase_t *>( pOutputTensor )
                                : static_cast<const QCBufferDescriptorBase_t *>( pOutputBuffer );
                status = Execute( pInput, pOutput );
            }
        }
    }

    return status;
}

QCStatus_e Radar::ValidateBuffer( const QCBufferDescriptorBase_t *pBuffer, bool isInput )
{
    if ( pBuffer->size == 0 )
    {
        QC_ERROR( "Buffer size is zero" );
        return QC_STATUS_INVALID_BUF;
    }
    if ( pBuffer->dmaHandle == 0 )
    {
        QC_ERROR( "Invalid DMA handle" );
        return QC_STATUS_INVALID_BUF;
    }
    uint32_t maxSize = isInput ? m_config.maxInputBufferSize : m_config.maxOutputBufferSize;
    if ( pBuffer->GetDataSize() > maxSize )
    {
        QC_ERROR( "%s buffer size (%zu) exceeds maximum (%u)", isInput ? "Input" : "Output",
                  pBuffer->GetDataSize(), maxSize );
        return QC_STATUS_INVALID_BUF;
    }
    return QC_STATUS_OK;
}

QCStatus_e Radar::Execute( const QCBufferDescriptorBase_t *pInput,
                           const QCBufferDescriptorBase_t *pOutput )
{
    QCStatus_e ret = QC_STATUS_OK;

    if ( QC_OBJECT_STATE_RUNNING != m_state )
    {
        ret = QC_STATUS_BAD_STATE;
        QC_ERROR( "Radar component not in running state" );
    }
    else
    {
        if ( nullptr == pInput || nullptr == pOutput )
        {
            ret = QC_STATUS_BAD_ARGUMENTS;
            QC_ERROR( "Input or output buffer is null" );
        }
        else
        {
            // Validate buffers
            ret = ValidateBuffer( pInput, true );
            if ( QC_STATUS_OK != ret )
            {
                QC_ERROR( "Input buffer validation failed" );
            }
            else
            {
                ret = ValidateBuffer( pOutput, false );
                if ( QC_STATUS_OK != ret )
                {
                    QC_ERROR( "Output buffer validation failed" );
                }
                else
                {
                    // Check if RadarIface is properly initialized
                    if ( !m_radarIface.IsInitialized() )
                    {
                        ret = QC_STATUS_BAD_STATE;
                        QC_ERROR( "RadarIface not initialized" );
                    }
                    else
                    {
                        size_t inputSize = pInput->GetDataSize();
                        size_t outputSize = pOutput->GetDataSize();
                        uint64_t inputHandle = pInput->dmaHandle;
                        uint64_t outputHandle = pOutput->dmaHandle;

                        ret = m_radarIface.Execute( inputHandle, inputSize, outputHandle,
                                                    outputSize );
                        if ( QC_STATUS_OK != ret )
                        {
                            QC_ERROR( "RadarIface execution failed" );
                        }
                        else
                        {
                            QC_DEBUG( "Radar processing completed successfully" );
                        }
                    }
                }
            }
        }
    }

    return ret;
}


}   // namespace Node
}   // namespace QC
