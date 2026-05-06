*Menu*:
- [🚗 **QCNode - Qualcomm Compute Node**](#-qcnode---qualcomm-compute-node)
- [1. Overview](#1-overview)
- [2. 🧩 Architecture](#2--architecture)
- [3. 🔧 API Summary](#3--api-summary)
  - [3.1 Lifecycle Management](#31-lifecycle-management)
    - [`Initialize(QCNodeInit_t &config)`](#initializeqcnodeinit_t-config)
    - [`Start()`](#start)
    - [`Stop()`](#stop)
    - [`DeInitialize()`](#deinitialize)
  - [3.2 Frame Processing](#32-frame-processing)
    - [`ProcessFrameDescriptor(QCFrameDescriptorNodeIfs &frameDesc)`](#processframedescriptorqcframedescriptornodeifs-framedesc)
  - [3.3 State \& Interface Access](#33-state--interface-access)
    - [`GetState()`](#getstate)
    - [`GetConfigurationIfs()`](#getconfigurationifs)
    - [`GetMonitoringIfs()`](#getmonitoringifs)
- [4. Functional Safety](#4-functional-safety)
  - [4.1 ASIL](#41-asil)
  - [4.2 Assumptions of Use (SWAOU)](#42-assumptions-of-use-swaou)
    - [QCNODE-COMMON-SWAOU-1](#qcnode-common-swaou-1)
    - [QCNODE-COMMON-SWAOU-2](#qcnode-common-swaou-2)

---

# 🚗 **QCNode - Qualcomm Compute Node**

# 1. Overview

**QCNode** (Qualcomm Compute Node) is part of Qualcomm's ADAS hardware abstraction layer. It provides a **service-oriented**, **user-friendly API** to access Qualcomm's ADAS hardware accelerators, enabling efficient execution of perception and computer vision tasks.

QCNode is designed to simplify integration and development by offering modular components and utilities tailored for ADAS workloads.

---

# 2. 🧩 Architecture

All QCNode implementations must inherit from the abstract base class `QCNodeIfs`. This interface defines a **unified API** for:

- Lifecycle management
- Frame descriptor processing
- Configuration and monitoring access

This ensures consistency and interoperability across all QCNode components.

---

# 3. 🔧 API Summary

## 3.1 Lifecycle Management

### `Initialize(QCNodeInit_t &config)`
Initializes the node with the provided configuration.

- **Parameters**: `config` – Initialization parameters
- **Returns**: `QCStatus_e` – Status of the operation
- **Usage**: Must be called before `Start()`

---

### `Start()`
Activates the node, making it ready to process data.

- **Returns**: `QCStatus_e`
- **Usage**: Typically follows a successful `Initialize()`

---

### `Stop()`
Gracefully halts node operations.

- **Returns**: `QCStatus_e`
- **Usage**: Should be followed by `DeInitialize()` if shutting down

---

### `DeInitialize()`
Cleans up resources and prepares the node for shutdown.

- **Returns**: `QCStatus_e`
- **Usage**: Called after `Stop()`

---

## 3.2 Frame Processing

### `ProcessFrameDescriptor(QCFrameDescriptorNodeIfs &frameDesc)`
Handles a frame descriptor either by queuing it for asynchronous processing or by processing it immediately, depending on the node's internal design and capabilities.

- **Parameters**: `frameDesc` – Frame descriptor to be processed, for more details of `frameDesc`, refer [Node Frame Descriptor](./buffer.md#qcnode-node-frame-descriptor).
- **Returns**: `QCStatus_e` – Processing status
- **Usage**: Used during active node operation

---

## 3.3 State & Interface Access

### `GetState()`
Retrieves the current state of the node.

- **Returns**: `QCObjectState_e` – e.g., Initialized, Running, Stopped
- **Usage**: Useful for monitoring and control logic

---

### `GetConfigurationIfs()`
Provides access to the node’s configuration interface.

- **Returns**: `QCNodeConfigIfs&`
- **Usage**: Allows querying or updating configuration parameters

---

### `GetMonitoringIfs()`
Provides access to the node’s monitoring interface.

- **Returns**: `QCNodeMonitoringIfs&`
- **Usage**: Enables retrieval of runtime metrics and status indicators

---

# 4. Functional Safety

This section provides an overview of QCNode common usage for functional safety use cases.

## 4.1 ASIL

| Node  | ASIL (or equivalent) | Supported Platforms |
|-------|----------------------|---------------------|
| QCNode  | ASIL B            |      SA8797         |


## 4.2 Assumptions of Use (SWAOU)
**SWAOU:** Software Assumption of Use.

### QCNODE-COMMON-SWAOU-1

- **Assumption:**  
  The system integrator **shall** implement a timeout mechanism when invoking blocking QCNode APIs, including `Initialize`, `Start`, `Stop`, `DeInitialize`, and `ProcessFrameDescriptor`, to prevent indefinite blocking in the event of a failure.

- **Sample of "How AoU can be met?":**  
  The application encapsulates QCNode API calls within a supervised execution context (e.g., a monitored thread or a task with a watchdog). If an API call fails to return within the specified Worst-Case Execution Time (WCET), the supervision mechanism detects the timeout and triggers a fault recovery strategy, such as restarting the node or transitioning the system to a safe state.

- **SW AoU Rationale:**  
  Underlying hardware accelerators or system resources may become unresponsive due to hardware faults or deadlock conditions. Without a timeout mechanism, the calling thread could hang indefinitely, violating real-time constraints and compromising the availability of the safety function.

### QCNODE-COMMON-SWAOU-2

- **Assumption:**  
  The system integrator **shall** profile the application to determine the safe operating limits of the SMMU. To maintain system stability, the number of shared buffers between the CPU and hardware accelerators (e.g., DSP, VPU, GPU) **shall** be restricted to a validated maximum (e.g., using a finite ping-pong scheme) to avoid exceeding mapping resources.

- **Sample of "How AoU can be met?":**  
  The system integrator determines the SMMU mapping limit through stress testing and configuration analysis. The application is then designed to use a bounded pool of buffers (e.g., a fixed-size circular buffer) that guarantees the total number of mapped buffers never exceeds this limit.

- **SW AoU Rationale:**  
  Unbounded buffer allocation or mapping can exhaust SMMU resources, leading to mapping failures and potential system instability. Restricting shared buffers to a validated maximum ensures deterministic resource usage and prevents runtime failures that could compromise safety functions.
