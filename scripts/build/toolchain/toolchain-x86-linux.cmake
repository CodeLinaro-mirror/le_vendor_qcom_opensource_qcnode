# x86-linux native build toolchain (no cross-compilation)
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use the host native compiler — no cross-compiler prefix
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# No sysroot — link against the host system libraries directly

# Third-party libraries installed alongside the project build
include_directories($ENV{QCNODE_INSTALL_DIR}/opt/qcnode/include)
include_directories($ENV{QCNODE_INSTALL_DIR}/opt/qcnode/include/WF)
add_link_options("-L$ENV{QCNODE_INSTALL_DIR}/opt/qcnode/lib")
