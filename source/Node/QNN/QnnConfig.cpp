// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "QC/Node/QNN.hpp"
#include "QnnImpl.hpp"
#include "QnnSdkBuildId.h"
#include "QnnTypeMacros.hpp"
#include <unistd.h>

namespace QC
{
namespace Node
{

// JSON Schema (Draft 2020-12) bodies, embedded as raw strings. Keep these
// aligned with docs/qnn-node.schema.json#/$defs/{staticConfig,dynamicConfig}.
// The full GetOptions() output structure is documented in
// docs/ConfigIfsGetOptions.md.
//
// "config" is self-contained: it carries its own $defs alongside $ref-style
// properties.{static,dynamic}, so a user can copy `config` and paste it into
// a tool like JEdison without dragging anything else along.

static const std::string QNN_STATIC_CONFIG_SCHEMA = R"json({
    "type": "object",
    "title": "QNN Static Config",
    "required": ["schemaVersion", "type", "id", "socId"],
    "additionalProperties": false,
    "properties": {
        "name":          { "type": "string", "description": "Node unique name" },
        "type":          { "type": "string", "const": "QNN",
                           "description": "Node type identifier. Always \"QNN\" for the QNN node." },
        "id":            { "type": "integer", "minimum": 0, "maximum": 255,
                           "description": "Node unique ID" },
        "schemaVersion": { "$ref": "#/$defs/schemaVersion" },
        "socId":         { "type": "integer",
                           "description": "Target SOC ID. Patched to a const at construction from the QC_TARGET_SOC compile-time macro; the user MUST include this in their static config and the value must match the build target. Drives the SOC-specific narrowing rules in allOf below (processorType enum, coreIds bounds, …)." },
        "logLevel":      { "type": "string", "default": "ERROR",
                           "enum": ["VERBOSE","DEBUG","INFO","WARN","ERROR"] },
        "processorType": { "type": "string", "default": "htp0",
                           "enum": ["htp0","htp1","htp2","htp3","cpu","gpu"],
                           "description": "Base enum is the union of all SOCs' supported processors. Per-SOC allOf branches narrow this to the actually-valid subset for the active build (e.g. 8650 has no htp2/htp3)." },
        "executeMode":   { "type": "string", "default": "sync",
                           "enum": ["sync","async"],
                           "description": "Execution mode for ProcessFrameDescriptor. sync blocks until inference completes; async returns immediately and invokes a callback on completion. Default is sync so callers without a QCNodeInit_t.callback get blocking behaviour without having to override anything." },
        "coreIds":       { "type": "array",
                           "items": { "type": "integer", "minimum": 0, "maximum": 3 },
                           "uniqueItems": true, "maxItems": 4, "default": [0],
                           "description": "Base bounds: at most 4 cores, with valid IDs 0..3 (matches the loosest SOC in allOf below — 8797 htp0). Per-SOC allOf branches narrow `items.maximum` and `maxItems` further. Convention: `items.maximum=N-1` and `maxItems=N` together mean \"N cores, valid IDs 0..N-1\"." },
        "loadType":      { "type": "string", "default": "binary",
                           "enum": ["binary","library","buffer"] },
        "modelPath":     { "$ref": "#/$defs/nonEmptyString",
                           "description": "Required when loadType is binary or library, or when loadType is absent (defaults to binary)." },
        "contextBufferId": { "type": "integer", "minimum": 0, "maximum": 65535,
                             "description": "Required when loadType is buffer. Bound is uint16 max (65535) — opaque ID into QCNodeInit_t.buffers." },
        "bufferIds":     { "type": "array",
                           "items": { "type": "integer", "minimum": 0, "maximum": 65535 },
                           "uniqueItems": true, "maxItems": 65535,
                           "description": "Buffer IDs registered to this node. Bounds are uint16 max — opaque IDs into QCNodeInit_t.buffers; uniqueItems guards against duplicates." },
        "priority":      { "type": "string", "default": "normal",
                           "enum": ["low","normal","normal_high","high"] },
        "perfProfile":   { "type": "string", "default": "default",
                           "enum": ["low_balanced","balanced","default","high_performance",
                                    "sustained_high_performance","burst","low_power_saver",
                                    "power_saver","high_power_saver","extreme_power_saver"] },
        "udoPackages": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["udoLibPath","interfaceProvider"],
                "additionalProperties": false,
                "properties": {
                    "udoLibPath":        { "$ref": "#/$defs/nonEmptyString" },
                    "interfaceProvider": { "$ref": "#/$defs/nonEmptyString" }
                }
            }
        },
        "deRegisterAllBuffersWhenStop": { "type": "boolean", "default": false },
        "weightSharingEnabled":         { "type": "boolean", "default": false },
        "extendedUdma":                 { "type": "boolean", "default": false }
    },
    "allOf": [
        { "if":   { "properties": { "loadType": { "enum": ["binary","library"] } },
                    "required": ["loadType"] },
          "then": { "required": ["modelPath"],
                    "not": { "required": ["contextBufferId"] } } },
        { "if":   { "properties": { "loadType": { "const": "buffer" } },
                    "required": ["loadType"] },
          "then": { "required": ["contextBufferId"],
                    "not": { "required": ["modelPath"] } } },
        { "if":   { "not": { "required": ["loadType"] } },
          "then": { "required": ["modelPath"],
                    "not": { "required": ["contextBufferId"] } } },

        { "if":   { "properties": { "socId": { "const": 8650 } }, "required": ["socId"] },
          "then": { "properties": {
                    "processorType": { "enum": ["cpu","gpu","htp0","htp1"] },
                    "coreIds":       { "items": { "maximum": 0 }, "maxItems": 1 } } } },
        { "if":   { "properties": { "socId": { "const": 8797 } }, "required": ["socId"] },
          "then": { "properties": {
                    "processorType": { "enum": ["cpu","gpu","htp0","htp1","htp2","htp3"] },
                    "coreIds":       { "items": { "maximum": 3 }, "maxItems": 4 } } } },
        { "if":   { "properties": { "socId":         { "const": 8797 },
                                    "processorType": { "enum":  ["cpu","gpu"] } },
                    "required": ["socId","processorType"] },
          "then": { "properties": {
                    "coreIds":       { "items": { "maximum": 0 }, "maxItems": 1 } } } },
        { "if":   { "properties": { "socId":         { "const": 8797 },
                                    "processorType": { "enum":  ["htp1","htp2","htp3"] } },
                    "required": ["socId","processorType"] },
          "then": { "properties": {
                    "coreIds":       { "items": { "maximum": 0 }, "maxItems": 1 } } } },
        { "if":   { "properties": { "socId": { "const": 0 } }, "required": ["socId"] },
          "then": { "properties": {
                    "processorType": { "enum": ["cpu", "gpu"] },
                    "coreIds":       { "items": { "maximum": 0 }, "maxItems": 1 } } } }
    ],
    "examples": [
        {
            "type":            "QNN",
            "id":              0,
            "name":            "qnn_detector_htp0",
            "logLevel":        "ERROR",
            "processorType":   "htp0",
            "loadType":        "binary",
            "modelPath":       "/data/local/models/detector.qnn.bin",
            "coreIds":         [0, 1, 2, 3],
            "priority":        "normal",
            "perfProfile":     "high_performance"
        }
    ]
})json";

static const std::string QNN_DYNAMIC_CONFIG_SCHEMA = R"json({
    "type": "object",
    "title": "QNN Dynamic Config",
    "required": ["schemaVersion"],
    "additionalProperties": false,
    "properties": {
        "schemaVersion": { "$ref": "#/$defs/schemaVersion" },
        "enablePerf":    { "type": "boolean", "default": false,
                           "description": "Enable / disable QNN performance profiling at runtime. Default: false." }
    },
    "examples": [
        { "enablePerf": true }
    ]
})json";

// Shared $defs entry — referenced by both staticConfig.schemaVersion and
// dynamicConfig.schemaVersion via $ref: "#/$defs/schemaVersion". Kept as a
// single source of truth so the two config halves can never disagree on the
// version they declare.
static const std::string QNN_SCHEMA_VERSION = R"json({
    "type": "string",
    "const": "2.0.0",
    "description": "Config schema version. Fixed at \"2.0.0\" for this node revision."
})json";

// Shared $defs entry for non-empty string values (modelPath, udoLibPath,
// interfaceProvider). Factored out per the DRY review item: the same
// `{type: "string", minLength: 1}` was duplicated across three sites.
static const std::string QNN_NON_EMPTY_STRING = R"json({
    "type": "string",
    "minLength": 1,
    "description": "Non-empty string."
})json";

QCStatus_e QnnConfig::LoadJson( const std::string &json, DataTree &dt )
{
    std::string errors;
    QCStatus_e status = dt.Load( json, errors );
    if ( QC_STATUS_OK != status )
    {
        QC_LOG_ERROR( "QnnConfig::LoadJson failed (status=%d): %s", status, errors.c_str() );
        // Reset dt to a known-empty state so callers don't see partial data.
        dt = DataTree();
    }
    return status;
}

DataTree QnnConfig::ConvertTensorInfoToJson( const Qnn_Tensor_t &info )
{
    DataTree dt;
    uint32_t rank = QNN_TENSOR_GET_RANK( &info );
    uint32_t *dimensions = QNN_TENSOR_GET_DIMENSIONS( &info );
    std::vector<uint32_t> dims( dimensions, dimensions + rank );

    dt.Set<std::string>( "name", std::string( QNN_TENSOR_GET_NAME( &info ) ) );
    dt.SetTensorType( "type",
                      m_pQnnImpl->SwitchFromQnnDataType( QNN_TENSOR_GET_DATA_TYPE( &info ) ) );
    dt.Set<std::uint32_t>( "dims", dims );

    Qnn_QuantizeParams_t quantizeParams = QNN_TENSOR_GET_QUANT_PARAMS( &info );
    if ( QNN_QUANTIZATION_ENCODING_SCALE_OFFSET == quantizeParams.quantizationEncoding )
    {
        dt.Set<std::string>( "quantType", "scale_offset" );
        dt.Set<float>( "quantScale", quantizeParams.scaleOffsetEncoding.scale );
        dt.Set<int32_t>( "quantOffset", quantizeParams.scaleOffsetEncoding.offset );
    }
    else
    {
        dt.Set<std::string>( "quantType", "none" );
        dt.Set<float>( "quantScale", 1.0f );
        dt.Set<int32_t>( "quantOffset", 0 );
    }

    return dt;
}

std::vector<DataTree>
QnnConfig::ConvertTensorInfoListToJson( const std::vector<Qnn_Tensor_t> &infoList )
{
    std::vector<DataTree> dts;

    for ( auto &info : infoList )
    {
        DataTree dt = ConvertTensorInfoToJson( info );
        dts.push_back( dt );
    }

    return dts;
}

std::vector<DataTree>
QnnConfig::ConvertTensorListToBufferDescriptors( const std::vector<Qnn_Tensor_t> &tensors )
{
    // Per docs/qnn-node.schema.json#/$defs/tensorBufferDescriptor, every
    // QNN buffer has bufferType=="tensor" with a populated tensorProps.
    std::vector<DataTree> result;

    for ( auto &t : tensors )
    {
        DataTree entry;
        uint32_t rank = QNN_TENSOR_GET_RANK( &t );
        uint32_t *dimensions = QNN_TENSOR_GET_DIMENSIONS( &t );
        std::vector<uint32_t> dims( dimensions, dimensions + rank );

        entry.Set<std::string>( "name", std::string( QNN_TENSOR_GET_NAME( &t ) ) );
        entry.Set<std::string>( "bufferType", "tensor" );
        entry.SetTensorType( "tensorProps.tensorType",
                             m_pQnnImpl->SwitchFromQnnDataType( QNN_TENSOR_GET_DATA_TYPE( &t ) ) );
        entry.Set<uint32_t>( "tensorProps.dims", dims );
        entry.Set<uint32_t>( "tensorProps.numDims", rank );

        result.push_back( entry );
    }

    return result;
}

// Build the node's {name, id} mapping from logical tensor name to
// globalBufferId in NodeFrameDescriptor. Inputs come first (id=0..n-1),
// outputs follow (id=n..n+m-1). See docs/buffer.md for globalBufferId
// semantics.
static std::vector<DataTree> BuildBufferIdMap( const std::vector<Qnn_Tensor_t> &inputs,
                                               const std::vector<Qnn_Tensor_t> &outputs )
{
    std::vector<DataTree> result;
    uint32_t globalId = 0;

    for ( auto &t : inputs )
    {
        DataTree entry;
        entry.Set<std::string>( "name", std::string( QNN_TENSOR_GET_NAME( &t ) ) );
        entry.Set<uint32_t>( "id", globalId++ );
        result.push_back( entry );
    }

    for ( auto &t : outputs )
    {
        DataTree entry;
        entry.Set<std::string>( "name", std::string( QNN_TENSOR_GET_NAME( &t ) ) );
        entry.Set<uint32_t>( "id", globalId++ );
        result.push_back( entry );
    }

    return result;
}

QCStatus_e QnnConfig::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;
    QCStatus_e status2;

    std::string type = dt.Get<std::string>( "type", "QNN" );
    if ( "QNN" != type )
    {
        errors += "the type is not QNN, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    uint32_t id = dt.Get<uint32_t>( "id", UINT32_MAX );
    if ( UINT32_MAX == id )
    {
        errors += "the id is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::string processorType = dt.Get<std::string>( "processorType", "htp0" );
    if ( ( "htp0" != processorType ) && ( "htp1" != processorType ) &&
         ( "htp2" != processorType ) && ( "htp3" != processorType ) && ( "cpu" != processorType ) &&
         ( "gpu" != processorType ) )
    {
        errors += "the processorType " + processorType + " is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::string perfProfile = dt.Get<std::string>( "perfProfile", "default" );
    if ( ( "low_balanced" != perfProfile ) && ( "balanced" != perfProfile ) &&
         ( "default" != perfProfile ) && ( "high_performance" != perfProfile ) &&
         ( "sustained_high_performance" != perfProfile ) && ( "burst" != perfProfile ) &&
         ( "low_power_saver" != perfProfile ) && ( "power_saver" != perfProfile ) &&
         ( "high_power_saver" != perfProfile ) && ( "extreme_power_saver" != perfProfile ) )
    {
        errors += "the perfProfile " + perfProfile + " is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::string loadType = dt.Get<std::string>( "loadType", "binary" );
    if ( ( "binary" == loadType ) || ( "library" == loadType ) )
    {
        std::string modelPath = dt.Get<std::string>( "modelPath", "" );
        if ( "" == modelPath )
        {
            errors += "the modelPath is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else
        {
            int err = access( modelPath.c_str(), F_OK );
            if ( 0 != err )
            {
                errors += "the modelPath <" + modelPath + "> is invalid, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
        }
    }
    else if ( "buffer" == loadType )
    {
        uint32_t contextBufferId = dt.Get<uint32_t>( "contextBufferId", UINT32_MAX );
        if ( UINT32_MAX == contextBufferId )
        {
            errors += "the contextBufferId is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }
    else
    {
        errors += "the loadType <" + loadType + "> is invalid, ";
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

    std::string priority = dt.Get<std::string>( "priority", "normal" );
    if ( ( "low" != priority ) && ( "normal" != priority ) && ( "normal_high" != priority ) &&
         ( "high" != priority ) )
    {
        errors += "the priority is invalid, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    std::vector<DataTree> udos;
    status2 = dt.Get( "udoPackages", udos );
    if ( QC_STATUS_OUT_OF_BOUND == status2 )
    {
        /* OK if not configured */
    }
    else if ( QC_STATUS_OK != status2 )
    {
        errors += "the udoPackages is invalid, ";
        status = status2;
    }
    else
    {
        uint32_t idx = 0;
        for ( DataTree &udo : udos )
        {
            std::string udoLibPath = udo.Get<std::string>( "udoLibPath", "" );
            std::string interfaceProvider = udo.Get<std::string>( "interfaceProvider", "" );
            if ( "" == udoLibPath )
            {
                errors += "the udo " + std::to_string( idx ) + " library path is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            if ( "" == interfaceProvider )
            {
                errors += "the udo " + std::to_string( idx ) + " interface is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            idx++;
        }
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


QCStatus_e QnnConfig::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    QnnImplConfig_t &config = m_pQnnImpl->GetConfig();

    status = VerifyStaticConfig( dt, errors );
    if ( QC_STATUS_OK == status )
    {
        config.nodeId.name = dt.Get<std::string>( "name", "" );
        config.nodeId.id = dt.Get<uint8_t>( "id", UINT8_MAX );
        config.nodeId.type = QC_NODE_TYPE_QNN;
        std::string processorType = dt.Get<std::string>( "processorType", "htp0" );
        if ( "htp0" == processorType )
        {
            config.processorType = QNN_PROCESSOR_HTP0;
        }
        else if ( "htp1" == processorType )
        {
            config.processorType = QNN_PROCESSOR_HTP1;
        }
        else if ( "htp2" == processorType )
        {
            config.processorType = QNN_PROCESSOR_HTP2;
        }
        else if ( "htp3" == processorType )
        {
            config.processorType = QNN_PROCESSOR_HTP3;
        }
        else if ( "cpu" == processorType )
        {
            config.processorType = QNN_PROCESSOR_CPU;
        }
        else
        {
            config.processorType = QNN_PROCESSOR_GPU;
        }

        std::string perfProfile = dt.Get<std::string>( "perfProfile", "default" );
        QC_DEBUG( "QNN perf profile: %s", perfProfile.c_str() );
        if ( "low_balanced" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_LOW_BALANCED;
        }
        else if ( "balanced" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_BALANCED;
        }
        else if ( "high_performance" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_HIGH_PERFORMANCE;
        }
        else if ( "sustained_high_performance" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE;
        }
        else if ( "burst" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_BURST;
        }
        else if ( "low_power_saver" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_LOW_POWER_SAVER;
        }
        else if ( "power_saver" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_POWER_SAVER;
        }
        else if ( "high_power_saver" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_HIGH_POWER_SAVER;
        }
        else if ( "extreme_power_saver" == perfProfile )
        {
            config.perfProfile = QNN_PERF_PROFILE_EXTREME_POWER_SAVER;
        }
        else
        {
            config.perfProfile = QNN_PERF_PROFILE_DEFAULT;
        }

        config.coreIds = dt.Get<std::vector<uint32_t>>( "coreIds", std::vector<uint32_t>( { 0 } ) );
        std::string loadType = dt.Get<std::string>( "loadType", "binary" );
        if ( "binary" == loadType )
        {
            config.modelPath = dt.Get<std::string>( "modelPath", "" );
            config.loadType = QNN_LOAD_CONTEXT_BIN_FROM_FILE;
            QC_DEBUG( "QNN binary model: %s", config.modelPath.c_str() );
        }
        else if ( "library" == loadType )
        {
            config.modelPath = dt.Get<std::string>( "modelPath", "" );
            config.loadType = QNN_LOAD_SHARED_LIBRARY_FROM_FILE;
            QC_DEBUG( "QNN library model: %s", config.modelPath.c_str() );
        }
        else /* buffer */
        {
            config.contextBufferId = dt.Get<uint32_t>( "contextBufferId", UINT32_MAX );
            config.modelPath = "";
            config.loadType = QNN_LOAD_CONTEXT_BIN_FROM_BUFFER;
        }

        std::string priority = dt.Get<std::string>( "priority", "normal" );
        if ( "low" == priority )
        {
            config.priority = QNN_PRIORITY_LOW;
        }
        else if ( "normal" == priority )
        {
            config.priority = QNN_PRIORITY_NORMAL;
        }
        else if ( "normal_high" == priority )
        {
            config.priority = QNN_PRIORITY_NORMAL_HIGH;
        }
        else
        {
            config.priority = QNN_PRIORITY_HIGH;
        }

        config.bufferIds = dt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );

        std::vector<DataTree> dts;
        (void) dt.Get( "udoPackages", dts );
        config.udoPackages.resize( dts.size() );
        uint32_t idx = 0;
        for ( DataTree &udt : dts )
        {
            config.udoPackages[idx].udoLibPath = udt.Get<std::string>( "udoLibPath", "" );
            config.udoPackages[idx].interfaceProvider =
                    udt.Get<std::string>( "interfaceProvider", "" );
            idx++;
        }

        std::vector<DataTree> globalBufferIdMap;
        (void) dt.Get( "globalBufferIdMap", globalBufferIdMap );
        config.globalBufferIdMap.resize( globalBufferIdMap.size() );
        idx = 0;
        for ( DataTree &gbm : globalBufferIdMap )
        {
            config.globalBufferIdMap[idx].name = gbm.Get<std::string>( "name", "" );
            config.globalBufferIdMap[idx].globalBufferId = gbm.Get<uint32_t>( "id", UINT32_MAX );
            idx++;
        }

        config.bDeRegisterAllBuffersWhenStop =
                dt.Get<bool>( "deRegisterAllBuffersWhenStop", false );
        config.bWeightSharingEnabled = dt.Get<bool>( "weightSharingEnabled", false );
        config.bUseExtendedUdma = dt.Get<bool>( "extendedUdma", false );
    }

    return status;
}

QCStatus_e QnnConfig::ApplyDynamicConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( dt.Exists( "enablePerf" ) )
    {
        bool bEnablePerf = dt.Get<bool>( "enablePerf", false );
        if ( bEnablePerf )
        {
            status = m_pQnnImpl->EnablePerf();
        }
        else
        {
            status = m_pQnnImpl->DisablePerf();
        }

        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to %s perf: %d", bEnablePerf ? "Enable" : "Disable", status );
        }
        else
        {
            QC_DEBUG( "%s perf OK", bEnablePerf ? "Enable" : "Disable" );
        }
    }
    else
    {
        QC_ERROR( "only support dynamic enablePerf option" );
        status = QC_STATUS_UNSUPPORTED;
    }

    return status;
}

QCStatus_e QnnConfig::VerifyAndSet( const std::string config, std::string &errors )
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
            status = m_dataTree.Get( "dynamic", dt );
            if ( QC_STATUS_OK == status )
            {
                status = ApplyDynamicConfig( dt, errors );
            }
        }
    }

    return status;
}

QCStatus_e QnnConfig::GetOptions( std::string &options )
{
    QCStatus_e status = QC_STATUS_OK;
    std::vector<Qnn_Tensor_t> inputTensors;
    std::vector<Qnn_Tensor_t> outputTensors;

    // First call: parse the file-scope schema literals into m_optionsTree
    // and serialise. Gated by m_bSchemaLoaded so subsequent calls skip the
    // (relatively expensive) JSON parsing. If any LoadJson fails — which
    // would indicate a malformed schema literal — log and return the
    // (empty) m_options; the flag stays false so a future call can retry
    // (useful in tests that may patch the literals at runtime).
    if ( false == m_bSchemaLoaded )
    {
        DataTree staticSchema;
        DataTree dynamicSchema;
        DataTree schemaVersion;
        DataTree nonEmptyString;
        QCStatus_e loadStatus = QC_STATUS_OK;

        loadStatus = LoadJson( QNN_STATIC_CONFIG_SCHEMA, staticSchema );
        if ( QC_STATUS_OK == loadStatus )
        {
            loadStatus = LoadJson( QNN_DYNAMIC_CONFIG_SCHEMA, dynamicSchema );
        }
        if ( QC_STATUS_OK == loadStatus )
        {
            loadStatus = LoadJson( QNN_SCHEMA_VERSION, schemaVersion );
        }
        if ( QC_STATUS_OK == loadStatus )
        {
            loadStatus = LoadJson( QNN_NON_EMPTY_STRING, nonEmptyString );
        }
        if ( QC_STATUS_OK != loadStatus )
        {
            QC_ERROR( "QnnConfig: failed to parse one of the file-scope schema "
                      "literals; m_options remains empty." );
            status = loadStatus;
        }
        else
        {
            // Discriminator: pin properties.socId.const to the build's
            // QC_TARGET_SOC. The schema body itself carries SOC-narrowing
            // rules for every SOC we know about (in allOf); this const
            // pins which branch's `if` actually fires for instances
            // validated against this build's schema.
            staticSchema.Set<uint32_t>( "properties.socId.const", QC_TARGET_SOC );

            m_optionsTree.Set<uint32_t>( "version", QCNODE_QNN_VERSION );

            // qnnSdkVersion — grouped object identifying the QNN/QAIRT
            // SDK package this build was compiled against and (post-init)
            // the runtime-discovered backend / system interface versions:
            //   buildId         — string from QnnSdkBuildId.h
            //                     QNN_SDK_BUILD_ID (e.g. "v2.45.0.260326154327"),
            //                     compile-time, always present.
            //   apiVersion      — backend interface "M.N.P", post-init only.
            //   systemApiVersion — system interface "M.N.P", post-init only.
            // The two runtime fields are added in the model-load branch
            // below; they're not known until Initialize() walks the
            // providers list.
            m_optionsTree.Set<std::string>( "qnnSdkVersion.buildId", QNN_SDK_BUILD_ID );

            // config — self-contained Draft 2020-12 schema. Carries its
            // own $defs so the user can copy `config` and paste into
            // JEdison standalone.
            m_optionsTree.Set<std::string>( "config.$schema",
                                            "https://json-schema.org/draft/2020-12/schema" );
            m_optionsTree.Set<std::string>( "config.type", "object" );
            std::vector<std::string> configRequired = { "static" };
            m_optionsTree.Set( "config.required", configRequired );
            m_optionsTree.Set<std::string>( "config.properties.static.$ref",
                                            "#/$defs/staticConfig" );
            m_optionsTree.Set<std::string>( "config.properties.dynamic.$ref",
                                            "#/$defs/dynamicConfig" );
            m_optionsTree.Set<bool>( "config.additionalProperties", false );

            m_optionsTree.Set( "config.$defs.staticConfig", staticSchema );
            m_optionsTree.Set( "config.$defs.dynamicConfig", dynamicSchema );
            m_optionsTree.Set( "config.$defs.schemaVersion", schemaVersion );
            m_optionsTree.Set( "config.$defs.nonEmptyString", nonEmptyString );

            // buffers / bufferIdMap — empty placeholders. Populated
            // below once GetInputTensors / GetOutputTensors succeed.
            std::vector<DataTree> emptyArr;
            m_optionsTree.Set( "buffers.inputs", emptyArr );
            m_optionsTree.Set( "buffers.outputs", emptyArr );
            m_optionsTree.Set( "bufferIdMap", emptyArr );

            m_options = m_optionsTree.Dump();
            m_bSchemaLoaded = true;
        }
    }

    // Model + buffer descriptors are layered on top of the schema cache.
    // Skipped if the schema layer never succeeded (m_bSchemaLoaded stays
    // false on schema-parse failure) — we'd just be writing concrete
    // tensor descriptors into an empty m_optionsTree, which then can't
    // round-trip back into a meaningful m_options.
    if ( ( true == m_bSchemaLoaded ) && ( false == m_bOptionsBuilt ) )
    {
        status = m_pQnnImpl->GetInputTensors( inputTensors );
        if ( QC_STATUS_OK == status )
        {
            status = m_pQnnImpl->GetOutputTensors( outputTensors );
        }

        if ( QC_STATUS_OK == status )
        {
            std::vector<DataTree> inputDts = ConvertTensorInfoListToJson( inputTensors );
            std::vector<DataTree> outputDts = ConvertTensorInfoListToJson( outputTensors );
            m_optionsTree.Set( "model.inputs", inputDts );
            m_optionsTree.Set( "model.outputs", outputDts );

            std::vector<DataTree> inputBufs = ConvertTensorListToBufferDescriptors( inputTensors );
            std::vector<DataTree> outputBufs =
                    ConvertTensorListToBufferDescriptors( outputTensors );
            m_optionsTree.Set( "buffers.inputs", inputBufs );
            m_optionsTree.Set( "buffers.outputs", outputBufs );

            std::vector<DataTree> globalMap = BuildBufferIdMap( inputTensors, outputTensors );
            m_optionsTree.Set( "bufferIdMap", globalMap );

            // Runtime-discovered QNN versions — populated by Initialize()
            // when QnnImpl walked the (system) interface providers list
            // and called the backend's backendGetApiVersion(). All three
            // go under the qnnSdkVersion.* nested object alongside the
            // compile-time qnnSdkVersion.buildId set in the schema-load
            // branch above.
            //   backendCoreApiVersion — backend's coreApiVersion as
            //                          reported at runtime by
            //                          backendGetApiVersion().
            //   backendApiVersion    — backend's backendApiVersion sub-
            //                          field from the same call.
            //   systemApiVersion     — system interface version matched
            //                          against QNN_SYSTEM_API_VERSION_*.
            // Formatted "M.N.P".
            const Qnn_ApiVersion_t &backendVer = m_pQnnImpl->GetBackendVersion();
            const Qnn_Version_t &sysVer = m_pQnnImpl->GetSystemApiVersion();
            m_optionsTree.Set<std::string>(
                    "qnnSdkVersion.backendCoreApiVersion",
                    std::to_string( backendVer.coreApiVersion.major ) + "." +
                            std::to_string( backendVer.coreApiVersion.minor ) + "." +
                            std::to_string( backendVer.coreApiVersion.patch ) );
            m_optionsTree.Set<std::string>(
                    "qnnSdkVersion.backendApiVersion",
                    std::to_string( backendVer.backendApiVersion.major ) + "." +
                            std::to_string( backendVer.backendApiVersion.minor ) + "." +
                            std::to_string( backendVer.backendApiVersion.patch ) );
            m_optionsTree.Set<std::string>( "qnnSdkVersion.systemApiVersion",
                                            std::to_string( sysVer.major ) + "." +
                                                    std::to_string( sysVer.minor ) + "." +
                                                    std::to_string( sysVer.patch ) );

            m_options = m_optionsTree.Dump();
            m_bOptionsBuilt = true;
        }
        else if ( QC_STATUS_BAD_STATE != status )
        {
            QC_ERROR( "Failed to get QNN input output information" );
            // Leave m_options unchanged — it still holds the cached
            // schema-only view (version + config + capability + empty
            // buffers/bufferIdMap), which remains useful pre-init.
        }
        else
        {
            // QC_STATUS_BAD_STATE: model not yet loaded (expected
            // pre-init); silently keep the cached schema-only view.
        }
    }
    options = m_options;
    return status;
}

void QnnConfig::Reset()
{
    m_bOptionsBuilt = false;
    // m_bSchemaLoaded can stay true — schema is static
    // Clear model-specific fields from the options tree:
    std::vector<DataTree> emptyArr;
    if ( m_bSchemaLoaded )
    {
        m_optionsTree.Set( "model.inputs", emptyArr );
        m_optionsTree.Set( "model.outputs", emptyArr );
        m_optionsTree.Set( "buffers.inputs", emptyArr );
        m_optionsTree.Set( "buffers.outputs", emptyArr );
        m_optionsTree.Set( "bufferIdMap", emptyArr );
        m_options = m_optionsTree.Dump();
    }
}

const QCNodeConfigBase_t &QnnConfig::Get()
{
    return m_pQnnImpl->GetConfig();
}

}   // namespace Node
}   // namespace QC
