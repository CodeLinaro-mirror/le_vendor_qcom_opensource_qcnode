// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/Voxelization.hpp"
#include "VoxelizationImpl.hpp"

namespace QC
{
namespace Node
{
QCStatus_e VoxelizationConfig::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e ret = QC_STATUS_OK;
    QCStatus_e ret2 = QC_STATUS_OK;

    uint32_t id = dt.Get<uint32_t>( "id", UINT32_MAX );
    if ( UINT32_MAX == id )
    {
        errors += "the id is empty, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( ( QC_STATUS_OK == ret ) && ( dt.Exists( "coreId" ) ) )
    {
        uint32_t coreId = dt.Get<uint32_t>( "coreId", UINT32_MAX );
        if ( NSP_CORES_ID_MAX < coreId )
        {
            errors += "coreId invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        uint32_t maxPointNum = dt.Get<uint32_t>( "maxPointNum", 0 );
        if ( 0 == maxPointNum )
        {
            errors += "the maxPointNum is empty, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        uint32_t maxPlrNum = dt.Get<uint32_t>( "maxPlrNum", 0 );
        if ( 0 == maxPlrNum )
        {
            errors += "the maxPlrNum is empty, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        uint32_t maxPointNumPerPlr = dt.Get<uint32_t>( "maxPointNumPerPlr", 0 );
        if ( 0 == maxPointNumPerPlr )
        {
            errors += "the maxPointNumPerPlr is empty, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        std::string inputMode = dt.Get<std::string>( "inputMode", "" );
        if ( "" == inputMode )
        {
            errors += "the inputMode is empty, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( ( inputMode != "xyzr" ) && ( inputMode != "xyzrt" ) )
        {
            errors += "the inputMode is invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            /* OK */
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        uint32_t outputFeatureDimNum = dt.Get<uint32_t>( "outputFeatureDimNum", 0 );
        if ( 0 == outputFeatureDimNum )
        {
            errors += "the outputFeatureDimNum is empty, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( dt.Exists( "inputPcdBufferIds" ) )
    {
        std::vector<uint32_t> inputPcdBufferIds =
                dt.Get<uint32_t>( "inputPcdBufferIds", std::vector<uint32_t>{} );
        if ( 0 == inputPcdBufferIds.size() )
        {
            errors += "the inputPcdBufferIds is invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( dt.Exists( "outputPlrBufferIds" ) )
    {
        std::vector<uint32_t> outputPlrBufferIds =
                dt.Get<uint32_t>( "outputPlrBufferIds", std::vector<uint32_t>{} );
        if ( 0 == outputPlrBufferIds.size() )
        {
            errors += "the outputPlrBufferIds is invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }
    else
    {
        errors += "the outputPlrBufferIds is not set, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( dt.Exists( "outputFeatureBufferIds" ) )
    {
        std::vector<uint32_t> outputFeatureBufferIds =
                dt.Get<uint32_t>( "outputFeatureBufferIds", std::vector<uint32_t>{} );
        if ( 0 == outputFeatureBufferIds.size() )
        {
            errors += "the outputFeatureBufferIds is invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
    }
    else
    {
        errors += "the outputFeatureBufferIds is not set, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( QC_STATUS_OK == ret )
    {
        QCProcessorType_e processorType = dt.GetProcessorType( "processorType", QC_PROCESSOR_HTP0 );
        if ( QC_PROCESSOR_MAX == processorType )
        {
            errors += "the processorType is invalid, ";
            ret = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( QC_PROCESSOR_GPU == processorType )
        {
            if ( false == dt.Exists( "plrPointsBufferId" ) )
            {
                errors += "the plrPointsBufferId is not set, ";
                ret = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( false == dt.Exists( "coordToPlrIdxBufferId" ) )
            {
                errors += "the coordToPlrIdxBufferId is not set, ";
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
        }
        else
        {
            /* OK */
        }
    }

    std::vector<DataTree> globalBufferIdMap;
    ret2 = dt.Get( "globalBufferIdMap", globalBufferIdMap );
    if ( QC_STATUS_OUT_OF_BOUND == ret2 )
    {
        /* OK if not configured */
    }
    else if ( QC_STATUS_OK != ret2 )
    {
        errors += "the globalBufferIdMap is invalid, ";
        ret = ret2;
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
                ret = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( UINT32_MAX == index )
            {
                errors += "the globalIdMap " + std::to_string( idx ) + " id is empty, ";
                ret = QC_STATUS_BAD_ARGUMENTS;
            }
            idx++;
        }
    }


    float pillarXSize = dt.Get<float>( "Xsize", 0.0f );
    if ( 0.0f == pillarXSize )
    {
        errors += "the pillarXSize is 0, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    float pillarYSize = dt.Get<float>( "Ysize", 0.0f );
    if ( 0.0f == pillarYSize )
    {
        errors += "the pillarYSize is 0, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    float pillarZSize = dt.Get<float>( "Zsize", 0.0f );
    if ( 0.0f == pillarZSize )
    {
        errors += "the pillarZSize is 0, ";
        ret = QC_STATUS_BAD_ARGUMENTS;
    }

    return ret;
}

QCStatus_e VoxelizationConfig::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e ret = QC_STATUS_OK;

    VoxelizationImplConfig_t &config = m_pVoxelImpl->GetConifg();

    ret = VerifyStaticConfig( dt, errors );
    if ( QC_STATUS_OK == ret )
    {
        config.nodeId.name = dt.Get<std::string>( "name", "" );
        config.nodeId.id = dt.Get<uint8_t>( "id", UINT8_MAX );
        config.voxelConfig.processor = dt.GetProcessorType( "processorType", QC_PROCESSOR_HTP0 );
        config.voxelConfig.coreId = dt.Get<uint32_t>( "coreId", 0 );
        config.voxelConfig.pillarXSize = dt.Get<float>( "Xsize", 0.0f );
        config.voxelConfig.pillarYSize = dt.Get<float>( "Ysize", 0.0f );
        config.voxelConfig.pillarZSize = dt.Get<float>( "Zsize", 0.0f );
        config.voxelConfig.minXRange = dt.Get<float>( "Xmin", 0.0f );
        config.voxelConfig.minYRange = dt.Get<float>( "Ymin", 0.0f );
        config.voxelConfig.minZRange = dt.Get<float>( "Zmin", 0.0f );
        config.voxelConfig.maxXRange = dt.Get<float>( "Xmax", 0.0f );
        config.voxelConfig.maxYRange = dt.Get<float>( "Ymax", 0.0f );
        config.voxelConfig.maxZRange = dt.Get<float>( "Zmax", 0.0f );
        config.voxelConfig.maxNumInPts = dt.Get<uint32_t>( "maxPointNum", UINT32_MAX );
        config.voxelConfig.maxNumPlrs = dt.Get<uint32_t>( "maxPlrNum", UINT32_MAX );
        config.voxelConfig.maxNumPtsPerPlr = dt.Get<uint32_t>( "maxPointNumPerPlr", UINT32_MAX );
        config.voxelConfig.numOutFeatureDim = dt.Get<uint32_t>( "outputFeatureDimNum", UINT32_MAX );

        /*inputMode is being verified multiple times and no other mode reaches here
          so validating only for xzyr & xzyrt */
        std::string inputMode = dt.Get<std::string>( "inputMode", "" );
        if ( "xyzr" == inputMode )
        {
            config.voxelConfig.inputMode = VOXELIZATION_INPUT_MODE_XYZR;
            config.voxelConfig.numInFeatureDim = 4;
        }

        if ( "xyzrt" == inputMode )
        {
            config.voxelConfig.inputMode = VOXELIZATION_INPUT_MODE_XYZRT;
            config.voxelConfig.numInFeatureDim = 5;
        }
    }

    if ( QC_STATUS_OK == ret )
    {
        config.inputPcdBufferIds = dt.Get<uint32_t>( "inputPcdBufferIds", std::vector<uint32_t>{} );
        config.outputPlrBufferIds =
                dt.Get<uint32_t>( "outputPlrBufferIds", std::vector<uint32_t>{} );
        config.outputFeatureBufferIds =
                dt.Get<uint32_t>( "outputFeatureBufferIds", std::vector<uint32_t>{} );
        config.plrPointsBufferId = dt.Get<uint32_t>( "plrPointsBufferId", UINT32_MAX );
        config.coordToPlrIdxBufferId = dt.Get<uint32_t>( "coordToPlrIdxBufferId", UINT32_MAX );

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
    }

    return ret;
}

QCStatus_e VoxelizationConfig::VerifyAndSet( const std::string config, std::string &errors )
{
    QCStatus_e ret = QC_STATUS_OK;
    DataTree dt;

    ret = NodeConfigIfs::VerifyAndSet( config, errors );
    if ( QC_STATUS_OK == ret )
    {
        ret = m_dataTree.Get( "static", dt );
    }

    if ( QC_STATUS_OK == ret )
    {
        ret = ParseStaticConfig( dt, errors );
    }

    return ret;
}

const std::string &VoxelizationConfig::GetOptions()
{
    return m_options;
}

const QCNodeConfigBase_t &VoxelizationConfig::Get()
{
    return m_pVoxelImpl->GetConifg();
}

}   // namespace Node
}   // namespace QC
