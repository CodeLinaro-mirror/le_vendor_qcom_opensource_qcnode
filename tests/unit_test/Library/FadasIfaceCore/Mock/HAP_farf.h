// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef HAP_FARF_H
#define HAP_FARF_H

#include <stdio.h>

#define ERROR 1
#define HIGH 2
#define LOW 3

#define FARF( level, fmt, ... ) printf( "[%d] " fmt "\n", level, ##__VA_ARGS__ )

#endif
