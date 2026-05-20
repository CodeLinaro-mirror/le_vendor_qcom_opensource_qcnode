# QCNode Build Guide

This document explains how to build the QCNode package. There are three primary methods:

1.  **Docker-based Build (Recommended)**: Uses a containerized environment with `build-package.py`. Supports QNX, Linux, and Ubuntu targets.
2.  **Native QNX Chipcode Build**: Builds directly within a QNX Chipcode environment using the native toolchain.
3.  **Native Linux/Ubuntu Yocto Build**: Builds within the Linux/Ubuntu Yocto build system (BitBake).

## Table of Contents
- [QCNode Build Guide](#qcnode-build-guide)
  - [Table of Contents](#table-of-contents)
  - [1. Method 1: Docker-based Build (Recommended)](#1-method-1-docker-based-build-recommended)
    - [1.1 Prerequisites](#11-prerequisites)
    - [1.2 Toolchain Preparation](#12-toolchain-preparation)
      - [1.2.1 Extracting QNX Toolchain](#121-extracting-qnx-toolchain)
      - [1.2.2 Preparing Linux/Ubuntu Toolchain](#122-preparing-linuxubuntu-toolchain)
      - [1.2.3 Handling Missing Dependencies](#123-handling-missing-dependencies)
    - [1.3 Build Script: `build-package.py`](#13-build-script-build-packagepy)
    - [1.4 Build Examples](#14-build-examples)
      - [1.4.1 Build for QNX (HQX)](#141-build-for-qnx-hqx)
      - [1.4.2 Build for HGY Linux](#142-build-for-hgy-linux)
      - [1.4.3 Build for HGY Ubuntu](#143-build-for-hgy-ubuntu)
      - [1.4.4 Customizing Build Options with `env_script`](#144-customizing-build-options-with-env_script)
  - [2. Method 2: Native QNX Chipcode Build](#2-method-2-native-qnx-chipcode-build)
    - [2.1 Prerequisites](#21-prerequisites)
    - [2.2 How to Build](#22-how-to-build)
  - [3. Method 3: Native Linux/Ubuntu Yocto Build](#3-method-3-native-linuxubuntu-yocto-build)
    - [3.1 Prerequisites](#31-prerequisites)
    - [3.2 How to Build](#32-how-to-build)
    - [3.3 Customizing Build Options](#33-customizing-build-options)
  - [4. Common Build Options](#4-common-build-options)
  - [5. Directory Structure](#5-directory-structure)

## 1. Method 1: Docker-based Build (Recommended)

This method ensures a consistent build environment across different host machines.

### 1.1 Prerequisites

Before starting, ensure you have the following:

- **Docker**: Docker must be installed and running on your host machine.
- **Docker Image**: The `qcnode-toolchain-base:v1.0` image is required.
    - See [Guide to build qcnode-toolchain base docker](docker/README.md) for build instructions.
- **QNN SDK**: Download and unzip the Qualcomm Neural Network (QNN) SDK (e.g., to `/opt/qnn_sdk`).
- **Toolchain**: You need **one** of the following toolchains depending on your target:
    - **QNX**: QNX SDP (7.1 or 8.0) installed or extracted.
    - **Linux**: Linux SDK installer (`oecore-*.sh`) or installed sysroots.
    - **Ubuntu**: Ubuntu SDK installer (`oecore-*.sh`) or installed sysroots.

### 1.2 Toolchain Preparation

#### 1.2.1 Extracting QNX Toolchain

For QNX builds, the toolchain needs to be extracted from the complex QNX BSP structure into a format compatible with the build script.

**Important**: The extraction scripts generate an `env.sh` file within the toolchain directory that sets `QNX_HOME=/opt/qnx`. The `build-package.py` script automatically mounts the provided toolchain path to `/opt/qnx` inside the Docker container to ensure these paths match.

**QNX SDP 7.1 (HQX)**
Use [`scripts/build/toolchain/extract-qnx7-toolchain.py`](toolchain/extract-qnx7-toolchain.py) to extract the toolchain from a QNX BSP build.

**Note**: `/path/to/qnx_bsp_root` should be the directory that contains the `qnx_ap` folder.

```bash
python3 scripts/build/toolchain/extract-qnx7-toolchain.py \
    --input /path/to/qnx_bsp_root \
    --version QOS222 \
    --output /path/to/output/qnx7_toolchain
```

**QNX SDP 8.0 (Gen4/Gen5)**
Use [`scripts/build/toolchain/extract-qnx8-toolchain.py`](toolchain/extract-qnx8-toolchain.py) for QNX SDP 8.0.

**Note**: `/path/to/qnx_bsp_root` should be the directory that contains the `qnx_ap` folder.

```bash
python3 scripts/build/toolchain/extract-qnx8-toolchain.py \
    --input /path/to/qnx_bsp_root \
    --version SDP800 \
    --output /path/to/output/qnx8_toolchain
```

**Note**: The output path (`/path/to/output/qnx*_toolchain/qnx`) will be used as the `--toolchain` argument in [`build-package.py`](../../build-package.py).

#### 1.2.2 Preparing Linux/Ubuntu Toolchain

For Linux and Ubuntu, the toolchain is typically provided as a self-extracting shell script (e.g., `oecore-x86_64-aarch64-toolchain-nodistro.0.sh`).

**Automatic Installation (Recommended)**
The build system supports automatic installation of the toolchain. This is the **recommended** method because Yocto SDKs have hardcoded paths that must match between installation and usage.

1.  Create a directory for the toolchain (e.g., `/opt/linux`).
2.  Place the `.sh` installer script inside this directory.
3.  Pass this directory as the `--toolchain` argument to [`build-package.py`](../../build-package.py).
4.  The script will detect the installer and install it **inside the Docker container** (at `/opt/linux` or `/opt/ubuntu`).

**Manual Installation Note**
If you choose to install the SDK manually on your host:
*   **Linux**: You **must** install it to `/opt/linux`.
*   **Ubuntu**: You **must** install it to `/opt/ubuntu`.

This is because [`build-package.py`](../../build-package.py) mounts the toolchain directory to `/opt/linux` (or `/opt/ubuntu` or `/opt/qnx`) inside the Docker container. If the install path on the host differs from the mount path in Docker, the SDK environment scripts (which use absolute paths) will fail.

#### 1.2.3 Handling Missing Dependencies
**Important**: The standard SDK installer often lacks certain header files required for specific features (like Demuxer, FastRPC, or Multimedia) because they are part of the proprietary source tree. Consequently, **the first build attempt may fail**.

You must identify these missing files in your source tree (chipcode) and copy them to the installed toolchain's include directory.

**Common Missing Headers**
*   **Video Demuxer**: `parserinternaldefs.h`
*   **FastRPC & Graphics**: `rpcmem.h`, `comdef.h`, `gbm.h`, `gbm_priv.h`, `color_metadata.h`

**Steps to Fix:**
1.  Run the build once. It will install the toolchain and likely fail.
2.  Locate the missing header files in your source tree (e.g., under `apps_proc/vendor/qcom/proprietary/video-driver/...`).
3.  Copy them to the toolchain's sysroot include path:
    ```bash
    # Example for Linux
    cp /path/to/missing/file.h /path/to/installed_sdk/sysroots/aarch64-oe-linux/usr/include/
    ```
4.  Re-run the build.

### 1.3 Build Script: [`build-package.py`](../../build-package.py)

The [`build-package.py`](../../build-package.py) script is the primary entry point. It sets up the Docker container, mounts necessary directories, and executes the build.

**Usage**

```bash
python3 build-package.py [options]
```

**Arguments**

| Argument | Long Argument | Description | Required | Default |
| :--- | :--- | :--- | :--- | :--- |
| **Target Config** | | | | |
| `--variant` | | Build variant: `qnx`, `linux`, `ubuntu` | No | `linux` |
| `--socid` | | Target SoC ID (e.g., `8797`, `8650`, `8620`) | No | `8797` |
| **Paths** | | | | |
| `-q` | `--qnn_sdk` | Path to QNN SDK Root | **Yes** | `./toolchain/qnn_sdk` |
| `-t` | `--toolchain` | Path to compiler toolchain folder | **Yes** | `./toolchain/linux` |
| **Environment** | | | | |
| `-d` | `--docker` | Docker image name | No | `qcnode-toolchain-base:v1.0` |
| `-e` | `--env_script` | Env script to source inside docker | No | `""` |
| **Other** | | | | |
| `-c` | `--clean` | Clean build directory before building | No | `False` |
| | `--no_sudo` | Do not use `sudo` for docker | No | `True` (uses sudo) |

### 1.4 Build Examples

#### 1.4.1 Build for QNX (HQX)
Builds for QNX target (e.g., 8650). Requires an environment script for QNX SDP setup.

```bash
python3 build-package.py \
    --variant qnx \
    --socid 8650 \
    --qnn_sdk /opt/qnn_sdk \
    --toolchain /opt/qnx \
    --env_script scripts/env_qnx.sh
```

#### 1.4.2 Build for HGY Linux
Builds for Linux target (e.g., 8797).

```bash
python3 build-package.py \
    --variant linux \
    --socid 8797 \
    --qnn_sdk /opt/qnn_sdk \
    --toolchain /opt/linux
```

#### 1.4.3 Build for HGY Ubuntu
Builds for Ubuntu target (e.g., 8650).

```bash
python3 build-package.py \
    --variant ubuntu \
    --socid 8650 \
    --qnn_sdk /opt/qnn_sdk \
    --toolchain /opt/ubuntu_sdk
```

#### 1.4.4 Customizing Build Options with `env_script`

You can customize the build by setting environment variables in a shell script and passing it via `--env_script`. See [Common Build Options](#4-common-build-options) for a list of available options.

1.  **Create** a script file (e.g., `my_options.sh`):

    ```bash
    #!/bin/bash
    export ENABLE_DEMUXER=ON
    export ENABLE_TINYVIZ=OFF
    ```

2.  **Pass** it to the build script:

    ```bash
    python3 build-package.py ... --env_script my_options.sh
    ```

## 2. Method 2: Native QNX Chipcode Build

This method allows building QCNode directly within a QNX Chipcode environment without using Docker. This relies on the root `Makefile` and expects the standard QNX build environment variables to be set.

### 2.1 Prerequisites
- QNX SDP installed and environment sourced (setting `QNX_HOST`, `QNX_TARGET`, etc.).
- QNX Chipcode environment setup (setting `BSP_ROOT`).

### 2.2 How to Build
1.  **Environment Setup**: Ensure your shell is configured with the QNX Chipcode environment.
    ```bash
    # Example
    source setenv_sdp800.sh
    make clean; make
    ```

2.  **Build**: Navigate to the qcnode directory and run `make`.
    You can also export build options (e.g., `ENABLE_TINYVIZ`, `ENABLE_DEMUXER`) as described in [Common Build Options](#4-common-build-options).

    ```bash
    cd qnx_ap/AMSS/qcnode
    source /path/to/qnn_sdk/bin/envsetup.sh
    export ENABLE_TINYVIZ=ON
    export ENABLE_DEMUXER=ON
    make install
    ```
    This will:
    - Invoke `scripts/build/build-target.sh aarch64-qnx .`.
    - Detect `BSP_ROOT` and use the native toolchain defined in the environment.
    - Install binaries to `${INSTALL_ROOT_nto}/aarch64le/bin/qcnode` (via `QC_INSTALL_ROOT`).
    - Copy security policy and buildfiles to `BSP_ROOT`.
    - Generate the output package: `qcnode-aarch64-qnx.tar.gz`.

## 3. Method 3: Native Linux/Ubuntu Yocto Build

This method allows building QCNode within the standard Linux/Ubuntu Yocto build system (e.g., `meta-qti-automotive`).

### 3.1 Prerequisites
- Linux/Ubuntu Yocto build environment set up (sourced `setup-environment`).
- `meta-qti-automotive` layer included in the build.

### 3.2 How to Build
1.  **Environment Setup**: Source the Yocto build environment.
    ```bash
    source conf/set_bb_env.sh -t sa8797
    ```

2.  **Build**: Run `bitbake` for the `qcnode` recipe.
    ```bash
    bitbake qcnode
    ```

### 3.3 Customizing Build Options
You can customize the build options by modifying the recipe (`qcnode_2.0.bb`).

**Note**:
*   TinyViz is not supported in the Native Linux/Ubuntu Yocto Build.
*   Only `QNN_SDK_ROOT` option is applicable for configuration in this method.

Example update in `qcnode_2.0.bb`:
```bash
QNN_SDK_ROOT = "/path/to/qnn_sdk"
```

## 4. Common Build Options

These options can be used with all build methods.
- **Docker Build**: Set them in an `env_script`.
- **Native QNX Build**: Export them as environment variables.
- **Native Yocto Build**: Set them as `QCNODE_<OPTION>` in the recipe.

| Option | Description | Notes |
| :--- | :--- | :--- |
| `ENABLE_GCOV` | Enable GCC Coverage | Default: `OFF` |
| `ENABLE_CTC` | Enable CTC Coverage | Default: `OFF` |
| `ENABLE_DEMUXER` | Enable Video Demuxer | Default: `OFF` |
| `ENABLE_TINYVIZ` | Enable TinyViz (SDL) | Default: `ON` |
| `ENABLE_FADAS` | Enable FastADAS | Default: `ON` |
| `ENABLE_SDP8` | Enable QNX SDP 8.0 | Auto-detected for 8797 |
| `ENABLE_C2D` | Enable C2D support | Platform dependent |
| `ENABLE_EVA` | Enable EVA SV (Snapdragon Vision) support | Default: `ON` for 8797, else `OFF` |
| `ENABLE_EVA_AUTO` | Enable EVA Auto support | Default: `OFF` for 8797, else `ON` |
| `ENABLE_TRACE` | Enable Tracing | Default: `ON` |
| `ENABLE_HS` | Enable Hetero Scheduler | Default: `OFF` |
| `ENABLE_COMP_RES_SCHED` | Enable Compure Resource Scheduler | Default: `OFF` |

## 5. Directory Structure

- `scripts/build/`:
    - `README.md`: This file.
    - [`build-target.sh`](build-target.sh): Core shell script executed inside Docker.
    - `docker/`: Docker build scripts.
    - `toolchain/`: Scripts to extract/prepare toolchains.
- [`build-package.py`](../../build-package.py): Python wrapper for Docker execution.
- [`Makefile`](../../Makefile): Root Makefile for Native QNX Chipcode Build.
