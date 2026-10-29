// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "CameraImpl.hpp"
#include "QC/Infras/Memory/UtilsBase.hpp"
#include "QC/Node/Camera.hpp"

namespace QC
{
namespace Node
{
namespace
{

/**
 * @brief The fixed top-level envelope of the options document reported by GetOptions.
 * The "version", "qcarcamVersion" and "config" keys are layered in by LoadConfigSchema, the
 * "buffers" key by BuildBufferInfo; they start out empty so a caller reading the document before
 * the node is configured sees the keys with no content rather than no keys.
 * The buffer groups are the "inputs" and "outputs" every node reports. The Camera node produces
 * camera frames, so the streams it is configured with are outputs; a metadata group contributes
 * the metadata buffers it feeds the ISP with as inputs and the metadata results it receives as
 * outputs.
 */
const char *const s_pCamOptionsEnvelope = R"JSON(
{
    "buffers": {
        "inputs": [],
        "outputs": []
    }
}
)JSON";

/**
 * @brief The allocator every camera buffer must be allocated from, reported to the client so it
 * does not have to hard-code the camera-specific allocator.
 */
const char *const s_pCamBufferAllocator = "dma_camera";

/** @brief The cache attribute every camera buffer must be allocated with. */
const char *const s_pCamBufferCache = "cacheable";

/**
 * @brief Assemble one entry of a reported metadata buffer group.
 * Every metadata buffer is reported with the same shape, whether it is an input or an output, so a
 * client walks either array uniformly: the buffer name identifying it, the buffer type and the
 * properties the buffer must be allocated with. The buffer size is not reported: the client owns
 * the camera_metadata layout it allocates, so publishing a size here would duplicate a calculation
 * the node does not control.
 * @param[in]  name       The logical buffer name of the group.
 * @param[out] metaDataDt The assembled buffer descriptor.
 */
void BuildMetaDataBufferDescriptor( const std::string &name, DataTree &metaDataDt )
{
    DataTree bufferPropsDt;

    bufferPropsDt.Set<std::string>( "allocatorType", s_pCamBufferAllocator );
    bufferPropsDt.Set<std::string>( "cache", s_pCamBufferCache );

    metaDataDt.Set<std::string>( "name", name );
    metaDataDt.Set<std::string>( "bufferType", "raw" );
    metaDataDt.Set( "bufferProps", bufferPropsDt );
}

/**
 * @brief The self-contained Draft 2020-12 JSON Schema of the Camera configuration surface.
 * Mirrors CameraConfig::VerifyStaticConfig, so a configuration that validates against this
 * schema is one the node accepts. The bounds and enums track their sources of truth:
 * QCNODE_CAMERA_MAX_STREAM_NUM (32), QCNODE_CAMERA_MAX_STREAM_ID (255),
 * QCNODE_CAMERA_MAX_BUFFER_NUM (20), DataTree::GetImageFormat and GetMetaDataType. The
 * sentinel maxima (4294967294 = UINT32_MAX - 1, 254 = UINT8_MAX - 1) mirror the "is empty"
 * rejections of VerifyStaticConfig. No dynamicConfig is declared, because VerifyAndSet
 * consumes only the "static" object, and no per-SOC discriminator is emitted, because the
 * structural limits above are build-invariant.
 */
const char *const s_pCamConfigSchema = R"JSON(
{
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "title": "QCNode Camera configuration",
    "description": "Configuration accepted by QC::Node::CameraConfig::VerifyAndSet",
    "type": "object",
    "required": [ "static" ],
    "properties": {
        "schemaVersion": { "$ref": "#/$defs/schemaVersion" },
        "static": { "$ref": "#/$defs/staticConfig" }
    },
    "$defs": {
        "schemaVersion": {
            "description": "Optional semantic version of the configuration document",
            "type": "string",
            "pattern": "^[0-9]+\\.[0-9]+\\.[0-9]+$"
        },
        "nonEmptyString": {
            "type": "string",
            "minLength": 1
        },
        "staticConfig": {
            "description": "Init-time configuration of the Camera node",
            "type": "object",
            "required": [
                "name", "id", "inputId", "clientId", "srcId", "inputMode", "ispUseCase",
                "camFrameDropPattern", "camFrameDropPeriod", "opMode", "streamConfigs"
            ],
            "properties": {
                "name": {
                    "description": "The Node unique name",
                    "$ref": "#/$defs/nonEmptyString"
                },
                "id": {
                    "description": "The Node unique ID",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "inputId": {
                    "description": "Camera input id",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "srcId": {
                    "description": "Input source identifier",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "clientId": {
                    "description": "Used for multi-client use case",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "inputMode": {
                    "description": "Index into QCarCamInputModes_t pModes",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "ispUseCase": {
                    "description": "ISP use case defined by qcarcam",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "camFrameDropPattern": {
                    "description": "Frame drop pattern defined by qcarcam, 0 when unused",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "camFrameDropPeriod": {
                    "description": "Frame drop period defined by qcarcam",
                    "type": "integer", "minimum": 0, "maximum": 254
                },
                "opMode": {
                    "description": "Operation mode defined by qcarcam",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "streamConfigs": {
                    "description": "Configuration for each camera stream",
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 32,
                    "items": { "$ref": "#/$defs/streamConfig" }
                },
                "metaDataConfigs": {
                    "description": "Camera metadata configuration, required when enableMetaData",
                    "type": "array",
                    "minItems": 1,
                    "items": { "$ref": "#/$defs/metaDataConfig" }
                },
                "requestMode": {
                    "description": "Flag to set request buffer mode",
                    "type": "boolean", "default": true
                },
                "primary": {
                    "description": "Flag to indicate the session is primary for the clientId",
                    "type": "boolean", "default": true
                },
                "enableMetaData": {
                    "description": "Flag to enable metadata",
                    "type": "boolean", "default": false
                },
                "enableMultiStreamFrameReady": {
                    "description": "Report multiple streams frame ready in one callback",
                    "type": "boolean", "default": false
                },
                "recovery": {
                    "description": "Flag to enable self-recovery for the session",
                    "type": "boolean", "default": false
                }
            },
            "if": {
                "properties": { "enableMetaData": { "const": true } },
                "required": [ "enableMetaData" ]
            },
            "then": { "required": [ "metaDataConfigs" ] }
        },
        "streamConfig": {
            "description": "Configuration of one camera stream",
            "type": "object",
            "required": [
                "streamId", "width", "height", "format", "submitRequestPattern", "bufferIds"
            ],
            "properties": {
                "streamId": {
                    "description": "Camera stream id",
                    "type": "integer", "minimum": 0, "maximum": 255
                },
                "contextId": {
                    "description": "Context id this stream belongs to",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "width": {
                    "description": "Camera frame width",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "height": {
                    "description": "Camera frame height",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "format": {
                    "description": "Camera frame format",
                    "type": "string",
                    "enum": [ "nv12", "nv12_ubwc", "uyvy", "rgb", "bgr", "p010", "tp10_ubwc" ]
                },
                "submitRequestPattern": {
                    "description": "Buffer submit request pattern",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "bufferIds": {
                    "description": "Indices of camera frame buffers in QCNodeInit::buffers",
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                }
            }
        },
        "metaDataConfig": {
            "description": "Configuration of one camera metadata group",
            "type": "object",
            "required": [ "bufferListId", "tag", "bufferIds" ],
            "properties": {
                "bufferListId": {
                    "description": "The index of the metadata buffer group",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "tag": {
                    "description": "The metadata tag identified by QCarCam",
                    "type": "string",
                    "enum": [
                        "INJECTION_SENSOR_METADATA", "TUNING_FEATURE_1_MODE",
                        "TUNING_FEATURE_2_MODE"
                    ]
                },
                "bufferIds": {
                    "description": "Indices of camera metadata buffers in QCNodeInit::buffers",
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                },
                "outputBufferListId": {
                    "description": "Buffer list ID for output metadata buffers, 0 when unused",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "outputBufferIds": {
                    "description": "Indices of output metadata buffers in QCNodeInit::buffers",
                    "type": "array",
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                },
                "InjectionConfig": { "$ref": "#/$defs/injectionConfig" }
            },
            "if": {
                "properties": { "tag": { "const": "INJECTION_SENSOR_METADATA" } },
                "required": [ "tag" ]
            },
            "then": { "required": [ "InjectionConfig" ] }
        },
        "injectionConfig": {
            "description": "ISP injection configuration, used when the tag is INJECTION_SENSOR_METADATA",
            "type": "object",
            "required": [ "inputBufferIds" ],
            "properties": {
                "inputId": {
                    "description": "The camera id providing the captured image",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "inputBufferListId": {
                    "description": "Buffer list ID for raw frame input buffers",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "inputBufferIds": {
                    "description": "Indices of raw frame input buffers in QCNodeInit::buffers",
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                },
                "inputMode": {
                    "description": "The input mode in QCarCamInputModes_t",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "inputTuningParamFeature1Mode": {
                    "description": "Input Tuning Parameter Feature 1 Mode",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "inputTuningParamFeature2Mode": {
                    "description": "Input Tuning Parameter Feature 2 Mode",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "inputSceneMode": {
                    "description": "Input Scene Mode",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "width": {
                    "description": "Width of the raw frame injection input buffer",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "height": {
                    "description": "Height of the raw frame injection input buffer",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "stride": {
                    "description": "Stride of the raw frame injection input buffer",
                    "type": "integer", "minimum": 0, "maximum": 4294967294, "default": 0
                },
                "format": {
                    "description": "Color format of the raw frame injection input buffer",
                    "type": "string",
                    "enum": [
                        "mipiraw_8", "mipiraw_10", "mipiraw_12", "mipiraw_14", "mipiraw_16",
                        "plain16_10", "plain16_12", "plain16_14", "plain16_16"
                    ],
                    "default": "mipiraw_12"
                },
                "headerBufferListId": {
                    "description": "Buffer list ID for sensor header metadata buffers",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "headerBufferIds": {
                    "description": "Indices of sensor header buffers in QCNodeInit::buffers",
                    "type": "array",
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                },
                "eepromBufferListId": {
                    "description": "Buffer list ID for EEPROM calibration buffers",
                    "type": "integer", "minimum": 0, "maximum": 4294967294
                },
                "eepromBufferIds": {
                    "description": "Indices of EEPROM calibration buffers in QCNodeInit::buffers",
                    "type": "array",
                    "maxItems": 20,
                    "uniqueItems": true,
                    "items": { "type": "integer", "minimum": 0 }
                }
            }
        }
    },
    "examples": [
        {
            "schemaVersion": "1.0.0",
            "static": {
                "name":                "camera_front",
                "id":                  0,
                "inputId":             0,
                "clientId":            0,
                "srcId":               0,
                "inputMode":           0,
                "ispUseCase":          0,
                "camFrameDropPattern": 0,
                "camFrameDropPeriod":  1,
                "opMode":              0,
                "streamConfigs": [
                    {
                        "streamId":             0,
                        "width":                1920,
                        "height":               1080,
                        "format":               "nv12",
                        "submitRequestPattern": 1,
                        "bufferIds":            [ 0, 1, 2, 3 ]
                    }
                ]
            }
        },
        {
            "static": {
                "name":                "camera_front_isp",
                "id":                  1,
                "inputId":             0,
                "clientId":            0,
                "srcId":               0,
                "inputMode":           0,
                "ispUseCase":          0,
                "camFrameDropPattern": 0,
                "camFrameDropPeriod":  1,
                "opMode":              0,
                "enableMetaData":      true,
                "streamConfigs": [
                    {
                        "streamId":             0,
                        "width":                1920,
                        "height":               1080,
                        "format":               "nv12_ubwc",
                        "submitRequestPattern": 1,
                        "bufferIds":            [ 0, 1, 2, 3 ]
                    }
                ],
                "metaDataConfigs": [
                    {
                        "bufferListId": 0,
                        "tag":          "INJECTION_SENSOR_METADATA",
                        "bufferIds":    [ 4, 5 ],
                        "InjectionConfig": {
                            "inputBufferListId": 1,
                            "inputBufferIds":    [ 6, 7 ],
                            "width":             1920,
                            "height":            1080,
                            "stride":            3840,
                            "format":            "mipiraw_12"
                        }
                    }
                ]
            }
        }
    ]
}
)JSON";

}   // namespace

QCStatus_e CameraConfig::VerifyStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    bool enableMetaData = false;
    std::vector<DataTree> streamConfigs;
    std::vector<DataTree> metaDataConfigs;
    std::vector<uint32_t> bufferIds;
    std::unordered_set<uint32_t> usedBufferIds;

    std::string name = dt.Get<std::string>( "name", "" );
    if ( "" == name )
    {
        errors += "the name is empty, ";
        status = QC_STATUS_BAD_ARGUMENTS;
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t id = dt.Get<uint32_t>( "id", UINT32_MAX );
        if ( UINT32_MAX == id )
        {
            errors += "the id is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t inputId = dt.Get<uint32_t>( "inputId", UINT32_MAX );
        if ( UINT32_MAX == inputId )
        {
            errors += "the inputId is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t clientId = dt.Get<uint32_t>( "clientId", UINT32_MAX );
        if ( UINT32_MAX == clientId )
        {
            errors += "the clientId is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t srcId = dt.Get<uint32_t>( "srcId", UINT32_MAX );
        if ( UINT32_MAX == srcId )
        {
            errors += "the srcId is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t inputMode = dt.Get<uint32_t>( "inputMode", UINT32_MAX );
        if ( UINT32_MAX == inputMode )
        {
            errors += "the inputMode is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t ispUseCase = dt.Get<uint32_t>( "ispUseCase", UINT32_MAX );
        if ( UINT32_MAX == ispUseCase )
        {
            errors += "the ispUseCase is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t camFrameDropPattern = dt.Get<uint32_t>( "camFrameDropPattern", UINT32_MAX );
        if ( UINT32_MAX == camFrameDropPattern )
        {
            errors += "the camFrameDropPattern is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint8_t camFrameDropPeriod = dt.Get<uint8_t>( "camFrameDropPeriod", UINT8_MAX );
        if ( UINT8_MAX == static_cast<uint32_t>( camFrameDropPeriod ) )
        {
            errors += "the camFrameDropPeriod is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        uint32_t opMode = dt.Get<uint32_t>( "opMode", UINT32_MAX );
        if ( UINT32_MAX == opMode )
        {
            errors += "the opMode is empty, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        status = dt.Get( "streamConfigs", streamConfigs );
        if ( QC_STATUS_OK != status )
        {
            errors += "the streamConfigs is invalid";
        }
    }

    if ( QC_STATUS_OK == status )
    {
        size_t streamNum = streamConfigs.size();
        if ( streamNum > QCNODE_CAMERA_MAX_STREAM_NUM )
        {
            errors += "the streamNum is larger than maximum, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
        else if ( 0 == streamNum )
        {
            errors += "the streamNum is 0, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        for ( uint32_t i = 0; i < streamConfigs.size(); i++ )
        {
            DataTree streamConfig = streamConfigs[i];

            uint32_t streamId = streamConfig.Get<uint32_t>( "streamId", UINT32_MAX );
            if ( UINT32_MAX == streamId )
            {
                errors += "the streamId for stream " + std::to_string( i ) + " is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }
            else if ( streamId > QCNODE_CAMERA_MAX_STREAM_ID )
            {
                errors += "the streamId for stream " + std::to_string( i ) +
                          " is out of valid range (must be 0 to " +
                          std::to_string( QCNODE_CAMERA_MAX_STREAM_ID ) + "), ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( QC_STATUS_OK == status )
            {
                uint32_t width = streamConfig.Get<uint32_t>( "width", UINT32_MAX );
                if ( UINT32_MAX == width )
                {
                    errors += "the width for stream " + std::to_string( i ) + " is empty, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
            }

            if ( QC_STATUS_OK == status )
            {
                uint32_t height = streamConfig.Get<uint32_t>( "height", UINT32_MAX );
                if ( UINT32_MAX == height )
                {
                    errors += "the height for stream " + std::to_string( i ) + " is empty, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
            }

            if ( QC_STATUS_OK == status )
            {
                QCImageFormat_e format =
                        streamConfig.GetImageFormat( "format", QC_IMAGE_FORMAT_MAX );
                if ( QC_IMAGE_FORMAT_MAX == format )
                {
                    errors += "the format for stream " + std::to_string( i ) + " is empty, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
            }

            uint32_t submitRequestPattern =
                    streamConfig.Get<uint32_t>( "submitRequestPattern", UINT32_MAX );
            if ( UINT32_MAX == submitRequestPattern )
            {
                errors += "the submitRequestPattern for stream " + std::to_string( i ) +
                          " is empty, ";
                status = QC_STATUS_BAD_ARGUMENTS;
            }

            if ( QC_STATUS_OK == status )
            {
                std::vector<uint32_t> streamBufferIds =
                        streamConfig.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
                if ( 0 == streamBufferIds.size() )
                {
                    errors += "the bufferIds size for stream " + std::to_string( i ) + " is 0, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
                else if ( QCNODE_CAMERA_MAX_BUFFER_NUM < streamBufferIds.size() )
                {
                    errors += "the bufferIds size for stream " + std::to_string( i ) +
                              " is larger than maximum, ";
                    status = QC_STATUS_BAD_ARGUMENTS;
                }
                else
                {
                    // Check for duplicate buffer id
                    for ( uint32_t bufferId : streamBufferIds )
                    {
                        if ( usedBufferIds.find( bufferId ) != usedBufferIds.end() )
                        {
                            errors += "duplicate buffer ID " + std::to_string( bufferId ) +
                                      " found in stream " + std::to_string( i ) + ", ";
                            status = QC_STATUS_BAD_ARGUMENTS;
                            break;
                        }
                        usedBufferIds.insert( bufferId );
                    }
                }
            }

            if ( QC_STATUS_OK != status )
            {
                QC_ERROR( "Config verification failed for stream %u, ", i );
                break;
            }
        }
    }

    if ( QC_STATUS_OK == status )
    {
        enableMetaData = dt.Get<bool>( "enableMetaData", false );
        if ( true == enableMetaData )
        {
            status = dt.Get( "metaDataConfigs", metaDataConfigs );
            if ( QC_STATUS_OK != status )
            {
                errors += "the metaDataConfigs is invalid";
            }
        }
    }

    if ( ( QC_STATUS_OK == status ) && ( true == enableMetaData ) )
    {
        size_t metadataNum = metaDataConfigs.size();
        if ( 0 == metadataNum )
        {
            errors += "the number of metadata configs is 0, ";
            status = QC_STATUS_BAD_ARGUMENTS;
        }

        if ( QC_STATUS_OK == status )
        {
            for ( size_t i = 0; i < metadataNum; i++ )
            {
                DataTree metadataConfig = metaDataConfigs[i];
                std::string tag = metadataConfig.Get<std::string>( "tag", "" );

                if ( QC_STATUS_OK == status )
                {
                    uint32_t bufferListId =
                            metadataConfig.Get<uint32_t>( "bufferListId", UINT32_MAX );
                    if ( UINT32_MAX == bufferListId )
                    {
                        errors += "the bufferListId for metadata " + std::to_string( i ) +
                                  " is empty, ";
                        status = QC_STATUS_BAD_ARGUMENTS;
                    }
                }

                if ( QC_STATUS_OK == status )
                {
                    CameraMetaDataType_e tagType = GetMetaDataType( tag );
                    if ( ( CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE != tagType ) &&
                         ( CAMERA_METADATA_TYPE_ISP_INJECTION != tagType ) )
                    {
                        errors += "the tag '" + tag + "' for metadata " + std::to_string( i ) +
                                  " is invalid (expected INJECTION_SENSOR_METADATA, "
                                  "TUNING_FEATURE_1_MODE or TUNING_FEATURE_2_MODE), ";
                        status = QC_STATUS_BAD_ARGUMENTS;
                    }
                }

                if ( QC_STATUS_OK == status )
                {
                    std::vector<uint32_t> metadataBufferIds =
                            metadataConfig.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
                    if ( 0 == metadataBufferIds.size() )
                    {
                        errors += "the bufferIds size for metadata " + std::to_string( i ) +
                                  " is 0, ";
                        status = QC_STATUS_BAD_ARGUMENTS;
                    }
                    else if ( QCNODE_CAMERA_MAX_BUFFER_NUM < metadataBufferIds.size() )
                    {
                        errors += "the bufferIds size for metadata " + std::to_string( i ) +
                                  " is larger than maximum, ";
                        status = QC_STATUS_BAD_ARGUMENTS;
                    }
                    else
                    {
                        // Check for duplicate buffer id
                        for ( uint32_t bufferId : metadataBufferIds )
                        {
                            if ( usedBufferIds.find( bufferId ) != usedBufferIds.end() )
                            {
                                errors += "duplicate buffer ID " + std::to_string( bufferId ) +
                                          " found in metadata " + std::to_string( i ) + ", ";
                                status = QC_STATUS_BAD_ARGUMENTS;
                                break;
                            }
                            usedBufferIds.insert( bufferId );
                        }
                    }
                }

                // Validate INJECTION_SENSOR_METADATA specific config
                if ( QC_STATUS_OK == status )
                {
                    if ( "INJECTION_SENSOR_METADATA" == tag )
                    {
                        DataTree injCfgDt;
                        QCStatus_e injStatus = metadataConfig.Get( "InjectionConfig", injCfgDt );
                        if ( QC_STATUS_OK != injStatus )
                        {
                            errors +=
                                    "the InjectionConfig for INJECTION_SENSOR_METADATA metadata " +
                                    std::to_string( i ) + " is missing, ";
                            status = QC_STATUS_BAD_ARGUMENTS;
                        }
                        else
                        {
                            // Validate injection input buffer IDs
                            std::vector<uint32_t> injInputBufIds = injCfgDt.Get<uint32_t>(
                                    "inputBufferIds", std::vector<uint32_t>{} );
                            if ( 0 == injInputBufIds.size() )
                            {
                                errors += "the inputBufferIds for INJECTION_SENSOR_METADATA "
                                          "metadata " +
                                          std::to_string( i ) + " is empty, ";
                                status = QC_STATUS_BAD_ARGUMENTS;
                            }
                            else
                            {
                                for ( uint32_t bufferId : injInputBufIds )
                                {
                                    if ( usedBufferIds.find( bufferId ) != usedBufferIds.end() )
                                    {
                                        errors += "duplicate buffer ID " +
                                                  std::to_string( bufferId ) +
                                                  " found in INJECTION_SENSOR_METADATA "
                                                  "inputBufferIds "
                                                  "metadata " +
                                                  std::to_string( i ) + ", ";
                                        status = QC_STATUS_BAD_ARGUMENTS;
                                        break;
                                    }
                                    usedBufferIds.insert( bufferId );
                                }
                            }

                            if ( QC_STATUS_OK == status )
                            {
                                std::string injFormat =
                                        injCfgDt.Get<std::string>( "format", "mipiraw_12" );
                                QCarCamColorFmt_e injFmt = GetInjectQcarCamFormat( injFormat );
                                if ( QCARCAM_FMT_MAX == injFmt )
                                {
                                    errors += "the injection format '" + injFormat +
                                              "' for INJECTION_SENSOR_METADATA metadata " +
                                              std::to_string( i ) +
                                              " is invalid (expected mipiraw_8/10/12/14/16 or "
                                              "plain16_10/12/14/16), ";
                                    status = QC_STATUS_BAD_ARGUMENTS;
                                    break;
                                }
                            }
                        }
                    }
                }

                // Validate outputBufferIds (if outputBufferListId is set)
                if ( QC_STATUS_OK == status )
                {
                    uint32_t outputBufListId =
                            metadataConfig.Get<uint32_t>( "outputBufferListId", 0 );
                    if ( 0 != outputBufListId )
                    {
                        std::vector<uint32_t> outputBufIds = metadataConfig.Get<uint32_t>(
                                "outputBufferIds", std::vector<uint32_t>{} );
                        for ( uint32_t bufferId : outputBufIds )
                        {
                            if ( usedBufferIds.find( bufferId ) != usedBufferIds.end() )
                            {
                                errors += "duplicate buffer ID " + std::to_string( bufferId ) +
                                          " found in outputBufferIds for metadata " +
                                          std::to_string( i ) + ", ";
                                status = QC_STATUS_BAD_ARGUMENTS;
                                break;
                            }
                            usedBufferIds.insert( bufferId );
                        }
                    }
                }

                if ( QC_STATUS_OK != status )
                {
                    QC_ERROR( "Config verification failed for metadata %u, ", i );
                    break;
                }
            }
        }
    }

    if ( QC_STATUS_OK != status )
    {
        QC_ERROR( "Config verification failed for CameraConfigIfs, error: %s", errors.c_str() );
    }

    return status;
}

QCStatus_e CameraConfig::ParseStaticConfig( DataTree &dt, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;

    std::vector<DataTree> streamConfigs;
    std::vector<DataTree> metaDataConfigs;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else
    {
        CameraImplConfig_t &config = m_pCamImpl->GetConifg();

        status = VerifyStaticConfig( dt, errors );
        if ( QC_STATUS_OK == status )
        {
            config.nodeId.name = dt.Get<std::string>( "name", "" );
            config.nodeId.id = dt.Get<uint8_t>( "id", UINT8_MAX );
            config.inputId = dt.Get<uint32_t>( "inputId", UINT32_MAX );
            config.clientId = dt.Get<uint32_t>( "clientId", UINT32_MAX );
            config.srcId = dt.Get<uint32_t>( "srcId", UINT32_MAX );
            config.inputMode = dt.Get<uint32_t>( "inputMode", UINT32_MAX );
            config.ispUseCase = dt.Get<uint32_t>( "ispUseCase", UINT32_MAX );
            config.camFrameDropPattern = dt.Get<uint32_t>( "camFrameDropPattern", UINT32_MAX );
            config.camFrameDropPeriod = dt.Get<uint8_t>( "camFrameDropPeriod", UINT8_MAX );
            config.opMode = dt.Get<uint32_t>( "opMode", UINT32_MAX );
            status = dt.Get( "streamConfigs", streamConfigs );
        }

        if ( QC_STATUS_OK == status )
        {
            for ( uint32_t i = 0; i < streamConfigs.size(); i++ )
            {
                CameraStreamConfig_t streamConfig;
                DataTree streamConfigDt = streamConfigs[i];

                streamConfig.streamId = streamConfigDt.Get<uint32_t>( "streamId", UINT32_MAX );
                streamConfig.contextId = streamConfigDt.Get<uint32_t>( "contextId", 0U );
                streamConfig.width = streamConfigDt.Get<uint32_t>( "width", UINT32_MAX );
                streamConfig.height = streamConfigDt.Get<uint32_t>( "height", UINT32_MAX );
                streamConfig.format =
                        streamConfigDt.GetImageFormat( "format", QC_IMAGE_FORMAT_MAX );
                streamConfig.submitRequestPattern =
                        streamConfigDt.Get<uint32_t>( "submitRequestPattern", UINT32_MAX );
                streamConfig.bufferIds =
                        streamConfigDt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
                config.streamConfigs.push_back( std::move( streamConfig ) );
            }
        }

        if ( QC_STATUS_OK == status )
        {
            config.bRequestMode = dt.Get<bool>( "requestMode", true );
            config.bPrimary = dt.Get<bool>( "primary", true );
            config.bEnalbleMetaData = dt.Get<bool>( "enableMetaData", false );
            config.bMultiStreamFrameReady = dt.Get<bool>( "enableMultiStreamFrameReady", false );
            config.bRecovery = dt.Get<bool>( "recovery", false );
        }

        if ( ( QC_STATUS_OK == status ) && ( true == config.bEnalbleMetaData ) )
        {
            status = dt.Get( "metaDataConfigs", metaDataConfigs );

            if ( QC_STATUS_OK == status )
            {
                for ( size_t i = 0; i < metaDataConfigs.size(); i++ )
                {
                    CameraMetaDataConfig_t metaDataConfig = {};
                    DataTree metaDataConfigDt = metaDataConfigs[i];

                    metaDataConfig.bufferListId =
                            metaDataConfigDt.Get<uint32_t>( "bufferListId", UINT32_MAX );
                    metaDataConfig.bufferIds =
                            metaDataConfigDt.Get<uint32_t>( "bufferIds", std::vector<uint32_t>{} );
                    metaDataConfig.metaDataType =
                            GetMetaDataType( metaDataConfigDt.Get<std::string>( "tag", "" ) );

                    if ( CAMERA_METADATA_TYPE_ISP_INJECTION == metaDataConfig.metaDataType )
                    {
                        DataTree injCfgDt;
                        QCStatus_e injStatus = metaDataConfigDt.Get( "InjectionConfig", injCfgDt );
                        if ( QC_STATUS_OK == injStatus )
                        {
                            metaDataConfig.injectionConfig.inputId =
                                    injCfgDt.Get<uint32_t>( "inputId", 0 );
                            metaDataConfig.injectionConfig.inputMode =
                                    injCfgDt.Get<uint32_t>( "inputMode", 0 );
                            metaDataConfig.injectionConfig.inputTuningParamFeature1Mode =
                                    injCfgDt.Get<uint32_t>( "inputTuningParamFeature1Mode", 0 );
                            metaDataConfig.injectionConfig.inputTuningParamFeature2Mode =
                                    injCfgDt.Get<uint32_t>( "inputTuningParamFeature2Mode", 0 );
                            metaDataConfig.injectionConfig.inputSceneMode =
                                    injCfgDt.Get<uint32_t>( "inputSceneMode", 0 );
                            metaDataConfig.injectionConfig.inputBufferListId =
                                    injCfgDt.Get<uint32_t>( "inputBufferListId",
                                                            QCARCAM_BUFFERLIST_ID_INPUT_0 );
                            metaDataConfig.injectionConfig.inputBufferIds = injCfgDt.Get<uint32_t>(
                                    "inputBufferIds", std::vector<uint32_t>{} );
                            metaDataConfig.injectionConfig.format = GetInjectQcarCamFormat(
                                    injCfgDt.Get<std::string>( "format", "mipiraw_12" ) );
                            metaDataConfig.injectionConfig.width =
                                    injCfgDt.Get<uint32_t>( "width", 0 );
                            metaDataConfig.injectionConfig.height =
                                    injCfgDt.Get<uint32_t>( "height", 0 );
                            metaDataConfig.injectionConfig.stride =
                                    injCfgDt.Get<uint32_t>( "stride", 0 );

                            // Sensor header / per-frame metadata buffers
                            metaDataConfig.injectionConfig.headerBufferListId =
                                    injCfgDt.Get<uint32_t>( "headerBufferListId",
                                                            QCARCAM_BUFFERLIST_ID_INPUT_METADATA );
                            metaDataConfig.injectionConfig.headerBufferIds = injCfgDt.Get<uint32_t>(
                                    "headerBufferIds", std::vector<uint32_t>{} );

                            // EEPROM calibration data buffers
                            metaDataConfig.injectionConfig.eepromBufferListId =
                                    injCfgDt.Get<uint32_t>( "eepromBufferListId",
                                                            QCARCAM_BUFFERLIST_ID_INPUT_METADATA +
                                                                    1U );
                            metaDataConfig.injectionConfig.eepromBufferIds = injCfgDt.Get<uint32_t>(
                                    "eepromBufferIds", std::vector<uint32_t>{} );
                        }
                    }

                    // Parse output metadata buffer config (optional, 0 = not used)
                    metaDataConfig.outputBufferListId =
                            metaDataConfigDt.Get<uint32_t>( "outputBufferListId", 0 );
                    metaDataConfig.outputBufferIds = metaDataConfigDt.Get<uint32_t>(
                            "outputBufferIds", std::vector<uint32_t>{} );

                    config.metaDataConfigs.push_back( std::move( metaDataConfig ) );
                }
            }
        }

        if ( QC_STATUS_OK == status )
        {
            config.executeMode = QC_EXECUTE_MODE_ASYNC;
            config.processor = QC_PROCESSOR_MAX;
            config.coreIds.clear();
            config.inputs.clear();
            config.outputs.clear();
            config.outputs.reserve( config.streamConfigs.size() );
            Memory::UtilsBase memoryUtils;
            for ( const CameraStreamConfig_t &streamConfig : config.streamConfigs )
            {
                Memory::ImageBasicProps_t basicProps( streamConfig.width, streamConfig.height,
                                                      streamConfig.format );
                Memory::ImageProps_t outputProps;
                status = memoryUtils.SetImagePropsFromImageBasicProp( basicProps, outputProps );
                if ( QC_STATUS_OK != status )
                {
                    errors += "failed to derive camera output buffer properties, ";
                    config.outputs.clear();
                    break;
                }

                config.outputs.push_back(
                        { "stream" + std::to_string( streamConfig.streamId ), QC_BUFFER_TYPE_IMAGE,
                          std::move( outputProps ) } );
            }
        }
    }

    return status;
}

QCarCamColorFmt_e CameraConfig::GetInjectQcarCamFormat( const std::string &format )
{
    QCarCamColorFmt_e qcarcamFormat = QCARCAM_FMT_MAX;

    if ( "mipiraw_8" == format )
    {
        qcarcamFormat = QCARCAM_FMT_MIPIRAW_8;
    }
    else if ( "mipiraw_10" == format )
    {
        qcarcamFormat = QCARCAM_FMT_MIPIRAW_10;
    }
    else if ( "mipiraw_12" == format )
    {
        qcarcamFormat = QCARCAM_FMT_MIPIRAW_12;
    }
    else if ( "mipiraw_14" == format )
    {
        qcarcamFormat = QCARCAM_FMT_MIPIRAW_14;
    }
    else if ( "mipiraw_16" == format )
    {
        qcarcamFormat = QCARCAM_FMT_MIPIRAW_16;
    }
    else if ( "plain16_10" == format )
    {
        qcarcamFormat = QCARCAM_FMT_PLAIN16_10;
    }
    else if ( "plain16_12" == format )
    {
        qcarcamFormat = QCARCAM_FMT_PLAIN16_12;
    }
    else if ( "plain16_14" == format )
    {
        qcarcamFormat = QCARCAM_FMT_PLAIN16_14;
    }
    else if ( "plain16_16" == format )
    {
        qcarcamFormat = QCARCAM_FMT_PLAIN16_16;
    }
    else
    {
        QC_ERROR( "GetInjectQcarCamFormat: unsupported inject format string: %s", format.c_str() );
    }

    return qcarcamFormat;
}

CameraMetaDataType_e CameraConfig::GetMetaDataType( const std::string &tag )
{
    CameraMetaDataType_e metaDataType = CAMERA_METADATA_TYPE_UNKNOWN;

    if ( ( "TUNING_FEATURE_1_MODE" == tag ) || ( "TUNING_FEATURE_2_MODE" == tag ) )
    {
        metaDataType = CAMERA_METADATA_TYPE_TUNING_FEATURE_MODE;
    }
    else if ( "INJECTION_SENSOR_METADATA" == tag )
    {
        metaDataType = CAMERA_METADATA_TYPE_ISP_INJECTION;
    }
    else if ( "" != tag )
    {
        metaDataType = CAMERA_METADATA_TYPE_GENERIC;
    }
    else
    {
        QC_ERROR( "Unsupported metadata type: %s", tag.c_str() );
    }

    return metaDataType;
}

QCStatus_e CameraConfig::VerifyAndSet( const std::string config, std::string &errors )
{
    QCStatus_e status = QC_STATUS_OK;
    DataTree dt;

    status = NodeConfigBase::VerifyAndSet( config, errors );
    if ( QC_STATUS_OK == status )
    {

        status = m_dataTree.Get( "static", dt );
    }
    if ( QC_STATUS_OK == status )
    {
        status = ParseStaticConfig( dt, errors );

        /* The buffer information cached in the options document describes the configuration
         * parsed before this call, so force GetOptions to rebuild it. */
        m_bOptionsBuilt = false;
    }

    return status;
}

QCStatus_e CameraConfig::LoadConfigSchema()
{
    QCStatus_e status = QC_STATUS_OK;
    std::string errors;
    DataTree schemaDt;

    status = m_optionsTree.Load( std::string( s_pCamOptionsEnvelope ), errors );
    if ( QC_STATUS_OK != status )
    {
        QC_ERROR( "Failed to parse the Camera options envelope: %s", errors.c_str() );
        status = QC_STATUS_NOMEM;
    }

    if ( QC_STATUS_OK == status )
    {
        status = schemaDt.Load( std::string( s_pCamConfigSchema ), errors );
        if ( QC_STATUS_OK != status )
        {
            QC_ERROR( "Failed to parse the Camera configuration schema: %s", errors.c_str() );
            status = QC_STATUS_NOMEM;
        }
    }

    if ( QC_STATUS_OK == status )
    {
        /* The QCarCam version the node was compiled against. QCarCam exposes no runtime version
         * query, so this is the compile-time header version, reported as "major.minor.patch" so a
         * client reads it directly instead of having to know the QCARCAM_VERSION packing. */
        const std::string qcarcamVersion = std::to_string( QCARCAM_VERSION_MAJOR ) + "." +
                                           std::to_string( QCARCAM_VERSION_MINOR ) + "." +
                                           std::to_string( QCARCAM_VERSION_PATCH );

        m_optionsTree.Set<uint32_t>( "version", QCNODE_CAMERA_VERSION );
        m_optionsTree.Set<std::string>( "qcarcamVersion", qcarcamVersion );
        m_optionsTree.Set( "config", schemaDt );
        m_options = m_optionsTree.Dump();
        m_bSchemaLoaded = true;
    }
    else
    {
        m_options.clear();
    }

    return status;
}

QCStatus_e CameraConfig::BuildBufferInfo()
{
    QCStatus_e status = QC_STATUS_OK;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
        status = QC_STATUS_NOMEM;
    }
    else
    {
        const CameraImplConfig_t &config = m_pCamImpl->GetConifg();

        if ( 0 == config.streamConfigs.size() )
        {
            /* The node is not configured yet, so there is no buffer information to report. The
             * buffers key stays empty and a later call rebuilds it once the configuration has
             * been parsed. */
            QC_DEBUG( "No camera stream is configured yet, reporting the schema only" );
        }
        else
        {
            DataTree buffersDt;
            std::vector<DataTree> inputDts;
            std::vector<DataTree> outputDts;
            MemUtils memUtils;

            /* A camera stream produces frames, so every configured stream is an output buffer. */
            for ( size_t i = 0; ( i < config.streamConfigs.size() ) && ( QC_STATUS_OK == status );
                  i++ )
            {
                const CameraStreamConfig_t &streamConfig = config.streamConfigs[i];
                DataTree streamDt;
                DataTree imagePropsDt;
                std::string name =
                        config.nodeId.name + ".stream" + std::to_string( streamConfig.streamId );
                ImageBasicProps_t basicProps( 1U, streamConfig.width, streamConfig.height,
                                              streamConfig.format );
                ImageDescriptor_t imageDesc;

                /* Resolve the plane layout the client must allocate for this stream. The
                 * platform plane-definition library is the single source of truth for it, so
                 * the node reports the same stride, height and plane size the client would
                 * derive itself, including for the tiled UBWC formats. */
                status = memUtils.SetImageDescFromImageBasicProp( basicProps, imageDesc );
                if ( QC_STATUS_OK != status )
                {
                    QC_ERROR( "Failed to resolve the image layout of stream %u: %d",
                              streamConfig.streamId, status );
                }
                else
                {
                    std::vector<uint32_t> strides( imageDesc.stride,
                                                   imageDesc.stride + imageDesc.numPlanes );
                    std::vector<uint32_t> actualHeights(
                            imageDesc.actualHeight, imageDesc.actualHeight + imageDesc.numPlanes );
                    std::vector<uint32_t> planeBufSizes(
                            imageDesc.planeBufSize, imageDesc.planeBufSize + imageDesc.numPlanes );

                    imagePropsDt.SetImageFormat( "format", imageDesc.format );
                    imagePropsDt.Set<uint32_t>( "batchSize", imageDesc.batchSize );
                    imagePropsDt.Set<uint32_t>( "width", imageDesc.width );
                    imagePropsDt.Set<uint32_t>( "height", imageDesc.height );
                    imagePropsDt.Set<uint32_t>( "numPlanes", imageDesc.numPlanes );
                    imagePropsDt.Set<uint32_t>( "stride", strides );
                    imagePropsDt.Set<uint32_t>( "actualHeight", actualHeights );
                    imagePropsDt.Set<uint32_t>( "planeBufSize", planeBufSizes );

                    streamDt.Set<std::string>( "name", name );
                    streamDt.Set<std::string>( "bufferType", "image" );
                    streamDt.Set( "imageProps", imagePropsDt );

                    outputDts.push_back( streamDt );
                }
            }

            for ( size_t i = 0; ( i < config.metaDataConfigs.size() ) && ( QC_STATUS_OK == status );
                  i++ )
            {
                const CameraMetaDataConfig_t &metaDataConfig = config.metaDataConfigs[i];
                const CameraInjectConfig_t &injectConfig = metaDataConfig.injectionConfig;
                const std::string metaDataName =
                        config.nodeId.name + ".metadata" + std::to_string( i );
                DataTree metaDataDt;

                /* The group's own metadata buffer is the one a ProcessFrameDescriptor call carries:
                 * the node receives it as the CameraMetaDataDescriptor of that frame descriptor. It
                 * is an input buffer, because every metadata tag the configuration accepts - the
                 * TUNING_FEATURE modes and INJECTION_SENSOR_METADATA - feeds the ISP through an
                 * INPUT_METADATA buffer list. */
                BuildMetaDataBufferDescriptor( metaDataName + ".inputBuffer", metaDataDt );
                inputDts.push_back( metaDataDt );

                /* The ISP injection buffer lists are buffers of their metadata group, so they are
                 * named after it, and they feed the offline ISP, so they are inputs too. Unlike the
                 * group's metadata buffer they are registered from QCNodeInit::buffers at
                 * Initialize and addressed by (bufferListId, bufferIdx) inside the metadata
                 * descriptor, so they never occupy a frame descriptor slot: the client must
                 * allocate them all the same. Only the lists that carry buffers are advertised,
                 * matching the registration conditions in CameraImpl::SetMetaDataBuffers. */
                if ( CAMERA_METADATA_TYPE_ISP_INJECTION == metaDataConfig.metaDataType )
                {
                    const std::string injectName = metaDataName + ".injection";

                    if ( injectConfig.inputBufferIds.size() > 0 )
                    {
                        DataTree inputBufferDt;
                        BuildMetaDataBufferDescriptor( injectName + ".inputBuffer", inputBufferDt );
                        inputDts.push_back( inputBufferDt );
                    }
                    if ( injectConfig.headerBufferIds.size() > 0 )
                    {
                        DataTree headerBufferDt;
                        BuildMetaDataBufferDescriptor( injectName + ".headerBuffer",
                                                       headerBufferDt );
                        inputDts.push_back( headerBufferDt );
                    }
                    if ( injectConfig.eepromBufferIds.size() > 0 )
                    {
                        DataTree eepromBufferDt;
                        BuildMetaDataBufferDescriptor( injectName + ".eepromBuffer",
                                                       eepromBufferDt );
                        inputDts.push_back( eepromBufferDt );
                    }
                }

                /* The output metadata buffer list receives the ISP results, so it is the one output
                 * buffer a metadata group contributes. It exists for every metadata type and is
                 * advertised under the same condition SetMetaDataBuffers registers it. It is an
                 * Initialize-time buffer as well. */
                if ( ( 0 != metaDataConfig.outputBufferListId ) &&
                     ( metaDataConfig.outputBufferIds.size() > 0 ) )
                {
                    DataTree outputBufferDt;
                    BuildMetaDataBufferDescriptor( metaDataName + ".outputBuffer", outputBufferDt );
                    outputDts.push_back( outputBufferDt );
                }
            }

            if ( QC_STATUS_OK == status )
            {
                buffersDt.Set( "inputs", inputDts );
                buffersDt.Set( "outputs", outputDts );
                m_optionsTree.Set( "buffers", buffersDt );
                m_options = m_optionsTree.Dump();
                m_bOptionsBuilt = true;
            }
        }
    }

    return status;
}

QCStatus_e CameraConfig::GetOptions( std::string &options )
{
    QCStatus_e status = QC_STATUS_OK;

    if ( false == m_bSchemaLoaded )
    {
        status = LoadConfigSchema();
    }

    if ( ( QC_STATUS_OK == status ) && ( false == m_bOptionsBuilt ) )
    {
        status = BuildBufferInfo();
    }

    if ( QC_STATUS_OK == status )
    {
        options = m_options;
    }

    return status;
}

void CameraConfig::Reset()
{
    // Clear the parsed config tree so a subsequent Initialize starts from a clean state.
    m_dataTree = DataTree();

    // Force GetOptions to rebuild the configuration-derived buffer info.
    m_bOptionsBuilt = false;

    // m_bSchemaLoaded can stay true — the schema is static. Drop the configuration-derived buffer
    // info from the cached options so the reported document no longer describes the released
    // configuration (restoring the empty arrays the options envelope starts with).
    if ( m_bSchemaLoaded )
    {
        std::vector<DataTree> emptyArr;
        m_optionsTree.Set( "buffers.inputs", emptyArr );
        m_optionsTree.Set( "buffers.outputs", emptyArr );
        m_options = m_optionsTree.Dump();
    }
}

const QCNodeConfigBase_t &CameraConfig::Get()
{
    static const CameraImplConfig_t s_defaultConfig{};
    const QCNodeConfigBase_t *pConfig = &s_defaultConfig;

    if ( nullptr == m_pCamImpl )
    {
        QC_ERROR( "CameraImpl not allocated (out of memory)" );
    }
    else
    {
        pConfig = &m_pCamImpl->GetConifg();
    }

    return *pConfig;
}

}   // namespace Node
}   // namespace QC
