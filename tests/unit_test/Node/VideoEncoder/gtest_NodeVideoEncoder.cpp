// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <stdio.h>
#include <string>
#include "QC/Node/VideoEncoder.hpp"
#include "QC/sample/BufferManager.hpp"
#include "vidc_driver_mockup.hpp"

using namespace QC;
using namespace QC::Node;
using namespace QC::sample;

static std::mutex g_IoctlMutex;
static std::mutex g_InMutex;
static std::condition_variable g_InCondVar;
static std::mutex g_OutMutex;
static std::condition_variable g_OutCondVar;
static uint32_t g_nodeId = 0;
static uint64_t g_timestamp = 0;

void OnDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    QCFrameDescriptorNodeIfs &frameDesc = eventInfo.frameDesc;

    // INPUT frame:
    QCBufferDescriptorBase_t &inBufDesc =
            frameDesc.GetBuffer( QC_NODE_VIDEO_ENCODER_INPUT_BUFF_ID );
    const VideoFrameDescriptor_t *pInSharedBuffer =
            dynamic_cast<VideoFrameDescriptor_t *>( &inBufDesc );
    if ( nullptr != pInSharedBuffer )
    {
        ASSERT_EQ( QC_STATUS_OK, eventInfo.status );
        ASSERT_EQ( g_nodeId, eventInfo.node.id );
    }
    // Send signal
    g_InCondVar.notify_one();

    // OUTPUT frame:
    QCBufferDescriptorBase_t &outBufDesc =
            frameDesc.GetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID );
    const VideoFrameDescriptor_t *pOutSharedBuffer =
            dynamic_cast<VideoFrameDescriptor_t *>( &outBufDesc );
    if ( nullptr != pOutSharedBuffer )
    {
        ASSERT_EQ( QC_STATUS_OK, eventInfo.status );
        ASSERT_EQ( g_nodeId, eventInfo.node.id );
    }
    // Send signal
    g_OutCondVar.notify_one();
}

// Mockup
typedef union
{
    vidc_profile_type profile;        /**< The codec profile payload */
    vidc_buffer_reqmnts_type reqmnts; /* Buffer requirements */
    vidc_level_type level;            /**< The codec level payload */
    vidc_frame_rate_type frameRate;   /**< Frame rate for Encoder */
    vidc_iperiod_type iPeriod;      /**< The data type to set I frame period pattern for encoder */
    vidc_idr_period_type idrPeriod; /**< The IDR frame periodicity within Intra coded frames */
    vidc_target_bitrate_type bitrate;    /**< The encoder target bitrate */
    vidc_enable_type enableSyncFrameSeq; /**< Enable sync frame sequence header */
    vidc_plane_def_type planeDefY;       /**< Specifies layout of raw data for planeY */
    vidc_plane_def_type planeDefUV;      /**< Specifies layout of raw data for planeUV */
} __mockup_VidcEncoderData_t;

typedef struct __mockup_vidc_drv_property_type_t
{
    vidc_property_hdr_type prop_hdr; /**< vidc property header >*/
    __mockup_VidcEncoderData_t payload;
} __mockup_vidc_drv_property_type;

struct MockupVidcConfig
{
    bool use_open_mockup = false;
    bool use_ioctl_mockup = false;
    bool use_close_mockup = false;
    bool use_timer_mockup = false;
    bool open_fail = false;

    // Per-IOCTL return codes (0 = success)
    int rc_load_resources = 0;
    int rc_release_resources = 0;
    int rc_start = 0;
    int rc_stop = 0;
    int rc_set_property = 0;   // fallback for any SET_PROPERTY
    int rc_get_property = 0;   // fallback for any GET_PROPERTY

    // Fine-grained SET_PROPERTY failures (0 = success; nonzero => fail only that property)
    int rc_set_prop_rate_control = 0;   // VIDC_I_ENC_RATE_CONTROL
    int rc_set_prop_color_format = 0;   // VIDC_I_COLOR_FORMAT
    int rc_set_prop_gop = 0;            // VIDC_I_ENC_INTRA_PERIOD
    int rc_set_prop_idr = 0;            // VIDC_I_ENC_IDR_PERIOD
    int rc_set_prop_bitrate = 0;        // VIDC_I_TARGET_BITRATE
    int rc_set_prop_sync_hdr = 0;       // VIDC_I_ENC_SYNC_FRAME_SEQ_HDR
    int rc_set_prop_profile = 0;        // VIDC_I_PROFILE
    int rc_set_prop_level = 0;          // VIDC_I_LEVEL
    int rc_set_prop_spatial = 0;        // VIDC_I_VPE_SPATIAL_TRANSFORM

    // Fine-grained GET_PROPERTY failures (0 = success; nonzero => fail only that property)
    int rc_get_prop_frame_rate = 0;     // VIDC_I_FRAME_RATE
    int rc_get_prop_plane_def_y = 0;    // VIDC_I_PLANE_DEF (plane_index=1)
    int rc_get_prop_plane_def_uv = 0;   // VIDC_I_PLANE_DEF (plane_index=2)
    int rc_get_prop_buf_req = 0;        // VIDC_I_BUFFER_REQUIREMENTS

    int rc_set_buffer = 0;
    int rc_free_buffer = 0;
    int rc_empty_input = 0;
    int rc_fill_output = 0;

    // Whether to emit completion events (simulate driver async callbacks)
    bool emit_evt_load_done = true;
    bool emit_evt_start_all = true;   // VIDC_EVT_RESP_START
    bool emit_evt_start_input_done = true;
    bool emit_evt_start_done = true;
    bool emit_evt_stop_done = true;
    bool emit_evt_release_done = true;

    // Simulated properties:
    uint32_t prop_frame_rate_num = 30;   // fps numerator
    uint32_t prop_frame_rate_den = 1;    // fps denominator
    uint32_t prop_in_plane_stride_y = 320;
    uint32_t prop_in_plane_stride_uv = 320;

    // “Property” payloads your framework expects
    // (adapt to real structs/IDs in your project)
    uint32_t fps_num = 30, fps_den = 1;
    vidc_buffer_type buf_type = VIDC_BUFFER_INPUT;
    uint32_t req_in_count = 4, req_out_count = 4;
    uint32_t req_in_size = 128 * 1024, req_out_size = 128 * 1024;

    __mockup_vidc_drv_property_type prop[16];
    unsigned int curr_prop_idx = 0;
};

static struct MockupVidcConfig g_mockup_cfg;

void Mockup_Cfg_Reset()
{
    g_mockup_cfg = MockupVidcConfig{};
}

// ----------------------------------------------------------------------------
// Fake device state (very small; extend if you need concurrency, etc.)
// ----------------------------------------------------------------------------
struct fake_handle
{
    ioctl_callback_t cb{};
    bool opened = false;
};
struct fake_handle g_dev;

// Helper: send an event to upper layers via the callback
void emit_evt( unsigned int event_type )
{
    if ( !g_dev.opened || !g_dev.cb.handler ) return;

    vidc_drv_msg_info_type msg{};
    msg.event_type = static_cast<vidc_event_type>( event_type );

    (void) g_dev.cb.handler( reinterpret_cast<unsigned char *>( &msg ), sizeof( msg ),
                             g_dev.cb.data );
}

// ----------------------------------------------------------------------------
// Optional: fast timer
// ----------------------------------------------------------------------------
int __mockup_MM_Timer_Sleep( unsigned int ms )
{
    if ( !g_mockup_cfg.use_timer_mockup )
    {
        usleep( ms % 1000 );
        for ( ; ( ms /= 1000 ); ) usleep( 1000 );
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Mockup driver entry points
// ----------------------------------------------------------------------------
void *__mockup_device_open( const char *path, ioctl_callback_t *cb )
{
    void *ret;
    if ( !g_mockup_cfg.use_open_mockup )
    {
        ret = device_open( (char *) path, cb );
    }
    else
    {
        if ( g_mockup_cfg.open_fail ) return nullptr;
        g_dev = {};
        if ( cb ) g_dev.cb = *cb;   // store callback
        g_dev.opened = true;
        ret = &g_dev;
    }
    return ret;
}

int __mockup_device_close( ioctl_session_t *handle )
{
    int ret = 0;
    if ( !g_mockup_cfg.use_close_mockup )
    {
        ret = device_close( handle );
    }
    else
    {
        g_dev = {};
    }
    return ret;
}

int __mockup_device_ioctl( ioctl_session_t *handle, unsigned int cmd, unsigned char *in,
                           unsigned int in_sz, unsigned char *out, unsigned int out_sz )
{
    int ret = 0;
    if ( !g_mockup_cfg.use_ioctl_mockup )
    {
        ret = device_ioctl( handle, cmd, in, in_sz, out, out_sz );
    }
    else
    {
        switch ( cmd )
        {
            case VIDC_IOCTL_LOAD_RESOURCES:
                if ( g_mockup_cfg.rc_load_resources ) return g_mockup_cfg.rc_load_resources;
                if ( g_mockup_cfg.emit_evt_load_done ) emit_evt( VIDC_EVT_RESP_LOAD_RESOURCES );
                return 0;

            case VIDC_IOCTL_RELEASE_RESOURCES:
                if ( g_mockup_cfg.rc_release_resources ) return g_mockup_cfg.rc_release_resources;
                if ( g_mockup_cfg.emit_evt_release_done )
                    emit_evt( VIDC_EVT_RESP_RELEASE_RESOURCES );
                return 0;

            case VIDC_IOCTL_START:
                if ( g_mockup_cfg.rc_start ) return g_mockup_cfg.rc_start;
                if ( g_mockup_cfg.emit_evt_start_done ) emit_evt( VIDC_EVT_RESP_START );
                // If your client expects START_INPUT_DONE/OUTPUT_DONE, emit those too.
                return 0;

            case VIDC_IOCTL_STOP:
                if ( g_mockup_cfg.rc_stop ) return g_mockup_cfg.rc_stop;
                if ( g_mockup_cfg.emit_evt_stop_done ) emit_evt( VIDC_EVT_RESP_STOP );
                return 0;

            case VIDC_IOCTL_SET_PROPERTY:
            {
                if ( g_mockup_cfg.rc_set_property ) return g_mockup_cfg.rc_set_property;
                if ( !in /* || in_sz < sizeof(__mockup_vidc_drv_property_type)*/ ) return -1;
                auto *p = reinterpret_cast<__mockup_vidc_drv_property_type *>( in );
                switch ( p->prop_hdr.prop_id )
                {
                    case VIDC_I_ENC_RATE_CONTROL:
                        return g_mockup_cfg.rc_set_prop_rate_control;
                    case VIDC_I_COLOR_FORMAT:
                        return g_mockup_cfg.rc_set_prop_color_format;
                    case VIDC_I_ENC_INTRA_PERIOD:
                        return g_mockup_cfg.rc_set_prop_gop;
                    case VIDC_I_ENC_IDR_PERIOD:
                        return g_mockup_cfg.rc_set_prop_idr;
                    case VIDC_I_TARGET_BITRATE:
                        return g_mockup_cfg.rc_set_prop_bitrate;
                    case VIDC_I_ENC_SYNC_FRAME_SEQ_HDR:
                        return g_mockup_cfg.rc_set_prop_sync_hdr;
                    case VIDC_I_PROFILE:
                        return g_mockup_cfg.rc_set_prop_profile;
                    case VIDC_I_LEVEL:
                        return g_mockup_cfg.rc_set_prop_level;
                    case VIDC_I_VPE_SPATIAL_TRANSFORM:
                        return g_mockup_cfg.rc_set_prop_spatial;
                    default:
                        return 0;
                }
            }

            case VIDC_IOCTL_GET_PROPERTY:
            {
                if ( !in /*|| in_sz < sizeof(__mockup_vidc_drv_property_type)*/ ) return -1;
                auto *p = reinterpret_cast<__mockup_vidc_drv_property_type *>( in );

                // Fine-grained failures first
                if ( p->prop_hdr.prop_id == VIDC_I_FRAME_RATE &&
                     g_mockup_cfg.rc_get_prop_frame_rate )
                    return -1;
                if ( p->prop_hdr.prop_id == VIDC_I_BUFFER_REQUIREMENTS &&
                     g_mockup_cfg.rc_get_prop_buf_req )
                    return -1;
                if ( p->prop_hdr.prop_id == VIDC_I_PLANE_DEF )
                {
                    if ( out && out_sz >= sizeof( vidc_plane_def_type ) )
                    {
                        auto *pd = reinterpret_cast<vidc_plane_def_type *>( out );
                        if ( pd->plane_index == 1 && g_mockup_cfg.rc_get_prop_plane_def_y )
                            return -1;
                        if ( pd->plane_index == 2 && g_mockup_cfg.rc_get_prop_plane_def_uv )
                            return -1;
                    }
                }

                // Legacy scripted behavior (prop[] ring)
                if ( g_mockup_cfg.curr_prop_idx <
                             sizeof( g_mockup_cfg.prop ) / sizeof( g_mockup_cfg.prop[0] ) &&
                     g_mockup_cfg.prop[g_mockup_cfg.curr_prop_idx].prop_hdr.size != 0 )
                {
                    std::unique_lock<std::mutex> inLock( g_IoctlMutex );
                    std::memcpy( out, &g_mockup_cfg.prop[g_mockup_cfg.curr_prop_idx].payload,
                                 std::min( out_sz, g_mockup_cfg.prop[g_mockup_cfg.curr_prop_idx]
                                                           .prop_hdr.size ) );
                    g_mockup_cfg.curr_prop_idx++;
                    return 0;
                }

                // On-the-fly synthesis by property id
                switch ( p->prop_hdr.prop_id )
                {
                    case VIDC_I_FRAME_RATE:
                        if ( out && out_sz >= sizeof( vidc_frame_rate_type ) )
                        {
                            auto *fr = reinterpret_cast<vidc_frame_rate_type *>( out );
                            fr->buf_type = VIDC_BUFFER_INPUT;
                            fr->fps_numerator = g_mockup_cfg.prop_frame_rate_num;
                            fr->fps_denominator = g_mockup_cfg.prop_frame_rate_den;
                            return 0;
                        }
                        return -1;

                    case VIDC_I_PLANE_DEF:
                        if ( out && out_sz >= sizeof( vidc_plane_def_type ) )
                        {
                            auto *pd = reinterpret_cast<vidc_plane_def_type *>( out );
                            pd->actual_stride =
                                    ( pd->plane_index == 1 )
                                            ? static_cast<int32_t>(
                                                      g_mockup_cfg.prop_in_plane_stride_y )
                                            : static_cast<int32_t>(
                                                      g_mockup_cfg.prop_in_plane_stride_uv );
                            return 0;
                        }
                        return -1;

                    case VIDC_I_BUFFER_REQUIREMENTS:
                        if ( out && out_sz >= sizeof( vidc_buffer_reqmnts_type ) )
                        {
                            auto *br = reinterpret_cast<vidc_buffer_reqmnts_type *>( out );
                            if ( g_mockup_cfg.buf_type == VIDC_BUFFER_OUTPUT )
                            {
                                br->buf_type = VIDC_BUFFER_OUTPUT;
                                br->size = g_mockup_cfg.req_out_size;
                                br->actual_count = g_mockup_cfg.req_out_count;
                            }
                            else
                            {
                                br->buf_type = VIDC_BUFFER_INPUT;
                                br->size = g_mockup_cfg.req_in_size;
                                br->actual_count = g_mockup_cfg.req_in_count;
                            }
                            return 0;
                        }
                        return -1;

                    default:
                        return g_mockup_cfg.rc_get_property;   // fallback
                }
            }

            case VIDC_IOCTL_PAUSE:
                emit_evt( VIDC_EVT_RESP_PAUSE );
                return 0;

            case VIDC_IOCTL_RESUME:
                emit_evt( VIDC_EVT_RESP_RESUME );
                return 0;

            case VIDC_IOCTL_SET_BUFFER:
                return g_mockup_cfg.rc_set_buffer;

            case VIDC_IOCTL_FREE_BUFFER:
                return g_mockup_cfg.rc_free_buffer;

            case VIDC_IOCTL_EMPTY_INPUT_BUFFER:
                if ( g_mockup_cfg.rc_empty_input ) return g_mockup_cfg.rc_empty_input;
                emit_evt( VIDC_EVT_RESP_INPUT_DONE );
                return 0;

            case VIDC_IOCTL_FILL_OUTPUT_BUFFER:
                if ( g_mockup_cfg.rc_fill_output ) return g_mockup_cfg.rc_fill_output;
                emit_evt( VIDC_EVT_RESP_OUTPUT_DONE );
                return 0;

            case VIDC_IOCTL_DRAIN:
                return -1;

            default:
                return 0;
        }
    }
    return ret;
}

// =====================================================================================
// ==== Additional Coverage Tests (existing + new) =====================================
// =====================================================================================

// Helper: Build a minimal valid encoder config quickly. Hits VideoEncoder::Initialize
static QCNodeInit_t BuildConfig( uint32_t id, uint32_t w, uint32_t h, uint32_t fps, uint32_t br,
                                 bool dynIn, bool dynOut, uint32_t nIn = 4, uint32_t nOut = 4,
                                 const char *inFmt = "nv12", const char *outFmt = "h264",
                                 const char *profile = "H264_MAIN", const char *rc = "CBR_CFR" )
{
    DataTree dt;
    dt.Set<std::string>( "name", "Cfg" );
    dt.Set<uint32_t>( "id", id );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", w );
    dt.Set<uint32_t>( "height", h );
    dt.Set<uint32_t>( "bitrate", br );
    dt.Set<uint32_t>( "gop", 20 );
    dt.Set<bool>( "bInputDynamicMode", dynIn );
    dt.Set<bool>( "bOutputDynamicMode", dynOut );
    dt.Set<uint32_t>( "numInputBufferReq", nIn );
    dt.Set<uint32_t>( "numOutputBufferReq", nOut );
    dt.Set<uint32_t>( "frameRate", fps );
    dt.Set<std::string>( "profile", profile );
    dt.Set<std::string>( "rateControlMode", rc );
    dt.Set<std::string>( "inputImageFormat", inFmt );
    dt.Set<std::string>( "outputImageFormat", outFmt );
    DataTree dtAll;
    dtAll.Set( "static", dt );
    QCNodeInit_t cfg = { .config = dtAll.Dump(), .callback = OnDoneCb };
    return cfg;
}

// Convenience: fill buffer descriptors with size/stride and dummy pointers
static void PrepareBuffers( std::vector<std::unique_ptr<VideoFrameDescriptor>> &storage,
                            std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &outRefs,
                            size_t count, uint32_t size, int32_t strideY, int32_t strideUV )
{
    storage.resize( count );
    for ( size_t i = 0; i < count; ++i )
    {
        storage[i] = std::make_unique<VideoFrameDescriptor>();
        storage[i]->size = size;
        storage[i]->pBuf = reinterpret_cast<void *>( 0x1000 + i * 0x100 );
        storage[i]->dmaHandle = 0xAAA0 + i;
        storage[i]->stride[0] = strideY;
        storage[i]->stride[1] = strideUV;
        outRefs.emplace_back( *storage[i] );
    }
}

TEST( NodeVideoEncoder, INIT_Fails_WhenDeviceOpenFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_open_mockup = true;
    g_mockup_cfg.open_fail = true;

    auto cfg = BuildConfig( 11, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // device_open fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenSetOutputBuffersFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_set_buffer = 1;   // any SET_BUFFER call fails.

    g_mockup_cfg.prop[0].prop_hdr.prop_id = VIDC_I_FRAME_RATE;
    g_mockup_cfg.prop[0].prop_hdr.size = sizeof( vidc_frame_rate_type );
    g_mockup_cfg.prop[0].payload.frameRate.buf_type = VIDC_BUFFER_OUTPUT;
    g_mockup_cfg.prop[0].payload.frameRate.fps_denominator = 1;
    g_mockup_cfg.prop[0].payload.frameRate.fps_numerator = 30;

    g_mockup_cfg.prop[3].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[3].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[3].payload.reqmnts.buf_type = VIDC_BUFFER_OUTPUT;
    g_mockup_cfg.prop[3].payload.reqmnts.size = 1;
    g_mockup_cfg.prop[3].payload.reqmnts.actual_count = 1;

    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenInvalidProfile )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true, 4, 4, "nv12", "h264",
                            "INVALID_PROFILE" );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, ProcessFrame_ReturnsError_WhenNotRunning )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 17, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );

    NodeFrameDescriptor frameDesc( QC_NODE_VIDEO_ENCODER_EVENT_BUFF_ID + 1 );
    EXPECT_NE( QC_STATUS_OK, node->ProcessFrameDescriptor( frameDesc ) );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, ERROR_WrongStateCalls_BeforeInitialize )
{
    Mockup_Cfg_Reset();
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    NodeFrameDescriptor frameDesc( QC_NODE_VIDEO_ENCODER_EVENT_BUFF_ID + 1 );
    EXPECT_NE( QC_STATUS_OK, node->ProcessFrameDescriptor( frameDesc ) );
    EXPECT_NE( QC_STATUS_OK, node->Start() );

    delete node;
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, SUCCESS_HEVC_Main10_EndToEnd_InitStartStop )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.prop_frame_rate_num = 30;
    g_mockup_cfg.req_in_count = 4;
    g_mockup_cfg.req_out_count = 4;
    g_mockup_cfg.req_in_size = 256 * 1024;
    g_mockup_cfg.req_out_size = 256 * 1024;

    auto cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                            /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h265", "HEVC_MAIN10",
                            "CBR_CFR" );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );   // init succeeds
    ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->Start() );   // Start → running
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->Stop() );
    ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->DeInitialize() );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenGetPropertyFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_get_property = 1;   // force GetDrvProperty error.

    auto cfg = BuildConfig( 12, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // GetInputInformation fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenSetPropertyFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_set_property = 1;   // any SetDrvProperty call fails.

    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // InitDrvProperty fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenInputBufferReqNegotiationMismatch )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;

    // Driver requests more than app provides -> BAD_ARGUMENTS in VidcNodeBase::NegotiateBufferReq.
    g_mockup_cfg.req_in_count = 8;
    g_mockup_cfg.req_out_count = 8;

    g_mockup_cfg.prop[0].prop_hdr.prop_id = VIDC_I_FRAME_RATE;
    g_mockup_cfg.prop[0].prop_hdr.size = sizeof( vidc_frame_rate_type );
    g_mockup_cfg.prop[0].payload.frameRate.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[0].payload.frameRate.fps_denominator = 1;
    g_mockup_cfg.prop[0].payload.frameRate.fps_numerator = 30;

    g_mockup_cfg.prop[3].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[3].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[3].payload.reqmnts.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[3].payload.reqmnts.size = 30;
    g_mockup_cfg.prop[3].payload.reqmnts.actual_count = 30;

    g_mockup_cfg.prop[5].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[5].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[5].payload.reqmnts.buf_type = VIDC_BUFFER_OUTPUT;
    g_mockup_cfg.prop[5].payload.reqmnts.size = 1;
    g_mockup_cfg.prop[5].payload.reqmnts.actual_count = 1;

    auto cfg = BuildConfig( 14, 176, 144, 30, 64000, /*dynIn*/ false, /*dynOut*/ false,
                            /*nIn*/ 4, /*nOut*/ 4 );

    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // Negotiation fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenOutputBufferReqNegotiationMismatch )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;

    // Driver requests more than app provides -> BAD_ARGUMENTS in VidcNodeBase::NegotiateBufferReq.
    g_mockup_cfg.req_in_count = 8;
    g_mockup_cfg.req_out_count = 8;

    g_mockup_cfg.prop[0].prop_hdr.prop_id = VIDC_I_FRAME_RATE;
    g_mockup_cfg.prop[0].prop_hdr.size = sizeof( vidc_frame_rate_type );
    g_mockup_cfg.prop[0].payload.frameRate.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[0].payload.frameRate.fps_denominator = 1;
    g_mockup_cfg.prop[0].payload.frameRate.fps_numerator = 30;

    g_mockup_cfg.prop[3].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[3].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[3].payload.reqmnts.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[3].payload.reqmnts.size = 1;
    g_mockup_cfg.prop[3].payload.reqmnts.actual_count = 1;

    g_mockup_cfg.prop[5].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[5].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[5].payload.reqmnts.buf_type = VIDC_BUFFER_OUTPUT;
    g_mockup_cfg.prop[5].payload.reqmnts.size = 41;
    g_mockup_cfg.prop[5].payload.reqmnts.actual_count = 33;

    auto cfg = BuildConfig( 14, 176, 144, 30, 64000, /*dynIn*/ false, /*dynOut*/ false,
                            /*nIn*/ 4, /*nOut*/ 4 );

    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // Negotiation fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenLoadResourceFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_load_resources = 1;   // LOAD_RESOURCES ioctl fails.

    g_mockup_cfg.prop[0].prop_hdr.prop_id = VIDC_I_FRAME_RATE;
    g_mockup_cfg.prop[0].prop_hdr.size = sizeof( vidc_frame_rate_type );
    g_mockup_cfg.prop[0].payload.frameRate.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[0].payload.frameRate.fps_denominator = 1;
    g_mockup_cfg.prop[0].payload.frameRate.fps_numerator = 30;

    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenReleaseResourceFails )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    EXPECT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_release_resources = 1;   // RELEASE_RESOURCES ioctl fails.
    EXPECT_EQ( QC_STATUS_FAIL, node->DeInitialize() );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, START_TimesOut_WhenNoStartEventEmitted )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.rc_start = 1;
    g_mockup_cfg.emit_evt_start_all = false;
    g_mockup_cfg.emit_evt_start_input_done = false;

    auto cfg = BuildConfig( 15, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    // Suppress START events so VidcNodeBase::WaitForState(RUNNING) times out.
    g_mockup_cfg.use_ioctl_mockup = true;
    EXPECT_NE( QC_STATUS_OK, node->Start() );   // timeout → ERROR.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, STOP_TimesOut_WhenNoStopEventEmitted )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.emit_evt_stop_done = false;

    auto cfg = BuildConfig( 16, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    EXPECT_EQ( QC_OBJECT_STATE_RUNNING, node->GetState() );

    g_mockup_cfg.use_ioctl_mockup = true;
    EXPECT_NE( QC_STATUS_OK, node->Stop() );   // timeout path in driver client.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, NonDynamic_CheckInputBuffer_StrideOrSizeMismatchFails )
{
    Mockup_Cfg_Reset();

    // Driver requires large input size/stride; provided buffer will be too small/wrong stride.
    g_mockup_cfg.req_in_count = 4;
    g_mockup_cfg.req_out_count = 4;
    g_mockup_cfg.req_in_size = 1024 * 1024;   // large to trigger size check
    g_mockup_cfg.prop_in_plane_stride_y = 512;
    g_mockup_cfg.prop_in_plane_stride_uv = 512;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    QCNodeIfs *node = new QC::Node::VideoEncoder();

    auto cfg = BuildConfig( 18, 176, 144, 30, 64000, /*dynIn*/ false, /*dynOut*/ true,
                            /*nIn*/ 4, /*nOut*/ 4, "nv12", "h264" );

    // Provide 4 small input buffers (default allocation) – likely < req_in_size.
    std::vector<VideoFrameDescriptor_t> in;
    for ( int i = 0; i < 4; ++i )
    {
        VideoFrameDescriptor_t d;
        ASSERT_EQ( QC_STATUS_OK,
                   bufMgr.Allocate( ImageBasicProps( 176, 144, QC_IMAGE_FORMAT_NV12 ), d ) );
        // Force wrong stride to fail Vidc CheckBuffer even if size passes.
        d.stride[0] = d.stride[1] = 128;
        in.push_back( d );
    }

    // Provide 4 dynamic output placeholders (won't be set in non-dynamic = true? It is true for
    // out)
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> refs;
    for ( auto &b : in ) refs.push_back( b );

    QCNodeInit_t init = cfg;
    init.buffers = refs;

    // Initialize must fail during input buffer CheckBuffer due to stride/size.
    EXPECT_NE( QC_STATUS_OK, node->Initialize( init ) );

    for ( auto &b : in )
    {
        EXPECT_EQ( QC_STATUS_OK, bufMgr.Free( b ) );
    }
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, NonDynamic_CheckOutputBuffer_StrideOrSizeMismatchFails )
{
    Mockup_Cfg_Reset();

    // Driver requires large input size/stride; provided buffer will be too small/wrong stride.
    g_mockup_cfg.req_in_count = 4;
    g_mockup_cfg.req_out_count = 4;
    g_mockup_cfg.req_in_size = 1024 * 1024;   // large to trigger size check
    g_mockup_cfg.prop_in_plane_stride_y = 512;
    g_mockup_cfg.prop_in_plane_stride_uv = 512;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    QCNodeIfs *node = new QC::Node::VideoEncoder();

    auto cfg = BuildConfig( 18, 176, 144, 30, 64000, /*dynIn*/ true, /*dynOut*/ false,
                            /*nIn*/ 4, /*nOut*/ 4, "nv12", "h264" );

    // Provide 4 small output buffers (default allocation) – likely < req_in_size.
    std::vector<VideoFrameDescriptor_t> out;
    for ( uint32_t i = 0; i < 4; i++ )
    {
        ImageProps_t imgProps;
        imgProps.batchSize = 1;
        imgProps.width = 172;
        imgProps.height = 140;
        imgProps.numPlanes = 1;
        imgProps.planeBufSize[0] = 2 * 1024 * 1024;
        imgProps.format = QC_IMAGE_FORMAT_COMPRESSED_H264;
        imgProps.allocatorType = QC_MEMORY_ALLOCATOR_DMA_VPU;
        imgProps.cache = QC_CACHEABLE;

        VideoFrameDescriptor_t bufDesc;
        ASSERT_EQ( QC_STATUS_OK, bufMgr.Allocate( imgProps, bufDesc ) );
        out.push_back( bufDesc );
    }

    // Provide 4 dynamic output placeholders (won't be set in non-dynamic = true? It is true for
    // out)
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> refs;
    for ( auto &b : out ) refs.push_back( b );

    QCNodeInit_t init = cfg;
    init.buffers = refs;

    // Initialize must fail during input buffer CheckBuffer due to stride/size.
    EXPECT_NE( QC_STATUS_OK, node->Initialize( init ) );

    for ( auto &b : out )
    {
        EXPECT_EQ( QC_STATUS_OK, bufMgr.Free( b ) );
    }
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, LIFECYCLE_TwoStartStopCycles_Succeed )
{
    Mockup_Cfg_Reset();

    auto cfg = BuildConfig( 19, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    for ( int i = 0; i < 2; ++i )
    {
        ASSERT_EQ( QC_STATUS_OK, node->Start() );
        ASSERT_EQ( QC_OBJECT_STATE_RUNNING, node->GetState() );
        ASSERT_EQ( QC_STATUS_OK, node->Stop() );
        ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );
    }
    ASSERT_EQ( QC_STATUS_OK, node->DeInitialize() );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, node->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_Dynamic )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "SANITY_VideoEncoder_Dynamic" );
    dt.Set<uint32_t>( "id", g_nodeId = 1 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", 176 );
    dt.Set<uint32_t>( "height", 144 );
    dt.Set<uint32_t>( "bitrate", 64000 );
    dt.Set<uint32_t>( "gop", 20 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    QCNodeConfigIfs &cfgIfs = pNodeVide->GetConfigurationIfs();
    const std::string &options = cfgIfs.GetOptions();
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    printf( "options: %s\n", options.c_str() );

    DataTree optionsDt;
    ret = optionsDt.Load( options, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    uint32_t width = dt.Get( "width", 0 );
    uint32_t height = dt.Get( "height", 0 );
    uint32_t numInputBufferReq = dt.Get( "numInputBufferReq", 1 );
    uint32_t numOutputBufferReq = dt.Get( "numOutputBufferReq", 1 );
    QCImageFormat_e inFormat = dt.GetImageFormat( "inputImageFormat", QC_IMAGE_FORMAT_NV12 );
    QCImageFormat_e outFormat =
            dt.GetImageFormat( "outputImageFormat", QC_IMAGE_FORMAT_COMPRESSED_H264 );

    ret = pNodeVide->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    NodeFrameDescriptor frameDesc( QC_NODE_VIDEO_ENCODER_EVENT_BUFF_ID + 1 );

    std::vector<VideoFrameDescriptor_t> inputs;
    std::vector<VideoFrameDescriptor_t> outputs;

    for ( uint32_t i = 0; i < numInputBufferReq; i++ )
    {
        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( ImageBasicProps( width, height, inFormat ), bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( bufDesc );
    }

    for ( uint32_t i = 0; i < numOutputBufferReq; i++ )
    {
        ImageProps_t imgProps;
        imgProps.batchSize = 1;
        imgProps.width = width;
        imgProps.height = height;
        imgProps.numPlanes = 1;
        imgProps.planeBufSize[0] = 118784;
        imgProps.format = outFormat;

        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( imgProps, bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        outputs.push_back( bufDesc );
    }

    frameDesc.Clear();

    uint32_t nr = std::min( numInputBufferReq, numOutputBufferReq );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    for ( uint32_t i = 0; i < nr; i++ )
    {
        // Set up an input buffer for the frame
        ret = frameDesc.SetBuffer( QC_NODE_VIDEO_ENCODER_INPUT_BUFF_ID, inputs[i] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Set up an output buffer for the frame
        ret = frameDesc.SetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID, outputs[i] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the frame
        ret = pNodeVide->ProcessFrameDescriptor( frameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    // wait input done signal
    std::unique_lock<std::mutex> inLock( g_InMutex );
    g_InCondVar.wait( inLock );

    // wait output done signal
    std::unique_lock<std::mutex> outLock( g_OutMutex );
    g_OutCondVar.wait( outLock );

    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    ret = pNodeVide->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );

    for ( auto &output : outputs )
    {
        ret = bufMgr.Free( output );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
    for ( auto &input : inputs )
    {
        ret = bufMgr.Free( input );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_NonDynamic )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "SANITY_VideoEncoder_NonDynamic" );
    dt.Set<uint32_t>( "id", g_nodeId = 2 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", 1920 );
    dt.Set<uint32_t>( "height", 1080 );
    dt.Set<uint32_t>( "bitrate", 20000000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", false );
    dt.Set<bool>( "bOutputDynamicMode", false );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 60 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    uint32_t width = dt.Get( "width", 0 );
    uint32_t height = dt.Get( "height", 0 );
    uint32_t numInputBufferReq = dt.Get( "numInputBufferReq", 1 );
    uint32_t numOutputBufferReq = dt.Get( "numOutputBufferReq", 1 );
    QCImageFormat_e inFormat = dt.GetImageFormat( "inputImageFormat", QC_IMAGE_FORMAT_NV12 );
    QCImageFormat_e outFormat =
            dt.GetImageFormat( "outputImageFormat", QC_IMAGE_FORMAT_COMPRESSED_H264 );

    std::vector<VideoFrameDescriptor_t> buffers;

    for ( uint32_t i = 0; i < numInputBufferReq; i++ )
    {
        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( ImageBasicProps( width, height, inFormat,
                                                QC_MEMORY_ALLOCATOR_DMA_VPU, QC_CACHEABLE ),
                               bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        buffers.push_back( bufDesc );
    }

    for ( uint32_t i = 0; i < numOutputBufferReq; i++ )
    {
        ImageProps_t imgProps;
        imgProps.batchSize = 1;
        imgProps.width = width;
        imgProps.height = height;
        imgProps.numPlanes = 1;
        imgProps.planeBufSize[0] = 2 * 1024 * 1024;
        imgProps.format = outFormat;
        imgProps.allocatorType = QC_MEMORY_ALLOCATOR_DMA_VPU;
        imgProps.cache = QC_CACHEABLE;

        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( imgProps, bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        buffers.push_back( bufDesc );
    }

    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> bufferRefs;
    for ( VideoFrameDescriptor_t &frameDesc : buffers )
    {
        bufferRefs.push_back( frameDesc );
    }

    QCNodeInit_t config = { .config = dataTree.Dump(),
                            .callback = OnDoneCb,
                            .buffers = bufferRefs };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    QCNodeConfigIfs &cfgIfs = pNodeVide->GetConfigurationIfs();
    const std::string &options = cfgIfs.GetOptions();
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    printf( "options: %s\n", options.c_str() );

    ret = pNodeVide->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    uint32_t nr = std::min( numInputBufferReq, numOutputBufferReq );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    NodeFrameDescriptor frameDesc( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID + 1 );
    frameDesc.Clear();

    for ( uint32_t i = 0; i < nr; i++ )
    {
        // Set up an input buffer for the frame
        ret = frameDesc.SetBuffer( QC_NODE_VIDEO_ENCODER_INPUT_BUFF_ID, buffers[i] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the input frame
        ret = pNodeVide->ProcessFrameDescriptor( frameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    // wait input done signal
    std::unique_lock<std::mutex> inLock( g_InMutex );
    g_InCondVar.wait( inLock );

    // wait output done signal
    std::unique_lock<std::mutex> outLock( g_OutMutex );
    g_OutCondVar.wait( outLock );

    frameDesc.Clear();

    ret = frameDesc.SetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID, buffers[numInputBufferReq] );
    ASSERT_EQ( QC_STATUS_OK, ret );

    // Process the output frame
    ret = pNodeVide->ProcessFrameDescriptor( frameDesc );

    ret = pNodeVide->Stop();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_Resolution )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoderResolution_128x128" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    dt.Set<std::string>( "static.name", "VideoEncoderResolution_176x144" );
    dt.Set<uint32_t>( "width", 176 );
    dt.Set<uint32_t>( "height", 144 );
    dt.Set<uint32_t>( "bitrate", 1000000 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    dt.Set<std::string>( "static.name", "VideoEncoderResolution_1280x720" );
    dt.Set<uint32_t>( "width", 1280 );
    dt.Set<uint32_t>( "height", 720 );
    dt.Set<uint32_t>( "bitrate", 2000000 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    dt.Set<std::string>( "static.name", "VideoEncoderResolution_1920x1080" );
    dt.Set<uint32_t>( "width", 1920 );
    dt.Set<uint32_t>( "height", 1080 );
    dt.Set<uint32_t>( "bitrate", 5000000 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    dt.Set<std::string>( "static.name", "VideoEncoderResolution_1920x1088" );
    dt.Set<uint32_t>( "width", 1920 );
    dt.Set<uint32_t>( "height", 1088 );
    dt.Set<uint32_t>( "bitrate", 10000000 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );

    dt.Set<std::string>( "static.name", "VideoEncoderResolution_3840x2160" );
    dt.Set<uint32_t>( "width", 3840 );
    dt.Set<uint32_t>( "height", 2160 );
    dt.Set<uint32_t>( "bitrate", 20000000 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );
    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InitError )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_width" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 4 );
    dt.Set<uint32_t>( "width", 0 );
    dt.Set<uint32_t>( "height", 0 );
    dt.Set<uint32_t>( "bitrate", 0 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 1 );
    dt.Set<uint32_t>( "numOutputBufferReq", 128 );
    dt.Set<uint32_t>( "frameRate", 0 );
    dt.Set<std::string>( "profile", "max" );
    dt.Set<std::string>( "rateControlMode", "UNUSED" );
    dt.Set<std::string>( "inputImageFormat", "MAX" );
    dt.Set<std::string>( "outputImageFormat", "MAX" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    uint32_t numInputBufferReq = 1;
    uint32_t numOutputBufferReq = 128;

    printf( "config: %s\n", config.config.c_str() );

    NodeFrameDescriptor frameDesc( QC_NODE_VIDEO_ENCODER_EVENT_BUFF_ID + 1 );

    std::vector<VideoFrameDescriptor_t> inputs;
    std::vector<VideoFrameDescriptor_t> outputs;

    for ( unsigned int i = 0; i < numInputBufferReq; ++i )
    {
        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( ImageBasicProps( 176, 144, QC_IMAGE_FORMAT_NV12 ), bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( bufDesc );
    }
    for ( unsigned int i = 0; i < numOutputBufferReq; ++i )
    {
        ImageProps_t imgProps;
        imgProps.batchSize = 1;
        imgProps.width = 176;
        imgProps.height = 144;
        imgProps.numPlanes = 1;
        imgProps.planeBufSize[0] = 118784;
        imgProps.format = QC_IMAGE_FORMAT_COMPRESSED_H264;

        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( imgProps, bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        outputs.push_back( bufDesc );
    }

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_rc" );
    dt.Set<uint32_t>( "width", 176 );
    dt.Set<uint32_t>( "height", 144 );
    dt.Set<uint32_t>( "bitrate", 20000000 );
    dt.Set<std::string>( "inputImageFormat", "p010" );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_rc" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_informat" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_outformat" );
    dt.Set<std::string>( "outputImageFormat", "h265" );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_inreq" );
    dt.Set<uint32_t>( "numInputBufferReq", 8 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_outreq" );
    dt.Set<uint32_t>( "numOutputBufferReq", 8 );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    dt.Set<std::string>( "name", "VideoEncoderErrorTest_bad_profile" );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dataTree.Set( "static", dt );
    config.config = dataTree.Dump();
    config.callback = OnDoneCb;
    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );

    for ( auto &output : outputs )
    {
        ret = bufMgr.Free( output );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
    for ( auto &input : inputs )
    {
        ret = bufMgr.Free( input );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }
}

TEST( NodeVideoEncoder, INIT_Fails_WhenSetBuffersFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_set_buffer = 1;   // any SET_BUFFER call fails.

    g_mockup_cfg.prop[0].prop_hdr.prop_id = VIDC_I_FRAME_RATE;
    g_mockup_cfg.prop[0].prop_hdr.size = sizeof( vidc_frame_rate_type );
    g_mockup_cfg.prop[0].payload.frameRate.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[0].payload.frameRate.fps_denominator = 1;
    g_mockup_cfg.prop[0].payload.frameRate.fps_numerator = 30;

    g_mockup_cfg.prop[3].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[3].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[3].payload.reqmnts.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[3].payload.reqmnts.size = 1;
    g_mockup_cfg.prop[3].payload.reqmnts.actual_count = 1;

    g_mockup_cfg.prop[4].prop_hdr.prop_id = VIDC_I_BUFFER_REQUIREMENTS;
    g_mockup_cfg.prop[4].prop_hdr.size = sizeof( vidc_buffer_reqmnts_type );
    g_mockup_cfg.prop[4].payload.reqmnts.buf_type = VIDC_BUFFER_INPUT;
    g_mockup_cfg.prop[4].payload.reqmnts.size = 33;
    g_mockup_cfg.prop[4].payload.reqmnts.actual_count = 66;

    auto cfg = BuildConfig( 13, 176, 144, 30, 64'000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // SET_BUFFER for input fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, STOP_TimesOut_WhenStopCommandFailed )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 16, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_NE( nullptr, node );

    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    EXPECT_EQ( QC_OBJECT_STATE_RUNNING, node->GetState() );

    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_stop = 1;
    EXPECT_NE( QC_STATUS_OK, node->Stop() );   // failure path in driver client.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

// 1) GetInputInformation: fps mismatch triggers FAIL (after frame-rate GET).
TEST( NodeVideoEncoder, INIT_Fails_WhenFrameRateMismatches_GetInputInformation )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    // Let fake device report fps != config.frameRate
    g_mockup_cfg.prop_frame_rate_num = 999;   // mismatch
    auto cfg = BuildConfig( 21, 640, 480, /*fps*/ 30, 500000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );   // GetInputInformation fails on mismatch
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

// 2) GetInputInformation: plane-def GET fails (Y and UV)
TEST( NodeVideoEncoder, INIT_Fails_WhenPlaneDefY_GetPropertyFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_get_prop_plane_def_y = 1;   // fail only plane Y
    auto cfg = BuildConfig( 22, 640, 480, 30, 500000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, INIT_Fails_WhenPlaneDefUV_GetPropertyFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_get_prop_plane_def_uv = 1;   // fail only plane UV
    auto cfg = BuildConfig( 23, 640, 480, 30, 500000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    EXPECT_NE( QC_STATUS_OK, node->Initialize( cfg ) );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );
    Mockup_Cfg_Reset();
}

// 3) Start(): non-dynamic output pre-submission — FillBuffer failure
TEST( NodeVideoEncoder, START_Fails_WhenPreFillOutputBufferFails_InNonDynamicOutputMode )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.emit_evt_load_done = true;
    g_mockup_cfg.use_open_mockup = true;
    g_mockup_cfg.use_close_mockup = true;

    // Non-dynamic output => Start() will call FillBuffer() for each output buffer
    // Return buffer requirements small, but OK.
    g_mockup_cfg.req_in_count = 4;
    g_mockup_cfg.req_out_count = 4;
    g_mockup_cfg.req_in_size = 64;
    g_mockup_cfg.req_out_size = 64;

    BufferManager bufMgr = BufferManager( { "VENC", QC_NODE_TYPE_VENC, 0 } );

    // Provide 4 small input buffers (default allocation) – likely < req_in_size.
    std::vector<VideoFrameDescriptor_t> buffers;
    for ( uint32_t i = 0; i < g_mockup_cfg.req_out_count; i++ )
    {
        ImageProps_t imgProps;
        imgProps.batchSize = 1;
        imgProps.width = 176;
        imgProps.height = 144;
        imgProps.numPlanes = 1;
        imgProps.planeBufSize[0] = 2 * 1024 * 1024;
        imgProps.format = QC_IMAGE_FORMAT_COMPRESSED_H264;
        imgProps.allocatorType = QC_MEMORY_ALLOCATOR_DMA_VPU;
        imgProps.cache = QC_CACHEABLE;

        VideoFrameDescriptor_t bufDesc;
        auto ret = bufMgr.Allocate( imgProps, bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        buffers.push_back( bufDesc );
    }

    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> bufferRefs;
    for ( VideoFrameDescriptor_t &frameDesc : buffers )
    {
        bufferRefs.push_back( frameDesc );
    }

    auto cfg = BuildConfig( 24, 176, 144, 30, 64000, /*dynIn*/ true, /*dynOut*/ false, /*nIn*/ 4,
                            /*nOut*/ 4 );
    cfg.buffers = bufferRefs;
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );

    // Make FillBuffer return error
    g_mockup_cfg.rc_fill_output = 1;
    EXPECT_NE( QC_STATUS_OK, node->Start() );   // returns error after FillBuffer failure
    Mockup_Cfg_Reset();
}

// 4) Validate output buffer path via ProcessFrameDescriptor (mismatch format)
TEST( NodeVideoEncoder, START_ValidateOutputBuffer_FailsViaProcessFrameDescriptor )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 25, 176, 144, 30, 64000, /*dynIn*/ true, /*dynOut*/ true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_STATUS_OK, node->Start() );   // RUNNING

    // Submit ONLY an output descriptor with raw format (should mismatch compressed out)
    NodeFrameDescriptor fake( 3 );
    VideoFrameDescriptor_t out{};
    out.pBuf = (void *) 0xBEEF;
    out.size = 64;
    out.dmaHandle = 0x22;
    out.width = 176;
    out.height = 144;
    out.format = QC_IMAGE_FORMAT_NV12;   // mismatch vs H264/H265
    out.stride[0] = 256;
    out.stride[1] = 128;
    fake.SetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID, out );

    EXPECT_NE( QC_STATUS_OK, node->ProcessFrameDescriptor( fake ) );
    (void) node->Stop();
    Mockup_Cfg_Reset();
}

// 5) Both buffers null (RUNNING) => INVALID_BUF
TEST( NodeVideoEncoder, ProcessFrameDescriptor_InvalidBuf_WhenBothNull_WhileRunning )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 26, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    NodeFrameDescriptor frame( 3 );   // empty slots => both null
    EXPECT_EQ( QC_STATUS_INVALID_BUF, node->ProcessFrameDescriptor( frame ) );
    (void) node->Stop();
    Mockup_Cfg_Reset();
}

// 6) Input-only with null pBuf => BAD_ARGUMENTS
TEST( NodeVideoEncoder, ProcessFrameDescriptor_InputNullPtr_BadArguments )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 27, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    NodeFrameDescriptor frame( 3 );
    VideoFrameDescriptor_t in{};
    in.pBuf = nullptr;
    in.size = 128;
    in.dmaHandle = 0x11;
    in.width = 176;
    in.height = 144;
    in.format = QC_IMAGE_FORMAT_NV12;
    frame.SetBuffer( QC_NODE_VIDEO_ENCODER_INPUT_BUFF_ID, in );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, node->ProcessFrameDescriptor( frame ) );
    (void) node->Stop();
    Mockup_Cfg_Reset();
}

// 7) Output-only with null pBuf => BAD_ARGUMENTS
TEST( NodeVideoEncoder, ProcessFrameDescriptor_OutputNullPtr_BadArguments )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 28, 176, 144, 30, 64000, true, true );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    NodeFrameDescriptor frame( 3 );
    VideoFrameDescriptor_t out{};
    out.pBuf = nullptr;
    out.size = 0;
    out.dmaHandle = 0x22;
    frame.SetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID, out );
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS, node->ProcessFrameDescriptor( frame ) );
    (void) node->Stop();
    Mockup_Cfg_Reset();
}

// 8) Event forwarding: verify event id string reaches callback
TEST( NodeVideoEncoder, EventCallback_ForwardsEventId_ToUserCallback )
{
    Mockup_Cfg_Reset();
    std::vector<std::string> eventNames;
    auto cb = [&eventNames]( const QCNodeEventInfo_t &info ) {
        auto &fd = info.frameDesc;
        auto &out = fd.GetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID );
        if ( !out.name.empty() ) eventNames.push_back( out.name );
    };

    DataTree dt;
    dt.Set<std::string>( "name", "evt" );
    dt.Set<uint32_t>( "id", 77 );
    dt.Set<uint32_t>( "width", 176 );
    dt.Set<uint32_t>( "height", 144 );
    dt.Set<uint32_t>( "bitrate", 64000 );
    dt.Set<uint32_t>( "gop", 20 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );
    DataTree all;
    all.Set( "static", dt );
    QCNodeInit_t init{ .config = all.Dump(), .callback = cb };

    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, node->Start() );
    g_mockup_cfg.use_ioctl_mockup = true;

    emit_evt( VIDC_EVT_RESP_PAUSE );
    emit_evt( VIDC_EVT_RESP_RESUME );

    ASSERT_GE( eventNames.size(), 2u );
    EXPECT_EQ(eventNames[0], std::to_string(VIDC_IOCTL_PAUSE));
    EXPECT_EQ(eventNames[1], std::to_string(VIDC_IOCTL_SET_BUFFER));
    (void) node->Stop();
    Mockup_Cfg_Reset();
}

// 9) InitDrvProperty: exercise later SET_PROPERTY failures (profile/level/spatial)
TEST( NodeVideoEncoder, INIT_Fails_WhenProfileLevelOrSpatialTransformSetPropertyFails )
{
    Mockup_Cfg_Reset();
    g_mockup_cfg.use_ioctl_mockup = true;
    auto cfg = BuildConfig( 29, 1280, 720, 30, 1000000, true, true, 4, 4, "nv12", "h264",
                            "H264_MAIN", "CBR_CFR" );

    g_mockup_cfg.rc_set_prop_profile = 1;
    EXPECT_NE( QC_STATUS_OK, ( new QC::Node::VideoEncoder() )->Initialize( cfg ) );
    Mockup_Cfg_Reset();

    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_set_prop_level = 1;
    EXPECT_NE( QC_STATUS_OK, ( new QC::Node::VideoEncoder() )->Initialize( cfg ) );
    Mockup_Cfg_Reset();

    g_mockup_cfg.use_ioctl_mockup = true;
    g_mockup_cfg.rc_set_prop_spatial = 1;
    EXPECT_NE( QC_STATUS_OK, ( new QC::Node::VideoEncoder() )->Initialize( cfg ) );
    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, SUCCESS_Init_RateCtrlMode )
{
    Mockup_Cfg_Reset();

    auto cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                            /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h264", "H264_HIGH",
                            "VBR_CFR" );
    QCNodeIfs *node = new QC::Node::VideoEncoder();
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    printf( "Starting...\n" );

    ASSERT_EQ( QC_STATUS_OK, node->DeInitialize() );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, node->GetState() );

    cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                       /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h264", "H264_HIGH",
                       "VBR_CFR" );
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->DeInitialize() );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, node->GetState() );

    cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                       /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h264", "H264_BASELINE",
                       "VBR_VFR" );
    ASSERT_EQ( QC_STATUS_OK, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_READY, node->GetState() );

    ASSERT_EQ( QC_STATUS_OK, node->DeInitialize() );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, node->GetState() );

    cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                       /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h265", "HEVC_MAIN",
                       "UNUSED" );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );

    cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                       /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h265", "HEVC_MAIN10",
                       "INV!" );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );

    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, FAILURE_Init_ProfileMismatch )
{
    Mockup_Cfg_Reset();
    auto cfg = BuildConfig( 10, 1920, 1080, 30, 5'000'000,
                            /*dynIn*/ true, /*dynOut*/ true, 4, 4, "p010", "h265", "H264_BASELINE",
                            "CBR_VFR" );
    QCNodeIfs *node = new QC::Node::VideoEncoder();

    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, node->Initialize( cfg ) );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, node->GetState() );

    Mockup_Cfg_Reset();
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InvalidResolution )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoderResolution_Invalid" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 3 );
    dt.Set<uint32_t>( "height", 7 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InvalidInputFormat )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoder_InvalidInputFormat" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "h265" );
    dt.Set<std::string>( "outputImageFormat", "nv12" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InvalidOutputFormat )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoder_InvalidOutputFormat" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "nv12" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InvalidInputNumBuffers )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoder_InvalidInputNumBuffer" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", ~0u );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h265" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

TEST( NodeVideoEncoder, SANITY_VideoEncoder_InvalidOutputNumBuffers )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoEncoder_InvalidOutputNumBuffer" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", ~0u );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h265" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_BAD_ARGUMENTS, ret );
    ASSERT_EQ( QC_OBJECT_STATE_ERROR, pNodeVide->GetState() );

    ret = pNodeVide->DeInitialize();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, pNodeVide->GetState() );
}

// Force the VideoEncoder::CheckBuffer size condition true for INPUT:
// (size_t)m_bufSize[VIDEO_CODEC_BUF_INPUT] > vidFrmDesc.size
TEST( NodeVideoEncoder_CheckBuffer, DynamicInput_SizeTooSmallTriggersInvalidBuf )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "DynamicInput_SizeTooSmallTriggersInvalidBuf" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    // Build a single input-only frame with size SMALLER than negotiated m_bufSize[INPUT].
    NodeFrameDescriptor frame( /*num slots*/ 3 );
    VideoFrameDescriptor_t in{};
    in.width = 128;
    in.height = 128;
    in.format = QC_IMAGE_FORMAT_NV12;
    in.pBuf = reinterpret_cast<void *>( 0xDEAD );
    in.dmaHandle = 0x111;
    in.size = 64;         // 64B triggers size guard
    in.stride[0] = 128;   // matches fake plane Y
    in.stride[1] = 128;   // matches fake plane UV

    ASSERT_EQ( QC_STATUS_OK, frame.SetBuffer( QC_NODE_VIDEO_ENCODER_INPUT_BUFF_ID, in ) );
    ASSERT_EQ( QC_STATUS_OK, pNodeVide->Start() );   // go to RUNNING so SubmitInputFrame is allowed

    // ProcessFrameDescriptor -> SubmitInputFrame -> ValidateFrameSubmission -> CheckBuffer
    // CheckBuffer sees m_bufSize[INPUT] (1 MiB) > in.size (64 KiB) -> returns
    // QC_STATUS_INVALID_BUF.
    QCStatus_e st = pNodeVide->ProcessFrameDescriptor( frame );
    EXPECT_EQ( st, QC_STATUS_INVALID_BUF );

    (void) pNodeVide->Stop();

    Mockup_Cfg_Reset();
}

// Force the VideoEncoder::CheckBuffer size condition true for OUTPUT:
// (size_t)m_bufSize[VIDEO_CODEC_BUF_INPUT] > vidFrmDesc.size
TEST( NodeVideoEncoder_CheckBuffer, DynamicOutput_SizeTooSmallTriggersInvalidBuf )
{
    Mockup_Cfg_Reset();
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoEncoder();

    DataTree dt;
    dt.Set<std::string>( "name", "DynamicOutput_SizeTooSmallTriggersInvalidBuf" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", g_nodeId = 3 );
    dt.Set<uint32_t>( "width", 128 );
    dt.Set<uint32_t>( "height", 128 );
    dt.Set<uint32_t>( "bitrate", 512000 );
    dt.Set<uint32_t>( "gop", 0 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<std::string>( "profile", "H264_MAIN" );
    dt.Set<std::string>( "rateControlMode", "CBR_CFR" );
    dt.Set<std::string>( "inputImageFormat", "nv12" );
    dt.Set<std::string>( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };

    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    // Build a single input-only frame with size SMALLER than negotiated m_bufSize[OUTPUT].
    NodeFrameDescriptor frame( /*num slots*/ 3 );
    VideoFrameDescriptor_t out{};
    out.width = 128;
    out.height = 128;
    out.format = QC_IMAGE_FORMAT_COMPRESSED_H265;
    out.pBuf = reinterpret_cast<void *>( 0xDEAD );
    out.dmaHandle = 0x111;
    out.size = 64;         // 64B triggers size guard
    out.stride[0] = 128;   // matches fake plane Y
    out.stride[1] = 128;   // matches fake plane UV

    ASSERT_EQ( QC_STATUS_OK, frame.SetBuffer( QC_NODE_VIDEO_ENCODER_OUTPUT_BUFF_ID, out ) );
    ASSERT_EQ( QC_STATUS_OK,
               pNodeVide->Start() );   // go to RUNNING so SubmitOutputFrame is allowed

    // ProcessFrameDescriptor -> SubmitOutputFrame -> ValidateFrameSubmission -> CheckBuffer
    // CheckBuffer sees m_bufSize[OUTPUT] (1 MiB) > in.size (64 KiB) -> returns
    // QC_STATUS_INVALID_BUF.
    QCStatus_e st = pNodeVide->ProcessFrameDescriptor( frame );
    EXPECT_EQ( st, QC_STATUS_INVALID_BUF );

    (void) pNodeVide->Stop();

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
