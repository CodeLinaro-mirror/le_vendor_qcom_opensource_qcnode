// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "TimestampSync.hpp"
#include <gtest/gtest.h>

using namespace QC::sample;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static uint64_t run( const std::vector<std::vector<uint64_t>> &ts,
                     uint64_t                                  threshold,
                     std::vector<size_t>                      &idx )
{
    return FindMinSpreadIndices( ts, threshold, idx );
}

/* ── TC1: perfect sync — all cameras share the same timestamps ────────────── *
 * Expected: spread=0, result picks newest frames [2,2,2].                    */
TEST( FindMinSpreadIndices, PerfectSync )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 100000, 130000, 160000 },
        { 100000, 130000, 160000 },
        { 100000, 130000, 160000 },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 0, idx );

    EXPECT_EQ( spread, 0u );
    EXPECT_EQ( idx[0], 2u );
    EXPECT_EQ( idx[1], 2u );
    EXPECT_EQ( idx[2], 2u );
}

/* ── TC2: 3-camera 30ms-gap example from TimestampSync.hpp ───────────────── *
 * Cam0: 100/130/160 ms, Cam1: 103/133/163 ms (3ms late),                    *
 * Cam2: 98/128 ms (2ms early, depth=2).  Threshold=10ms.                    *
 * Expected: ptrs=[1,1,1], spread=5000 µs.                                   */
TEST( FindMinSpreadIndices, ThreeCam30msGap )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 100000, 130000, 160000 },
        { 103000, 133000, 163000 },
        {  98000, 128000         },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 10000 /* 10ms */, idx );

    EXPECT_EQ( spread, 5000u );
    EXPECT_EQ( idx[0], 1u );
    EXPECT_EQ( idx[1], 1u );
    EXPECT_EQ( idx[2], 1u );
}

/* ── TC3: queue_depth=1 — single frame per camera, no history ────────────── *
 * Expected: only combination [0,0,0]; spread reflects raw inter-camera gap. */
TEST( FindMinSpreadIndices, DepthOne )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 160000 },
        { 163000 },
        { 128000 },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 0, idx );

    EXPECT_EQ( spread, 35000u );
    EXPECT_EQ( idx[0], 0u );
    EXPECT_EQ( idx[1], 0u );
    EXPECT_EQ( idx[2], 0u );
}

/* ── TC4: tie prefers newest — two selections have the same minimum spread ── *
 * Cam0: [100, 200], Cam1: [105, 205].                                        *
 * [1,1]=spread 5, [0,0]=spread 5.  Newest [1,1] must win.                   */
TEST( FindMinSpreadIndices, TiePrefersNewest )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 100, 200 },
        { 105, 205 },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 0, idx );

    EXPECT_EQ( spread, 5u );
    EXPECT_EQ( idx[0], 1u );
    EXPECT_EQ( idx[1], 1u );
}

/* ── TC5: global minimum not at newest ───────────────────────────────────── *
 * Cam0: [100000, 160000], Cam1: [102000, 195000].                           *
 * Newest [1,1]=spread 35ms.  Best is [0,0]=spread 2ms (≤ 5ms threshold).   */
TEST( FindMinSpreadIndices, GlobalMinNotNewest )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 100000, 160000 },
        { 102000, 195000 },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 5000 /* 5ms */, idx );

    EXPECT_EQ( spread, 2000u );
    EXPECT_EQ( idx[0], 0u );
    EXPECT_EQ( idx[1], 0u );
}

/* ── TC6: early exit on threshold ────────────────────────────────────────── *
 * Cam0: [100000,130000,160000], Cam1: [104000,134000,164000] (4ms late).    *
 * Spread at any aligned pair is 4ms.  Threshold=5ms → exits at [2,2].      */
TEST( FindMinSpreadIndices, EarlyExitOnThreshold )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 100000, 130000, 160000 },
        { 104000, 134000, 164000 },
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 5000 /* 5ms */, idx );

    EXPECT_EQ( spread, 4000u );
    /* Must select newest pair [2,2] because spread ≤ threshold at first step */
    EXPECT_EQ( idx[0], 2u );
    EXPECT_EQ( idx[1], 2u );
}

/* ── TC7: asymmetric depths ──────────────────────────────────────────────── *
 * Cam0: depth=1, Cam1: depth=3, Cam2: depth=2.                              *
 * Cam0 is fixed; algorithm searches Cam1+Cam2 for the best match to Cam0.  */
TEST( FindMinSpreadIndices, AsymmetricDepths )
{
    std::vector<std::vector<uint64_t>> ts = {
        { 130000                          },   /* depth=1, fixed */
        { 103000, 133000, 163000          },   /* depth=3 */
        {  98000, 128000                  },   /* depth=2 */
    };
    std::vector<size_t> idx;
    uint64_t spread = run( ts, 10000 /* 10ms */, idx );

    /* Best: cam0=130000, cam1[1]=133000, cam2[1]=128000 → spread=5000 */
    EXPECT_EQ( spread, 5000u );
    EXPECT_EQ( idx[0], 0u );
    EXPECT_EQ( idx[1], 1u );
    EXPECT_EQ( idx[2], 1u );
}

/* ── TC8: 8 cameras, depth=3, verifies scale behaviour ───────────────────── *
 * All cameras 30ms apart, slight per-camera jitter.  Global minimum should  *
 * be found and be within 10ms.                                               */
TEST( FindMinSpreadIndices, EightCameras )
{
    /* Each camera offset from the reference by 0..7 ms */
    const uint64_t gap   = 30000; /* 30ms in µs */
    const uint64_t base  = 100000;
    const uint32_t N     = 8;
    const uint32_t depth = 3;

    std::vector<std::vector<uint64_t>> ts( N );
    for ( uint32_t cam = 0; cam < N; cam++ )
    {
        for ( uint32_t d = 0; d < depth; d++ )
        {
            ts[cam].push_back( base + d * gap + (uint64_t) cam * 1000 );
        }
    }

    std::vector<size_t> idx;
    uint64_t spread = run( ts, 10000 /* 10ms */, idx );

    /* With 1ms inter-camera jitter, aligned frames should be within 7ms */
    EXPECT_LT( spread, 10000u );
    EXPECT_EQ( idx.size(), (size_t) N );
}
