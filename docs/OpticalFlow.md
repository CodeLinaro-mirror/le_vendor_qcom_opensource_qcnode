*Menu*:
- [1. Overview](#1-overview)
    - [Key Features](#key-features)
- [2. Optical Flow Configuration](#2-optical-flow-configuration)
  - [2.1 Static JSON Configuration](#21-static-json-configuration)
  - [2.2 Dynamic JSON Configuration](#22-dynamic-json-configuration)
- [3. Optical Flow APIs](#3-optical-flow-apis)
  - [3.1 QCNode Optical Flow APIs](#31-qcnode-optical-flow-apis)
  - [3.2 QCNode Configuration Interfaces](#32-qcnode-configuration-interfaces)
- [4. Typical Optical Flow API Usage Examples](#4-typical-optical-flow-api-usage-examples)
  - [4.1 Basic Forward Optical Flow in Synchronous Mode](#41-basic-forward-optical-flow-in-synchronous-mode)
- [5. Functional Safety](#5-functional-safety)
  - [5.1 ASIL](#51-asil)
  - [5.2 Assumptions of Use (SWAOU)](#52-assumptions-of-use-swaou)
    - [QCNODE-OFL-SWAOU-1](#qcnode-ofl-swaou-1)
    - [QCNODE-OFL-SWAOU-2](#qcnode-ofl-swaou-2)
- [6. References](#6-references)


# 1. Overview

**QCNode Optical Flow** provides high-performance motion estimation capabilities, enabling users to compute dense motion vectors between consecutive video frames.

### Key Features

- **Multiple Motion Directions**
  Support for forward, backward, and bidirectional motion estimation.

- **Flexible Image Format Support**
  Process images in NV12 and NV12_UBWC formats with configurable resolutions.

- **Confidence Mapping**
  Optional confidence output to assess the reliability of motion vector estimates.

- **Configurable Accuracy Levels**
  Multiple computation accuracy modes (low, medium, high) for performance-quality trade-offs.

- **Motion Map Upscaling**
  Support for upscaling motion maps by 2x or 4x for higher resolution output.

- **Advanced Refinement**
  Refinement options for improved motion vector accuracy.

# 2. Optical Flow Configuration

## 2.1 Static JSON Configuration

| Parameter  | Required  | Type        | Description            |
|------------|-----------|-------------|------------------------|
| `name`     | true      | string      | The Node unique name.  |
| `id`       | true      | uint32_t    | The Node unique ID.    |
| `width`    | true      | uint32_t    | The width in pixels of the input frames. Must be greater than 0. |
| `height`   | true      | uint32_t    | The height in pixels of the input frames. Must be greater than 0. |
| `fps`      | false     | uint32_t    | Input frames per second. Must be greater than 0. <br> Default: `30` |
| `format`   | false     | string      | Input image format. <br> Options: `NV12`, `NV12_UBWC` <br> Default: `NV12` |
| `motionMapFormat` | false | uint8_t | Motion map output format. <br> Options: `0` (MOTION_FORMAT_12_LA) <br> Default: `0` |
| `motionMapFracEn` | false | bool | Enable fractional bits of motion vector values. <br> Default: `true` |
| `motionMapUpscale` | false | uint8_t | Upscaling of final motion vector output. <br> Options: `0` (NONE), `1` (UPSCALE_2), `2` (UPSCALE_4) <br> Default: `0` |
| `motionDirection` | false | uint8_t | Direction of local motion estimation. <br> Options: `0` (FORWARD), `1` (BACKWARD), `2` (BIDIRECTIONAL) <br> Default: `0` |
| `motionMapStepSize` | false | uint8_t | Sampling for motion map. <br> Options: `0` (STEP_1), `1` (STEP_2), `2` (STEP_4) <br> Default: `0` |
| `confidenceOutputEn` | false | bool | Enable confidence map output. <br> Default: `false` |
| `refinementLevel` | false | uint8_t | Refinement level for output motion map. <br> Options: `0` (NONE), `1` (REFINED_L1) <br> Default: `1` |
| `chromaProcEn` | false | bool | Enable chrominance processing. <br> Default: `false` |
| `maskLowTextureEn` | false | bool | Enable masking of low-texture regions. <br> Default: `false` |
| `computationAccuracy` | false | uint8_t | Underlying implementation to use. <br> Options: `0` (LOW), `1` (MEDIUM), `2` (HIGH) <br> Default: `1` |
| `noiseScaleSrc` | false | float32_t | Source image noise scaling factor. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `noiseScaleDst` | false | float32_t | Destination image noise scaling factor. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `noiseOffsetSrc` | false | float32_t | Source image noise offset. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `noiseOffsetDst` | false | float32_t | Destination image noise offset. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `motionVarianceTolerance` | false | float32_t | Motion variance tolerance. <br> Range: [0.0, 1.0] <br> Default: `0.36` |
| `occlusionTolerance` | false | float32_t | Occlusion tolerance. <br> Range: [0.0, 1.0] <br> Default: `0.5` |
| `edgePenalty` | false | float32_t | Edge penalty for motion calculation. <br> Range: [-1.0, 1.0] <br> Default: `0.08` |
| `initialPenalty` | false | float32_t | Initial penalty for motion propagation. <br> Range: [-1.0, 1.0] <br> Default: `0.19` |
| `neighborPenalty` | false | float32_t | Neighbor penalty for motion calculation. <br> Range: [-1.0, 1.0] <br> Default: `0.22` |
| `smoothnessPenalty` | false | float32_t | Smoothness constraint penalty. <br> Range: [-1.0, 1.0] <br> Default: `0.41` |
| `textureMetric` | false | uint32_t | Texture quality importance metric. <br> Range: [0, 100] <br> Default: `100` |
| `edgeAlignMetric` | false | uint32_t | Edge alignment importance metric. <br> Range: [0, 100] <br> Default: `100` |
| `motionVarianceMetric` | false | uint32_t | Motion variance importance metric. <br> Range: [0, 100] <br> Default: `100` |
| `occlusionMetric` | false | uint32_t | Occlusion consistency importance metric. <br> Range: [0, 100] <br> Default: `100` |
| `segmentationThreshold` | false | float32_t | Segmentation bias threshold. <br> Range: [0.0, 1.0] <br> Default: `0.5` |
| `imageSharpnessThreshold` | false | float32_t | Image sharpness threshold for edge detection. <br> Range: [0.0, 1.0] <br> Default: `0.4` (or `0.0` if `chromaProcEn` is `true`) |
| `mvEdgeThreshold` | false | float32_t | Motion vector edge selection threshold. <br> Range: [0.0, 1.0] <br> Default: `0.43` |
| `refinementThreshold` | false | float32_t | Refinement threshold for motion vectors. <br> Range: [0.0, 1.0] <br> Default: `0.75` |
| `textureThreshold` | false | float32_t | Texture threshold for low-texture region detection. <br> Range: [0.0, 1.0] <br> Default: `0.167` |
| `lightingCondition` | false | uint8_t | Lighting condition for automotive platform. <br> Options: `0` (LOW), `1` (HIGH) <br> Default: `1` |
| `isFirstRequest` | false | bool | Indicates whether this is the first request. <br> Default: `false` |
| `requestId` | false | uint64_t | Unique identifier for the request. <br> Default: `0` |


- Example Configurations
  - Basic Forward Optical Flow with NV12 Format
    ```json
    {
      "static": {
        "name": "OFL0",
        "id": 0,
        "width": 1920,
        "height": 1024,
        "fps": 30,
        "format": "NV12",
        "confidenceOutputEn": true,
        "motionDirection": 0,
        "computationAccuracy": 1,
        "isFirstRequest": true
      }
    }
    ```
    Refer to [OFL L0_NV12_FWD](../tests/unit_test/Node/OpticalFlow/gtest_OpticalFlow.cpp#L373) for more details.

  - Backward Optical Flow with UBWC Format
    ```json
    {
      "static": {
        "name": "OFL0",
        "id": 0,
        "width": 1920,
        "height": 1024,
        "fps": 30,
        "format": "NV12_UBWC",
        "confidenceOutputEn": true,
        "motionDirection": 1,
        "computationAccuracy": 1,
        "isFirstRequest": true
      }
    }
    ```
    Refer to [OFL L0_NV12_UBWC_BWD](../tests/unit_test/Node/OpticalFlow/gtest_OpticalFlow.cpp#L430) for more details.

  - Bidirectional Optical Flow
    ```json
    {
      "static": {
        "name": "OFL0",
        "id": 0,
        "width": 1920,
        "height": 1024,
        "fps": 30,
        "format": "NV12",
        "confidenceOutputEn": true,
        "motionDirection": 2,
        "computationAccuracy": 1,
        "isFirstRequest": true
      }
    }
    ```
    Refer to [OFL L0_NV12_BID](../tests/unit_test/Node/OpticalFlow/gtest_OpticalFlow.cpp#L450) for more details.

## 2.2 Dynamic JSON Configuration

Dynamic configuration is currently not supported for Optical Flow. All configuration parameters must be set during initialization through the static configuration.

# 3. Optical Flow APIs

## 3.1 QCNode Optical Flow APIs

- [OpticalFlow::Initialize](../include/QC/Node/OpticalFlow.hpp#L321) Initialize Optical Flow node

- [OpticalFlow::GetConfigurationIfs](../include/QC/Node/OpticalFlow.hpp#L327) Get Optical Flow configuration interfaces

- [OpticalFlow::GetMonitoringIfs](../include/QC/Node/OpticalFlow.hpp#L333) Get Optical Flow monitoring interfaces

- [OpticalFlow::Start](../include/QC/Node/OpticalFlow.hpp#L339) Start the Optical Flow node

- [OpticalFlow::ProcessFrameDescriptor](../include/QC/Node/OpticalFlow.hpp#L362) Execute optical flow estimation with input frame pairs and output motion vectors/confidence maps

- [OpticalFlow::Stop](../include/QC/Node/OpticalFlow.hpp#L368) Stop the Optical Flow node

- [OpticalFlow::DeInitialize](../include/QC/Node/OpticalFlow.hpp#L374) Deinitialize the Optical Flow node

- [OpticalFlow::GetState](../include/QC/Node/OpticalFlow.hpp#L380) Get the current state of the Optical Flow node

## 3.2 QCNode Configuration Interfaces

- [OpticalFlowConfigIfs::GetOptions](../include/QC/Node/OpticalFlow.hpp#L279) Get Configuration Options
  - Use this API to query the version information of the Optical Flow node.
    - Below is an example output:
      ```json
      {
        "version": 131072
      }
      ```
      The version is encoded as: `(MAJOR << 16) | (MINOR << 8) | PATCH`
      The current version is **2.0.0** (`(2 << 16) | (0 << 8) | 0 = 131072`).

# 4. Typical Optical Flow API Usage Examples

## 4.1 Basic Forward Optical Flow in Synchronous Mode

```c++
// Include the QCNode Optical Flow header files
#include "QC/Node/OpticalFlow.hpp"
using namespace QC::Node;

// Use BufferManager for buffer allocations (matches the gtest approach)
#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

class MyOpticalFlowApp {
private:
  OpticalFlow m_opticalFlow; // Create an Optical Flow node.
  BufferManager m_bufMgr{ { "LME", QC_NODE_TYPE_EVA_OPTICAL_FLOW, 0 } }; // Buffer manager
  ImageDescriptor_t m_referenceImgDesc;
  ImageDescriptor_t m_currentImgDesc;
  TensorDescriptor_t m_fwdMotionMapDesc;
  TensorDescriptor_t m_fwdConfidenceMapDesc;
  NodeFrameDescriptor *m_frameDesc = nullptr;

public:
  void Init() {
    // Initialize the Optical Flow node.
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "OFL0",
          "id": 0,
          "width": 1920,
          "height": 1024,
          "fps": 30,
          "format": "NV12",
          "confidenceOutputEn": true,
          "motionDirection": 0,
          "computationAccuracy": 1,
          "isFirstRequest": true
        }
      })"
    };

    QCStatus_e status = m_opticalFlow.Initialize(config);

    // Allocate reference (previous) image buffer
    ImageBasicProps_t refImgProp;
    refImgProp.format = QC_IMAGE_FORMAT_NV12;
    refImgProp.batchSize = 1;
    refImgProp.width = 1920;
    refImgProp.height = 1024;
    status = m_bufMgr.Allocate( refImgProp, m_referenceImgDesc );

    // Allocate current image buffer
    ImageBasicProps_t curImgProp;
    curImgProp.format = QC_IMAGE_FORMAT_NV12;
    curImgProp.batchSize = 1;
    curImgProp.width = 1920;
    curImgProp.height = 1024;
    status = m_bufMgr.Allocate( curImgProp, m_currentImgDesc );

    // Allocate forward motion map output buffer
    // Motion map has 2 channels (x, y) per pixel
    TensorProps_t fwdMotionMapProp = {
      QC_TENSOR_TYPE_UINT_16,
      { 1, 1024, 1920 * 2, 1 }
    };
    status = m_bufMgr.Allocate( fwdMotionMapProp, m_fwdMotionMapDesc );

    // Allocate forward confidence map output buffer
    TensorProps_t fwdConfMapProp = {
      QC_TENSOR_TYPE_UINT_8,
      { 1, 1024, 1920, 1 }
    };
    status = m_bufMgr.Allocate( fwdConfMapProp, m_fwdConfidenceMapDesc );

    // Create frame descriptor and set buffers
    m_frameDesc = new NodeFrameDescriptor( QC_NODE_OF_LAST_BUFF_ID );
    (void)m_frameDesc->SetBuffer( QC_NODE_OF_REFERENCE_IMAGE_BUFF_ID, m_referenceImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_OF_CURRENT_IMAGE_BUFF_ID, m_currentImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_OF_FWD_MOTION_BUFF_ID, m_fwdMotionMapDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_OF_FWD_CONF_BUFF_ID, m_fwdConfidenceMapDesc );

    // Start the Optical Flow node.
    status = m_opticalFlow.Start();
  }

  void Run() {
    // Process the frame descriptor
    QCStatus_e status = m_opticalFlow.ProcessFrameDescriptor( *m_frameDesc );
    (void)status;
  }

  void Deinit() {
    // Stop the Optical Flow node.
    (void)m_opticalFlow.Stop();

    // Deinitialize the Optical Flow node.
    (void)m_opticalFlow.DeInitialize();

    // Free buffers and delete frame descriptor
    m_bufMgr.Free( m_referenceImgDesc );
    m_bufMgr.Free( m_currentImgDesc );
    m_bufMgr.Free( m_fwdMotionMapDesc );
    m_bufMgr.Free( m_fwdConfidenceMapDesc );
    delete m_frameDesc;
    m_frameDesc = nullptr;
  }
};
```

# 5. Functional Safety

This section provides an overview of QCNode Optical Flow usage for functional safety use cases.

## 5.1 ASIL

| Node         | ASIL (or equivalent) | Supported Platforms |
|--------------|----------------------|---------------------|
| OpticalFlow  | ASIL B               |      SA8797         |

## 5.2 Assumptions of Use (SWAOU)
**SWAOU:** Software Assumption of Use.

### QCNODE-OFL-SWAOU-1

- **Assumption:**  
  All SV Auto libraries and artifacts must be obtained from a single SDK version.

- **Sample of "How AoU can be met?":**  
  Enforced by system integration/configuration management and verified during integration/release packaging.

- **SW AoU Rationale:**  
  Avoids incompatible library/artifact combinations that can lead to incorrect outputs, runtime errors, or initialization failures due to ABI/API mismatches—i.e., prevents a systematic integration fault from cascading into QCNode EVA misbehavior.

### QCNODE-OFL-SWAOU-2

- **Assumption:**  
  The system integrator shall implement a timeout mechanism when invoking blocking QCNode APIs, including `Initialize`, `Start`, `Stop`, `DeInitialize`, and `ProcessFrameDescriptor`, to prevent indefinite blocking in the event of a failure.

- **Sample of "How AoU can be met?":**  
  Implemented at the integration layer (caller/application/framework) and validated with fault-injection / negative testing (e.g., induced SDK non-response).

- **SW AoU Rationale:**  
  Prevents QCNode EVA init/execute/deinit from becoming stuck (deadlock/livelock/indefinite wait) and causing system-level timing/resource starvation. Ensures the caller can regain control and transition the system to a safe state (abort/retry/reset) if a dependent component or underlying execution hangs.


# 6. References

- [OpticalFlow Header](../include/QC/Node/OpticalFlow.hpp)
- [OpticalFlow Implementation](../source/Node/OpticalFlow/OpticalFlow.cpp)
- [gtest OpticalFlow](../tests/unit_test/Node/OpticalFlow/gtest_OpticalFlow.cpp)
