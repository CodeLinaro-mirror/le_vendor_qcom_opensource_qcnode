// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "QC/Infras/Log/Logger.hpp"
#include "VidcDrvClient.hpp"   // we’ll provide link-time fakes for its methods here
#include "VidcNodeBase.hpp"   // brings VidcNodeBase, VidcNodeBase_Config_t, VideoFrameDescriptor_t, etc.
#include "vidc_driver_mockup.hpp"

//==============================================================
// C-level driver stubs (device_*, MM_Timer_Sleep) used by the
// real VidcDrvClient implementation.
//==============================================================

extern "C"
{

    // Captured callback from device_open
    static ioctl_callback_t g_vidc_cb{};
    static ioctl_session_t *g_fake_handle = reinterpret_cast<ioctl_session_t *>( 0xFACEFEED );

    static bool g_open_should_fail = false;

    // Track a tiny sleep counter for WaitForState/WaitForCmdCompleted
    static std::atomic<int> g_sleep_calls{ 0 };
    int __mockup_MM_Timer_Sleep( unsigned int msec )
    {
        g_sleep_calls++;
        return 0;
    }

    // Fake device_open: store callback, return handle or nullptr
    void *__mockup_device_open( const char *name, ioctl_callback_t *cb )
    {
        if ( g_open_should_fail ) return nullptr;
        if ( cb ) g_vidc_cb = *cb;
        return static_cast<ioctl_session_t *>( g_fake_handle );
    }

    int __mockup_device_close( ioctl_session_t *handle )
    {
        return 0;
    }

    // Helper to emit a VIDC event to the registered VidcDrvClient callback
    static void EmitVidcEvent( vidc_event_type evt_id, const vidc_frame_data_type *f = nullptr )
    {
        if ( !g_vidc_cb.handler ) return;
        vidc_drv_msg_info_type evt{};
        evt.event_type = evt_id;
        if ( f ) evt.payload.frame_data = *f;
        g_vidc_cb.handler( reinterpret_cast<uint8_t *>( &evt ), sizeof( evt ), g_vidc_cb.data );
    }

    // Minimal property store to support InitDriver()/NegotiateBufferReq() paths if used
    static vidc_buffer_reqmnts_type g_req_in{};
    static vidc_buffer_reqmnts_type g_req_out{};

    int __mockup_device_ioctl( ioctl_session_t *handle, uint32_t cmd, uint8_t *in, uint32_t in_len,
                               uint8_t *out, uint32_t out_len )
    {
        switch ( cmd )
        {
            case VIDC_IOCTL_LOAD_RESOURCES:
                // Real client will wait for RESP_LOAD_RESOURCES via WaitForCmdCompleted
                EmitVidcEvent( VIDC_EVT_RESP_LOAD_RESOURCES );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_RELEASE_RESOURCES:
                EmitVidcEvent( VIDC_EVT_RESP_RELEASE_RESOURCES );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_START:
            {
                // VidcDrvClient::StartDriver handles 3 cases: ALL, INPUT, OUTPUT.
                if ( in && in_len >= sizeof( vidc_start_mode_type ) )
                {
                    auto mode = *reinterpret_cast<vidc_start_mode_type *>( in );
                    if ( mode == VIDC_START_INPUT )
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_START_INPUT_DONE );
                    }
                    else if ( mode == VIDC_START_OUTPUT )
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_START_OUTPUT_DONE );
                    }
                    else
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_START );
                    }
                }
                else
                {
                    EmitVidcEvent( VIDC_EVT_RESP_START );   // "start all"
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_DRAIN:
                EmitVidcEvent( VIDC_EVT_RESP_DRAIN );
                EmitVidcEvent( VIDC_EVT_LAST_FLAG );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_STOP:
            {
                if ( in && in_len >= sizeof( vidc_stop_mode_type ) )
                {
                    auto mode = *reinterpret_cast<vidc_stop_mode_type *>( in );
                    if ( mode == VIDC_STOP_INPUT )
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_STOP_INPUT_DONE );
                    }
                    else if ( mode == VIDC_STOP_OUTPUT )
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_STOP_OUTPUT_DONE );
                    }
                    else
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_STOP );
                    }
                }
                else
                {
                    EmitVidcEvent( VIDC_EVT_RESP_STOP );
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_SET_BUFFER:
            case VIDC_IOCTL_FREE_BUFFER:
            case VIDC_IOCTL_EMPTY_INPUT_BUFFER:
            case VIDC_IOCTL_FILL_OUTPUT_BUFFER:
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_SET_PROPERTY:
            {
                if ( !in || in_len < sizeof( vidc_drv_property_type ) ) return VIDC_ERR_BAD_PARAM;
                auto *p = reinterpret_cast<vidc_drv_property_type *>( in );
                if ( p->prop_hdr.prop_id == VIDC_I_BUFFER_REQUIREMENTS )
                {
                    vidc_buffer_reqmnts_type tmp{};
                    std::memcpy( &tmp, p->payload,
                                 std::min<uint32_t>( p->prop_hdr.size, sizeof( tmp ) ) );
                    if ( tmp.buf_type == VIDC_BUFFER_INPUT ) g_req_in = tmp;
                    if ( tmp.buf_type == VIDC_BUFFER_OUTPUT ) g_req_out = tmp;
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_GET_PROPERTY:
            {
                if ( !in || in_len < sizeof( vidc_drv_property_type ) || !out || out_len == 0 )
                    return VIDC_ERR_BAD_PARAM;
                auto *p = reinterpret_cast<vidc_drv_property_type *>( in );
                if ( p->prop_hdr.prop_id == VIDC_I_BUFFER_REQUIREMENTS )
                {
                    vidc_buffer_reqmnts_type tmp{};
                    std::memcpy( &tmp, p->payload,
                                 std::min<uint32_t>( p->prop_hdr.size, sizeof( tmp ) ) );
                    if ( tmp.buf_type == VIDC_BUFFER_INPUT ) tmp = g_req_in;
                    if ( tmp.buf_type == VIDC_BUFFER_OUTPUT ) tmp = g_req_out;
                    std::memcpy(
                            out, &tmp,
                            std::min<uint32_t>( out_len, static_cast<uint32_t>( sizeof( tmp ) ) ) );
                }
                return VIDC_ERR_NONE;
            }

            default:
                return VIDC_ERR_NONE;
        }
    }

}   // extern "C"

using namespace QC;
using namespace QC::Node;

//==============================================================
// Test helper: testable config and monitor ifs
//==============================================================

struct TestableVidcNodeConfig : public VidcNodeBase_Config_t
{
};

class TestableVidcNodeConfigIfs : public VidcNodeBaseConfigIfs
{
public:
    TestableVidcNodeConfigIfs( Logger &logger ) : VidcNodeBaseConfigIfs( logger ) {}

    ~TestableVidcNodeConfigIfs() {}

    QCStatus_e VerifyAndSet( DataTree &dt, const std::string cfg, std::string &errors,
                             VidcNodeBase_Config_t &config )
    {
        DataTree old_dataTree = m_dataTree;
        m_dataTree = dt;
        QCStatus_e status = VidcNodeBaseConfigIfs::VerifyAndSet( cfg, errors, config );
        m_dataTree = old_dataTree;
        return status;
    }

    const std::string &GetOptions() { return m_options; }

    const QCNodeConfigBase_t &Get() { return m_config; }

private:
    VidcNodeBase_Config_t m_config{};
    std::string m_options = "{}";
};

struct TestableVidcNodeMonitorConfig : public QCNodeMonitoringBase_t
{
};

class TestableVidcNodeMonitoringIfs : public QCNodeMonitoringIfs
{
public:
    TestableVidcNodeMonitoringIfs( Logger &logger ) : m_logger( logger ) {}
    ~TestableVidcNodeMonitoringIfs() {}

    QCStatus_e VerifyAndSet( const std::string config, std::string &errors )
    {
        return QC_STATUS_UNSUPPORTED;
    }

    const std::string &GetOptions() { return m_options; }

    const QCNodeMonitoringBase_t &Get() { return m_config; }

    inline uint32_t GetMaximalSize() { return UINT32_MAX; }
    inline uint32_t GetCurrentSize() { return UINT32_MAX; }

    QCStatus_e Place( void *ptr, uint32_t &size ) { return QC_STATUS_UNSUPPORTED; }

private:
    Logger &m_logger;
    std::string m_options;
    TestableVidcNodeMonitorConfig m_config;
};

//=====================================================================
// Test helper: subclass to expose EventCallback & wire real client
//=====================================================================

class TestableVidcNodeBase : public VidcNodeBase
{
public:
    using VidcNodeBase::EventCallback;
    using VidcNodeBase::ValidateBuffer;
    using VidcNodeBase::ValidateBuffers;
    using VidcNodeBase::ValidateFrameSubmission;

    using VidcNodeBase::m_bufSize;
    using VidcNodeBase::m_inputBufferList;
    using VidcNodeBase::m_outputBufferList;
    using VidcNodeBase::m_pConfig;
    using VidcNodeBase::m_state;

    TestableVidcNodeBase() : m_configIfs( m_logger ), m_monitorIfs( m_logger ) {}

    // Trampoline so we can pass a C-style callback to VidcDrvClient::OpenDriver
    static void InDoneCb( VideoFrameDescriptor &frame, void *ctx )
    {
        auto *self = static_cast<TestableVidcNodeBase *>( ctx );
        // Not needed for state; provided for completeness
        (void) self;
    }
    static void OutDoneCb( VideoFrameDescriptor &frame, void *ctx )
    {
        auto *self = static_cast<TestableVidcNodeBase *>( ctx );
        // Not needed for state; provided for completeness
        (void) self;
    }
    static void EvtCb( VideoCodec_EventType_e evt, const void *pMsg, void *ctx )
    {
        auto *self = static_cast<TestableVidcNodeBase *>( ctx );
        self->EventCallback( evt, pMsg );   // protected → accessible here
    }

    // Small utility to fully wire the *real* VidcDrvClient as VidcNodeBase expects:
    //  - Init the client (name, log level, enc/dec type, buf counts)
    //  - Open the device with our static callbacks that forward to EventCallback
    void WireRealClient( const std::string &name, Logger_Level_e level, VideoEncDecType_e type,
                         const VidcNodeBase_Config_t &cfg, const VidcCodecMeta_t &meta )
    {
        m_drvClient.Init( name, level, type, cfg );   // sets m_bufNum[], type
        ASSERT_EQ( QC_STATUS_OK, m_drvClient.OpenDriver( &TestableVidcNodeBase::InDoneCb,
                                                         &TestableVidcNodeBase::OutDoneCb,
                                                         &TestableVidcNodeBase::EvtCb,
                                                         this ) );   // registers DeviceCallback
        ASSERT_EQ( QC_STATUS_OK,
                   m_drvClient.InitDriver( meta ) );   // sets codec, fps, size via SET_PROPERTY
    }

    void CloseDriver() { m_drvClient.CloseDriver(); }

    void SetConfig( const VidcNodeBase_Config_t *cfg ) { m_pConfig = cfg; }
    void SetState( QCObjectState_e s ) { m_state = s; }
    QCObjectState_e GetState() { return m_state; }

    void SetConfigPtr( const VidcNodeBase_Config_t *c ) { m_pConfig = c; }
    VidcDrvClient &Driver() { return m_drvClient; }

    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> &InList()
    {
        return m_inputBufferList;
    }
    std::vector<std::reference_wrapper<VideoFrameDescriptor_t>> &OutList()
    {
        return m_outputBufferList;
    }
    uint32_t *BufSizeArr() { return m_bufSize; }

    QCStatus_e Initialize( QCNodeInit_t & ) { return QC_STATUS_OK; }

    QCStatus_e ProcessFrameDescriptor( QCFrameDescriptorNodeIfs & ) { return QC_STATUS_OK; }

    QCStatus_e ValidateFrameSubmission( const VideoFrameDescriptor_t &frameDesc,
                                        VideoCodec_BufType_e bufferType,
                                        bool requireNonZeroSize = true )
    {
        return VidcNodeBase::ValidateFrameSubmission( frameDesc, bufferType, requireNonZeroSize );
    }

    QCNodeConfigIfs &GetConfigurationIfs() { return m_configIfs; }
    QCNodeMonitoringIfs &GetMonitoringIfs() { return m_monitorIfs; }

private:
    TestableVidcNodeConfigIfs m_configIfs;
    TestableVidcNodeMonitoringIfs m_monitorIfs;
};

//==============================================================
// Fixture
//==============================================================
class VidcNodeBaseTest : public testing::Test
{
protected:
    TestableVidcNodeBase node;
    VidcNodeBase_Config_t m_cfg{};
    VidcCodecMeta_t m_meta{};

    void SetUp() override
    {
        g_open_should_fail = false;
        g_sleep_calls = 0;

        // Default property store for buffer requirements (allows Negotiate if needed)
        g_req_in = {};
        g_req_in.buf_type = VIDC_BUFFER_INPUT;
        g_req_in.actual_count = 4;
        g_req_in.size = 4096;

        g_req_out = {};
        g_req_out.buf_type = VIDC_BUFFER_OUTPUT;
        g_req_out.actual_count = 4;
        g_req_out.size = 8192;

        // Minimal valid node config
        m_cfg.nodeId.name = "vidc-node";
        m_cfg.nodeId.id = 42;
        m_cfg.width = 1280;
        m_cfg.height = 720;
        m_cfg.frameRate = 30;
        m_cfg.numInputBufferReq = 4;
        m_cfg.numOutputBufferReq = 4;
        m_cfg.bInputDynamicMode = false;
        m_cfg.bOutputDynamicMode = false;
        m_cfg.inFormat = QC_IMAGE_FORMAT_NV12;
        m_cfg.outFormat = QC_IMAGE_FORMAT_NV12;

        // Codec meta for the driver client
        m_meta.codecType = VIDEO_CODEC_H264;   // -> maps to VIDC_CODEC_H264 internally
        m_meta.width = m_cfg.width;
        m_meta.height = m_cfg.height;
        m_meta.frameRate = m_cfg.frameRate;

        node.SetConfig( &m_cfg );

        // Wire the real client & open the device with callbacks to node.EventCallback
        node.WireRealClient( "vidc-node", LOGGER_LEVEL_DEBUG, VIDEO_ENC, m_cfg,
                             m_meta );   // encoder path uses RESP_START → RUNNING
    }

    void TearDown() override { node.CloseDriver(); }

    static VideoFrameDescriptor_t MakeFrame( uint64_t handle, void *addr, uint32_t size, uint32_t w,
                                             uint32_t h, QCImageFormat_e fmt )
    {
        VideoFrameDescriptor_t f{};
        f.dmaHandle = handle;
        f.pBuf = addr;
        f.size = size;
        f.width = w;
        f.height = h;
        f.format = fmt;
        f.pid = 1234;
        return f;
    }
};

// Build a valid baseline config for tests
static VidcNodeBase_Config_t MakeValidConfig( bool inDyn = false, bool outDyn = false,
                                              uint32_t w = 1920, uint32_t h = 1080,
                                              QCImageFormat_e inF = QC_IMAGE_FORMAT_NV12,
                                              QCImageFormat_e outF = QC_IMAGE_FORMAT_NV12 )
{
    VidcNodeBase_Config_t c{};
    c.nodeId.name = "node";
    c.nodeId.id = 1;
    c.width = w;
    c.height = h;
    c.frameRate = 30;
    c.bInputDynamicMode = inDyn;
    c.bOutputDynamicMode = outDyn;
    c.numInputBufferReq = 2;
    c.numOutputBufferReq = 3;
    c.inFormat = inF;
    c.outFormat = outF;
    return c;
}

//==============================================================
// Tests
//==============================================================

TEST_F( VidcNodeBaseTest, InitFailure )
{
    node.SetState( QC_OBJECT_STATE_ERROR );
    EXPECT_EQ( QC_STATUS_BAD_STATE, node.Init( m_cfg ) );
}

TEST_F( VidcNodeBaseTest, StopFailed )
{
    node.SetState( QC_OBJECT_STATE_ERROR );
    EXPECT_EQ( QC_STATUS_BAD_STATE, node.Stop() );
}

TEST_F( VidcNodeBaseTest, DeInitializeFailed )
{
    node.SetState( QC_OBJECT_STATE_DEINITIALIZING );
    EXPECT_EQ( QC_STATUS_BAD_STATE, node.DeInitialize() );
}

TEST_F( VidcNodeBaseTest, Test_WhenCallbackFailures )
{
    const VideoCodec_EventType_e evttab[] = { VIDEO_CODEC_EVT_FLUSH_INPUT_DONE,
                                              VIDEO_CODEC_EVT_FLUSH_OUTPUT_DONE,
                                              VIDEO_CODEC_EVT_INPUT_RECONFIG,
                                              VIDEO_CODEC_EVT_OUTPUT_RECONFIG,
                                              VIDEO_CODEC_EVT_RESP_START,
                                              VIDEO_CODEC_EVT_RESP_START_INPUT_DONE,
                                              VIDEO_CODEC_EVT_RESP_START_OUTPUT_DONE,
                                              VIDEO_CODEC_EVT_RESP_PAUSE,
                                              VIDEO_CODEC_EVT_RESP_RESUME,
                                              VIDEO_CODEC_EVT_RESP_LOAD_RESOURCES,
                                              VIDEO_CODEC_EVT_RESP_RELEASE_RESOURCES,
                                              VIDEO_CODEC_EVT_RESP_STOP,
                                              VIDEO_CODEC_EVT_ERROR,
                                              VIDEO_CODEC_EVT_FATAL };

    for ( int i = 0; i < sizeof( evttab ) / sizeof( evttab[0] ); ++i )
    {
        TestableVidcNodeBase::EvtCb( evttab[i], nullptr, &node );
    }
}

TEST_F( VidcNodeBaseTest, CallbackFailures_SpecialCases )
{
    node.SetState( QC_OBJECT_STATE_PAUSING );
    TestableVidcNodeBase::EvtCb( VIDEO_CODEC_EVT_RESP_PAUSE, nullptr, &node );
    node.SetState( QC_OBJECT_STATE_RESUMING );
    TestableVidcNodeBase::EvtCb( VIDEO_CODEC_EVT_RESP_RESUME, nullptr, &node );
}

TEST_F( VidcNodeBaseTest, ValidateInputFrameSubmissionErrors )
{
    auto f = MakeFrame( 0x1, (void *) 0xAAA, 4096, /*w*/ 640, /*h*/ 480, m_cfg.inFormat );
    node.SetState( QC_OBJECT_STATE_ERROR );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, false );
    node.SetState( QC_OBJECT_STATE_ERROR );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, true );

    /* NULL buffered frame descriptor */
    f = MakeFrame( 0x1, nullptr, 4096, /*w*/ 640, /*h*/ 480, m_cfg.inFormat );
    node.SetState( QC_OBJECT_STATE_RUNNING );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT );

    /* Zero size frame with requireNonZeroSize==true */
    f = MakeFrame( 0x1, (void *) 0xAAA, 0, /*w*/ 640, /*h*/ 480, m_cfg.inFormat );
    node.SetState( QC_OBJECT_STATE_RUNNING );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, true );
}

TEST_F( VidcNodeBaseTest, ValidateOutputFrameSubmissionErrors )
{
    auto f = MakeFrame( 0x1, (void *) 0xAAA, 4096, /*w*/ 640, /*h*/ 480, m_cfg.outFormat );
    node.SetState( QC_OBJECT_STATE_ERROR );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_OUTPUT, false );
    node.SetState( QC_OBJECT_STATE_ERROR );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_OUTPUT, true );

    /* NULL buffered frame descriptor */
    f = MakeFrame( 0x1, nullptr, 4096, /*w*/ 640, /*h*/ 480, m_cfg.outFormat );
    node.SetState( QC_OBJECT_STATE_RUNNING );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_OUTPUT );

    /* Zero size frame with requireNonZeroSize==true */
    f = MakeFrame( 0x1, (void *) 0xAAA, 0, /*w*/ 640, /*h*/ 480, m_cfg.outFormat );
    node.SetState( QC_OBJECT_STATE_RUNNING );
    node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_OUTPUT, true );
}

TEST_F( VidcNodeBaseTest, PostInit_InvalidLoadResourcesState )
{
    // Precondition not achievable required by EventCallback for RESP_LOAD_RESOURCES → STATE_ERROR.
    node.SetState( QC_OBJECT_STATE_ERROR );
    EXPECT_NE( QC_OBJECT_STATE_READY, node.PostInit() );
}

TEST_F( VidcNodeBaseTest, PostInit_TransitionsToReadyViaRealClientEvents )
{
    // Precondition required by EventCallback for RESP_LOAD_RESOURCES → READY.
    node.SetState( QC_OBJECT_STATE_INITIALIZING );
    EXPECT_EQ( QC_STATUS_OK, node.PostInit() );
    EXPECT_EQ( QC_OBJECT_STATE_READY, node.GetState() );
}

TEST_F( VidcNodeBaseTest, Start_Encoder_TransitionsToRunning )
{
    node.SetState( QC_OBJECT_STATE_READY );
    EXPECT_EQ( QC_STATUS_OK, node.Start() );   // StartDriver emits RESP_START
    EXPECT_EQ( QC_OBJECT_STATE_RUNNING,
               node.GetState() );   // EventCallback sets RUNNING on RESP_START
}

TEST_F( VidcNodeBaseTest, Stop_Encoder_TransitionsToReady )
{
    node.SetState( QC_OBJECT_STATE_RUNNING );
    EXPECT_EQ( QC_STATUS_OK, node.Stop() );                // StopEncoder emits RESP_STOP
    EXPECT_EQ( QC_OBJECT_STATE_READY, node.GetState() );   // EventCallback sets READY on RESP_STOP
}

TEST_F( VidcNodeBaseTest, DeInitialize_TransitionsToInitialAndCloses )
{
    // Move to READY (PostInit already set READY in the first test; ensure again for isolation)
    node.SetState( QC_OBJECT_STATE_READY );
    EXPECT_EQ( QC_STATUS_OK,
               node.DeInitialize() );   // ReleaseResources emits RESP_RELEASE_RESOURCES
    EXPECT_EQ( QC_OBJECT_STATE_INITIAL,
               node.GetState() );   // EventCallback sets INITIAL on RESP_RELEASE_RESOURCES
}

TEST_F( VidcNodeBaseTest, ValidateBuffer_DimensionMismatchIsInvalid )
{
    auto f = MakeFrame( 0x1, (void *) 0xAAA, 4096, /*w*/ 640, /*h*/ 480, m_cfg.inFormat );
    EXPECT_EQ( QC_STATUS_INVALID_BUF,
               node.ValidateBuffer( f, VIDEO_CODEC_BUF_INPUT ) );   // checks width/height vs config
}

TEST_F( VidcNodeBaseTest, ValidateBuffer_FormatMismatchForInputIsInvalid )
{
    auto f =
            MakeFrame( 0x1, (void *) 0xAAA, 4096, m_cfg.width, m_cfg.height, QC_IMAGE_FORMAT_P010 );
    EXPECT_EQ( QC_STATUS_INVALID_BUF,
               node.ValidateBuffer(
                       f, VIDEO_CODEC_BUF_INPUT ) );   // compares format to inFormat/outFormat
}

TEST_F( VidcNodeBaseTest, ValidateBuffers_DetectsBadDynamicConfig )
{
    m_cfg.bInputDynamicMode = true;
    auto f = MakeFrame( 0x1, (void *) 0x1, 4096, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    node.m_inputBufferList.push_back( f );   // illegal when input is dynamic
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               node.ValidateBuffers() );   // logic checks list vs dynamic flags
}

TEST_F( VidcNodeBaseTest, AllocateBuffer_NonDynamic_InsufficientDescriptors )
{
    m_cfg.bInputDynamicMode = false;
    m_cfg.numInputBufferReq = 4;
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> bufs;
    auto f1 = MakeFrame( 0x10, (void *) 0xB1, 4096, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    auto f2 = MakeFrame( 0x11, (void *) 0xB2, 4096, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    bufs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( f1 ) );
    bufs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( f2 ) );
    EXPECT_EQ( QC_STATUS_NOMEM,
               node.AllocateBuffer( bufs, /*idx*/ 0,
                                    VIDEO_CODEC_BUF_INPUT ) );   // needs >= numInputBufferReq
}

TEST_F( VidcNodeBaseTest, AllocateBuffer_NonDynamic_SizeTooSmall )
{
    m_cfg.bInputDynamicMode = false;
    m_cfg.numInputBufferReq = 2;
    node.m_bufSize[VIDEO_CODEC_BUF_INPUT] = 4096;   // typically set by NegotiateBufferReq

    auto f1 = MakeFrame( 0x10, (void *) 0xB1, /*size*/ 1024, m_cfg.width, m_cfg.height,
                         m_cfg.inFormat );
    auto f2 = MakeFrame( 0x11, (void *) 0xB2, /*size*/ 2048, m_cfg.width, m_cfg.height,
                         m_cfg.inFormat );
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> bufs{
            reinterpret_cast<QCBufferDescriptorBase_t &>( f1 ),
            reinterpret_cast<QCBufferDescriptorBase_t &>( f2 ),
    };
    EXPECT_EQ( QC_STATUS_NOMEM,
               node.AllocateBuffer( bufs, 0,
                                    VIDEO_CODEC_BUF_INPUT ) );   // checks each desc size >= bufSize
}

TEST_F( VidcNodeBaseTest, AllocateBuffer_NonDynamic_SliceIsCollected )
{
    m_cfg.bInputDynamicMode = false;
    m_cfg.numInputBufferReq = 2;
    node.m_bufSize[VIDEO_CODEC_BUF_INPUT] = 1024;

    auto f0 = MakeFrame( 0x10, (void *) 0xB0, 1024, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    auto f1 = MakeFrame( 0x11, (void *) 0xB1, 1024, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    auto f2 = MakeFrame( 0x12, (void *) 0xB2, 1024, m_cfg.width, m_cfg.height, m_cfg.inFormat );
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> bufs{
            reinterpret_cast<QCBufferDescriptorBase_t &>( f0 ),
            reinterpret_cast<QCBufferDescriptorBase_t &>( f1 ),
            reinterpret_cast<QCBufferDescriptorBase_t &>( f2 ),
    };
    EXPECT_EQ(
            QC_STATUS_OK,
            node.AllocateBuffer(
                    bufs, /*idx*/ 1,
                    VIDEO_CODEC_BUF_INPUT ) );   // InitBufferForNonDynamicMode picks [1..1+count)
    EXPECT_EQ( 2u, node.m_inputBufferList.size() );
}

TEST_F( VidcNodeBaseTest, SetBuffer_SetsDynamicMode_AndDelegatesWhenNonDynamic )
{
    // SetBuffer() → SetDynamicMode(..) and if non-dynamic → SetBuffer(..) on real client.
    m_cfg.bInputDynamicMode = false;
    EXPECT_EQ( QC_STATUS_OK, node.SetBuffer( VIDEO_CODEC_BUF_INPUT ) );

    m_cfg.bOutputDynamicMode = true;   // dynamic: only SetDynamicMode
    EXPECT_EQ( QC_STATUS_OK, node.SetBuffer( VIDEO_CODEC_BUF_OUTPUT ) );
}

TEST_F( VidcNodeBaseTest, NegotiateBufferReq_StoresDriverSize )
{
    // App can satisfy the count; fake driver will return values from property store.
    m_cfg.numOutputBufferReq = 6;   // app availability
    // property store already has g_req_out.size = 8192 (see SetUp)
    EXPECT_EQ( QC_STATUS_OK,
               node.NegotiateBufferReq(
                       VIDEO_CODEC_BUF_OUTPUT ) );   // uses real client Get/Set property flow
    EXPECT_EQ( 8192u, node.m_bufSize[VIDEO_CODEC_BUF_OUTPUT] );
}

TEST_F( VidcNodeBaseTest, NegotiateBufferReq_FailsIfDriverNeedsMoreThanAppHas )
{
    m_cfg.numInputBufferReq = 3;   // app has 3
    g_req_in.actual_count = 5;     // driver needs 5
    EXPECT_EQ( QC_STATUS_BAD_ARGUMENTS,
               node.NegotiateBufferReq(
                       VIDEO_CODEC_BUF_INPUT ) );   // node validates app vs driver counts
}

TEST_F( VidcNodeBaseTest, WaitForState_TimesOutWhenStateNeverMatches )
{
    node.SetState( QC_OBJECT_STATE_READY );
    g_sleep_calls = 0;
    EXPECT_EQ( QC_STATUS_TIMEOUT,
               node.WaitForState(
                       QC_OBJECT_STATE_RUNNING ) );   // internal timeout after small tick loop
    EXPECT_GE( g_sleep_calls.load(), 1 );
}

TEST_F( VidcNodeBaseTest, GetVidcFormat_Mapping )
{
    EXPECT_EQ( VIDC_COLOR_FORMAT_NV12, node.GetVidcFormat( QC_IMAGE_FORMAT_NV12 ) );
    EXPECT_EQ( VIDC_COLOR_FORMAT_NV12_UBWC, node.GetVidcFormat( QC_IMAGE_FORMAT_NV12_UBWC ) );
    EXPECT_EQ( VIDC_COLOR_FORMAT_NV12_P010, node.GetVidcFormat( QC_IMAGE_FORMAT_P010 ) );
    EXPECT_EQ( VIDC_COLOR_FORMAT_UNUSED,
               node.GetVidcFormat( QC_IMAGE_FORMAT_COMPRESSED_MAX ) );   // default branch
}

TEST_F( VidcNodeBaseTest, ReturnsBadStateIfNotInitial )
{
    auto cfg = MakeValidConfig();
    node.SetState( QC_OBJECT_STATE_READY );   // force bad state
    auto st = node.Init( cfg );
    EXPECT_EQ( st, QC_STATUS_BAD_STATE );   // state precondition check (Init)
}

TEST_F( VidcNodeBaseTest, SetsConfigAndNameOnSuccess )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig();
    node.SetState( QC_OBJECT_STATE_INITIAL );
    auto st = node.Init( cfg );
    EXPECT_EQ( st, QC_STATUS_OK );   // init path OK
}

TEST_F( VidcNodeBaseTest, WidthHeightAndFormatChecks )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig();
    ASSERT_EQ( node.Init( cfg ), QC_STATUS_OK );

    VideoFrameDescriptor_t f{};
    f.width = cfg.width;
    f.height = cfg.height;
    f.format = cfg.inFormat;

    // OK for input
    EXPECT_EQ( node.ValidateBuffer( f, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_OK );   // dims+format match

    // Width mismatch
    f.width = cfg.width + 1;
    EXPECT_EQ( node.ValidateBuffer( f, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_INVALID_BUF );   // mismatch -> INVALID_BUF
    f.width = cfg.width;                  // restore

    // Input format mismatch
    f.format = QC_IMAGE_FORMAT_P010;
    EXPECT_EQ( node.ValidateBuffer( f, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_INVALID_BUF );   // input format mismatch

    // Output format mismatch
    f.format = QC_IMAGE_FORMAT_P010;
    EXPECT_EQ( node.ValidateBuffer( f, VIDEO_CODEC_BUF_OUTPUT ),
               QC_STATUS_INVALID_BUF );   // output format mismatch
}

TEST_F( VidcNodeBaseTest, DynamicVsNonDynamicRules )
{
    TestableVidcNodeBase node1;

    // Case: input dynamic but input list non-empty -> BAD_ARGUMENTS
    auto cfg1 = MakeValidConfig( true, false );
    ASSERT_EQ( node1.Init( cfg1 ), QC_STATUS_OK );
    VideoFrameDescriptor_t in{};
    node1.InList().push_back( in );
    EXPECT_EQ( node1.ValidateBuffers(),
               QC_STATUS_BAD_ARGUMENTS );   // should not provide input in dynamic mode
    node1.InList().clear();

    // Case: input non-dynamic but list empty -> BAD_ARGUMENTS
    TestableVidcNodeBase node2;
    auto cfg2 = MakeValidConfig( false, false );
    ASSERT_EQ( node2.Init( cfg2 ), QC_STATUS_OK );
    EXPECT_EQ( node2.ValidateBuffers(),
               QC_STATUS_BAD_ARGUMENTS );   // should provide input in non-dynamic mode

    // Case: output dynamic but list non-empty -> BAD_ARGUMENTS
    TestableVidcNodeBase node3;
    auto cfg3 = MakeValidConfig( true, true );
    ASSERT_EQ( node3.Init( cfg3 ), QC_STATUS_OK );
    VideoFrameDescriptor_t out{};
    node3.OutList().push_back( out );
    EXPECT_EQ( node3.ValidateBuffers(),
               QC_STATUS_BAD_ARGUMENTS );   // should not provide output in dynamic mode
    node3.OutList().clear();

    // Case: output non-dynamic but list empty -> BAD_ARGUMENTS
    TestableVidcNodeBase node4;
    auto cfg4 = MakeValidConfig( false, false );
    node4.InList().push_back( in );
    ASSERT_EQ( node4.Init( cfg4 ), QC_STATUS_OK );
    EXPECT_EQ( node4.ValidateBuffers(),
               QC_STATUS_BAD_ARGUMENTS );   // should provide output in non-dynamic mode
}

TEST_F( VidcNodeBaseTest, BasicGuards )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig();
    ASSERT_EQ( node.Init( cfg ), QC_STATUS_OK );

    VideoFrameDescriptor_t f{};
    f.pBuf = reinterpret_cast<void *>( 0x1 );
    f.size = 16;

    node.SetState( QC_OBJECT_STATE_STOPING );
    EXPECT_EQ( node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, true ),
               QC_STATUS_BAD_STATE );   // not running

    node.SetState( QC_OBJECT_STATE_RUNNING );
    f.pBuf = nullptr;
    EXPECT_EQ( node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, true ),
               QC_STATUS_BAD_ARGUMENTS );   // null buf

    f.pBuf = reinterpret_cast<void *>( 0x1 );
    f.size = 0;
    EXPECT_EQ( node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_INPUT, true ),
               QC_STATUS_BAD_ARGUMENTS );   // zero size required non-zero

    f.size = 16;
    EXPECT_EQ( node.ValidateFrameSubmission( f, VIDEO_CODEC_BUF_OUTPUT, false ),
               QC_STATUS_OK );   // zero-size allowed if not required
}

TEST_F( VidcNodeBaseTest, OkAndTimeout )
{
    TestableVidcNodeBase node;
    node.SetState( QC_OBJECT_STATE_READY );
    EXPECT_EQ( node.WaitForState( QC_OBJECT_STATE_READY ),
               QC_STATUS_OK );   // already in expected state

    node.SetState( QC_OBJECT_STATE_INITIAL );
    EXPECT_EQ( node.WaitForState( QC_OBJECT_STATE_RUNNING ),
               QC_STATUS_TIMEOUT );   // times out (1ms threshold in code)
}

TEST_F( VidcNodeBaseTest, InputAndOutput )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig();
    ASSERT_EQ( node.Init( cfg ), QC_STATUS_OK );

    // OK paths
    EXPECT_EQ( node.FreeInputBuffers(), QC_STATUS_OK );    // direct forward to driver stub
    EXPECT_EQ( node.FreeOutputBuffers(), QC_STATUS_OK );   // direct forward to driver stub
}

TEST_F( VidcNodeBaseTest, NonDynamic_InsufficientAndSizeAndSuccess )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig( false, false );
    ASSERT_EQ( node.Init( cfg ), QC_STATUS_OK );

    // Provide fewer than required
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> v;
    VideoFrameDescriptor_t a{}, b{};
    a.size = 100;
    b.size = 100;
    v.push_back( a );   // only one provided vs requires 2 input/3 output

    node.BufSizeArr()[VIDEO_CODEC_BUF_INPUT] = 64;
    node.BufSizeArr()[VIDEO_CODEC_BUF_OUTPUT] = 64;

    EXPECT_EQ( node.AllocateBuffer( v, 0, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_NOMEM );   // insufficient descriptors

    // Enough descriptors but too small size
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> v2;
    VideoFrameDescriptor_t c{}, d{};
    c.size = 32;   // required 64
    v2.push_back( c );
    v2.push_back( d );
    EXPECT_EQ( node.AllocateBuffer( v2, 0, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_NOMEM );   // size too small

    // Success path: exactly required size/count, and test bufferIdx offset selection
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> v3;
    VideoFrameDescriptor_t x{}, y{}, z{};
    x.size = 64;
    y.size = 64;
    z.size = 64;
    v3.push_back( x );
    v3.push_back( y );
    v3.push_back( z );
    EXPECT_EQ( node.AllocateBuffer( v3, 1, VIDEO_CODEC_BUF_INPUT ),
               QC_STATUS_OK );   // picks y,z into input list
    EXPECT_EQ( node.InList().size(), 2u );
}

TEST_F( VidcNodeBaseTest, DynamicAndStaticModes )
{
    TestableVidcNodeBase node;
    auto cfg = MakeValidConfig( false, false );
    ASSERT_EQ( node.Init( cfg ), QC_STATUS_OK );
    // Non-dynamic -> SetDynamicMode + SetBuffer
    EXPECT_EQ( node.SetBuffer( VIDEO_CODEC_BUF_INPUT ), QC_STATUS_OK );

    // Dynamic -> SetDynamicMode only
    TestableVidcNodeBase node2;
    cfg = MakeValidConfig( true, true );
    ASSERT_EQ( node2.Init( cfg ), QC_STATUS_OK );
    EXPECT_EQ( node2.SetBuffer( VIDEO_CODEC_BUF_OUTPUT ), QC_STATUS_OK );
}

TEST_F( VidcNodeBaseTest, MapsKnownFormatsAndDefault )
{
    TestableVidcNodeBase node;
    EXPECT_EQ( node.GetVidcFormat( QC_IMAGE_FORMAT_NV12 ), VIDC_COLOR_FORMAT_NV12 );   // NV12 map
    EXPECT_EQ( node.GetVidcFormat( QC_IMAGE_FORMAT_NV12_UBWC ),
               VIDC_COLOR_FORMAT_NV12_UBWC );   // UBWC map
    EXPECT_EQ( node.GetVidcFormat( QC_IMAGE_FORMAT_P010 ),
               VIDC_COLOR_FORMAT_NV12_P010 );   // P010 map
    EXPECT_EQ( node.GetVidcFormat( QC_IMAGE_FORMAT_MAX ),
               VIDC_COLOR_FORMAT_UNUSED );   // default/unsupported
}

TEST_F( VidcNodeBaseTest, AllTransitionsAndGuards )
{
    TestableVidcNodeBase node;

    node.EventCallback( VIDEO_CODEC_EVT_FATAL, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_ERROR );   // FATAL -> ERROR

    node.SetState( QC_OBJECT_STATE_STARTING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_START, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_RUNNING );   // START from STARTING -> RUNNING

    node.SetState( QC_OBJECT_STATE_INITIALIZING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_LOAD_RESOURCES, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_READY );   // LOAD_RESOURCES -> READY

    node.SetState( QC_OBJECT_STATE_DEINITIALIZING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_RELEASE_RESOURCES, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_INITIAL );   // RELEASE_RESOURCES -> INITIAL

    node.SetState( QC_OBJECT_STATE_STOPING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_STOP, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_READY );   // STOP -> READY

    node.SetState( QC_OBJECT_STATE_PAUSING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_PAUSE, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_PAUSE );   // PAUSE -> PAUSE

    node.SetState( QC_OBJECT_STATE_RESUMING );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_RESUME, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_RUNNING );   // RESUME -> RUNNING

    node.EventCallback( VIDEO_CODEC_EVT_ERROR, nullptr );   // normal error event (no state change)

    // Wrong-state guards (a couple of samples)
    node.SetState( QC_OBJECT_STATE_READY );
    node.EventCallback( VIDEO_CODEC_EVT_RESP_START, nullptr );
    EXPECT_EQ( node.GetState(), QC_OBJECT_STATE_ERROR );   // START from wrong state -> ERROR
}

TEST_F( VidcNodeBaseTest, ValidAndInvalidStaticConfig )
{
    Logger dummy;
    TestableVidcNodeConfigIfs cfgIfs( dummy );

    // Prepare a good "static" config section
    DataTree staticDt;
    staticDt.Set( "name", std::string( "vidc-node" ) );
    staticDt.Set( "id", static_cast<uint8_t>( 7 ) );
    staticDt.Set( "width", static_cast<uint32_t>( 1280 ) );
    staticDt.Set( "height", static_cast<uint32_t>( 720 ) );
    staticDt.Set( "frameRate", static_cast<uint32_t>( 60 ) );
    staticDt.Set( "bInputDynamicMode", false );
    staticDt.Set( "bOutputDynamicMode", false );
    staticDt.Set( "numInputBufferReq", static_cast<uint32_t>( 2 ) );
    staticDt.Set( "numOutputBufferReq", static_cast<uint32_t>( 2 ) );
    staticDt.Set( "inputImageFormat", "nv12" );
    staticDt.Set( "outputImageFormat", "h264" );

    DataTree dataTree;
    dataTree.Set( "static", staticDt );

    std::string errors;
    VidcNodeBase_Config_t out{};
    EXPECT_EQ( cfgIfs.VerifyAndSet( dataTree, dataTree.Dump(), errors, out ),
               QC_STATUS_OK );   // happy path logs/assigns fields

    // Now register an invalid config to trigger ParseStaticConfig errors
    DataTree badDt;
    badDt.Set( "name", std::string( "bad" ) );
    badDt.Set( "id", static_cast<uint8_t>( 1 ) );
    badDt.Set( "width", static_cast<uint32_t>( 0 ) );       // invalid
    badDt.Set( "height", static_cast<uint32_t>( 0 ) );      // invalid
    badDt.Set( "frameRate", static_cast<uint32_t>( 0 ) );   // invalid
    badDt.Set( "bInputDynamicMode", false );
    badDt.Set( "bOutputDynamicMode", false );
    badDt.Set( "numInputBufferReq", static_cast<uint32_t>( 0 ) );    // invalid in non-dynamic
    badDt.Set( "numOutputBufferReq", static_cast<uint32_t>( 0 ) );   // invalid in non-dynamic
    badDt.Set( "inputImageFormat", std::string( "INVALID" ) );       // invalid
    badDt.Set( "outputImageFormat", std::string( "INVALID" ) );      // invalid

    dataTree = DataTree{};
    dataTree.Set( "static", badDt );

    errors.clear();
    EXPECT_EQ( cfgIfs.VerifyAndSet( dataTree, dataTree.Dump(), errors, out ),
               QC_STATUS_BAD_ARGUMENTS );
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
