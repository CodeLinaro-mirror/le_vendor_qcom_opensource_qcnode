// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "TimestampSync.hpp"

#include <set>

namespace QC
{
namespace sample
{

uint64_t FindMinSpreadIndices( const std::vector<std::vector<uint64_t>> &timestamps,
                               uint64_t threshold, std::vector<size_t> &bestIndices )
{
    size_t N = timestamps.size();

    /* Initialise pointers at newest frame per camera */
    std::vector<size_t> ptrs( N );
    for ( size_t i = 0; i < N; i++ )
    {
        ptrs[i] = timestamps[i].size() - 1;
    }

    /* Sorted window: (timestamp, cam_idx).  cam_idx breaks ties so every
     * camera has a distinct key even when two timestamps are equal. */
    std::set<std::pair<uint64_t, size_t>> window;
    for ( size_t i = 0; i < N; i++ )
    {
        window.insert( { timestamps[i][ptrs[i]], i } );
    }

    uint64_t bestSpread = window.rbegin()->first - window.begin()->first;
    bestIndices = ptrs;
    bool done = ( bestSpread <= threshold );

    while ( false == done )
    {
        /* Step the camera with the maximum timestamp one frame back */
        auto maxIt = std::prev( window.end() );
        size_t cam = maxIt->second;

        if ( ptrs[cam] == 0 )
        {
            done = true; /* exhausted — spread cannot shrink further */
        }
        else
        {
            window.erase( maxIt );
            ptrs[cam]--;
            window.insert( { timestamps[cam][ptrs[cam]], cam } );

            uint64_t spread = window.rbegin()->first - window.begin()->first;

            /* Strict < preserves the first (newest) winner on ties */
            if ( spread < bestSpread )
            {
                bestSpread = spread;
                bestIndices = ptrs;
                if ( bestSpread <= threshold )
                {
                    done = true;
                }
            }
        }
    }

    return bestSpread;
}

}   // namespace sample
}   // namespace QC
