// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "gtest/gtest.h"
#include <chrono>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "QC/Infras/NodeTrace/Ifs/QCNodeTraceIfs.hpp"
#include "QC/Infras/NodeTrace/NodeTrace.hpp"

using namespace QC;
using namespace QC::Node;

class NodeTraceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Unset environment variable to start clean if possible
        unsetenv( "QC_NODETRACE" );
    }

    void TearDown() override { unsetenv( "QC_NODETRACE" ); }
};

// Helper class to test macros
class TestNode
{
public:
    void TestMacros()
    {
        QC_TRACE_INIT( R"({"name": "TestNode", "processor": "cpu", "coreIds": [1]})" );
        QC_TRACE_BEGIN( "TestScope", {} );
        QC_TRACE_EVENT( "TestEvent", { QCNodeTraceArg( "val", 123 ) } );
        QC_TRACE_COUNTER( "TestCounter", { QCNodeTraceArg( "cnt", 456 ) } );
        QC_TRACE_IF( true, QC_TRACE_EVENT( "TrueEvent", {} ) );
        QC_TRACE_IF( false, QC_TRACE_EVENT( "FalseEvent", {} ) );
        QC_TRACE_END( "TestScope", {} );
    }

    // Test Init with bad config via macro if possible, or just public method if exposed
    void TestInit( std::string cfg ) { m_trace.Init( cfg ); }

private:
    QC_DECLARE_NODETRACE();
};

TEST_F( NodeTraceTest, ArgTypesCoverage )
{
    // Test all constructors of QCNodeTraceArg
    QCNodeTraceArg argStr( "str", std::string( "val" ) );
    EXPECT_EQ( argStr.type, QCNODE_TRACE_ARG_TYPE_STRING );

    QCNodeTraceArg argDouble( "d", 1.0 );
    EXPECT_EQ( argDouble.type, QCNODE_TRACE_ARG_TYPE_DOUBLE );

    QCNodeTraceArg argFloat( "f", 1.0f );
    EXPECT_EQ( argFloat.type, QCNODE_TRACE_ARG_TYPE_FLOAT );

    QCNodeTraceArg argU64( "u64", (uint64_t) 1 );
    EXPECT_EQ( argU64.type, QCNODE_TRACE_ARG_TYPE_UINT64 );

    QCNodeTraceArg argU32( "u32", (uint32_t) 1 );
    EXPECT_EQ( argU32.type, QCNODE_TRACE_ARG_TYPE_UINT32 );

    QCNodeTraceArg argU16( "u16", (uint16_t) 1 );
    EXPECT_EQ( argU16.type, QCNODE_TRACE_ARG_TYPE_UINT16 );

    QCNodeTraceArg argU8( "u8", (uint8_t) 1 );
    EXPECT_EQ( argU8.type, QCNODE_TRACE_ARG_TYPE_UINT8 );

    QCNodeTraceArg argI64( "i64", (int64_t) -1 );
    EXPECT_EQ( argI64.type, QCNODE_TRACE_ARG_TYPE_INT64 );

    QCNodeTraceArg argI32( "i32", (int32_t) -1 );
    EXPECT_EQ( argI32.type, QCNODE_TRACE_ARG_TYPE_INT32 );

    QCNodeTraceArg argI16( "i16", (int16_t) -1 );
    EXPECT_EQ( argI16.type, QCNODE_TRACE_ARG_TYPE_INT16 );

    QCNodeTraceArg argI8( "i8", (int8_t) -1 );
    EXPECT_EQ( argI8.type, QCNODE_TRACE_ARG_TYPE_INT8 );
}

TEST_F( NodeTraceTest, TraceLogic )
{
    NodeTrace trace;

    // 1. Env not set.
    unsetenv( "QC_NODETRACE" );
    trace.Init( "{}" );   // Should do nothing, s_pTraceFile remains null
    trace.Trace( "Event", QCNODE_TRACE_TYPE_EVENT, {} );   // Should do nothing

    // 2. Env set to invalid path (simulate fopen fail)
    // Note: This relies on s_pTraceFile being null initially.
    // If other tests ran before this and opened the file, this step won't trigger fopen.
    // Assuming this test runs first or in isolation or s_pTraceFile is null.
    // We try to pick a path that fails fopen, e.g. a directory.
    setenv( "QC_NODETRACE", "/", 1 );
    trace.Init( "{}" );   // Should fail to open file, print stderr, s_pTraceFile remains null

    // 3. Env set to valid path
    std::string validPath = "trace_test.bin";
    setenv( "QC_NODETRACE", validPath.c_str(), 1 );

    // 4. Init with invalid config
    // This will open the file (since env is set and valid), but then fail parsing config.
    trace.Init( "invalid json" );

    // 5. Init with valid config
    trace.Init( R"({"name": "Test", "processor": "DSP", "coreIds": [0, 1]})" );

    // 6. Trace with all arg types
    std::vector<QCNodeTraceArg_t> args;
    args.emplace_back( "str", std::string( "s" ) );
    args.emplace_back( "d", 1.23 );
    args.emplace_back( "f", 1.23f );
    args.emplace_back( "u64", (uint64_t) 100 );
    args.emplace_back( "u32", (uint32_t) 100 );
    args.emplace_back( "u16", (uint16_t) 100 );
    args.emplace_back( "u8", (uint8_t) 100 );
    args.emplace_back( "i64", (int64_t) -100 );
    args.emplace_back( "i32", (int32_t) -100 );
    args.emplace_back( "i16", (int16_t) -100 );
    args.emplace_back( "i8", (int8_t) -100 );

    trace.Trace( "ComplexEvent", QCNODE_TRACE_TYPE_EVENT, args );

    // 7. Init with missing fields (defaults)
    trace.Init( R"({"other": "value"})" );
}

TEST_F( NodeTraceTest, MacroUsage )
{
    // Ensure QC_ENABLE_NODETRACE is working
    TestNode node;

    // Ensure file is open (if TraceLogic didn't run or failed)
    setenv( "QC_NODETRACE", "trace_macro_test.bin", 1 );

    node.TestMacros();
}
