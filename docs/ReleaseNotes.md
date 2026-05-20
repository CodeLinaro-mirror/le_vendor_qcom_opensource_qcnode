*Menu*:
- [1. Overview](#1-overview)
- [2. Feature Changes](#2-feature-changes)
  - [2.1 Version 2.0.0](#21-version-200)
  - [2.2 Version 2.1.0 (FuSA)](#22-version-210-fusa)
- [3. Quality Improvements](#3-quality-improvements)
  - [Version 2.1.0](#version-210)
- [4. Verified Platforms](#4-verified-platforms)

# 1. Overview
The **QCNode** is the Qualcomm ADAS hardware abstraction layer that provides user‑friendly APIs to access the Qualcomm ADAS hardware accelerator. It is service‑oriented and designed to deliver easy‑to‑use components and utilities for perception and computer vision tasks.

- **QCNode Nodes** are high‑quality modules that can be directly used by customers with minimal effort.
- **QCNode Samples** demonstrate how to use QC components effectively, including memory zero‑copy and buffer lifecycle management.
- **Multi‑threaded Samples** showcase how to improve FPS throughput by leveraging parallel execution.
- Each **QCNode Node** provides essential logging (especially for errors) to simplify debugging.
- The **QCNode Logger** is customizable and can be easily integrated with customer ADAS application logging systems.

# 2. Feature Changes
## 2.1 Version 2.0.0
- Initial release of **QCNode v2.0.0**, introducing the upgrade of RideHal to the new unified Node API.
  - Included nodes:
    - **Camera**: Built on *qcarcam* to support camera streaming
    - **Remap**: Powered by *FastADAS* for color conversion, ROI cropping, downscaling, and undistortion
    - **QNN**: Based on *QNN SDK* for AI model inference
    - **Video Encoder**: Built on *vidc* to encode images into video frames
    - **Video Decoder**: Built on *vidc* to decode video frames into images
    - **Voxelization**: Uses *FastADAS* and *OpenCL* to generate pillars from point clouds
    - **CL2DFlex**: Powered by *OpenCL* for color conversion, ROI cropping, downscaling, and undistortion
    - **Optical Flow**: Based on *SV* to compute optical flow
    - **Depth From Stereo**: Based on *SV* to compute depth from stereo images

## 2.2 Version 2.1.0 (FuSA)

- New Nodes
  - **Radar**: Promoted to a standalone node, supporting both HQX and HGY platforms
  - **Simulation**: Added SimulationNode for testing and simulating node behaviors (error conditions, processing delays, and state transitions)

- Camera Node
  - Bumped to version 2.3.2
  - Added metadata feature support and new dependent platform libs
  - Fixed multi-stream issue; added FuSA AoU documentation

- Remap Node
  - Added NSP0/1/2/3 core selection support on Nordy
  - Fixed GTests for `ProcessFrameDescriptor` and `MapDimensionsValidation`

- CL2DFlex Node
  - Added memory deregister on stop with stress GTests
  - Fixed UV plane mismatch in NV12 UBWC decompress; added `convert_ubwc` work mode for NV12 UBWC → NV12 decompression

- Voxelization Node
  - Added NSP core 0–3 support on Nordy
  - Fixed GPU output correctness issue

- QNN Node
  - Added `loadType` option to select model loading method (`binary`, `library`, or `buffer`)
  - Added FuSA Software Assumption Usecase documentation

- EVA Node
  - Enabled Lemans platform support
  - Added condition checks for set-configuration properties

- Video Encoder/Decoder
  - Fixed Encoder deinitialization failure (CR4329991) and invalid output issue
  - Fixed Video Decoder issue (CR4416935)
  - Made output buffer size configurable
  - Added VPU Mock Library for code coverage

- Memory Manager
  - Fixed thread-safety issue in public APIs
  - Fixed pool handle collision under concurrent `CreatePool`
  - Fixed resource reclaim error during deinitialization

- FADAS
  - Added FastRPC BCS support for SMMU-mapped communication
  - Fixed `fastrpc_munmap` to always release SMMU mapping on stop

- Framework / Infrastructure
  - Temporal: support multiple Temporal tensors
  - DataTree/MemoryManager: security code review fixes
  - Node: fixed security issues and updated node ID data type
  - QNX Secpol: added channel connect permission for `compresmgr_service_t`
  - QCRun & Secpol: joined `hetero_sched_group` and `mqueue` group to enable hetero scheduler
  - QCRun: joined `safetymonitor_group` to access FuSA CRC libs
  - Replaced `HAP_farf` logging with `printf`-mapped `_QAIC_FARF` macro

- Sample Apps
  - Hetero Scheduler integration across all sample pipelines (SampleCamera, SampleDataReader, SampleFrameSync, SampleQNN/Remap/CL2DFlex/Voxelization, SV/EVA)
  - SampleFrameSync: added FrameSync and Temporal Hetero Scheduler support; fixed empty frame publish issue
  - SampleVideo: added H264 pipeline support; use `dmaHandle` to maintain buffer life cycle
  - SampleQNN: fixed UDO log formatting and image-to-tensor conversion bug
  - SampleApp: NSP multi-core resource protection optimization
  - Added lidar coordinate computation sample with Neon acceleration

- FuSA
  - Added Common SWAoU documentation for QCNode, Camera, and Voxelization nodes
  - QNN: added FuSA Software Assumption Usecase document

- Build / Toolchain
  - Updated toolchain to support QNN SDK lib-safe and FastRPC BCS
  - Added safetylibs and camera metadata toolchain support
  - Replaced hardcoded cache path with `THIRD_PARTY_CACHE` variable
  - Cleaned up engineering-only FADAS remap format and pipeline macros

# 3. Quality Improvements

## Version 2.1.0

- MISRA-C / Static Analysis
  - Enabled `-Wall` compiler option across all nodes (Remap, Voxelization, Video Encoder/Decoder, CL2DFlex, QNN, Infras, OpenCL Iface), resolving all outstanding warnings
  - Fixed MISRA-C rule violations in CL2DFlex node
  - Applied Klockwork static analysis fixes across the codebase

- Code Coverage
  - Improved unit test coverage across multiple components:
  - Voxelization, Remap, EVA, CL2DFlex: added dedicated code coverage GTests; fixed build warnings in Voxelization tests
  - Memory Manager: reached 90% code coverage with additional test cases
  - Radar: improved code coverage
  - FADAS (FadasIfaceCore, MemUtils): added unit test cases
  - CL2DFlex: fixed mock coverage GTest failure on Nordy HQX

- Security
  - Fixed security code review issues in Node base class and DataTree/MemoryManager infrastructure
  - Fixed DAC issue in qcrun shell script

# 4. Verified Platforms
- QCNode is verified on Lemans device with the following meta build:
  - HQX QNX Snapdragon_Auto.HQX.4.5.6.0.1.r1
  - HQX QNX Snapdragon_Auto.HQX.4.8.9.0.1
  - HGY Ubuntu Snapdragon_Auto.HGY.4.1.6.0.r1
  - HGY Linux Snapdragon_Auto.HGY.4.1.6.0.1.r1

- QCNode is verified on Nordy device with the following meta build:
  - HGY Linux SA8797P.HGY.5.1.7.0.r1
  - HQX QNX SA8797P.HQX.5.7.7.0.r1
