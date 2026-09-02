*Menu*:
- [1. Overview](#1-overview)
    - [Key Features](#key-features)
- [2. QNN Configuration](#2-qnn-configuration)
  - [2.1 QNN Static JSON Configuration](#21-qnn-static-json-configuration)
  - [2.2 QNN Dynamic JSON Configuration](#22-qnn-dynamic-json-configuration)
- [3. QNN APIs](#3-qnn-apis)
  - [3.1 QCNode QNN APIS](#31-qcnode-qnn-apis)
  - [3.2 QCNode Configuration Interfaces](#32-qcnode-configuration-interfaces)
- [4. Typical QNN API Usage Examples](#4-typical-qnn-api-usage-examples)
  - [4.1 Load from Context Binary File and Run in Synchronous Mode](#41-load-from-context-binary-file-and-run-in-synchronous-mode)
  - [4.2 Load from Context Binary File and Run in Asynchronous Mode](#42-load-from-context-binary-file-and-run-in-asynchronous-mode)
  - [4.3 Dynamic Batch Size](#43-dynamic-batch-size)
- [5. References](#5-references)
- [6. Limitations](#6-limitations)
  - [6.1 QNN system context graph information versions supported](#61-qnn-system-context-graph-information-versions-supported)
  - [6.2 QNN system context binary information versions supported](#62-qnn-system-context-binary-information-versions-supported)
  - [6.3 QNN device platform information versions supported](#63-qnn-device-platform-information-versions-supported)
- [7. Functional Safety](#7-functional-safety)
  - [7.1 ASIL](#71-asil)
  - [7.2 Assumptions of Use (SWAOU)](#72-assumptions-of-use-swaou)
    - [QCNODE-QNN-SWAOU-1](#qcnode-qnn-swaou-1)
    - [QCNODE-QNN-SWAOU-2](#qcnode-qnn-swaou-2)
    - [QCNODE-QNN-SWAOU-3](#qcnode-qnn-swaou-3)
    - [QCNODE-QNN-SWAOU-4](#qcnode-qnn-swaou-4)


# 1. Overview

**QCNode QNN** is a high-performance wrapper around the Qualcomm Neural Processing SDK (QNN SDK). It provides a simplified and user-friendly C++ API that abstracts the complexity of the underlying QNN runtime. Designed for efficiency and ease of use, QCNode QNN enables developers to seamlessly load, configure, and execute deep learning models across diverse hardware backends, including the Hexagon Tensor Processor (HTP), GPU, and CPU. It handles low-level details such as memory registration, backend initialization, and graph execution, allowing users to focus on application logic.

### Key Features

- **Flexible Model Loading**
  Supports multiple methods for loading QNN models to suit different deployment scenarios. Users can load models from serialized context binaries (stored as files or memory buffers) or shared libraries. Loading from a memory buffer is particularly useful for **IP protection**: users can store models in an encrypted format, decrypt them into a secure memory buffer at runtime, and then load the model directly from that buffer, ensuring the raw model is never exposed on the filesystem.

- **Comprehensive Tensor Information**
  Provides intuitive APIs to inspect the loaded model's structure. Users can easily retrieve detailed metadata for input and output tensors, including dimensions, data types, quantization parameters (scale/offset), and names, facilitating dynamic buffer management.

- **Performance Insights**
  Integrated performance monitoring capabilities allow users to access granular runtime metrics. This includes execution time breakdowns (e.g., accelerator vs. RPC overhead), aiding in identifying bottlenecks and optimizing application performance.

- **Zero-Copy Support**
  Optimized for high-throughput, low-latency applications. QCNode QNN supports zero-copy buffer sharing, enabling efficient data transfer between the application and the QNN backend without unnecessary memory copies, which is critical for real-time inference.

# 2. QNN Configuration

## 2.1 QNN Static JSON Configuration

| Parameter  | Required  | Type        | Description            |
|------------|-----------|-------------|------------------------|
| `name`     | true      | string      | The Node unique name.  |
| `id`       | true      | uint32_t    | The Node unique ID.    |
| `logLevel` | false     | string      | The message log level. <br> Options: `VERBOSE`, `DEBUG`, `INFO`, `WARN`, `ERROR` <br> Default: `ERROR`   |
| `processorType` | false | string     | The processor type. <br> Options: `htp0`, `htp1`, `htp2`, `htp3`, `cpu`, `gpu` <br> Default: `htp0` |
| `coreIds`  | false     | uint32_t[]  | A list of core IDs. <br> Default: `[0]` |
| `loadType` | false     | string      | The load type. <br> Options: `binary`, `library`, `buffer` <br> Default: `binary` |
| `modelPath` | depends  | string      | The QNN model file path (required if `loadType` is `binary` or `library`) |
| `contextBufferId` | depends | uint32_t | Context buffer index in `QCNodeInit::buffers` (required if `loadType` is `buffer`) |
| `bufferIds` | false    | uint32_t[]  | List of buffer indices in `QCNodeInit::buffers`  |
| `priority`  | false    | string      | QNN model scheduling priority. <br> Options: `low`, `normal`, `normal_high`, `high` <br> Default: `normal`           |
| `udoPackages` | depends | object[]    | List of UDO packages. <br> Each object contains:<br> - `udoLibPath` (string)<br> - `interfaceProvider` (string) |
| `globalBufferIdMap` | false | object[] | Mapping of buffer names to buffer indices in `QCFrameDescriptorNodeIfs`. <br>Each object contains:<br> - `name` (string)<br> - `id` (uint32_t)   |
| `deRegisterAllBuffersWhenStop` | false | bool     | Flag to deregister all buffers when stopped      <br>Default: `false` |
| `perfProfile` | false | string     | Specifies perf profile to set. <br> Options: `low_balanced`, `balanced`, `default`, `high_performance`, `sustained_high_performance`, `burst`, `low_power_saver`, `power_saver`, `high_power_saver`, `extreme_power_saver` <br> Default: `default` |
| `weightSharingEnabled` | false | bool     | Enables weight sharing. <br> Default: `false` |
| `extendedUdma` | false | bool     | Activates extended UDMA support. <br> Default: `false` |


- Example Configurations
  - Load QNN Model from a Serialized Context Binary
    ```json
    {
      "static": {
        "name": "QNN0",
        "id": 0,
        "loadType": "binary",
        "modelPath": "data/centernet/program.bin",
        "processorType": "htp0"
      }
    }
    ```
    Refer to [QNN SANITY_General](../tests/unit_test/Node/QNN/gtest_NodeQnn.cpp#L554) for more details..

  - Load QNN Model from a Serialized Context Buffer
    ```json
    {
      "static": {
        "name": "QNN0",
        "id": 0,
        "loadType": "buffer",
        "contextBufferId": 0,
        "processorType": "htp0"
      }
    }
    ```
    Refer to [QNN CreateModelFromBuffer](../tests/unit_test/Node/QNN/gtest_NodeQnn.cpp#L575) for more details. The `contextBufferId` was used together with the `QCNodeInit_t::buffers` to create a QNN model.

  - Load QNN Model with UDO package
    ```json
    {
    "static": {
      "id": 0,
      "loadType": "binary",
      "modelPath": "data/bevdet/program.bin",
      "name": "QNN0",
      "processorType": "htp0",
      "udoPackages": [
        {
          "interfaceProvider": "AutoAiswOpPackageInterfaceProvider",
          "udoLibPath": "libQnnAutoAiswOpPackage.so"
        }
      ]
    }
    ```
    Refer to [QNN LoadOpPackage](../tests/unit_test/Node/QNN/gtest_NodeQnn.cpp#L854) for more details.

## 2.2 QNN Dynamic JSON Configuration

| Parameter  | Required  | Type        | Description            |
|------------|-----------|-------------|------------------------|
| `enablePerf` | false   | bool        | enable performance profiling.  |

- Example Configurations
  - Enable Performance Profiling
  ```json
  {
    "dynamic": {
      "enablePerf": true
    }
  }
  ```
  Refer to [QNN Perf](../tests/unit_test/Node/QNN/gtest_NodeQnn.cpp#L632) for more details.

# 3. QNN APIs

## 3.1 QCNode QNN APIS

- [Qnn::Initialize](../include/QC/Node/QNN.hpp#L248) Initializes the QNN node using the provided configuration. This includes loading the model, registering buffers, and configuring the backend.

- [Qnn::GetConfigurationIfs](../include/QC/Node/QNN.hpp#L254) Provides access to the node's configuration interface, enabling retrieval of model options and tensor metadata.

- [Qnn::GetMonitoringIfs](../include/QC/Node/QNN.hpp#L260) Provides access to the monitoring interface for retrieving performance metrics and execution statistics.

- [Qnn::Start](../include/QC/Node/QNN.hpp#L266) Transitions the node to the running state, enabling it to accept inference requests.

- [Qnn::ProcessFrameDescriptor](../include/QC/Node/QNN.hpp#L295) Submits a frame descriptor containing input and output buffers for inference. Supports both synchronous execution (blocking until completion) and asynchronous execution (returning immediately and notifying via callback).

- [Qnn::Stop](../include/QC/Node/QNN.hpp#L301) Stops the node's execution, preventing further inference requests.

- [Qnn::DeInitialize](../include/QC/Node/QNN.hpp#L312) Releases all resources associated with the node, including memory and backend handles.

- [Qnn::GetState](../include/QC/Node/QNN.hpp#L322) Retrieves the current lifecycle state of the node.

## 3.2 QCNode Configuration Interfaces

- [QnnConfig::GetOptions](../include/QC/Node/QNN.hpp#L152) Get Configuration Options
  - Use this API to query detailed input and output information from the loaded QNN model.
    - Below was a example output for QNN model `centernet`
      ```json
      {
        "model": {
          "inputs": [
            {
              "dims": [1, 800, 1152, 3],
              "name": "input",
              "quantOffset": -114,
              "quantScale": 0.01865844801068306,
              "quantType": "scale_offset",
              "type": "ufixed_point8"
            }
          ],
          "outputs": [
            {
              "dims": [1,200,288,80],
              "name": "_256",
              "quantOffset": -247,
              "quantScale": 0.0852336436510086,
              "quantType": "scale_offset",
              "type": "ufixed_point8"
            },
            {
              "dims": [1,200,288,2],
              "name": "_259",
              "quantOffset": 0,
              "quantScale": 0.5239613056182861,
              "quantType": "scale_offset",
              "type": "ufixed_point8"
            },
            {
              "dims": [1,200,288,2],
              "name": "_262",
              "quantOffset": -20,
              "quantScale": 0.004786043893545866,
              "quantType": "scale_offset",
              "type": "ufixed_point8"
            }
          ]
        },
        "version": 131076
      }
      ```

# 4. Typical QNN API Usage Examples

## 4.1 Load from Context Binary File and Run in Synchronous Mode

This example demonstrates how to initialize a QNN node by loading a pre-compiled context binary from the filesystem. It sets up input and output buffers and executes inference synchronously, where the `ProcessFrameDescriptor` call blocks until the result is available.

```c++
// include the QCNode QNN header files
#include "QC/Node/QNN.hpp"
using namespace QC::Node;

#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

class MyQnnApp {
private:
  Qnn m_qnn; // Create a QNN node.
  BufferManager bufMgr = BufferManager( { "TENSOR", QC_NODE_TYPE_QNN, 0 } );
  TensorDescriptor_t m_inputDesc;
  TensorDescriptor_t m_output0Desc;
  TensorDescriptor_t m_output1Desc;
  TensorDescriptor_t m_output2Desc;
  QCSharedFrameDescriptorNode m_frameDesc( 5 );

  public:
  void Init() {
    // Initialize the QNN node.
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "QNN0",
          "id": 0,
          "loadType": "binary",
          "modelPath": "data/centernet/program.bin",
          "processorType": "htp0"
        }
      })"
    };

    status = m_qnn.Initialize(config);

    // Get the options that contain the detailed input and
    // output information of the QNN model.
    // This step was optional as generally this was known by
    // default when the QNN model was compiled.
    QCNodeConfigIfs &cfgIfs = m_qnn.GetConfigurationIfs();
    std::string options;
    status = cfgIfs.GetOptions( options );
    if ( QC_STATUS_OK != status )
    {
        QC_ERROR( "GetOptions failed: %d", status );
    }

    // Allocate input buffers, for high efficiency pipeline,
    // should allocate a set of buffers.
    // Here this was demo code, just use 1 buffer.
    if ( QC_STATUS_OK == status )
    {
        status = bufMgr.Allocate( TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 800, 1152, 3 } ), m_inputDesc );
        status = m_frameDesc.SetBuffer( 0, m_inputDesc );
    }

    // Allocate output buffers.
    if ( QC_STATUS_OK == status )
    {
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 80 } ),
            m_output0Desc );
        status = m_frameDesc.SetBuffer( 1, m_output0Desc );
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 2 } ),
            m_output1Desc );
        status = m_frameDesc.SetBuffer( 2, m_output1Desc );
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 2 } ),
            m_output2Desc );
        status = m_frameDesc.SetBuffer( 3, m_output2Desc );
    }

    // Start the QNN node.
    status = qnn.Start();
  }

  void Run() {
    // When input was ready in m_inputDesc buffer, call the
    // following function to process the frame descriptor.
    status = qnn.ProcessFrameDescriptor( m_frameDesc );
    // The output will be ready when here in
    // m_output0Desc/m_output1Desc/m_output2Desc buffer.
  }


  void Deinit() {
    // Stop the QNN node.
    status = qnn.Stop();

    // Deinitialize the QNN node.
    status = qnn.DeInitialize();
  }
};
```

## 4.2 Load from Context Binary File and Run in Asynchronous Mode

This example shows how to configure the QNN node for asynchronous execution by registering a callback function during initialization. The `ProcessFrameDescriptor` call returns immediately, and the application is notified via the callback when the inference completes.

```c++
// include the QCNode QNN header files.
#include "QC/Node/QNN.hpp"
using namespace QC::Node;

#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

#include <functional>

class MyQnnApp {
private:
  Qnn m_qnn; // Create a QNN node.
  BufferManager bufMgr = BufferManager( { "TENSOR", QC_NODE_TYPE_QNN, 0 } );
  TensorDescriptor_t m_inputDesc;
  TensorDescriptor_t m_output0Desc;
  TensorDescriptor_t m_output1Desc;
  TensorDescriptor_t m_output2Desc;
  QCSharedFrameDescriptorNode m_frameDesc( 5 );

  public:
  void Init() {
    // Initialize the QNN node
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "QNN0",
          "id": 0,
          "loadType": "binary",
          "modelPath": "data/centernet/program.bin",
          "processorType": "htp0"
        }
      })"
    };
    using std::placeholders::_1;
    config.callback = std::bind( &MyQnnApp::EventCallback, this, _1 );

    status = m_qnn.Initialize(config);

    // Get the options that contain the detailed input and
    // output information of the QNN model.
    // This step was optional as generally this was known by
    // default when the QNN model was compiled.
    QCNodeConfigIfs &cfgIfs = m_qnn.GetConfigurationIfs();
    std::string options;
    status = cfgIfs.GetOptions( options );
    if ( QC_STATUS_OK != status )
    {
        QC_ERROR( "GetOptions failed: %d", status );
    }

    // Allocate input buffers, for high efficiency pipeline,
    // should allocate a set of buffers.
    // Here this was demo code, just use 1 buffer.
    if ( QC_STATUS_OK == status )
    {
        status = bufMgr.Allocate( TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 800, 1152, 3 } ), m_inputDesc );
        status = m_frameDesc.SetBuffer( 0, m_inputDesc );
    }

    // Allocate output buffers.
    if ( QC_STATUS_OK == status )
    {
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 80 } ),
            m_output0Desc );
        status = m_frameDesc.SetBuffer( 1, m_output0Desc );
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 2 } ),
            m_output1Desc );
        status = m_frameDesc.SetBuffer( 2, m_output1Desc );
        status = bufMgr.Allocate(
            TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { 1, 200, 288, 2 } ),
            m_output2Desc );
        status = m_frameDesc.SetBuffer( 3, m_output2Desc );
    }

    // Start the QNN node.
    status = qnn.Start();
  }

  void EventCallback( const QCNodeEventInfo_t &info ) {
    if ( QC_STATUS_OK == info.status )
    {
        // When here, the output is ready.
        // The info.frameDesc will be exactly the reference to m_frameDesc.
    }
    else
    {
        // When here, the QNN inference failed.
        // The info.frameDesc will hole a QNN error code.
        QCBufferDescriptorBase_t &bufDesc = info.frameDesc.GetBuffer( 0 );
        Qnn_NotifyStatus_t *pQnnStatus = (Qnn_NotifyStatus_t *) bufDesc.pBuf;
        printf( "QNN callback with error %" PRIu64, pQnnStatus->error );
    }
  }

  void Run() {
    // When input was ready in m_inputDesc buffer, call the
    // following function to process the frame descriptor.
    status = qnn.ProcessFrameDescriptor( m_frameDesc );
    // When here, the output is not ready, the output will
    // be ready when EventCallback was invokded with status OK.
  }

  void Deinit() {
    // Stop the QNN node.
    status = qnn.Stop();

    // Deinitialize the QNN node.
    status = qnn.DeInitialize();
  }
};
```

## 4.3 Dynamic Batch Size

This example illustrates how to handle dynamic batch sizes at runtime. It allocates buffers for the maximum supported batch size during initialization and then updates the tensor dimensions and valid sizes in the frame descriptor to match the actual batch size for each inference request.

```c++
// include the QCNode QNN header files
#include "QC/Node/QNN.hpp"
using namespace QC::Node;

#include "QC/sample/BufferManager.hpp"
using namespace QC::sample;

class MyQnnApp {
private:
  Qnn m_qnn; // Create a QNN node.
  BufferManager bufMgr = BufferManager( { "TENSOR", QC_NODE_TYPE_QNN, 0 } );
  TensorDescriptor_t m_inputDesc;
  TensorDescriptor_t m_outputDesc;
  QCSharedFrameDescriptorNode m_frameDesc( 2 );
  const uint32_t m_maxBatchSize = 10;

  public:
  void Init() {
    // Initialize the QNN node.
    QCNodeInit_t config = {
      R"({
        "static": {
          "name": "QNN0",
          "id": 0,
          "loadType": "binary",
          "modelPath": "data/centernet/program.bin",
          "processorType": "htp0"
        }
      })"
    };

    status = m_qnn.Initialize(config);

    // Allocate input buffer with MAX batch size.
    status = bufMgr.Allocate( TensorProps_t( QC_TENSOR_TYPE_UFIXED_POINT_8, { m_maxBatchSize, 224, 224, 3 } ), m_inputDesc );
    status = m_frameDesc.SetBuffer( 0, m_inputDesc );

    // Allocate output buffer with MAX batch size.
    status = bufMgr.Allocate(
        TensorProps_t( QC_TENSOR_TYPE_FLOAT_32, { m_maxBatchSize, 1000 } ),
        m_outputDesc );
    status = m_frameDesc.SetBuffer( 1, m_outputDesc );

    // Start the QNN node.
    status = m_qnn.Start();
  }

  void Run( uint32_t currentBatchSize ) {
    if ( currentBatchSize > m_maxBatchSize ) {
       // Error handling
       return;
    }

    // Update input descriptor dimensions and valid size.
    m_inputDesc.dims[0] = currentBatchSize;
    m_inputDesc.validSize = ( m_inputDesc.size / m_maxBatchSize ) * currentBatchSize;

    // Update frame descriptor with modified tensor descriptor.
    status = m_frameDesc.SetBuffer( 0, m_inputDesc );

    // Update output descriptor dimensions and valid size.
    m_outputDesc.dims[0] = currentBatchSize;
    m_outputDesc.validSize = ( m_outputDesc.size / m_maxBatchSize ) * currentBatchSize;

    // Update frame descriptor with modified tensor descriptor.
    status = m_frameDesc.SetBuffer( 1, m_outputDesc );

    // Process frame descriptor.
    status = m_qnn.ProcessFrameDescriptor( m_frameDesc );
  }

  void Deinit() {
    // Stop the QNN node.
    status = m_qnn.Stop();

    // Deinitialize the QNN node.
    status = m_qnn.DeInitialize();
  }
};
```


# 5. References

- [SampleQnn](../tests/sample/source/SampleQnn.cpp#L474).
- [gtest QNN](../tests/unit_test/Node/QNN/gtest_NodeQnn.cpp#L356).

# 6. Limitations

## 6.1 QNN system context graph information versions supported
  - QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_1
  - QNN_SYSTEM_CONTEXT_GRAPH_INFO_VERSION_3

## 6.2 QNN system context binary information versions supported
  - QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1
  - QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2
  - QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_3

## 6.3 QNN device platform information versions supported
  - QNN_DEVICE_PLATFORM_INFO_VERSION_1

# 7. Functional Safety

This section provides an overview of QCNode QNN usage for functional safety use cases.

## 7.1 ASIL

| Node  | ASIL (or equivalent) | Supported Platforms |
|-------|----------------------|---------------------|
| QNN   | ASIL B               |      SA8797         |


## 7.2 Assumptions of Use (SWAOU)
**SWAOU:** Software Assumption of Use.

### QCNODE-QNN-SWAOU-1

- **Assumption:**  
  The system integrator **should** maintain its own set of input and output buffers for each QCNode QNN instance to prevent resource conflicts and ensure stable execution.

- **Sample of "How AoU can be met?":**  
  For every QCNode QNN instance, allocate dedicated input and output buffers that are not shared with other instances.

- **SW AoU Rationale:**  
  Prevents resource conflicts that could lead to data corruption or incorrect inference results. Ensures deterministic and isolated execution, which is critical for functional safety compliance. Supports scalability and concurrency by avoiding shared-state hazards between QNN instances.

### QCNODE-QNN-SWAOU-2

- **Assumption:**  
  By default, the maximum number of concurrent asynchronous QNN graph execution requests supported **shall** be **8**. The system integrator **shall** update [QNN_NOTIFY_PARAM_NUM](../source/Node/QNN/QnnImpl.hpp#L28) if more than 8 concurrent asynchronous requests are required.

- **Sample of "How AoU can be met?":**   
  The system integrator **shall** set [QNN_NOTIFY_PARAM_NUM](../source/Node/QNN/QnnImpl.hpp#L28) to a value greater than 8 when more than 8 concurrent asynchronous graph executions are needed.

- **SW AoU Rationale:**  
  The QCNode QNN enforces a default concurrency limit of 8 to ensure predictable resource usage. Allowing the user to configure [QNN_NOTIFY_PARAM_NUM](../source/Node/QNN/QnnImpl.hpp#L28) ensures flexibility while maintaining system stability.

### QCNODE-QNN-SWAOU-3

- **Assumption:**  
  The customer **shall** ensure proper system memory configuration, including a sufficiently large DDR device and adequate DMA memory allocation, to guarantee enough capacity for loading all QNN models.

- **Sample of "How AoU can be met?":**  
  The customer **shall** provision DDR and DMA memory during system setup based on the total size of all QNN models to be loaded, ensuring memory allocation meets QNN requirements.

- **SW AoU Rationale:**  
  QNN model loading and execution depend on available DDR and DMA memory. Insufficient memory can lead to failures or degraded performance. Proper memory provisioning ensures reliable QNN operation.

### QCNODE-QNN-SWAOU-4

- **Assumption:**  
  The system integrator **should** allocate all QNN-related memory during QNN initialization and then pass it to QCNode QNN for sequential registration.

- **Sample of "How AoU can be met?":**  
  The system integrator **should** allocate all required QNN buffers during initialization and register them with QCNode QNN before starting graph execution.

- **SW AoU Rationale:**  
  Pre-allocating and registering memory during initialization prevents runtime allocation failures and ensures deterministic behavior for QNN operations.
