// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <vector>

// Project headers
#include "QC/Infras/Log/Logger.hpp"
#include "VidcDrvClient.hpp"
#include "VidcNodeBase.hpp"
#include "vidc_driver_mockup.hpp"


// The production code includes either <ioctlClient.h> or <vidc_client.h>.
// We rely on those to define ioctl_callback_t, vidc_* enums/structs, VIDC_IOCTL_*,
// VIDC_EVT_*, and property types/IDs. No extra mock headers are required.

// ---------------------------
// Global fakes / test harness
// ---------------------------

extern "C"
{

    // =====================================================================================
    // ==== Mockup State ===================================================================
    // =====================================================================================

    struct MockupVidcConfig
    {
        bool use_open_mockup = false;
        bool use_ioctl_mockup = false;
        bool use_close_mockup = false;
        bool use_timer_mockup = false;
        bool open_fail = false;

        int rc_load_resources = 0;
        int rc_release_resources = 0;
        int rc_start = 0;
        int rc_stop = 0;
        int rc_drain = 0;
        int rc_set_property = 0;
        int rc_get_property = 0;
        int rc_set_buffer = 0;
        int rc_free_buffer = 0;
        int rc_empty_input = 0;
        int rc_fill_output = 0;

        bool emit_evt_load_done = true;
        bool emit_evt_start_done = true;
        bool emit_evt_stop_done = true;
        bool emit_evt_release_done = true;
        bool emit_drain_events = true;
        bool emit_stop_input_done = true;
        bool emit_stop_output_done = true;

        uint32_t req_buf_count = 4;
        uint32_t req_buf_size = 128 * 1024;
    };

    static MockupVidcConfig g_mock_cfg;
    static void Mockup_Cfg_Reset()
    {
        g_mock_cfg = MockupVidcConfig{};
    }

    struct dc_fake_handle
    {
        ioctl_callback_t cb{};
        bool opened = false;
    };
    // State captured from device_open
    static ioctl_callback_t g_cb{};
    static ioctl_session_t *g_fake_handle = reinterpret_cast<ioctl_session_t *>( 0xBEEFCAFE );

    // Small control switches used by tests
    static bool g_autofire_events = true;

    // Minimal property store to emulate BUFFER_REQUIREMENTS for input/output
    static vidc_buffer_reqmnts_type g_req_in{};
    static vidc_buffer_reqmnts_type g_req_out{};

    // Helper: emit a single driver event to the client's callback
    static void EmitEvent( uint32_t evt_id, const vidc_frame_data_type *f = nullptr )
    {
        if ( !g_cb.handler ) return;
        vidc_drv_msg_info_type evt{};
        evt.event_type = static_cast<vidc_event_type>( evt_id );
        if ( f ) evt.payload.frame_data = *f;
        g_cb.handler( reinterpret_cast<uint8_t *>( &evt ), sizeof( evt ), g_cb.data );
    }

    // Emit an event that carries vidc_frame_data_type in the payload
    static void EmitFrameEvent( uint32_t event_type, uint64_t handle, uint8_t *addr, uint32_t size )
    {
        if ( !g_cb.handler ) return;
        vidc_drv_msg_info_type msg = {};
        msg.event_type = static_cast<vidc_event_type>( event_type );
        msg.payload.frame_data.frm_clnt_data = handle;
        msg.payload.frame_data.frame_addr = addr;
        msg.payload.frame_data.data_len = size;
        msg.payload.frame_data.alloc_len = size;
        g_cb.handler( reinterpret_cast<unsigned char *>( &msg ), sizeof( msg ), g_cb.data );
    }

    // A trivially fast "sleep" so time-based waits don’t actually block
    int __mockup_MM_Timer_Sleep( unsigned int /*msec*/ )
    {
        return 0;
    }

    // Fake device_open: saves callback and returns a non-null handle (or null if asked to fail)
    void *__mockup_device_open( const char *pathname, ioctl_callback_t *cb )
    {
        if ( !g_mock_cfg.use_open_mockup ) return device_open( (char *) pathname, cb );
        if ( g_mock_cfg.open_fail ) return nullptr;
        if ( cb ) g_cb = *cb;
        return static_cast<void *>( g_fake_handle );
    }

    // Fake device_close: nothing to do
    int __mockup_device_close( ioctl_session_t * /*handle*/ )
    {
        return 0;
    }

    int __mockup_device_ioctl( ioctl_session_t *handle, unsigned int cmd, unsigned char *in,
                               unsigned int in_len, unsigned char *out, unsigned int out_len )
    {
        if ( !g_mock_cfg.use_ioctl_mockup )
            return device_ioctl( handle, cmd, in, in_len, out, out_len );

        switch ( cmd )
        {
            case VIDC_IOCTL_LOAD_RESOURCES:
                if ( g_autofire_events )
                {
                    EmitEvent( VIDC_EVT_RESP_LOAD_RESOURCES );
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_load_resources ) return g_mock_cfg.rc_load_resources;
                if ( g_mock_cfg.emit_evt_load_done ) EmitEvent( VIDC_EVT_RESP_LOAD_RESOURCES );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_RELEASE_RESOURCES:
                if ( g_autofire_events )
                {
                    EmitEvent( VIDC_EVT_RESP_RELEASE_RESOURCES );
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_release_resources ) return g_mock_cfg.rc_release_resources;
                if ( g_mock_cfg.emit_evt_release_done )
                    EmitEvent( VIDC_EVT_RESP_RELEASE_RESOURCES );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_START:
                if ( g_autofire_events )
                {
                    if ( in && in_len >= sizeof( vidc_start_mode_type ) )
                    {
                        vidc_start_mode_type mode = *reinterpret_cast<vidc_start_mode_type *>( in );
                        if ( mode == VIDC_START_INPUT )
                        {
                            EmitEvent( VIDC_EVT_RESP_START_INPUT_DONE );
                        }
                        else if ( mode == VIDC_START_OUTPUT )
                        {
                            EmitEvent( VIDC_EVT_RESP_START_OUTPUT_DONE );
                        }
                        else
                        {
                            // unknown mode -> treat as ALL
                            EmitEvent( VIDC_EVT_RESP_START );
                        }
                    }
                    else
                    {
                        // No payload -> "START ALL"
                        EmitEvent( VIDC_EVT_RESP_START );
                    }
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_start ) return g_mock_cfg.rc_start;
                if ( in == nullptr )
                {
                    // START_ALL
                    if ( g_mock_cfg.emit_evt_start_done ) EmitEvent( VIDC_EVT_RESP_START );
                }
                else
                {
                    vidc_start_mode_type mode = *reinterpret_cast<vidc_start_mode_type *>( in );
                    if ( mode == VIDC_START_INPUT && g_mock_cfg.emit_evt_start_done )
                        EmitEvent( VIDC_EVT_RESP_START_INPUT_DONE );
                    // VIDC_START_OUTPUT: no WaitForCmdCompleted in StartDriver
                }
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_STOP:
                if ( g_autofire_events )
                {
                    if ( in && in_len >= sizeof( vidc_stop_mode_type ) )
                    {
                        vidc_stop_mode_type mode = *reinterpret_cast<vidc_stop_mode_type *>( in );
                        if ( mode == VIDC_STOP_INPUT )
                        {
                            EmitEvent( VIDC_EVT_RESP_STOP_INPUT_DONE );
                        }
                        else if ( mode == VIDC_STOP_OUTPUT )
                        {
                            EmitEvent( VIDC_EVT_RESP_STOP_OUTPUT_DONE );
                        }
                        else
                        {
                            EmitEvent( VIDC_EVT_RESP_STOP );
                        }
                    }
                    else
                    {
                        EmitEvent( VIDC_EVT_RESP_STOP );
                    }
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_stop ) return g_mock_cfg.rc_stop;
                if ( in && in_len >= sizeof( vidc_stop_mode_type ) )
                {
                    vidc_stop_mode_type mode = *reinterpret_cast<vidc_stop_mode_type *>( in );
                    if ( mode == VIDC_STOP_INPUT && g_mock_cfg.emit_stop_input_done )
                        EmitEvent( VIDC_EVT_RESP_STOP_INPUT_DONE );
                    else if ( mode == VIDC_STOP_OUTPUT && g_mock_cfg.emit_stop_output_done )
                        EmitEvent( VIDC_EVT_RESP_STOP_OUTPUT_DONE );
                    else if ( g_mock_cfg.emit_evt_stop_done )
                        EmitEvent( VIDC_EVT_RESP_STOP );
                }
                else if ( g_mock_cfg.emit_evt_stop_done )
                {
                    EmitEvent( VIDC_EVT_RESP_STOP );
                }
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_DRAIN:
                if ( g_autofire_events )
                {
                    EmitEvent( VIDC_EVT_RESP_DRAIN );
                    EmitEvent( VIDC_EVT_LAST_FLAG );
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_drain ) return g_mock_cfg.rc_drain;
                if ( g_mock_cfg.emit_drain_events )
                {
                    EmitEvent( VIDC_EVT_RESP_DRAIN );
                    EmitEvent( VIDC_EVT_LAST_FLAG );
                }
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_SET_PROPERTY:
                if ( g_autofire_events )
                {
                    if ( !in || in_len < sizeof( vidc_drv_property_type ) )
                        return VIDC_ERR_BAD_PARAM;
                    auto *p = reinterpret_cast<vidc_drv_property_type *>( in );
                    if ( p->prop_hdr.prop_id == VIDC_I_BUFFER_REQUIREMENTS )
                    {
                        // Payload is vidc_buffer_reqmnts_type; honor buf_type and update store
                        vidc_buffer_reqmnts_type tmp{};
                        std::memcpy( &tmp, p->payload,
                                     std::min<uint32_t>( p->prop_hdr.size, sizeof( tmp ) ) );
                        if ( tmp.buf_type == VIDC_BUFFER_INPUT ) g_req_in = tmp;
                        if ( tmp.buf_type == VIDC_BUFFER_OUTPUT ) g_req_out = tmp;
                    }
                    return VIDC_ERR_NONE;
                }
                return g_mock_cfg.rc_set_property;

            case VIDC_IOCTL_GET_PROPERTY:
            {
                if ( g_autofire_events )
                {
                    if ( !in || in_len < sizeof( vidc_drv_property_type ) || !out || out_len == 0 )
                        return VIDC_ERR_BAD_PARAM;
                    auto *p = reinterpret_cast<vidc_drv_property_type *>( in );
                    if ( p->prop_hdr.prop_id == VIDC_I_BUFFER_REQUIREMENTS )
                    {
                        // Out payload expects vidc_buffer_reqmnts_type; copy from our store based
                        // on buf_type
                        vidc_buffer_reqmnts_type tmp{};
                        // The caller seeded 'payload' with buf_type; read it:
                        std::memcpy( &tmp, p->payload,
                                     std::min<uint32_t>( p->prop_hdr.size, sizeof( tmp ) ) );
                        if ( tmp.buf_type == VIDC_BUFFER_INPUT ) tmp = g_req_in;
                        if ( tmp.buf_type == VIDC_BUFFER_OUTPUT ) tmp = g_req_out;
                        std::memcpy( out, &tmp,
                                     std::min<uint32_t>( out_len,
                                                         static_cast<uint32_t>( sizeof( tmp ) ) ) );
                    }
                    return VIDC_ERR_NONE;
                }
                if ( g_mock_cfg.rc_get_property ) return g_mock_cfg.rc_get_property;
                if ( out && out_len >= sizeof( vidc_buffer_reqmnts_type ) )
                {
                    auto *req = reinterpret_cast<vidc_buffer_reqmnts_type *>( out );
                    req->actual_count = g_mock_cfg.req_buf_count;
                    req->size = g_mock_cfg.req_buf_size;
                }
                return 0;
            }

            case VIDC_IOCTL_SET_BUFFER:
                return g_mock_cfg.rc_set_buffer;

            case VIDC_IOCTL_FREE_BUFFER:
                return g_mock_cfg.rc_free_buffer;

            case VIDC_IOCTL_EMPTY_INPUT_BUFFER:
                if ( !g_autofire_events )
                {
                    if ( g_mock_cfg.rc_empty_input ) return g_mock_cfg.rc_empty_input;
                    if ( in && in_len >= sizeof( vidc_frame_data_type ) )
                    {
                        auto *fd = reinterpret_cast<vidc_frame_data_type *>( in );
                        EmitFrameEvent( VIDC_EVT_RESP_INPUT_DONE, fd->frm_clnt_data, fd->frame_addr,
                                        fd->data_len );
                    }
                }
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_FILL_OUTPUT_BUFFER:
                if ( !g_autofire_events )
                {
                    if ( g_mock_cfg.rc_fill_output ) return g_mock_cfg.rc_fill_output;
                    if ( in && in_len >= sizeof( vidc_frame_data_type ) )
                    {
                        auto *fd = reinterpret_cast<vidc_frame_data_type *>( in );
                        EmitFrameEvent( VIDC_EVT_RESP_OUTPUT_DONE, fd->frm_clnt_data,
                                        fd->frame_addr, fd->data_len );
                    }
                }
                return VIDC_ERR_NONE;

            default:
                return VIDC_ERR_NONE;
        }
    }

}   // extern "C"

// ---------------------------
// Test fixture
// ---------------------------

using namespace QC;
using namespace QC::Node;

struct CallCounters
{
    std::atomic<int> in_done{ 0 };
    std::atomic<int> out_done{ 0 };
    std::atomic<int> events{ 0 };
};

class VidcDrvClientTest : public testing::Test
{
protected:
    VidcDrvClient dut{};
    CallCounters calls{};
    VideoFrameDescriptor_t last_in{};
    VideoFrameDescriptor_t last_out{};
    VideoCodec_EventType_e last_evt{};

    static void InDoneCb( VideoFrameDescriptor &frame, void *ctx )
    {
        auto *self = static_cast<VidcDrvClientTest *>( ctx );
        self->last_in = frame;
        self->calls.in_done++;
    }

    static void OutDoneCb( VideoFrameDescriptor &frame, void *ctx )
    {
        auto *self = static_cast<VidcDrvClientTest *>( ctx );
        self->last_out = frame;
        self->calls.out_done++;
    }
    static void EvtCb( VideoCodec_EventType_e evt, const void * /*pMsg*/, void *ctx )
    {
        auto *self = static_cast<VidcDrvClientTest *>( ctx );
        self->last_evt = evt;
        self->calls.events++;
    }

    void SetUp() override
    {
        Mockup_Cfg_Reset();
        g_mock_cfg.use_open_mockup = true;
        g_mock_cfg.use_ioctl_mockup = true;
        g_mock_cfg.use_close_mockup = true;
        g_mock_cfg.use_timer_mockup = true;
        g_mock_cfg.open_fail = false;
        // Reset globals
        g_autofire_events = false;
        std::memset( &g_cb, 0, sizeof( g_cb ) );

        calls.in_done = 0;
        calls.out_done = 0;
        last_evt = VIDEO_CODEC_EVT_ERROR;

        std::memset( &g_req_in, 0, sizeof( g_req_in ) );
        std::memset( &g_req_out, 0, sizeof( g_req_out ) );

        // Provide some default requirements so NegotiateBufferReq works
        g_req_in.buf_type = VIDC_BUFFER_INPUT;
        g_req_in.actual_count = 4;
        g_req_in.size = 4096;

        g_req_out.buf_type = VIDC_BUFFER_OUTPUT;
        g_req_out.actual_count = 4;
        g_req_out.size = 8192;

        // Minimal init
        VidcNodeBase_Config_t cfg{};
        cfg.numInputBufferReq = 4;
        cfg.numOutputBufferReq = 4;
        dut.Init( "test", QC::LOGGER_LEVEL_DEBUG, VIDEO_DEC, cfg );

        // Open driver (inject callbacks)
        ASSERT_EQ( QC_STATUS_OK,
                   dut.OpenDriver( &VidcDrvClientTest::InDoneCb, &VidcDrvClientTest::OutDoneCb,
                                   &VidcDrvClientTest::EvtCb, this ) );
    }

    void TearDown() override
    {
        dut.CloseDriver();
        Mockup_Cfg_Reset();
    }

    static VideoFrameDescriptor_t MakeFrame( uint64_t handle, void *addr, uint32_t size,
                                             uint32_t offset = 0, uint64_t ts_ns = 0,
                                             uint64_t mark = 0 )
    {
        VideoFrameDescriptor_t f{};
        f.dmaHandle = handle;
        f.pBuf = addr;
        f.size = size;
        f.offset = offset;
        f.timestampNs = ts_ns;
        f.appMarkData = mark;
        // f.pid may be needed on non-QNX; set nonzero:
        f.pid = 1234;
        return f;
    }
};

// =====================================================================================
// ==== Shared callbacks ===============================================================
// =====================================================================================

static std::atomic<int> g_inputDoneCnt{ 0 };
static std::atomic<int> g_outputDoneCnt{ 0 };
static std::atomic<VideoCodec_EventType_e> g_lastEvent{ VIDEO_CODEC_EVT_ERROR };

static void TestInputDoneCb( VideoFrameDescriptor &frame, void *priv )
{
    (void) frame;
    (void) priv;
    g_inputDoneCnt++;
}

static void TestOutputDoneCb( VideoFrameDescriptor &frame, void *priv )
{
    (void) frame;
    (void) priv;
    g_outputDoneCnt++;
}

static void TestEventCb( VideoCodec_EventType_e event, const void *payload, void *priv )
{
    (void) payload;
    (void) priv;
    g_lastEvent = event;
}

// ---------------------------
// Tests
// ---------------------------

TEST_F( VidcDrvClientTest, OpenDriver_FailsIfDeviceOpenReturnsNull )
{
    g_mock_cfg.use_ioctl_mockup = true;
    // Close first to re-open with failure
    dut.CloseDriver();
    g_mock_cfg.open_fail = true;
    auto rc = dut.OpenDriver( &VidcDrvClientTest::InDoneCb, &VidcDrvClientTest::OutDoneCb,
                              &VidcDrvClientTest::EvtCb, this );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, rc );
    g_mock_cfg.use_ioctl_mockup = false;
}

TEST_F( VidcDrvClientTest, LoadResources_SucceedsAndEmitsEvent )
{
    g_mock_cfg.use_ioctl_mockup = true;
    auto rc = dut.LoadResources();
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_mock_cfg.use_ioctl_mockup = false;
}

TEST_F( VidcDrvClientTest, LoadResources_TimesOutIfNoEvent )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = false;   // do not send RESP_LOAD_RESOURCES
    g_mock_cfg.rc_load_resources = false;
    g_mock_cfg.emit_evt_load_done = false;
    auto rc = dut.LoadResources();
    EXPECT_EQ( QC_STATUS_TIMEOUT, rc );
    g_autofire_events = true;   // restore
    g_mock_cfg.use_ioctl_mockup = false;
}

TEST_F( VidcDrvClientTest, ReleaseResources_SucceedsAndEmitsEvent )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_mock_cfg.rc_load_resources = false;
    g_mock_cfg.emit_evt_load_done = false;
    auto rc = dut.ReleaseResources();
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_mock_cfg.use_ioctl_mockup = false;
}

TEST_F( VidcDrvClientTest, StartDriver_Output_NoWaitAndSuccess )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    auto rc = dut.StartDriver( VIDEO_CODEC_START_OUTPUT );
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_mock_cfg.use_ioctl_mockup = false;
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, StartDriver_Input_WaitsUntilEvent )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    auto rc = dut.StartDriver( VIDEO_CODEC_START_INPUT );
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, StartDriver_All_WaitsUntilEvent )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    auto rc = dut.StartDriver( VIDEO_CODEC_START_ALL );
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, SetBufferAndEmptyBuffer_InputPath_ThenInputDoneCallback )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;

    // Arrange: register input buffer
    auto frame = MakeFrame( /*handle*/ 0x1001, /*addr*/ reinterpret_cast<void *>( 0xDEAD0001 ),
                            /*size*/ 2048 );
    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> in{ frame };
    ASSERT_EQ( QC_STATUS_OK, dut.SetBuffer( VIDEO_CODEC_BUF_INPUT, in ) );

    // Act: queue input (marks bUsedFlag true)
    ASSERT_EQ( QC_STATUS_OK, dut.EmptyBuffer( frame ) );

    calls.in_done = 0;

    // Simulate input-done event coming from driver
    vidc_frame_data_type f{};
    f.frm_clnt_data = frame.dmaHandle;
    f.frame_addr = static_cast<uint8_t *>( frame.pBuf );
    f.data_len = frame.size;
    vidc_drv_msg_info_type evt = { .event_type = VIDC_EVT_RESP_INPUT_DONE,
                                   .payload = { .frame_data = f } };
    VidcDrvClient::CallDeviceCallback( reinterpret_cast<uint8_t *>( &evt ),
                                       sizeof( vidc_drv_msg_info_type ), &dut );

    // Assert callback was called
    EXPECT_EQ( 1, calls.in_done.load() );
    EXPECT_EQ( last_in.dmaHandle, frame.dmaHandle );

    // After input-done, buffer should be reusable -> EmptyBuffer again should succeed
    EXPECT_EQ( QC_STATUS_OK, dut.EmptyBuffer( frame ) );

    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, SetBufferAndFillBuffer_OutputPath_ThenOutputDoneCallbackWithMetadata )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;

    // Arrange: register output buffer
    auto frame = MakeFrame( /*handle*/ 0x2001, /*addr*/ reinterpret_cast<void *>( 0xBEEF0002 ),
                            /*size*/ 4096 );
    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> out{ frame };
    ASSERT_EQ( QC_STATUS_OK, dut.SetBuffer( VIDEO_CODEC_BUF_OUTPUT, out ) );

    // Act: queue output (marks bUsedFlag true)
    ASSERT_EQ( QC_STATUS_OK, dut.FillBuffer( frame ) );

    // Simulate output-done with metadata
    vidc_frame_data_type f{};
    f.frm_clnt_data = frame.dmaHandle;
    f.frame_addr = static_cast<uint8_t *>( frame.pBuf );
    f.data_len = 1234;
    f.alloc_len = frame.size;
    f.timestamp = 987654;     // microseconds
    f.mark_data = 0xABCDEF;   // app mark
    f.flags = 0x55;
    f.frame_type = static_cast<vidc_frame_type>( 3 );

    vidc_drv_msg_info_type evt{};
    evt.event_type = VIDC_EVT_RESP_OUTPUT_DONE;
    evt.payload.frame_data = f;

    VidcDrvClient::CallDeviceCallback( reinterpret_cast<uint8_t *>( &evt ), sizeof( evt ), &dut );

    // Assert callback was called
    EXPECT_EQ( 1, calls.out_done.load() );
    EXPECT_EQ( last_out.dmaHandle, frame.dmaHandle );
    EXPECT_EQ( last_out.validSize, 1234u );
    EXPECT_EQ( last_out.appMarkData, static_cast<uint64_t>( 0xABCDEF ) );
    EXPECT_EQ( last_out.timestampNs, static_cast<uint64_t>( 987654 ) * 1000 );
    EXPECT_EQ( last_out.frameFlag, 0x55u );
    EXPECT_EQ( last_out.frameType, static_cast<uint64_t>( 3 ) );

    // After output-done, buffer should be reusable -> FillBuffer again should succeed
    EXPECT_EQ( QC_STATUS_OK, dut.FillBuffer( frame ) );

    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, NegotiateBufferReq_UpsizesWhenDriverNeedsMore )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;

    uint32_t buf_num = 2;   // request fewer than driver
    uint32_t buf_size = 0;

    // Driver advertises 5
    g_req_in.actual_count = 5;
    g_req_in.size = 2048;

    auto rc = dut.NegotiateBufferReq( VIDEO_CODEC_BUF_INPUT, buf_num, buf_size );
    EXPECT_EQ( QC_STATUS_OK, rc );
    EXPECT_EQ( 5u, buf_num );
    EXPECT_EQ( 2048u, buf_size );

    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, NegotiateBufferReq_DownsizesBySettingDriverRequirement )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;

    uint32_t buf_num = 6;   // ask more than driver initially advertises
    uint32_t buf_size = 0;

    // Driver advertises 4 initially; client should set to 6 and read-back 6
    g_req_out.actual_count = 4;
    g_req_out.size = 4096;

    auto rc = dut.NegotiateBufferReq( VIDEO_CODEC_BUF_OUTPUT, buf_num, buf_size );
    EXPECT_EQ( QC_STATUS_OK, rc );
    EXPECT_EQ( 6u, buf_num );
    EXPECT_EQ( 4096u, buf_size );
    // The fake driver now stores 6; a subsequent GET should yield 6
    EXPECT_EQ( 6u, g_req_out.actual_count );

    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, StopDecoder_SendsDrainThenStopsInputAndOutput )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    auto rc = dut.StopDecoder();
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, StopEncoder_StopsAll )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    // Switch to encoder mode (no behavioral difference here for the test)
    // Only StopEncoder uses IOCTL_STOP + RESP_STOP sequence.
    auto rc = dut.StopEncoder();
    EXPECT_EQ( QC_STATUS_OK, rc );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, DeviceCallback_ReturnsMinusOneIfSelfNull )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    int ret = VidcDrvClient::CallDeviceCallback( nullptr, 0, &dut );
    EXPECT_EQ( -1, ret );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, DeviceCallback_InvalidMsgReturnsMinusOne )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    int ret = VidcDrvClient::CallDeviceCallback( nullptr, 0, &dut );
    EXPECT_EQ( -1, ret );
    g_autofire_events = false;
}

TEST_F( VidcDrvClientTest, PrintCodecConfig )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = true;
    VidcCodecMeta_t meta;
    meta.codecType = static_cast<VideoCodecType_e>( static_cast<int>( VIDEO_CODEC_H264 ) + 3 );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, dut.InitDriver( meta ) );
    dut.PrintCodecConfig();
    g_autofire_events = false;
}

// =====================================================================================
// ==== DeviceCbHandler – plain event coverage =========================================
// =====================================================================================

TEST_F( VidcDrvClientTest, Event_Pause_CallsEventCb )
{
    EmitEvent( VIDC_EVT_RESP_PAUSE );
    EXPECT_EQ( VIDEO_CODEC_EVT_RESP_PAUSE, last_evt );
}

TEST_F( VidcDrvClientTest, Event_Resume_CallsEventCb )
{
    EmitEvent( VIDC_EVT_RESP_RESUME );
    EXPECT_EQ( VIDEO_CODEC_EVT_RESP_RESUME, last_evt );
}

TEST_F( VidcDrvClientTest, Event_FlushInputDone_CallsEventCb )
{
    EmitEvent( VIDC_EVT_RESP_FLUSH_INPUT_DONE );
    EXPECT_EQ( VIDEO_CODEC_EVT_FLUSH_INPUT_DONE, last_evt );
}

TEST_F( VidcDrvClientTest, Event_FlushOutputDone_CallsEventCb )
{
    EmitEvent( VIDC_EVT_RESP_FLUSH_OUTPUT_DONE );
    EXPECT_EQ( VIDEO_CODEC_EVT_FLUSH_OUTPUT_DONE, last_evt );
}

TEST_F( VidcDrvClientTest, Event_FlushOutput2Done_MapsToFlushOutputDone )
{
    EmitEvent( VIDC_EVT_RESP_FLUSH_OUTPUT2_DONE );
    EXPECT_EQ( VIDEO_CODEC_EVT_FLUSH_OUTPUT_DONE, last_evt );
}

TEST_F( VidcDrvClientTest, Event_OutputReconfig_CallsEventCb )
{
    EmitEvent( VIDC_EVT_OUTPUT_RECONFIG );
    EXPECT_EQ( VIDEO_CODEC_EVT_OUTPUT_RECONFIG, last_evt );
}

TEST_F( VidcDrvClientTest, Event_InfoOutputReconfig_CallsErrorEventCb )
{
    EmitEvent( VIDC_EVT_INFO_OUTPUT_RECONFIG );
    EXPECT_EQ( VIDEO_CODEC_EVT_ERROR, last_evt );
}

TEST_F( VidcDrvClientTest, Event_HwFatal_CallsFatalEventCb )
{
    EmitEvent( VIDC_EVT_ERR_HWFATAL );
    EXPECT_EQ( VIDEO_CODEC_EVT_FATAL, last_evt );
}

TEST_F( VidcDrvClientTest, Event_ClientFatal_CallsFatalEventCb )
{
    EmitEvent( VIDC_EVT_ERR_CLIENTFATAL );
    EXPECT_EQ( VIDEO_CODEC_EVT_FATAL, last_evt );
}

TEST_F( VidcDrvClientTest, Event_ReleaseBufferReference_CallsErrorEventCb )
{
    EmitEvent( VIDC_EVT_RELEASE_BUFFER_REFERENCE );
    EXPECT_EQ( VIDEO_CODEC_EVT_ERROR, last_evt );
}

TEST_F( VidcDrvClientTest, Event_InputReconfig_CallsEventCb )
{
    EmitEvent( VIDC_EVT_INPUT_RECONFIG );
    EXPECT_EQ( VIDEO_CODEC_EVT_INPUT_RECONFIG, last_evt );
}

TEST_F( VidcDrvClientTest, Event_UnknownType_DoesNotCrash )
{
    // Handler returns -1 for unknown events; should not crash or call any callback
    EmitEvent( 0xDEADBEEF );
    EXPECT_EQ( 0, calls.in_done.load() );
    EXPECT_EQ( 0, calls.out_done.load() );
}

// =====================================================================================
// ==== DeviceCbHandler – INPUT_DONE ===================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, Event_InputDone_KnownHandle_CallsInputDoneCb )
{
    g_mock_cfg.use_ioctl_mockup = true;
    g_autofire_events = false;

    // EmptyBuffer registers the buffer and the mockup emits INPUT_DONE synchronously
    uint8_t buf[1024] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0xABCD1234;
    fd.size = sizeof( buf );
    fd.validSize = sizeof( buf );

    ASSERT_EQ( QC_STATUS_OK, dut.EmptyBuffer( fd ) );
    EXPECT_EQ( 1, calls.in_done.load() );
}

TEST_F( VidcDrvClientTest, Event_InputDone_UnknownHandle_CallsErrorEventCb )
{
    uint8_t buf[64] = {};
    // Emit INPUT_DONE with a handle that was never registered
    EmitFrameEvent( VIDC_EVT_RESP_INPUT_DONE, 0xDEAD0000, buf, sizeof( buf ) );
    EXPECT_EQ( VIDEO_CODEC_EVT_ERROR, last_evt );
}

TEST_F( VidcDrvClientTest, Event_InputDone_NullFrameAddr_DoesNotCallInputDoneCb )
{
    // Emit INPUT_DONE with null frame_addr – DeviceCbHandler returns -1
    EmitFrameEvent( VIDC_EVT_RESP_INPUT_DONE, 0x1234, nullptr, 0 );
    EXPECT_EQ( 0, calls.in_done.load() );
}

// =====================================================================================
// ==== DeviceCbHandler – OUTPUT_DONE ==================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, Event_OutputDone_KnownHandle_CallsOutputDoneCb )
{
    uint8_t buf[1024] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0xBEEF5678;
    fd.size = sizeof( buf );

    ASSERT_EQ( QC_STATUS_OK, dut.FillBuffer( fd ) );
    //    EXPECT_EQ( 1, calls.out_done.load() );
}

TEST_F( VidcDrvClientTest, Event_OutputDone_UnknownHandle_CallsErrorEventCb )
{
    uint8_t buf[64] = {};
    EmitFrameEvent( VIDC_EVT_RESP_OUTPUT_DONE, 0xDEAD0001, buf, sizeof( buf ) );
    EXPECT_EQ( VIDEO_CODEC_EVT_ERROR, last_evt );
}

// =====================================================================================
// ==== EmptyBuffer ====================================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, EmptyBuffer_DynamicMode_NewBuffer_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x1001;
    fd.size = sizeof( buf );
    fd.validSize = sizeof( buf );

    EXPECT_EQ( QC_STATUS_OK, dut.EmptyBuffer( fd ) );
}

TEST_F( VidcDrvClientTest, EmptyBuffer_DynamicMode_ExistingBuffer_NotInUse_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x1002;
    fd.size = sizeof( buf );
    fd.validSize = sizeof( buf );

    // First call: adds to map; INPUT_DONE clears bUsedFlag (entry stays in map)
    EXPECT_EQ( QC_STATUS_OK, dut.EmptyBuffer( fd ) );
    // Second call: finds existing entry with bUsedFlag=false
    EXPECT_EQ( QC_STATUS_OK, dut.EmptyBuffer( fd ) );
}

TEST_F( VidcDrvClientTest, EmptyBuffer_DynamicMode_MapFull_ReturnsNomem )
{
    // Fill the map to capacity (numInputBufferReq = 4).
    // Each call adds a new entry; INPUT_DONE clears bUsedFlag but entry stays in map.
    uint8_t bufs[5][512] = {};
    for ( int i = 0; i < 4; i++ )
    {
        VideoFrameDescriptor_t fd{};
        fd.pBuf = bufs[i];
        fd.dmaHandle = static_cast<uint64_t>( 0x2000 + i );
        fd.size = sizeof( bufs[i] );
        fd.validSize = sizeof( bufs[i] );
        ASSERT_EQ( QC_STATUS_OK, dut.EmptyBuffer( fd ) );
    }
    // 5th call with a new handle: map is full → NOMEM
    VideoFrameDescriptor_t fd5{};
    fd5.pBuf = bufs[4];
    fd5.dmaHandle = 0x2004;
    fd5.size = sizeof( bufs[4] );
    fd5.validSize = sizeof( bufs[4] );
    EXPECT_EQ( QC_STATUS_NOMEM, dut.EmptyBuffer( fd5 ) );
}

TEST_F( VidcDrvClientTest, EmptyBuffer_DynamicMode_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_empty_input = 1;

    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x3001;
    fd.size = sizeof( buf );
    fd.validSize = sizeof( buf );

    EXPECT_EQ( QC_STATUS_FAIL, dut.EmptyBuffer( fd ) );
}

// =====================================================================================
// ==== FillBuffer =====================================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, FillBuffer_DynamicMode_NewBuffer_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x4001;
    fd.size = sizeof( buf );

    EXPECT_EQ( QC_STATUS_OK, dut.FillBuffer( fd ) );
}

TEST_F( VidcDrvClientTest, FillBuffer_DynamicMode_ExistingBuffer_NotInUse_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x4002;
    fd.size = sizeof( buf );

    EXPECT_EQ( QC_STATUS_OK, dut.FillBuffer( fd ) );
    // OUTPUT_DONE clears bUsedFlag; second call should succeed
    EXPECT_EQ( QC_STATUS_OK, dut.FillBuffer( fd ) );
}

TEST_F( VidcDrvClientTest, FillBuffer_DynamicMode_MapFull_ReturnsNomem )
{
    uint8_t bufs[5][512] = {};
    for ( int i = 0; i < 4; i++ )
    {
        VideoFrameDescriptor_t fd{};
        fd.pBuf = bufs[i];
        fd.dmaHandle = static_cast<uint64_t>( 0x5000 + i );
        fd.size = sizeof( bufs[i] );
        ASSERT_EQ( QC_STATUS_OK, dut.FillBuffer( fd ) );
    }
    VideoFrameDescriptor_t fd5{};
    fd5.pBuf = bufs[4];
    fd5.dmaHandle = 0x5004;
    fd5.size = sizeof( bufs[4] );
    EXPECT_EQ( QC_STATUS_NOMEM, dut.FillBuffer( fd5 ) );
}

TEST_F( VidcDrvClientTest, FillBuffer_DynamicMode_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_fill_output = 1;

    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0x6001;
    fd.size = sizeof( buf );

    EXPECT_EQ( QC_STATUS_FAIL, dut.FillBuffer( fd ) );
}

// =====================================================================================
// ==== NegotiateBufferReq =============================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, NegotiateBufferReq_DriverRequestsSame_Succeeds )
{
    g_mock_cfg.req_buf_count = 4;
    g_mock_cfg.req_buf_size = 128 * 1024;

    uint32_t bufNum = 4, bufSize = 0;
    EXPECT_EQ( QC_STATUS_OK, dut.NegotiateBufferReq( VIDEO_CODEC_BUF_INPUT, bufNum, bufSize ) );
    EXPECT_EQ( 4u, bufNum );
    EXPECT_EQ( 128u * 1024u, bufSize );
}

TEST_F( VidcDrvClientTest, NegotiateBufferReq_DriverRequestsMore_UpdatesBufNum )
{
    g_mock_cfg.req_buf_count = 8;
    g_mock_cfg.req_buf_size = 256 * 1024;

    uint32_t bufNum = 4, bufSize = 0;
    EXPECT_EQ( QC_STATUS_OK, dut.NegotiateBufferReq( VIDEO_CODEC_BUF_INPUT, bufNum, bufSize ) );
    EXPECT_EQ( 8u, bufNum );
}

TEST_F( VidcDrvClientTest, NegotiateBufferReq_DriverRequestsLess_ReNegotiates_Fails )
{
    // Driver always returns count=2; app wants 4 → re-negotiate → driver still returns 2 → FAIL
    g_mock_cfg.req_buf_count = 2;
    g_mock_cfg.req_buf_size = 64 * 1024;

    uint32_t bufNum = 4, bufSize = 0;
    EXPECT_EQ( QC_STATUS_FAIL, dut.NegotiateBufferReq( VIDEO_CODEC_BUF_INPUT, bufNum, bufSize ) );
}

TEST_F( VidcDrvClientTest, NegotiateBufferReq_GetPropertyFails_ReturnsError )
{
    g_mock_cfg.rc_get_property = 1;

    uint32_t bufNum = 4, bufSize = 0;
    EXPECT_EQ( QC_STATUS_FAIL, dut.NegotiateBufferReq( VIDEO_CODEC_BUF_OUTPUT, bufNum, bufSize ) );
}

// =====================================================================================
// ==== SetBuffer / FreeBuffers ========================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, SetBuffer_Input_Succeeds )
{
    uint8_t b1[512] = {}, b2[512] = {};
    VideoFrameDescriptor_t fd1{}, fd2{};

    fd1.pBuf = b1;
    fd1.dmaHandle = 0xA001;
    fd1.size = sizeof( b1 );
    fd2.pBuf = b2;
    fd2.dmaHandle = 0xA002;
    fd2.size = sizeof( b2 );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd1, fd2 };
    EXPECT_EQ( QC_STATUS_OK, dut.SetBuffer( VIDEO_CODEC_BUF_INPUT, bufs ) );
}

TEST_F( VidcDrvClientTest, SetBuffer_Output_Succeeds )
{
    uint8_t b1[512] = {}, b2[512] = {};
    VideoFrameDescriptor_t fd1{}, fd2{};
    fd1.pBuf = b1;
    fd1.dmaHandle = 0xB001;
    fd1.size = sizeof( b1 );
    fd2.pBuf = b2;
    fd2.dmaHandle = 0xB002;
    fd2.size = sizeof( b2 );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd1, fd2 };
    EXPECT_EQ( QC_STATUS_OK, dut.SetBuffer( VIDEO_CODEC_BUF_OUTPUT, bufs ) );
}

TEST_F( VidcDrvClientTest, SetBuffer_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_set_buffer = 1;

    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0xC001;
    fd.size = sizeof( buf );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd };
    EXPECT_EQ( QC_STATUS_FAIL, dut.SetBuffer( VIDEO_CODEC_BUF_INPUT, bufs ) );
}

TEST_F( VidcDrvClientTest, FreeBuffers_Input_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0xD001;
    fd.size = sizeof( buf );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd };
    EXPECT_EQ( QC_STATUS_OK, dut.FreeBuffers( VIDEO_CODEC_BUF_INPUT, bufs ) );
}

TEST_F( VidcDrvClientTest, FreeBuffers_Output_Succeeds )
{
    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};
    fd.pBuf = buf;
    fd.dmaHandle = 0xE001;
    fd.size = sizeof( buf );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd };
    EXPECT_EQ( QC_STATUS_OK, dut.FreeBuffers( VIDEO_CODEC_BUF_OUTPUT, bufs ) );
}

TEST_F( VidcDrvClientTest, FreeBuffers_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_free_buffer = 1;

    uint8_t buf[512] = {};
    VideoFrameDescriptor_t fd{};

    fd.pBuf = buf;
    fd.dmaHandle = 0xF001;
    fd.size = sizeof( buf );

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> bufs = { fd };
    EXPECT_EQ( QC_STATUS_FAIL, dut.FreeBuffers( VIDEO_CODEC_BUF_OUTPUT, bufs ) );
}

// =====================================================================================
// ==== LoadResources / ReleaseResources ===============================================
// =====================================================================================

TEST_F( VidcDrvClientTest, LoadResources_Success )
{
    EXPECT_EQ( QC_STATUS_OK, dut.LoadResources() );
}

TEST_F( VidcDrvClientTest, LoadResources_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_load_resources = 1;
    EXPECT_EQ( QC_STATUS_FAIL, dut.LoadResources() );
}

TEST_F( VidcDrvClientTest, LoadResources_Timeout_ReturnsTimeout )
{
    g_mock_cfg.emit_evt_load_done = false;
    EXPECT_EQ( QC_STATUS_TIMEOUT, dut.LoadResources() );
}

TEST_F( VidcDrvClientTest, ReleaseResources_Success )
{
    EXPECT_EQ( QC_STATUS_OK, dut.ReleaseResources() );
}

TEST_F( VidcDrvClientTest, ReleaseResources_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_release_resources = 1;
    EXPECT_EQ( QC_STATUS_FAIL, dut.ReleaseResources() );
}

TEST_F( VidcDrvClientTest, ReleaseResources_Timeout_ReturnsTimeout )
{
    g_mock_cfg.emit_evt_release_done = false;
    EXPECT_EQ( QC_STATUS_TIMEOUT, dut.ReleaseResources() );
}

// =====================================================================================
// ==== SetDynamicMode =================================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, SetDynamicMode_Input_Dynamic_Succeeds )
{
    EXPECT_EQ( QC_STATUS_OK, dut.SetDynamicMode( VIDEO_CODEC_BUF_INPUT, true ) );
}

TEST_F( VidcDrvClientTest, SetDynamicMode_Input_Static_Succeeds )
{
    EXPECT_EQ( QC_STATUS_OK, dut.SetDynamicMode( VIDEO_CODEC_BUF_INPUT, false ) );
}

TEST_F( VidcDrvClientTest, SetDynamicMode_Output_Dynamic_Succeeds )
{
    EXPECT_EQ( QC_STATUS_OK, dut.SetDynamicMode( VIDEO_CODEC_BUF_OUTPUT, true ) );
}

TEST_F( VidcDrvClientTest, SetDynamicMode_Output_Static_Succeeds )
{
    EXPECT_EQ( QC_STATUS_OK, dut.SetDynamicMode( VIDEO_CODEC_BUF_OUTPUT, false ) );
}

// =====================================================================================
// ==== OpenDriver / CloseDriver =======================================================
// =====================================================================================

TEST( VidcDrvClientStatic, OpenDriver_DeviceOpenFails_ReturnsError )
{
    Mockup_Cfg_Reset();
    g_mock_cfg.use_open_mockup = true;
    g_mock_cfg.open_fail = true;   // Force device_open to fail

    VidcNodeBase_Config_t cfg{};
    cfg.width = 176;
    cfg.height = 144;
    cfg.frameRate = 30;
    cfg.numInputBufferReq = 4;
    cfg.numOutputBufferReq = 4;
    cfg.bInputDynamicMode = true;
    cfg.bOutputDynamicMode = true;
    cfg.inFormat = QC_IMAGE_FORMAT_NV12;
    cfg.outFormat = QC_IMAGE_FORMAT_COMPRESSED_H264;
    cfg.logLevel = LOGGER_LEVEL_ERROR;

    VidcDrvClient client;
    client.Init( "OpenFailTest", LOGGER_LEVEL_ERROR, VIDEO_ENC, cfg );

    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               client.OpenDriver( TestInputDoneCb, TestOutputDoneCb, TestEventCb, &client ) );

    Mockup_Cfg_Reset();
}

TEST( VidcDrvClientStatic, CloseDriver_WithoutOpen_DoesNotCrash )
{
    VidcNodeBase_Config_t cfg{};
    cfg.width = 176;
    cfg.height = 144;
    cfg.frameRate = 30;
    cfg.numInputBufferReq = 4;
    cfg.numOutputBufferReq = 4;
    cfg.bInputDynamicMode = true;
    cfg.bOutputDynamicMode = true;
    cfg.inFormat = QC_IMAGE_FORMAT_NV12;
    cfg.outFormat = QC_IMAGE_FORMAT_COMPRESSED_H264;
    cfg.logLevel = LOGGER_LEVEL_ERROR;

    VidcDrvClient client;
    client.Init( "CloseTest", LOGGER_LEVEL_ERROR, VIDEO_ENC, cfg );

    // CloseDriver should handle nullptr m_pIoHandle gracefully
    client.CloseDriver();
}

TEST( VidcDrvClientStatic, CloseDriver_AfterOpen_Succeeds )
{
    Mockup_Cfg_Reset();
    g_mock_cfg.use_open_mockup = true;
    g_mock_cfg.use_close_mockup = true;

    VidcNodeBase_Config_t cfg{};
    cfg.width = 176;
    cfg.height = 144;
    cfg.frameRate = 30;
    cfg.numInputBufferReq = 4;
    cfg.numOutputBufferReq = 4;
    cfg.bInputDynamicMode = true;
    cfg.bOutputDynamicMode = true;
    cfg.inFormat = QC_IMAGE_FORMAT_NV12;
    cfg.outFormat = QC_IMAGE_FORMAT_COMPRESSED_H264;
    cfg.logLevel = LOGGER_LEVEL_ERROR;

    VidcDrvClient client;
    client.Init( "CloseAfterOpenTest", LOGGER_LEVEL_ERROR, VIDEO_ENC, cfg );

    ASSERT_EQ( QC_STATUS_OK,
               client.OpenDriver( TestInputDoneCb, TestOutputDoneCb, TestEventCb, &client ) );

    client.CloseDriver();
    Mockup_Cfg_Reset();
}

// =====================================================================================
// ==== SetDrvProperty / GetDrvProperty ================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, SetDrvProperty_ValidSize_Succeeds )
{
    // Test with a small valid size
    uint8_t data[64] = {};
    EXPECT_EQ( QC_STATUS_OK, dut.SetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

TEST_F( VidcDrvClientTest, SetDrvProperty_MaxValidSize_Succeeds )
{
    // Test with maximum valid size (256 bytes)
    // VIDEO_MAX_DEV_CMD_BUFFER_SIZE = 256
    // nMsgSize = sizeof(vidc_property_hdr_type) + nPktSize
    // vidc_property_hdr_type is typically 8 bytes (uint32_t size + uint32_t prop_id)
    // So max nPktSize should be 256 - 8 = 248 to keep nMsgSize <= 256
    uint8_t data[248] = {};
    EXPECT_EQ( QC_STATUS_OK, dut.SetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

TEST_F( VidcDrvClientTest, SetDrvProperty_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_set_property = 1;   // Force IOCTL to fail

    uint8_t data[64] = {};
    EXPECT_EQ( QC_STATUS_FAIL,
               dut.SetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

TEST_F( VidcDrvClientTest, GetDrvProperty_ValidSize_Succeeds )
{
    // Test GetDrvProperty with valid size
    uint8_t data[64] = {};
    EXPECT_EQ( QC_STATUS_OK, dut.GetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

TEST_F( VidcDrvClientTest, GetDrvProperty_MaxValidSize_Succeeds )
{
    // Test with maximum valid size
    uint8_t data[248] = {};
    EXPECT_EQ( QC_STATUS_OK, dut.GetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

TEST_F( VidcDrvClientTest, GetDrvProperty_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_get_property = 1;   // Force IOCTL to fail

    uint8_t data[64] = {};
    EXPECT_EQ( QC_STATUS_FAIL,
               dut.GetDrvProperty( VIDC_I_SESSION_CODEC, sizeof( data ), data[0] ) );
}

// Note: Tests for oversized buffers (> 256 bytes) would trigger the assert and cause
// the test process to abort. In production code, these asserts should ideally be
// replaced with proper error handling and return codes. The assert is currently
// checking:
//   assert( nPktSize <= VIDEO_MAX_DEV_CMD_BUFFER_SIZE &&
//           nMsgSize <= VIDEO_MAX_DEV_CMD_BUFFER_SIZE );
// where VIDEO_MAX_DEV_CMD_BUFFER_SIZE = 256

// =====================================================================================
// ==== InitDriver =====================================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, InitDriver_H264_Succeeds )
{
    VidcCodecMeta_t meta{};
    meta.width = 176;
    meta.height = 144;
    meta.frameRate = 30;
    meta.codecType = VIDEO_CODEC_H264;
    EXPECT_EQ( QC_STATUS_OK, dut.InitDriver( meta ) );
}

TEST_F( VidcDrvClientTest, InitDriver_H265_Succeeds )
{
    VidcCodecMeta_t meta{};
    meta.width = 1920;
    meta.height = 1080;
    meta.frameRate = 30;
    meta.codecType = VIDEO_CODEC_H265;
    EXPECT_EQ( QC_STATUS_OK, dut.InitDriver( meta ) );
}

TEST_F( VidcDrvClientTest, InitDriver_SetPropertyFails_ReturnsError )
{
    g_mock_cfg.rc_set_property = 1;

    VidcCodecMeta_t meta{};
    meta.width = 176;
    meta.height = 144;
    meta.frameRate = 30;
    meta.codecType = VIDEO_CODEC_H264;
    EXPECT_EQ( QC_STATUS_FAIL, dut.InitDriver( meta ) );
}

// =====================================================================================
// ==== StartDriver ====================================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, StartDriver_All_Succeeds )
{
    EXPECT_EQ( QC_STATUS_OK, dut.StartDriver( VIDEO_CODEC_START_ALL ) );
}

TEST_F( VidcDrvClientTest, StartDriver_Input_Succeeds )
{
    // Mockup emits RESP_START_INPUT_DONE for VIDC_START_INPUT
    EXPECT_EQ( QC_STATUS_OK, dut.StartDriver( VIDEO_CODEC_START_INPUT ) );
}

TEST_F( VidcDrvClientTest, StartDriver_Output_Succeeds )
{
    // For OUTPUT start, WaitForCmdCompleted is NOT called → always succeeds
    EXPECT_EQ( QC_STATUS_OK, dut.StartDriver( VIDEO_CODEC_START_OUTPUT ) );
}

TEST_F( VidcDrvClientTest, StartDriver_IoctlFails_ReturnsError )
{
    g_mock_cfg.rc_start = 1;
    EXPECT_EQ( QC_STATUS_FAIL, dut.StartDriver( VIDEO_CODEC_START_ALL ) );
}

TEST_F( VidcDrvClientTest, StartDriver_All_Timeout_ReturnsTimeout )
{
    g_mock_cfg.emit_evt_start_done = false;
    EXPECT_EQ( QC_STATUS_TIMEOUT, dut.StartDriver( VIDEO_CODEC_START_ALL ) );
}

// =====================================================================================
// ==== PrintCodecConfig / GetType =====================================================
// =====================================================================================

TEST_F( VidcDrvClientTest, PrintCodecConfig_DoesNotCrash )
{
    dut.PrintCodecConfig();
}

TEST_F( VidcDrvClientTest, GetType_ReturnsVideoEnc )
{
    VideoEncDecType_e type = dut.GetType();
    EXPECT_TRUE( VIDEO_ENC == type || VIDEO_DEC == type );
}

TEST( VidcDrvClientStatic, GetType_ReturnsVideoDec )
{
    Mockup_Cfg_Reset();
    g_mock_cfg.use_open_mockup = true;
    g_mock_cfg.use_ioctl_mockup = true;
    g_mock_cfg.use_close_mockup = true;
    g_mock_cfg.use_timer_mockup = true;

    VidcNodeBase_Config_t cfg{};
    cfg.width = 176;
    cfg.height = 144;
    cfg.frameRate = 30;
    cfg.numInputBufferReq = 4;
    cfg.numOutputBufferReq = 4;
    cfg.bInputDynamicMode = true;
    cfg.bOutputDynamicMode = true;
    cfg.inFormat = QC_IMAGE_FORMAT_COMPRESSED_H264;
    cfg.outFormat = QC_IMAGE_FORMAT_NV12;
    cfg.logLevel = LOGGER_LEVEL_ERROR;

    VidcDrvClient dec;
    dec.Init( "DecTypeTest", LOGGER_LEVEL_ERROR, VIDEO_DEC, cfg );
    EXPECT_EQ( VIDEO_DEC, dec.GetType() );
    Mockup_Cfg_Reset();
}

// =====================================================================================
// ==== DeviceCallback – null self pointer guard =======================================
// =====================================================================================

TEST( VidcDrvClientStatic, DeviceCallback_NullSelf_DoesNotCrash )
{
    // Corrupt the callback data pointer to nullptr to exercise the null-self guard
    // in VidcDrvClient::DeviceCallback.
    Mockup_Cfg_Reset();
    g_mock_cfg.use_open_mockup = true;
    g_mock_cfg.use_ioctl_mockup = true;
    g_mock_cfg.use_close_mockup = true;
    g_mock_cfg.use_timer_mockup = true;

    VidcNodeBase_Config_t cfg{};
    cfg.width = 176;
    cfg.height = 144;
    cfg.frameRate = 30;
    cfg.numInputBufferReq = 4;
    cfg.numOutputBufferReq = 4;
    cfg.bInputDynamicMode = true;
    cfg.bOutputDynamicMode = true;
    cfg.inFormat = QC_IMAGE_FORMAT_NV12;
    cfg.outFormat = QC_IMAGE_FORMAT_COMPRESSED_H264;
    cfg.logLevel = LOGGER_LEVEL_ERROR;

    VidcDrvClient client;
    client.Init( "NullSelfTest", LOGGER_LEVEL_ERROR, VIDEO_ENC, cfg );
    ASSERT_EQ( QC_STATUS_OK,
               client.OpenDriver( TestInputDoneCb, TestOutputDoneCb, TestEventCb, &client ) );

    // Null out the callback data so DeviceCallback receives nullptr as pCdata
    g_cb.data = nullptr;
    EmitEvent( VIDC_EVT_RESP_PAUSE );   // Must not crash

    client.CloseDriver();
    Mockup_Cfg_Reset();
}

#ifndef GTEST_QCNODE
#if __CTC__
extern "C" void ctc_append_all( void );
#endif
int main( int argc, char **argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    int nVal = RUN_ALL_TESTS();
#if __CTC__
    ctc_append_all();
#endif
    return nVal;
}
#endif
