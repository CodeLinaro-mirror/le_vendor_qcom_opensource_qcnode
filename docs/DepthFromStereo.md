*Menu*:
- [1. Overview](#1-overview)
    - [Key Features](#key-features)
- [2. Depth From Stereo Configuration](#2-depth-from-stereo-configuration)
  - [2.1 Static JSON Configuration](#21-static-json-configuration)
  - [2.2 Dynamic JSON Configuration](#22-dynamic-json-configuration)
- [3. Depth From Stereo APIs](#3-depth-from-stereo-apis)
  - [3.1 QCNode Depth From Stereo APIs](#31-qcnode-depth-from-stereo-apis)
  - [3.2 QCNode Configuration Interfaces](#32-qcnode-configuration-interfaces)
- [4. Typical Depth From Stereo API Usage Examples](#4-typical-depth-from-stereo-api-usage-examples)
  - [4.1 Basic Stereo Depth Estimation in Synchronous Mode](#41-basic-stereo-depth-estimation-in-synchronous-mode)
  - [4.2 Stereo Depth Estimation with Confidence Output](#42-stereo-depth-estimation-with-confidence-output)
- [5. Functional Safety](#5-functional-safety)
  - [5.1 ASIL](#51-asil)
  - [5.2 Assumptions of Use (SWAOU)](#52-assumptions-of-use-swaou)
    - [QCNODE-DFS-SWAOU-1](#qcnode-dfs-swaou-1)
    - [QCNODE-DFS-SWAOU-2](#qcnode-dfs-swaou-2)
- [6. References](#6-references)


# 1. Overview

**QCNode Depth From Stereo** provides high-performance stereo vision depth estimation capabilities, enabling users to compute disparity maps from stereo image pairs.

### Key Features

- **Multiple Processing Modes**
  Support for automatic mode selection, deep learning-based, and Semi-Global Matching (SGM) algorithms.

- **Flexible Image Format Support**
  Process stereo images in NV12 and NV12_UBWC formats with configurable resolutions.

- **Confidence Mapping**
  Optional confidence output to assess the reliability of disparity estimates.

- **Advanced Refinement**
  Multi-level refinement options (L1, L2) for improved disparity accuracy.

- **Configurable Search Direction**
  Support for both left-to-right (L2R) and right-to-left (R2L) stereo matching.

- **Precision Control**
  Adjustable disparity map precision with fractional bit options (6-bit, 4-bit, or integer).

# 2. Depth From Stereo Configuration

## 2.1 Static JSON Configuration

| Parameter  | Required  | Type        | Description            |
|------------|-----------|-------------|------------------------|
| `name`     | true      | string      | The Node unique name.  |
| `id`       | true      | uint32_t    | The Node unique ID.    |
| `width`    | true      | uint32_t    | The width in pixels of the input stereo frames. Must be greater than 0. |
| `height`   | true      | uint32_t    | The height in pixels of the input stereo frames. Must be greater than 0. |
| `fps`      | false     | uint32_t    | Input frames per second. Must be greater than 0. <br> Default: `30` |
| `format`   | false     | string      | Input image format. <br> Options: `NV12`, `NV12_UBWC` <br> Default: `NV12` |
| `disparityFormat` | false | uint8_t | Disparity map output format. <br> Options: `0` (P012_LA_Y_ONLY) <br> Default: `0` |
| `confidenceOutputEn` | false | bool | Enable confidence map output. <br> Default: `false` |
| `processingMode` | false | uint8_t | Processing mode selector. <br> Options: `0` (AUTO), `1` (DL), `2` (SGM) <br> Default: `0` |
| `noiseOffsetPrimary` | false | float32_t | Primary camera noise offset. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `noiseOffsetAux` | false | float32_t | Auxiliary camera noise offset. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `modelType` | false | uint8_t | Model type for disparity computation. <br> Range: [0, 4] <br> Default: `1` |
| `modelSwitchFrameCount` | false | uint8_t | Minimum frames before model switch. <br> Default: `10` |
| `prevDisparityFactor` | false | float32_t | Previous disparity factor for temporal smoothing. <br> Range: [0.5, 2.0] <br> Default: `1.0` |
| `disparityMapPrecision` | false | uint8_t | Precision level for disparity map. <br> Options: `0` (FRAC_6BIT), `1` (FRAC_4BIT), `2` (INT) <br> Default: `0` |
| `refinementLevel` | false | uint8_t | Refinement level applied during disparity estimation. <br> Options: `0` (NONE), `1` (REFINED_L1), `2` (REFINED_L2) <br> Default: `2` |
| `occlusionOutputEn` | false | bool | Enable occlusion detection output. <br> Default: `false` |
| `disparityStatsEn` | false | bool | Enable disparity statistics tracking. <br> Default: `false` |
| `rectificationErrorStatsEn` | false | bool | Enable rectification error statistics. <br> Default: `false` |
| `chromaProcEN` | false | bool | Enable chrominance processing. <br> Default: `false` |
| `maskLowTextureEn` | false | bool | Enable masking of low-texture regions. <br> Default: `false` |
| `confidenceThreshold` | false | uint32_t | Minimum confidence threshold. <br> Range: [0, 255] <br> Default: `210` |
| `disparityThreshold` | false | uint32_t | Maximum disparity threshold. <br> Range: [0, 255] <br> Default: `64` |
| `noiseScalePrimary` | false | float32_t | Primary camera noise scaling factor. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `noiseScaleAux` | false | float32_t | Auxiliary camera noise scaling factor. <br> Range: [-1.0, 1.0] <br> Default: `0.0` |
| `rectificationErrTolerance` | false | float32_t | Rectification error tolerance. <br> Range: [0.0, 1.0] <br> Default: `0.29` |
| `segmentationThreshold` | false | float32_t | Segmentation threshold for region detection. <br> Range: [0.0, 1.0] <br> Default: `0.5` |
| `textureThreshold` | false | float32_t | Texture threshold for feature detection. <br> Range: [0.0, 1.0] <br> Default: `0.167` |
| `searchDirection` | false | uint8_t | Search direction for stereo matching. <br> Options: `0` (L2R), `1` (R2L) <br> Default: `0` |
| `edgePenalty` | false | float32_t | Penalty applied to edges during matching. <br> Range: [0.0, 1.0] <br> Default: `0.08` |
| `initialPenalty` | false | float32_t | Initial penalty for disparity propagation. <br> Range: [0.0, 1.0] <br> Default: `0.19` |
| `neighbourPenalty` | false | float32_t | Penalty based on neighboring disparities. <br> Range: [0.0, 1.0] <br> Default: `0.22` |
| `smoothnessPenalty` | false | float32_t | Smoothness constraint penalty. <br> Range: [0.0, 1.0] <br> Default: `0.41` |
| `imageSharpnessThreshold` | false | float32_t | Threshold for image sharpness checks. <br> Range: [0.0, 1.0] <br> Default: `0.0` |
| `farAwayDisparityLimit` | false | float32_t | Maximum disparity allowed for distant objects. <br> Range: [0.0, 1.0] <br> Default: `0.0` |
| `disparityEdgeThreshold` | false | float32_t | Threshold for disparity edge detection. <br> Range: [0.0, 1.0] <br> Default: `0.43` |
| `matchingCostMetric` | false | uint32_t | Cost metric used in matching algorithm. <br> Range: [0, 100] <br> Default: `100` |
| `textureMetric` | false | uint32_t | Texture-based cost metric. <br> Range: [0, 100] <br> Default: `100` |
| `edgeAlignMetric` | false | uint32_t | Edge alignment metric used in SGM. <br> Range: [0, 100] <br> Default: `100` |
| `disparityVarianceMetric` | false | uint32_t | Variance metric for disparity consistency. <br> Range: [0, 100] <br> Default: `100` |
| `occlusionMetric` | false | uint32_t | Occlusion detection metric. <br> Range: [0, 100] <br> Default: `100` |
| `disparityVarianceTolerance` | false | float32_t | Tolerance for disparity variance checks. <br> Range: [0.0, 1.0] <br> Default: `0.36` |
| `occlusionTolerance` | false | float32_t | Tolerance for occlusion detection. <br> Range: [0.0, 1.0] <br> Default: `0.5` |
| `refinementThreshold` | false | float32_t | Threshold for refinement steps. <br> Range: [0.0, 1.0] <br> Default: `0.75` |
| `maxDisparityRange` | false | uint32_t | Maximum disparity range supported. <br> Range: [16, 92] <br> Default: `64` |


- Example Configurations
  - Basic Stereo Depth Estimation with NV12 Format
    ```json
    {
      "static": {
        "name": "DFS0",
        "id": 0,
        "width": 1280,
        "height": 416,
        "fps": 30,
        "format": "NV12",
        "confidenceOutputEn": true,
        "searchDirection": 0
      }
    }
    ```
    Refer to [DFS L0_SANITY_NV12_L2R](../tests/unit_test/Node/DepthFromStereo/gtest_DepthFromStereo.cpp#L234) for more details.

  - Stereo Depth Estimation with UBWC Format
    ```json
    {
      "static": {
        "name": "DFS0",
        "id": 0,
        "width": 1280,
        "height": 416,
        "fps": 30,
        "format": "NV12_UBWC",
        "confidenceOutputEn": true,
        "searchDirection": 0
      }
    }
    ```
    Refer to [DFS L0_SANITY_NV12_UBWC_L2R](../tests/unit_test/Node/DepthFromStereo/gtest_DepthFromStereo.cpp#L248) for more details.

  - Right-to-Left Stereo Matching
    ```json
    {
      "static": {
        "name": "DFS0",
        "id": 0,
        "width": 1280,
        "height": 416,
        "fps": 30,
        "format": "NV12",
        "confidenceOutputEn": true,
        "searchDirection": 1
      }
    }
    ```
    Refer to [DFS L0_SANITY_NV12_R2L](../tests/unit_test/Node/DepthFromStereo/gtest_DepthFromStereo.cpp#L263) for more details.

## 2.2 Dynamic JSON Configuration

Dynamic configuration is currently not supported for Depth From Stereo. All configuration parameters must be set during initialization through the static configuration.

# 3. Depth From Stereo APIs

## 3.1 QCNode Depth From Stereo APIs

- [DepthFromStereo::Initialize](../include/QC/Node/DepthFromStereo.hpp#L324) Initialize Depth From Stereo node

- [DepthFromStereo::GetConfigurationIfs](../include/QC/Node/DepthFromStereo.hpp#L330) Get Depth From Stereo configuration interfaces

- [DepthFromStereo::GetMonitoringIfs](../include/QC/Node/DepthFromStereo.hpp#L336) Get Depth From Stereo monitoring interfaces

- [DepthFromStereo::Start](../include/QC/Node/DepthFromStereo.hpp#L342) Start the Depth From Stereo node

- [DepthFromStereo::ProcessFrameDescriptor](../include/QC/Node/DepthFromStereo.hpp#L344) Execute stereo depth estimation with input stereo images and output disparity/confidence maps

- [DepthFromStereo::Stop](../include/QC/Node/DepthFromStereo.hpp#L350) Stop the Depth From Stereo node

- [DepthFromStereo::DeInitialize](../include/QC/Node/DepthFromStereo.hpp#L356) Deinitialize the Depth From Stereo node

- [DepthFromStereo::GetState](../include/QC/Node/DepthFromStereo.hpp#L362) Get the current state of the Depth From Stereo node

## 3.2 QCNode Configuration Interfaces

- [DepthFromStereoConfigIfs::GetOptions](../include/QC/Node/DepthFromStereo.hpp#L272) Get Configuration Options
  - Use this API to query the version information of the Depth From Stereo node.
    - Below is an example output:
      ```json
      {
        "version": 131072
      }
      ```
      The version is encoded as: `(MAJOR << 16) | (MINOR << 8) | PATCH`
      The current version is **2.0.0** (`(2 << 16) | (0 << 8) | 0 = 131072`).

# 4. Typical Depth From Stereo API Usage Examples

## 4.1 Basic Stereo Depth Estimation in Synchronous Mode

```c++
// Include the QCNode Depth From Stereo header files
#include "QC/Node/DepthFromStereo.hpp"
using namespace QC::Node;

// Use BufferManager to allocate and free buffers (matches the gtest approach)
#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

class MyDfsApp {
private:
  DepthFromStereo m_dfs; // Create a Depth From Stereo node.
  BufferManager m_bufMgr{ { "DFS", QC_NODE_TYPE_EVA_DFS, 0 } };
  ImageDescriptor_t m_primaryImgDesc;
  ImageDescriptor_t m_auxiliaryImgDesc;
  TensorDescriptor_t m_disparityMapDesc;
  NodeFrameDescriptor *m_frameDesc = nullptr;

public:
  void Init() {
    // Initialize the Depth From Stereo node.
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "DFS0",
          "id": 0,
          "width": 1280,
          "height": 416,
          "fps": 30,
          "format": "NV12",
          "searchDirection": 0
        }
      })"
    };

    QCStatus_e status = m_dfs.Initialize(config);

    // Allocate primary (left) image buffer
    ImageBasicProps_t priImgProp;
    priImgProp.format = QC_IMAGE_FORMAT_NV12;
    priImgProp.batchSize = 1;
    priImgProp.width = 1280;
    priImgProp.height = 416;
    status = m_bufMgr.Allocate( priImgProp, m_primaryImgDesc );

    // Allocate auxiliary (right) image buffer
    ImageBasicProps_t auxImgProp;
    auxImgProp.format = QC_IMAGE_FORMAT_NV12;
    auxImgProp.batchSize = 1;
    auxImgProp.width = 1280;
    auxImgProp.height = 416;
    status = m_bufMgr.Allocate( auxImgProp, m_auxiliaryImgDesc );

    // Allocate disparity map output buffer
    TensorProps_t dispMapProp = {
      QC_TENSOR_TYPE_UINT_16,
      { 1, 416, 1280, 1 }
    };
    status = m_bufMgr.Allocate( dispMapProp, m_disparityMapDesc );

    // Create frame descriptor and set buffers
    m_frameDesc = new NodeFrameDescriptor( QC_NODE_DFS_LAST_BUFF_ID );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_PRIMARY_IMAGE_BUFF_ID, m_primaryImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_AUXILARY_IMAGE_BUFF_ID, m_auxiliaryImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_DISPARITY_MAP_BUFF_ID, m_disparityMapDesc );

    // Start the Depth From Stereo node
    status = m_dfs.Start();
  }

  void Run() {
    // Process the frame descriptor
    QCStatus_e status = m_dfs.ProcessFrameDescriptor( *m_frameDesc );
    (void)status;
  }

  void Deinit() {
    // Stop and deinitialize the node
    (void)m_dfs.Stop();
    (void)m_dfs.DeInitialize();

    // Free buffers and delete frame descriptor
    m_bufMgr.Free( m_primaryImgDesc );
    m_bufMgr.Free( m_auxiliaryImgDesc );
    m_bufMgr.Free( m_disparityMapDesc );
    delete m_frameDesc;
    m_frameDesc = nullptr;
  }
};
```

## 4.2 Stereo Depth Estimation with Confidence Output

When `confidenceOutputEn` is set to `true`, a confidence map buffer must also be provided in the frame descriptor using `QC_NODE_DFS_DISPARITY_CONFIDANCE_MAP_BUFF_ID`. The confidence map is a `uint8_t` tensor of the same spatial dimensions as the disparity map, where each value indicates the reliability of the corresponding disparity estimate.

```c++
// Include the QCNode Depth From Stereo header files
#include "QC/Node/DepthFromStereo.hpp"
using namespace QC::Node;

// Use BufferManager to allocate and free buffers
#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

class MyDfsConfApp {
private:
  DepthFromStereo m_dfs;
  BufferManager m_bufMgr{ { "DFS", QC_NODE_TYPE_EVA_DFS, 0 } };
  ImageDescriptor_t m_primaryImgDesc;
  ImageDescriptor_t m_auxiliaryImgDesc;
  TensorDescriptor_t m_disparityMapDesc;
  TensorDescriptor_t m_confidenceMapDesc;
  NodeFrameDescriptor *m_frameDesc = nullptr;

public:
  void Init() {
    // Initialize with confidence output enabled
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "DFS0",
          "id": 0,
          "width": 1280,
          "height": 416,
          "fps": 30,
          "format": "NV12",
          "confidenceOutputEn": true,
          "searchDirection": 0
        }
      })"
    };

    QCStatus_e status = m_dfs.Initialize(config);

    // Allocate primary (left) image buffer
    ImageBasicProps_t priImgProp;
    priImgProp.format = QC_IMAGE_FORMAT_NV12;
    priImgProp.batchSize = 1;
    priImgProp.width = 1280;
    priImgProp.height = 416;
    status = m_bufMgr.Allocate( priImgProp, m_primaryImgDesc );

    // Allocate auxiliary (right) image buffer
    ImageBasicProps_t auxImgProp;
    auxImgProp.format = QC_IMAGE_FORMAT_NV12;
    auxImgProp.batchSize = 1;
    auxImgProp.width = 1280;
    auxImgProp.height = 416;
    status = m_bufMgr.Allocate( auxImgProp, m_auxiliaryImgDesc );

    // Allocate disparity map output buffer (uint16)
    TensorProps_t dispMapProp = {
      QC_TENSOR_TYPE_UINT_16,
      { 1, 416, 1280, 1 }
    };
    status = m_bufMgr.Allocate( dispMapProp, m_disparityMapDesc );

    // Allocate confidence map output buffer (uint8)
    TensorProps_t confMapProp = {
      QC_TENSOR_TYPE_UINT_8,
      { 1, 416, 1280, 1 }
    };
    status = m_bufMgr.Allocate( confMapProp, m_confidenceMapDesc );

    // Create frame descriptor and set all buffers including confidence map
    m_frameDesc = new NodeFrameDescriptor( QC_NODE_DFS_LAST_BUFF_ID );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_PRIMARY_IMAGE_BUFF_ID, m_primaryImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_AUXILARY_IMAGE_BUFF_ID, m_auxiliaryImgDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_DISPARITY_MAP_BUFF_ID, m_disparityMapDesc );
    (void)m_frameDesc->SetBuffer( QC_NODE_DFS_DISPARITY_CONFIDANCE_MAP_BUFF_ID, m_confidenceMapDesc );

    // Start the Depth From Stereo node
    status = m_dfs.Start();
  }

  void Run() {
    // Process the frame descriptor; confidence map is populated alongside disparity map
    QCStatus_e status = m_dfs.ProcessFrameDescriptor( *m_frameDesc );
    (void)status;
  }

  void Deinit() {
    (void)m_dfs.Stop();
    (void)m_dfs.DeInitialize();

    m_bufMgr.Free( m_primaryImgDesc );
    m_bufMgr.Free( m_auxiliaryImgDesc );
    m_bufMgr.Free( m_disparityMapDesc );
    m_bufMgr.Free( m_confidenceMapDesc );
    delete m_frameDesc;
    m_frameDesc = nullptr;
  }
};
```

# 5. Functional Safety

This section provides an overview of QCNode Depth From Stereo usage for functional safety use cases.

## 5.1 ASIL

| Node             | ASIL (or equivalent) | Supported Platforms |
|------------------|----------------------|---------------------|
| DepthFromStereo  | ASIL B               |      SA8797         |

## 5.2 Assumptions of Use (SWAOU)
**SWAOU:** Software Assumption of Use.

### QCNODE-DFS-SWAOU-1

- **Assumption:**  
  All SV Auto libraries and artifacts must be obtained from a single SDK version.

- **Sample of "How AoU can be met?":**  
  Enforced by system integration/configuration management and verified during integration/release packaging.

- **SW AoU Rationale:**  
  Avoids incompatible library/artifact combinations that can lead to incorrect outputs, runtime errors, or initialization failures due to ABI/API mismatches—i.e., prevents a systematic integration fault from cascading into QCNode EVA misbehavior.

### QCNODE-DFS-SWAOU-2

- **Assumption:**  
  The system integrator shall implement a timeout mechanism when invoking blocking QCNode APIs, including `Initialize`, `Start`, `Stop`, `DeInitialize`, and `ProcessFrameDescriptor`, to prevent indefinite blocking in the event of a failure.

- **Sample of "How AoU can be met?":**  
  Implemented at the integration layer (caller/application/framework) and validated with fault-injection / negative testing (e.g., induced SDK non-response).

- **SW AoU Rationale:**  
  Prevents QCNode EVA init/execute/deinit from becoming stuck (deadlock/livelock/indefinite wait) and causing system-level timing/resource starvation. Ensures the caller can regain control and transition the system to a safe state (abort/retry/reset) if a dependent component or underlying execution hangs.


# 6. References

- [DepthFromStereo Header](../include/QC/Node/DepthFromStereo.hpp)
- [DepthFromStereo Implementation](../source/Node/DepthFromStereo/DepthFromStereo.cpp)
- [gtest DepthFromStereo](../tests/unit_test/Node/DepthFromStereo/gtest_DepthFromStereo.cpp)
