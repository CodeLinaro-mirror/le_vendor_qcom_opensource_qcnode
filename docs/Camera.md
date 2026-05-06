*Menu*:
- [1. Introduction](#1-introduction)
  - [1.1 Functional overview](#11-functional-overview)
  - [1.2 Operational overview](#12-operational-overview)
- [2. Camera Configuration](#2-camera-configuration)
  - [2.1 Camera Node Configuration](#21-camera-node-configuration)
  - [2.2 Camera Stream Configuration](#22-camera-stream-configuration)
  - [2.3 Camera Metadata Configuration](#23-camera-metadata-configuration)
- [3. Camera APIs](#3-camera-apis)
  - [3.1 QCNode Camera APIs](#31-qcnode-camera-apis)
  - [3.2 QCNode Configuration Interfaces](#32-qcnode-configuration-interfaces)
- [4. Camera Examples](#4-camera-examples)
  - [4.1 Camera working mode](#41-camera-working-mode)
    - [4.1.1 Request buffer mode](#411-request-buffer-mode)
    - [4.1.2 Non request buffer mode](#412-non-request-buffer-mode)
  - [4.2 header files and macro/variable definitions](#42-header-files-and-macrovariable-definitions)
  - [4.3 callback function definition](#43-callback-function-definition)
  - [4.4 Camera Config initialization](#44-camera-config-initialization)
  - [4.5 Camera stream configuration and buffer allocation](#45-camera-stream-configuration-and-buffer-allocation)
  - [4.6 Camera execution](#46-camera-execution)
  - [4.7 Buffer free](#47-buffer-free)
  - [4.8 Main function](#48-main-function)
  - [4.9 Typical usecases](#49-typical-usecases)
    - [4.9.1 Request buffer mode](#491-request-buffer-mode)
    - [4.9.2 Non-request buffer mode](#492-non-request-buffer-mode)
    - [4.9.3 Multi-stream with submit request pattern](#493-multi-stream-with-submit-request-pattern)
    - [4.9.4 Multi-client feature](#494-multi-client-feature)
    - [4.9.5 Camera frame drop pattern and period](#495-camera-frame-drop-pattern-and-period)
- [5. References](#5-references)
- [6. Functional Safety](#6-functional-safety)
  - [6.1 ASIL](#61-asil)
  - [6.2 Assumptions of Use (SWAOU)](#62-assumptions-of-use-swaou)
    - [QCNODE-CAMERA-SWAOU-1](#qcnode-camera-swaou-1)
    - [QCNODE-CAMERA-SWAOU-2](#qcnode-camera-swaou-2)
    - [QCNODE-CAMERA-SWAOU-3](#qcnode-camera-swaou-3)


# 1. Introduction
This document describes the QCNode Camera APIs and provides code samples demonstrating their usage.

## 1.1 Functional overview
QCNode Camera is a wrapper around QCarCam that offers user-friendly APIs for seamless camera integration in ADAS (Advanced Driver Assistance Systems) applications.

Camera SW arch
<img src="./images/camera-arch.jpg" width="512" height="768">

## 1.2 Operational overview
A camera instance is operated through a sequence of function calls for optimal performance, as outlined below:

- Initialization
  - Call Init() to configure parameters and create a camera session.
- Register Callbacks
  - Use RegisterCallback() to set up event callbacks for handling frame and driver events.
- Start Streaming
  - Invoke Start() to activate the camera hardware and begin processing sensor signals. When a new frame is captured, the frame-done callback is triggered. After processing the frame, the application must return it to the camera.

![camera flow](./images/camera-flow.png)

# 2. Camera Configuration

## 2.1 Camera Node Configuration
| Parameter    | Required  | Type        | Description            |
|--------------|-----------|-------------|------------------------|
| `name`       | true      | string      | The Node unique name.  |
| `id`         | true      | uint32_t    | The Node unique ID.    |
| `inputId`    | true      | uint32_t    | Camera input id.       |
| `srcId`      | true      | uint32_t    | Camera Input source identifier.       |
| `clientId`   | true      | uint32_t    | Used for multi-client use case.       |
| `inputMode`  | true      | uint32_t    | The input mode id is the index into #QCarCamInputModes_t pModex.       |
| `ispUseCase` | true      | uint32_t    | ISP use case defined by qcarcam.       |
| `opMode`     | true      | uint32_t    | Operation mode defined by qcarcam.     |
| `camFrameDropPattern` | true      | uint32_t    | Frame drop pattern defined by qcarcam. Default: `0`   |
| `camFrameDropPeriod`  | true      | uint32_t    | Frame drop period defined by qcarcam. Default: `0`   |
| `streamConfigs`       | true      | object[]    | Configurations for each camera stream. The stream object configuration is shown in Camera Stream Configuration table. |
| `requestMode`                 | false     | bool         | Flag to set request buffer mode.   |
| `enableMetaData`              | false     | bool         | Flag to enable metadata.   |
| `enableMultiStreamFrameReady` | false     | bool         | Flag to set multiple streams frame ready event in one callback.   |
| `primary`                     | false     | bool         | Flag to indicate if the session is primary or not when configured with the clientId.   |
| `recovery`                    | false     | bool         | Flag to enable self-recovery for the session.   |
| `metaDataConfigs`             | false     | object[]     | Configurations for each camera metadata. The metadata object configuration is shown in Camera Metadata Configuration table. |

## 2.2 Camera Stream Configuration
| Parameter    | Required  | Type         | Description                  |
|--------------|-----------|--------------|------------------------------|
| `streamId`   | true      | uint32_t     | Camera stream id.            |
| `bufferIds`  | true      | uint32_t[]   | The indices of camera frame buffers in QCNodeInit::buffers.  |
| `width`      | true      | uint32_t     | Camera frame width.          |
| `height`     | true      | uint32_t     | Camera frame height.         |
| `format`     | true      | string       | Camera frame format. Options: `nv12`, `nv12_ubwc`, `uyvy`, `rgb`, `bgr`, `p010`, `tp10_ubwc` |
| `submitRequestPattern`   | true         | uint32_t    | Buffer submit request pattern.   |

## 2.3 Camera Metadata Configuration
| Parameter    | Required  | Type         | Description                  |
|--------------|-----------|--------------|------------------------------|
| `bufferListId`| true      | uint32_t     | Camera metadata buffer list id. |
| `bufferIds`  | true      | uint32_t[]   | The indices of camera metadata buffers in QCNodeInit::buffers.  |

- Example Configurations
```json
{
    "static": {
        "name": "CAM0",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 0,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufCnt": 8,
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            }
        ],
        "requestMode": true,
        "primary": false,
        "recovery": false
    }
}
```

# 3. Camera APIs

## 3.1 QCNode Camera APIs

- [Camera::Initialize](../include/QC/Node/Camera.hpp#L226)
- [Camera::Start](../include/QC/Node/Camera.hpp#L244)
- [Camera::ProcessFrameDescriptor](../include/QC/Node/Camera.hpp#L257)
- [Camera::Stop](../include/QC/Node/Camera.hpp#L263)
- [Camera::DeInitialize](../include/QC/Node/Camera.hpp#L269)
- [Camera::GetConfigurationIfs](../include/QC/Node/Camera.hpp#L232)
- [Camera::GetMonitoringIfs](../include/QC/Node/Camera.hpp#L238)

## 3.2 QCNode Configuration Interfaces

- [CameraConfig::GetOptions](../include/QC/Node/Camera.hpp#L110) Get Configuration Options
  - Use this API to get the configuration options.
    - Below was a example output for Camera request mode:
        ```json
        {
            "static": {
                "name": "CAM0",
                "id": 0,
                "inputId": 8,
                "srcId": 0,
                "clientId": 0,
                "inputMode": 0,
                "ispUseCase": 65,
                "camFrameDropPattern": 0,
                "camFrameDropPeriod": 0,
                "opMode": 2,
                "streamConfigs": [
                    {
                        "streamId": 1,
                        "bufCnt": 8,
                        "format": "nv12",
                        "height": 2160,
                        "width": 3840,
                        "submitRequestPattern": 0
                    }
                ],
                "requestMode": true,
                "primary": false,
                "recovery": false
            }
        }
        ```

# 4. Camera Examples

## 4.1 Camera working mode

### 4.1.1 Request buffer mode

When the `requestMode` parameter in the camera configuration is set to true, the camera works in request buffer mode. In this mode, new frames are delivered through the frame callback function, and the application can request the next frame only after it has completed processing the current one.

### 4.1.2 Non request buffer mode

When the `requestMode` parameter in the camera configuration is set to false, the camera works in non‑request buffer mode. In this mode, new frames are delivered through the frame callback function, and the application is responsible for releasing each frame back to the camera once processing is complete.


## 4.2 header files and macro/variable definitions
```c++
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include "QC/Node/Camera.hpp"
#include "QC/sample/SharedBufferPool.hpp"
#include "gtest/gtest.h"

using namespace QC;
using namespace QC::Node;
using namespace QC::sample;

std::string g_CameraConfig = EXPAND_JSON( {
            "static": {
                "name": "CAM0",
                "id": 0,
                "inputId": 8,
                "srcId": 0,
                "clientId": 0,
                "inputMode": 0,
                "ispUseCase": 65,
                "camFrameDropPattern": 0,
                "camFrameDropPeriod": 0,
                "opMode": 2,
                "streamConfigs": [
                    {
                        "streamId": 1,
                        "bufferIds": [0, 1, 2, 3],
                        "format": "nv12",
                        "height": 2160,
                        "width": 3840,
                        "submitRequestPattern": 0
                    }
                ],
                "requestMode": true,
                "primary": false,
                "recovery": false,
                "enableMetaData": false
            }
        } );

QC::Node::Camera g_camera;
uint32_t g_frameIdx = 0;
std::vector<SharedBufferPool> g_bufferPools;
```

## 4.3 callback function definition
```c++
void ProcessDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    QCStatus_e ret = QC_STATUS_OK;

    QCFrameDescriptorNodeIfs &frameDescIfs = eventInfo.frameDesc;
    QCBufferDescriptorBase_t &bufDesc = frameDescIfs.GetBuffer( 0 );
    QCSharedFrameDescriptorNode frameDesc( 1 );

    const CameraFrameDescriptor_t *pCamFrameDesc =
            dynamic_cast<const CameraFrameDescriptor_t *>( &bufDesc );

    if ( QC_STATUS_OK == ret )
    {
        CameraFrameDescriptor_t camFrameDesc = *pCamFrameDesc;
        ret = frameDesc.SetBuffer( 0, camFrameDesc );
        ret = g_camera.ProcessFrameDescriptor( frameDesc );
        std::cout << "Process Frame index: " << g_frameIdx << std::endl;
        g_frameIdx++;
    }
}
```

## 4.4 Camera Config initialization
```c++
void Init_CameraConfig( std::string &jsonStr )
{
    QCStatus_e ret;
    DataTree dt;
    DataTree staticCfg;
    QCNodeInit_t config;

    ret = dt.Load( jsonStr, errors );
    std::cout << "config: " << config.config << std::endl;

    config.callback = ProcessDoneCb;

    ret = dt.Get( "static", staticCfg );
    std::string name = staticCfg.Get<std::string>( "name", "" );
    QCNodeID_t nodeId;
    nodeId.name = name;
    nodeId.type = QC_NODE_TYPE_QCX;
    nodeId.id = staticCfg.Get<uint32_t>( "id", UINT32_MAX );
}
```

## 4.5 Camera stream configuration and buffer allocation
```c++
void Init_CameraStreams()
{
    QCStatus_e ret;
    std::vector<DataTree> streamConfigs;
    ret = staticCfg.Get( "streamConfigs", streamConfigs );

    DataTree streamConfig;
    ImageProps_t imgProp;
    uint32_t streamId = 0;
    uint32_t bufCnt = 0;
    uint32_t bufferId = 0;
    uint32_t numStream = streamConfigs.size();
    g_bufferPools.resize( numStream );

    for ( uint32_t i = 0; i < numStream; i++ )
    {
        std::string bufPoolName = name + std::to_string( i );
        streamConfig = streamConfigs[i];
        streamId = streamConfig.Get<uint32_t>( "streamId", UINT32_MAX );
        bufCnt = streamConfig.Get<uint32_t>( "bufCnt", UINT32_MAX );
        imgProp.format = streamConfig.GetImageFormat( "format", QC_IMAGE_FORMAT_MAX );
        imgProp.width = streamConfig.Get<uint32_t>( "width", UINT32_MAX );
        imgProp.height = streamConfig.Get<uint32_t>( "height", UINT32_MAX );

        if ( ( QC_IMAGE_FORMAT_RGB888 == imgProp.format ) ||
             ( QC_IMAGE_FORMAT_BGR888 == imgProp.format ) )
        {
            imgProp.batchSize = 1;
            imgProp.stride[0] = QC_ALIGN_SIZE( imgProp.width * 3, 16 );
            imgProp.actualHeight[0] = imgProp.height;
            imgProp.numPlanes = 1;
            imgProp.planeBufSize[0] = 0;

            ret = g_bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufCnt, imgProp,
                                         QC_MEMORY_ALLOCATOR_DMA_CAMERA, QC_CACHEABLE );
        }
        else
        {
            ret = g_bufferPools[i].Init( bufPoolName, nodeId, LOGGER_LEVEL_ERROR, bufCnt,
                                         imgProp.width, imgProp.height, imgProp.format,
                                         QC_MEMORY_ALLOCATOR_DMA_CAMERA, QC_CACHEABLE );
        }

        ret = g_bufferPools[i].GetBuffers( config.buffers );
    }
}
```

## 4.6 Camera execution
```c++
ret = g_camera.Initialize( config );
ret = g_camera.Start();

while (...)
{
    // Waiting loop to get camera frames
}

ret = g_camera.Stop();
ret = g_camera.DeInitialize();
```

## 4.7 Buffer free
```c++
for ( uint32_t i = 0; i < numStream; i++ )
{
    ret = g_bufferPools[i].Deinit();
}
```

## 4.8 Main function
```c++
int main()
{
    QCStatus_e ret;
    Init_CameraConfig( g_CameraConfig );
    Init_CameraStreams();

    ret = g_camera.Initialize( config );
    ret = g_camera.Start();

    while (...)
    {
        // Waiting loop to get camera frames
    }

    ret = g_camera.Stop();
    ret = g_camera.DeInitialize();

    for ( uint32_t i = 0; i < numStream; i++ )
    {
        ret = g_bufferPools[i].Deinit();
    }

    return 0;
}
```

## 4.9 Typical usecases

### 4.9.1 Request buffer mode

Example configuration:
```json
{
    "static": {
        "name": "CAM",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 0,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufferIds": [0, 1, 2, 3],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            }
        ],
        "primary": false,
        "recovery": false,
        "requestMode": true,
        "enableMetaData": false
    }
}
```

### 4.9.2 Non-request buffer mode

Example configuration:
```json
{
    "static": {
        "name": "CAM",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 0,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufferIds": [0, 1, 2, 3],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            }
        ],
        "primary": false,
        "recovery": false,
        "requestMode": false,
        "enableMetaData": false
    }
}
```

### 4.9.3 Multi-stream with submit request pattern
The `submitRequestPattern` option in CameraStreamConfig_t is only valid when `requestMode` is set to true and `streamConfigs` has 2 or more elements. The purpose of submitRequestPattern is to reduce the camera's frames per second (FPS) to lower DDR usage. And the `enableMultiStreamFrameReady` option is used to set multiple streams frame ready event in one callback, which could reduce CPU loading.

Example configuration:
```json
{
    "static": {
        "name": "CAM",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 0,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufferIds": [0, 1, 2, 3],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            },
            {
                "streamId": 2,
                "bufferIds": [4, 5, 6, 7],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 3
            }
        ],
        "primary": false,
        "recovery": false,
        "requestMode": true,
        "enableMetaData": false,
        "enableMultiStreamFrameReady": true
    }
}
```
- For any stream with a non-zero submitRequestPattern, the FPS is calculated as:
    - FPS = The FPS of stream 0 / submitRequestPattern
- In this configuration:
    - The stream 0 will operate at 30 FPS.
    - The stream 1 will operate at 10 FPS (calculated as 30 / 3).

### 4.9.4 Multi-client feature

The camera supports multi‑client mode, allowing two or more processes to open and stream from the same camera sensor simultaneously. QCNode camera enables this functionality through the clientId and bPrimary configuration options. Note that when clientId is set to 0, the system defaults to single‑client mode, in which case only one process can open and stream from the camera sensor.

Example configuration for primary session:
```json
{
    "static": {
        "name": "CAM0",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 1,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufferIds": [0, 1, 2, 3],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            }
        ],
        "primary": true,
        "recovery": false,
        "requestMode": true,
        "enableMetaData": false
    }
}
```

Example configuration for subscriber (non-primary) session:
```json
{
    "static": {
        "name": "CAM1",
        "id": 0,
        "inputId": 8,
        "srcId": 0,
        "clientId": 3,
        "inputMode": 0,
        "ispUseCase": 65,
        "camFrameDropPattern": 0,
        "camFrameDropPeriod": 0,
        "opMode": 2,
        "streamConfigs": [
            {
                "streamId": 1,
                "bufferIds": [4, 5, 6, 7],
                "format": "nv12",
                "height": 2160,
                "width": 3840,
                "submitRequestPattern": 0
            }
        ],
        "primary": false,
        "recovery": false,
        "requestMode": true,
        "enableMetaData": false
    }
}
```

### 4.9.5 Camera frame drop pattern and period

The option `camFrameDropPattern` and `camFrameDropPeriod` can be used to drop camera frames to reduce camera FPS.
Below is the frame drop pattern and period has been tested.

| FPS    | pattern | period |
|--------|---------|--------|
|   15   |   10    |   3    |
|   7.5  |   7     |   3    |
|   10   |   6     |   2    |


# 5. References
- [gtest Camera](../tests/unit_test/Node/Camera/gtest_NodeCamera.cpp).
- [Sample Camera](../tests/sample/source/SampleCamera.cpp).

# 6. Functional Safety

This section provides an overview of QCNode Camera usage for functional safety use cases.

## 6.1 ASIL

| Node  | ASIL (or equivalent) | Supported Platforms |
|-------|----------------------|---------------------|
| Camera   | ASIL B               |      SA8797         |


## 6.2 Assumptions of Use (SWAOU)
**SWAOU:** Software Assumption of Use.

### QCNODE-CAMERA-SWAOU-1

- **Assumption:**  
  The system integrator **should** ensure strict synchronization between the QCNode Camera header files and the underlying library versions during the build process to ensure binary compatibility and interface consistency.

- **Sample of "How AoU can be met?":**  
  Verify that the version of the QCNode Camera headers used during compilation matches the version of the camera server libraries present in the runtime environment. This can be achieved through build-time version checks or package dependency management.

- **SW AoU Rationale:**  
  Discrepancies between header definitions and library implementations can cause mismatches in data structure layouts, leading to memory corruption, segmentation faults, or incorrect parameter interpretation. Ensuring version synchronization guarantees that the client and server communicate using the same interface contract, preventing undefined behavior and ensuring system stability required for safety-critical applications.

### QCNODE-CAMERA-SWAOU-2

- **Assumption:**  
  The system integrator **should** increase the camera buffer pool size allocated to QCNode Camera to reduce the likelihood of buffer exhaustion.

- **Sample of "How AoU can be met?":**  
  Configure the camera stream with a sufficiently large number of buffers to accommodate peak traffic and consumer latency.

- **SW AoU Rationale:**  
  A larger buffer pool mitigates the risk of full buffer occupancy during short‑term burst traffic or temporary slowdowns in downstream consumers, thereby reducing the probability of frame blocking, frame drops, or delayed delivery.

### QCNODE-CAMERA-SWAOU-3

- **Assumption:**  
  The system integration ensures proper QCarCam driver and stream configuration, including validated combinations of resolution, frame rate, and ISP features that stay within the supported ISP performance envelope.

- **Sample of "How AoU can be met?":**  
  Validate the camera configuration against the hardware specifications and ISP bandwidth limits. Ensure that the sum of pixel rates and processing requirements for all active streams does not exceed the maximum supported capacity of the ISP.

- **SW AoU Rationale:**  
  Configuring the camera beyond the ISP's performance envelope can lead to frame drops, image artifacts, or system instability, which violates safety requirements for reliable video stream delivery.
