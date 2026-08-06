// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_SAMPLE_TIMESTAMP_SYNC_HPP
#define QC_SAMPLE_TIMESTAMP_SYNC_HPP

#include <cstdint>
#include <vector>

namespace QC
{
namespace sample
{

/*
 * FindMinSpreadIndices — Smallest Range from K Sorted Lists
 * =========================================================
 *
 * Given N cameras each with D buffered timestamps (ascending: oldest→newest),
 * find one index per camera such that  max_ts − min_ts  is minimised.
 * When multiple selections share the same minimum spread the most recent
 * (highest-index) combination is returned.
 *
 * ── Algorithm ─────────────────────────────────────────────────────────────────
 *
 *   The only productive move at any step is to decrease the current maximum
 *   timestamp (step its camera one frame backward).  Decreasing any other
 *   camera's pointer can only widen or maintain the spread, never shrink it.
 *   This greedy property yields a provably optimal O(D×N×log N) solution.
 *
 *   1. Initialise all pointers at newest (last index per camera).
 *   2. Maintain a sorted window of (timestamp, cam_idx):
 *        front → minimum timestamp,  back → maximum timestamp.
 *   3. Evaluate spread = back − front.  Record with strict < to preserve the
 *      first (newest) winner on ties.  Exit early when spread ≤ threshold.
 *   4. Erase the back element (max camera), decrement its pointer.
 *   5. Re-insert the new timestamp.  If any camera reaches index 0 and is
 *      still the maximum, stop — the spread cannot be reduced further.
 *
 * ── Example: 3 cameras, queue_depth=3, 30ms frame gap, timestamps in µs ──────
 *
 *   Cam 0: [100000, 130000, 160000]   (oldest→newest, 30ms gap)
 *   Cam 1: [103000, 133000, 163000]   (3ms late, 30ms gap)
 *   Cam 2: [098000, 128000]           (2ms early, only 2 frames)
 *
 *   Step 0  ptrs=[2,2,1]  window={128k,160k,163k}  spread=35000
 *   Step 1  max=cam1→ptr1=1  window={128k,133k,160k}  spread=32000  ✓
 *   Step 2  max=cam0→ptr0=1  window={128k,130k,133k}  spread= 5000  ✓  ← done (≤10ms threshold)
 *
 *   Result: ptrs=[1,1,1], spread=5ms → frames at t130ms / t133ms / t128ms
 *
 * ── Complexity ────────────────────────────────────────────────────────────────
 *
 *   Time:  O(D × N × log N)   (at most D×N set operations, each O(log N))
 *   Space: O(N)               (one entry per camera in the window)
 *
 *   Comparison vs exhaustive search:
 *     D=3, N=8 : 3^8 × 8 ≈ 52 000 ops  →  3×8×3 ≈   72 ops  ( 730×)
 *     D=5, N=8 : 5^8 × 8 ≈  3 M ops   →  5×8×3 ≈  120 ops  (25 000×)
 *
 * @param timestamps  N×D matrix; each row must be non-empty and ascending.
 * @param threshold   Acceptable spread (same unit as timestamps).  Pass 0 to
 *                    always find the global minimum.
 * @param bestIndices Output: one index per camera for the best selection.
 * @return            The minimum spread found.
 */
uint64_t FindMinSpreadIndices( const std::vector<std::vector<uint64_t>> &timestamps,
                               uint64_t threshold, std::vector<size_t> &bestIndices );

}   // namespace sample
}   // namespace QC

#endif   // QC_SAMPLE_TIMESTAMP_SYNC_HPP
