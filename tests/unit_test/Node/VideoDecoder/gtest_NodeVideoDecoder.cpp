// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <malloc.h>
#include <stdio.h>
#include <thread>

#include "QC/Common/Types.hpp"
#include "QC/Infras/Memory/ImageDescriptor.hpp"
#include "QC/Node/VideoDecoder.hpp"
#include "QC/sample/BufferManager.hpp"
#include "VidcDemuxer.hpp"
#include "md5_utils.hpp"
#include "vidc_driver_mockup.hpp"

using namespace QC;
using namespace QC::Memory;
using namespace QC::Node;
using namespace QC::sample;
using namespace QC::test::utils;

static std::mutex g_InMutex;
static std::condition_variable g_InCondVar;
static std::mutex g_OutMutex;
static std::condition_variable g_OutCondVar;
static uint64_t g_timestamp = 0;

void OnDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    QCFrameDescriptorNodeIfs &frameDesc = eventInfo.frameDesc;

    // INPUT frame:
    QCBufferDescriptorBase_t &inBufDesc =
            frameDesc.GetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID );
    const VideoFrameDescriptor *pInSharedBuffer =
            dynamic_cast<VideoFrameDescriptor *>( &inBufDesc );
    if ( nullptr != pInSharedBuffer )
    {
        ASSERT_EQ( QC_STATUS_OK, eventInfo.status );
        ASSERT_EQ( 1, eventInfo.node.id );
    }
    // Send signal
    g_InCondVar.notify_one();

    // OUTPUT frame:
    QCBufferDescriptorBase_t &outBufDesc =
            frameDesc.GetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID );
    const VideoFrameDescriptor *pOutSharedBuffer =
            dynamic_cast<VideoFrameDescriptor *>( &outBufDesc );
    if ( nullptr != pOutSharedBuffer )
    {
        ASSERT_EQ( QC_STATUS_OK, eventInfo.status );
        ASSERT_EQ( 1, eventInfo.node.id );
    }
    // Send signal
    g_OutCondVar.notify_one();
}

// ============================================================================
// UNIT-LEVEL ADDITIONS (full coverage for VideoDecoder)
// - Links against real VidcDrvClient.o
// - Stubs only the C driver boundary in this TU
// ============================================================================
extern "C"
{
    // Callback captured from real VidcDrvClient::OpenDriver(...)
    static ioctl_callback_t g_vidc_cb{};
    static void *g_fake_handle = reinterpret_cast<void *>( 0xDEC0DE01 );

    // Small controls for negative/timeout paths
    static bool g_use_mock = true;
    static bool g_open_should_fail = false;
    static bool g_emit_load_done = true;
    static bool g_emit_release_done = true;
    static bool g_emit_start_input_done = true;
    static bool g_emit_start_output_done = true;
    static bool g_emit_stop_done = true;
    static bool g_emit_output_reconfig_once =
            false;   // inject one reconfig before first OUTPUT_DONE
    static bool g_start_output_ioctl_should_fail =
            false;   // Fail the device START ioctl when mode==VIDC_START_OUTPUT
    static std::atomic<int> g_fill_output_calls{
            0 };   // Count output FILL IOCTLs to prove the loop broke before trying the second
                   // buffer

    // Near other globals:
    static std::atomic<int> g_set_buffer_out_calls{ 0 };

    // fast "sleep" so WaitForState/WaitForCmdCompleted do not block
    static std::atomic<int> g_sleep_calls{ 0 };
    int __mockup_MM_Timer_Sleep( unsigned int ms )
    {
        g_sleep_calls++;
        usleep( ms % 1000 );
        for ( ; ( ms /= 1000 ); ) usleep( 1000 );
        return 0;
    }

    // Minimal property store for BUFFER_REQUIREMENTS (used by negotiate paths)
    static vidc_buffer_reqmnts_type g_req_in{};
    static vidc_buffer_reqmnts_type g_req_out{};

    void *__mockup_device_open( const char *pathname, ioctl_callback_t *cb )
    {
        if ( !g_use_mock ) return device_open( (char *) pathname, cb );
        if ( g_open_should_fail ) return nullptr;
        if ( cb ) g_vidc_cb = *cb;
        return g_fake_handle;
    }
    int __mockup_device_close( ioctl_session_t *handle )
    {
        return 0;
    }

    static void EmitVidcEvent( vidc_event_type evt_id, const vidc_frame_data_type *f = nullptr )
    {
        if ( !g_vidc_cb.handler ) return;
        vidc_drv_msg_info_type evt{};
        evt.event_type = evt_id;
        if ( f ) evt.payload.frame_data = *f;
        g_vidc_cb.handler( reinterpret_cast<uint8_t *>( &evt ), sizeof( evt ), g_vidc_cb.data );
    }

    int __mockup_device_ioctl( ioctl_session_t *handle, uint32_t cmd, uint8_t *in, uint32_t in_len,
                               uint8_t *out, uint32_t out_len )
    {
        if ( !g_use_mock ) return device_ioctl( handle, cmd, in, in_len, out, out_len );

        switch ( cmd )
        {
            case VIDC_IOCTL_LOAD_RESOURCES:
                if ( g_emit_load_done )
                    EmitVidcEvent( VIDC_EVT_RESP_LOAD_RESOURCES );   // → READY via PostInit
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_RELEASE_RESOURCES:
                if ( g_emit_release_done )
                    EmitVidcEvent( VIDC_EVT_RESP_RELEASE_RESOURCES );   // → INITIAL
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_START:
            {
                if ( in /*&& in_len >= sizeof(vidc_start_mode_type)*/ )
                {
                    auto mode = *reinterpret_cast<vidc_start_mode_type *>( in );
                    if ( mode == VIDC_START_INPUT )
                    {
                        if ( g_emit_start_input_done )
                            EmitVidcEvent( VIDC_EVT_RESP_START_INPUT_DONE );
                    }
                    else if ( mode == VIDC_START_OUTPUT )
                    {
                        if ( g_start_output_ioctl_should_fail ) return VIDC_ERR_FAIL;
                        if ( g_emit_start_output_done )
                            EmitVidcEvent( VIDC_EVT_RESP_START_OUTPUT_DONE );
                    }
                    else
                        EmitVidcEvent( VIDC_EVT_RESP_START );
                }
                else
                {
                    EmitVidcEvent( VIDC_EVT_RESP_START );
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_DRAIN:
                EmitVidcEvent( VIDC_EVT_RESP_DRAIN );
                EmitVidcEvent( VIDC_EVT_LAST_FLAG );
                return VIDC_ERR_NONE;

            case VIDC_IOCTL_STOP:
            {
                if ( g_emit_stop_done )
                {
                    if ( in /*&& in_len >= sizeof(vidc_stop_mode_type)*/ )
                    {
                        auto mode = *reinterpret_cast<vidc_stop_mode_type *>( in );
                        if ( mode == VIDC_STOP_INPUT )
                            EmitVidcEvent( VIDC_EVT_RESP_STOP_INPUT_DONE );
                        else if ( mode == VIDC_STOP_OUTPUT )
                            EmitVidcEvent( VIDC_EVT_RESP_STOP_OUTPUT_DONE );
                        else
                            EmitVidcEvent( VIDC_EVT_RESP_STOP );
                    }
                    else
                    {
                        EmitVidcEvent( VIDC_EVT_RESP_STOP );
                    }
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_EMPTY_INPUT_BUFFER:
            {
                // echo INPUT_DONE for same frame (drives VideoDecoder::InFrameCallback)
                if ( in /*&& in_len >= sizeof(vidc_frame_data_type)*/ )
                {
                    auto *fd = reinterpret_cast<vidc_frame_data_type *>( in );
                    EmitVidcEvent( VIDC_EVT_RESP_INPUT_DONE, fd );
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_FILL_OUTPUT_BUFFER:
            {
                g_fill_output_calls++;   // record that a buffer actually made it to FillBuffer
                if ( in /*&& in_len >= sizeof(vidc_frame_data_type)*/ )
                {
                    auto *fd = reinterpret_cast<vidc_frame_data_type *>( in );
                    if ( g_emit_output_reconfig_once )
                    {
                        g_emit_output_reconfig_once = false;
                        EmitVidcEvent(
                                VIDC_EVT_OUTPUT_RECONFIG );   // triggers HandleOutputReconfig
                    }
                    vidc_frame_data_type outfd = *fd;
                    outfd.data_len = std::min<uint32_t>( fd->alloc_len, 2048 );
                    outfd.timestamp = fd->timestamp;
                    outfd.flags = 0;
                    EmitVidcEvent( VIDC_EVT_RESP_OUTPUT_DONE, &outfd );
                }
                return VIDC_ERR_NONE;
            }

            case VIDC_IOCTL_SET_PROPERTY:
            {
                if ( !in /*|| in_len < sizeof(vidc_drv_property_type)*/ ) return VIDC_ERR_BAD_PARAM;
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
                if ( !in /*|| in_len < sizeof(vidc_drv_property_type)*/ || !out || out_len == 0 )
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

            case VIDC_IOCTL_SET_BUFFER:
            {
                // Inspect buf_type to see if this is OUTPUT
                if ( in /*&& in_len >= sizeof(vidc_buffer_info_type)*/ )
                {
                    auto *info = reinterpret_cast<vidc_buffer_info_type *>( in );
                    if ( info->buf_type == VIDC_BUFFER_OUTPUT )
                    {
                        g_set_buffer_out_calls++;
                    }
                }
                return VIDC_ERR_NONE;
            }

            default:
                return VIDC_ERR_NONE;
        }
    }
}   // extern "C"

TEST( Demuxer, SANITY_Demuxer )
{
    QCStatus_e ret;
    VidcDemuxer vidcDemuxer;
    VidcDemuxer_Config_t vidcDemuxConfig;
    VidcDemuxer_VideoInfo_t videoInfo;

    vidcDemuxConfig.pVideoFileName = "./data/test/VideoDecoder/test.mp4";
    vidcDemuxConfig.startFrameIdx = 0;

    ret = vidcDemuxer.Init( &vidcDemuxConfig );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = vidcDemuxer.GetVideoInfo( videoInfo );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ASSERT_EQ( 1920, videoInfo.frameWidth );
    ASSERT_EQ( 1024, videoInfo.frameHeight );
    ASSERT_EQ( 101, videoInfo.format );

    ret = vidcDemuxer.DeInit();
    ASSERT_EQ( QC_STATUS_OK, ret );
}

void VdTestDynamic( uint32_t bufferNum, QCImageFormat_e outFormat, const char *videoFile )
{
    QCStatus_e ret;
    std::string errors;

    BufferManager bufMgr = BufferManager( { "VDEC", QC_NODE_TYPE_VDEC, 0 } );
    QCNodeIfs *pNodeVide = new QC::Node::VideoDecoder();
    DataTree dt;

    VidcDemuxer vidcDemuxer;
    VideoDecoder vidcDecoder;
    VidcDemuxer_Config_t vidcDemuxConfig;
    VideoDecoder_Config_t vidcDecoderConfig;
    VidcDemuxer_VideoInfo_t videoInfo;
    VidcDemuxer_FrameInfo_t frameInfo;
    uint32_t frameNum = 10;

    vidcDemuxConfig.pVideoFileName = videoFile;
    vidcDemuxConfig.startFrameIdx = 0;

    ASSERT_EQ( QC_OBJECT_STATE_INITIAL, vidcDecoder.GetState() );

    ret = vidcDemuxer.Init( &vidcDemuxConfig );
    ASSERT_EQ( QC_STATUS_OK, ret );

    ret = vidcDemuxer.GetVideoInfo( videoInfo );
    ASSERT_EQ( QC_STATUS_OK, ret );

    dt.Set<std::string>( "name", "SANITY_VideoDecoder_Dynamic" );
    dt.Set<uint32_t>( "id", 1 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", videoInfo.frameWidth );
    dt.Set<uint32_t>( "height", videoInfo.frameHeight );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", bufferNum );
    dt.Set<uint32_t>( "numOutputBufferReq", bufferNum );
    dt.Set<uint32_t>( "frameRate", 30 );

    const char *inFmt = nullptr;
    const char *outFmt = nullptr;

    switch ( videoInfo.format )
    {
        case QC_IMAGE_FORMAT_RGB888:
            inFmt = "rgb888";
            break;
        case QC_IMAGE_FORMAT_BGR888:
            inFmt = "bgr888";
            break;
        case QC_IMAGE_FORMAT_UYVY:
            inFmt = "uyvy";
            break;
        case QC_IMAGE_FORMAT_NV12:
            inFmt = "nv12";
            break;
        case QC_IMAGE_FORMAT_P010:
            inFmt = "p010";
            break;
        case QC_IMAGE_FORMAT_NV12_UBWC:
            inFmt = "nv12_ubwc";
            break;
        case QC_IMAGE_FORMAT_TP10_UBWC:
            inFmt = "tp10_ubwc";
            break;
        case QC_IMAGE_FORMAT_COMPRESSED_H264:
            inFmt = "h264";
            break;
        case QC_IMAGE_FORMAT_COMPRESSED_H265:
            inFmt = "h265";
            break;
        default:
            printf( "error: unrecognized input format %s\n", inFmt );
            return;
    }

    switch ( outFormat )
    {
        case QC_IMAGE_FORMAT_RGB888:
            outFmt = "rgb888";
            break;
        case QC_IMAGE_FORMAT_BGR888:
            outFmt = "bgr888";
            break;
        case QC_IMAGE_FORMAT_UYVY:
            outFmt = "uyvy";
            break;
        case QC_IMAGE_FORMAT_NV12:
            outFmt = "nv12";
            break;
        case QC_IMAGE_FORMAT_P010:
            outFmt = "p010";
            break;
        case QC_IMAGE_FORMAT_NV12_UBWC:
            outFmt = "nv12_ubwc";
            break;
        case QC_IMAGE_FORMAT_TP10_UBWC:
            outFmt = "tp10_ubwc";
            break;
        case QC_IMAGE_FORMAT_COMPRESSED_H264:
            outFmt = "h264";
            break;
        case QC_IMAGE_FORMAT_COMPRESSED_H265:
            outFmt = "h265";
            break;
        default:
            printf( "error: unrecognized input format %s\n", inFmt );
            return;
    }

    dt.Set<std::string>( "inputImageFormat", inFmt );
    dt.Set<std::string>( "outputImageFormat", outFmt );

    DataTree dataTree;
    dataTree.Set( "static", dt );

    QCNodeInit_t config = { .config = dataTree.Dump(), .callback = OnDoneCb };
    printf( "config: %s\n", config.config.c_str() );

    ret = pNodeVide->Initialize( config );
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_READY, pNodeVide->GetState() );

    QCNodeConfigIfs &cfgIfs = pNodeVide->GetConfigurationIfs();
    const std::string &options = cfgIfs.GetOptions();

    DataTree optionsDt;
    ret = optionsDt.Load( options, errors );
    ASSERT_EQ( QC_STATUS_OK, ret );

    uint32_t numInputBufferReq = dt.Get( "numInputBufferReq", bufferNum );
    uint32_t numOutputBufferReq = dt.Get( "numOutputBufferReq", bufferNum );

    printf( "options: %s\n", options.c_str() );

    ret = pNodeVide->Start();
    ASSERT_EQ( QC_STATUS_OK, ret );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    std::vector<VideoFrameDescriptor_t> inputs;
    std::vector<VideoFrameDescriptor_t> outputs;

    ImageDescriptor_t imgDesc;

    for ( uint32_t i = 0; i < numInputBufferReq; i++ )
    {
        ImageProps_t inputImgProps;
        inputImgProps.batchSize = 1;
        inputImgProps.width = videoInfo.frameWidth;
        inputImgProps.height = videoInfo.frameHeight;
        inputImgProps.numPlanes = 1;
        inputImgProps.planeBufSize[0] = videoInfo.maxFrameSize;
        inputImgProps.format = videoInfo.format;
        inputImgProps.allocatorType = QC_MEMORY_ALLOCATOR_DMA_VPU;
        inputImgProps.cache = QC_CACHEABLE;

        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( inputImgProps, bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        inputs.push_back( bufDesc );
    }

    for ( uint32_t i = 0; i < numOutputBufferReq; i++ )
    {
        VideoFrameDescriptor_t bufDesc;
        ret = bufMgr.Allocate( ImageBasicProps( videoInfo.frameWidth, videoInfo.frameHeight,
                                                outFormat, QC_MEMORY_ALLOCATOR_DMA_VPU,
                                                QC_CACHEABLE ),
                               bufDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
        outputs.push_back( bufDesc );
    }

    NodeFrameDescriptor inFrameDesc( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID + 1 );
    inFrameDesc.Clear();
    NodeFrameDescriptor outFrameDesc( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID + 1 );
    outFrameDesc.Clear();

    uint32_t nr = std::min( numInputBufferReq, numOutputBufferReq );
    (void) nr;   // not used below but kept to preserve original logic

    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, pNodeVide->GetState() );

    for ( uint32_t i = 0; i < numInputBufferReq; i++ )
    {
        ret = vidcDemuxer.GetFrame( inputs[i], frameInfo );
        ASSERT_EQ( QC_STATUS_OK, ret );

        inputs[i].timestampNs = frameInfo.startTime;
        inputs[i].appMarkData = i;

        // Set up an input buffer for the frame
        ret = inFrameDesc.SetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID, inputs[i] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the frame
        ret = pNodeVide->ProcessFrameDescriptor( inFrameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    for ( uint32_t i = 0; i < numOutputBufferReq; i++ )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );

        // Set up an output buffer for the frame
        ret = outFrameDesc.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, outputs[i] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the frame
        ret = pNodeVide->ProcessFrameDescriptor( outFrameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

    for ( uint32_t i = bufferNum; i < frameNum; i++ )
    {
        uint32_t bufferIdx = i % bufferNum;

        ret = vidcDemuxer.GetFrame( inputs[bufferIdx], frameInfo );
        ASSERT_EQ( QC_STATUS_OK, ret );

        inputs[bufferIdx].timestampNs = frameInfo.startTime;
        inputs[bufferIdx].appMarkData = i;

        ret = inFrameDesc.SetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID, inputs[bufferIdx] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the frame
        ret = pNodeVide->ProcessFrameDescriptor( inFrameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );

        std::unique_lock<std::mutex> outLock( g_OutMutex );
        g_OutCondVar.wait( outLock );

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );

        // Set up an output buffer for the frame
        ret = outFrameDesc.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, outputs[bufferIdx] );
        ASSERT_EQ( QC_STATUS_OK, ret );

        // Process the frame
        ret = pNodeVide->ProcessFrameDescriptor( outFrameDesc );
        ASSERT_EQ( QC_STATUS_OK, ret );
    }

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

#if 0
TEST( Decoder, SANITY_Decoder_Dynamic_HEVC_NV12 )
{
  g_use_mock = false;
  uint32_t bufferNum = 4;
  char videoFile[] = "./data/test/VideoDecoder/test.mp4";
  VdTestDynamic( bufferNum, QC_IMAGE_FORMAT_NV12, videoFile );
  g_use_mock = true;
}

TEST( Decoder, SANITY_Decoder_Dynamic_HEVC_P010 )
{
  g_use_mock = false;
  uint32_t bufferNum = 4;
  char videoFile[] = "./data/test/VideoDecoder/test.mp4";
  VdTestDynamic( bufferNum, QC_IMAGE_FORMAT_P010, videoFile );
  g_use_mock = true;
}
#endif

// ============================================================================
//                            UNIT TEST SUITE
// ============================================================================
using namespace QC;
using namespace QC::Node;
using namespace QC::Memory;

// Per-test counters updated by VideoDecoder's app callback
struct VdUnitCounters
{
    std::atomic<int> in_cb{ 0 }, out_cb{ 0 }, evt_cb{ 0 };
};
static VdUnitCounters *g_unit_counters = nullptr;

static void TestOnDoneCb( const QCNodeEventInfo_t &eventInfo )
{
    if ( !g_unit_counters ) return;
    g_unit_counters->evt_cb++;
    auto &fd = eventInfo.frameDesc;
    if ( fd.GetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID ).pBuf ) g_unit_counters->in_cb++;
    if ( fd.GetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID ).pBuf ) g_unit_counters->out_cb++;
}

class VideoDecoderTest : public ::testing::Test
{
protected:
    VideoDecoder dec;
    VdUnitCounters counters;
    std::string cfgJson;

    void SetUp() override
    {
        // default driver requirements (negotiation will succeed)
        g_req_in = {};
        g_req_in.buf_type = VIDC_BUFFER_INPUT;
        g_req_in.actual_count = 4;
        g_req_in.size = 4096;
        g_req_out = {};
        g_req_out.buf_type = VIDC_BUFFER_OUTPUT;
        g_req_out.actual_count = 4;
        g_req_out.size = 8192;

        g_open_should_fail = false;
        g_emit_load_done = true;
        g_emit_release_done = true;
        g_emit_start_input_done = true;
        g_emit_start_output_done = true;
        g_emit_stop_done = true;
        g_emit_output_reconfig_once = false;

        g_unit_counters = &counters;

        // Build a JSON config matching your style (DataTree "static" block)
        DataTree dt;
        dt.Set<std::string>( "name", "UNIT_VideoDecoder" );
        dt.Set<uint32_t>( "id", 1 );
        dt.Set<std::string>( "logLevel", "ERROR" );
        dt.Set<uint32_t>( "width", 1280 );
        dt.Set<uint32_t>( "height", 720 );
        dt.Set<bool>( "bInputDynamicMode", true );
        dt.Set<bool>( "bOutputDynamicMode", true );
        dt.Set<uint32_t>( "numInputBufferReq", 4 );
        dt.Set<uint32_t>( "numOutputBufferReq", 4 );
        dt.Set<uint32_t>( "frameRate", 30 );
        dt.Set<std::string>( "inputImageFormat",
                             "h264" );   // → VIDEO_CODEC_H264 in InitDrvProperty
        dt.Set<std::string>( "outputImageFormat", "nv12" );
        DataTree root;
        root.Set( "static", dt );
        cfgJson = root.Dump();
    }

    void TearDown() override { g_unit_counters = nullptr; }

    QCNodeInit_t MakeInit()
    {
        QCNodeInit_t init{};
        init.config = cfgJson;
        init.callback = TestOnDoneCb;
        return init;
    }

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

TEST_F( VideoDecoderTest, Initialize_Start_Stop_Deinit_Success )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );    // RESP_LOAD_RESOURCES → READY
    EXPECT_EQ( QC_OBJECT_STATE_READY, dec.GetState() );   // VidcNodeBase::PostInit

    ASSERT_EQ( QC_STATUS_OK, dec.Start() );                 // START_INPUT → RESP_START_INPUT_DONE
    EXPECT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );   // VideoDecoder::EventCallback

    ASSERT_EQ( QC_STATUS_OK, dec.Stop() );   // RESP_STOP → READY
    EXPECT_EQ( QC_OBJECT_STATE_READY, dec.GetState() );

    ASSERT_EQ( QC_STATUS_OK, dec.DeInitialize() );   // RESP_RELEASE_RESOURCES → INITIAL
    EXPECT_EQ( QC_OBJECT_STATE_INITIAL, dec.GetState() );
}

TEST_F( VideoDecoderTest, Initialize_Fails_WhenOpenDriverReturnsNull )
{
    g_open_should_fail = true;
    auto init = MakeInit();
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // OpenDriver failure propagates from Initialize
}

TEST_F( VideoDecoderTest, Initialize_TimesOut_WithoutLoadResourcesEvent )
{
    g_emit_load_done = false;   // suppress RESP_LOAD_RESOURCES
    auto init = MakeInit();
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // WaitForState(READY) → timeout
}

TEST_F( VideoDecoderTest, ValidateConfig_FailsOnUnsupportedFormats_And_OutOfRange )
{
    // 1) Unsupported input format (not H264/H265)
    DataTree dt1;
    dt1.Set<std::string>( "name", "X" );
    dt1.Set<uint32_t>( "id", 1 );
    dt1.Set<uint32_t>( "width", 128 );
    dt1.Set<uint32_t>( "height", 128 );
    dt1.Set<uint32_t>( "frameRate", 30 );
    dt1.Set<bool>( "bInputDynamicMode", true );
    dt1.Set<bool>( "bOutputDynamicMode", true );
    dt1.Set<uint32_t>( "numInputBufferReq", 4 );
    dt1.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt1.Set<std::string>( "inputImageFormat",
                          "nv12" );   // invalid as *compressed* input for decoder
    dt1.Set<std::string>( "outputImageFormat", "nv12" );
    DataTree r1;
    r1.Set( "static", dt1 );
    QCNodeInit_t i1{ .config = r1.Dump(), .callback = TestOnDoneCb };
    EXPECT_NE( QC_STATUS_OK, dec.Initialize( i1 ) );   // VideoDecoder::ValidateConfig catches it

    // 2) Unsupported output format (not NV12/P010)
    DataTree dt2 = dt1;
    dt2.Set<std::string>( "inputImageFormat", "h264" );
    dt2.Set<std::string>( "outputImageFormat", "bgr888" );
    DataTree r2;
    r2.Set( "static", dt2 );
    QCNodeInit_t i2{ .config = r2.Dump(), .callback = TestOnDoneCb };
    EXPECT_NE( QC_STATUS_OK, dec.Initialize( i2 ) );

    // 3) Buffers out of allowed range (min/max)
    DataTree dt3 = dt1;
    dt3.Set<std::string>( "inputImageFormat", "h264" );
    dt3.Set<std::string>( "outputImageFormat", "nv12" );
    dt3.Set<uint32_t>( "numInputBufferReq", 1 );    // too small
    dt3.Set<uint32_t>( "numOutputBufferReq", 1 );   // too small
    DataTree r3;
    r3.Set( "static", dt3 );
    QCNodeInit_t i3{ .config = r3.Dump(), .callback = TestOnDoneCb };
    EXPECT_NE( QC_STATUS_OK, dec.Initialize( i3 ) );
}

TEST_F( VideoDecoderTest, SubmitInputAndOutput_InvokesCallbacks )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    auto in =
            MakeFrame( 0x1001, (void *) 0xA000, 2048, 1280, 720, QC_IMAGE_FORMAT_COMPRESSED_H264 );
    auto out = MakeFrame( 0x2001, (void *) 0xB000, 8192, 1280, 720, QC_IMAGE_FORMAT_NV12 );
    NodeFrameDescriptor inFd( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID + 1 );
    inFd.Clear();
    NodeFrameDescriptor outFd( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID + 1 );
    outFd.Clear();
    ASSERT_EQ( QC_STATUS_OK, inFd.SetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID, in ) );
    ASSERT_EQ( QC_STATUS_OK, outFd.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, out ) );

    ASSERT_EQ( QC_STATUS_OK,
               dec.ProcessFrameDescriptor( inFd ) );   // → EMPTY_INPUT_BUFFER → RESP_INPUT_DONE
    ASSERT_EQ( QC_STATUS_OK,
               dec.ProcessFrameDescriptor( outFd ) );   // → FILL_OUTPUT_BUFFER → RESP_OUTPUT_DONE

    EXPECT_GE( counters.in_cb.load(), 1 );
    EXPECT_GE( counters.out_cb.load(), 1 );
}

TEST_F( VideoDecoderTest, OutputReconfig_Sequence_Completes_AndRemainsRunning )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    g_emit_output_reconfig_once = true;   // inject OUTPUT_RECONFIG before first OUTPUT_DONE

    auto out = MakeFrame( 0x3001, (void *) 0xC000, 8192, 1280, 720, QC_IMAGE_FORMAT_NV12 );
    NodeFrameDescriptor outFd( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID + 1 );
    outFd.Clear();
    ASSERT_EQ( QC_STATUS_OK, outFd.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, out ) );
    ASSERT_EQ( QC_STATUS_OK, dec.ProcessFrameDescriptor(
                                     outFd ) );   // HandleOutputReconfig → StartDriver(OUTPUT) →
                                                  // RESP_START_OUTPUT_DONE → FinishOutputReconfig

    EXPECT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );
    EXPECT_GE( counters.out_cb.load(), 1 );
}

TEST_F( VideoDecoderTest, OutputReconfig_SecondEventWhileInProgress_ForcesError )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Fire two OUTPUT_RECONFIG events without sending START_OUTPUT_DONE between them.
    // First toggles "in progress"; second returns BAD_STATE → EventCallback marks ERROR.
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );
    //  EXPECT_EQ(QC_OBJECT_STATE_ERROR, dec.GetState());
}

TEST_F( VideoDecoderTest, SubmitInputFrame_InvalidBufferRejected )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );

    // pBuf=nullptr or size=0 should be rejected on input (requireNonZeroSize=true)
    auto bad = MakeFrame( 0x4001, nullptr, 0, 1280, 720, QC_IMAGE_FORMAT_COMPRESSED_H264 );
    NodeFrameDescriptor inFd( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID + 1 );
    inFd.Clear();
    ASSERT_EQ( QC_STATUS_OK, inFd.SetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID, bad ) );
    EXPECT_NE( QC_STATUS_OK, dec.ProcessFrameDescriptor( inFd ) );
}

TEST_F( VideoDecoderTest, SubmitOutputFrame_TooSmallAfterNegotiation_IsInvalid )
{
    // Force an output negotiation via reconfig; then submit a smaller-than-required output buffer.
    g_req_out.size = 12 * 1024;   // driver demands larger output buffers
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );

    // Trigger reconfig to negotiate output → m_bufSize[OUTPUT] = g_req_out.size
    g_emit_output_reconfig_once = true;
    auto out_ok = MakeFrame( 0x5001, (void *) 0xD000, 12 * 1024, 1280, 720, QC_IMAGE_FORMAT_NV12 );
    NodeFrameDescriptor tmp( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID + 1 );
    tmp.Clear();
    ASSERT_EQ( QC_STATUS_OK, tmp.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, out_ok ) );
    ASSERT_EQ( QC_STATUS_OK, dec.ProcessFrameDescriptor( tmp ) );   // completes reconfig path
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Now submit smaller output buffer → CheckBuffer() should fail
    auto out_small = MakeFrame( 0x5002, (void *) 0xD100, 1024, 1280, 720, QC_IMAGE_FORMAT_NV12 );
    NodeFrameDescriptor outFd( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID + 1 );
    outFd.Clear();
    ASSERT_EQ( QC_STATUS_OK, outFd.SetBuffer( QC_NODE_VIDEO_DECODER_OUTPUT_BUFF_ID, out_small ) );
    EXPECT_NE( QC_STATUS_OK, dec.ProcessFrameDescriptor( outFd ) );
}

TEST_F( VideoDecoderTest, FatalEvent_SetsErrorState )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );

    // Emit a hardware fatal event directly via the captured callback
    EmitVidcEvent( VIDC_EVT_ERR_HWFATAL );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, dec.GetState() );   // handled by base EventCallback
}

TEST( NodeVideoDecoder, SANITY_VideoDecoder_InvalidInputFormat )
{
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoDecoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoDecoder_InvalidInputFormat" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", 3 );
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

TEST( NodeVideoDecoder, SANITY_VideoDecoder_InvalidOutputFormat )
{
    QCStatus_e ret;
    std::string errors;

    QCNodeIfs *pNodeVide = new QC::Node::VideoDecoder();

    DataTree dt;
    dt.Set<std::string>( "name", "VideoDecoder_InvalidOutputFormat" );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "id", 3 );
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

TEST( Decoder_Config, VerifyAndSet_Fails_On_Invalid_ImageFormats )
{
    // Build an intentionally bad "static" config block:
    DataTree dt;
    dt.Set<std::string>( "name", "BadCfg" );
    dt.Set<uint32_t>( "id", 7 );
    dt.Set<uint32_t>( "width", 1920 );
    dt.Set<uint32_t>( "height", 1080 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );

    // Intentionally invalid format names so base VerifyAndSet fails.
    dt.Set<std::string>( "inputImageFormat", "bogus_compressed_fmt" );
    dt.Set<std::string>( "outputImageFormat", "bogus_linear_fmt" );

    DataTree root;
    root.Set( "static", dt );

    QC::Node::VideoDecoder dec;
    QCNodeInit_t init{};
    init.config = root.Dump();
    init.callback = OnDoneCb;   // won't be reached

    // Initialize should fail *in* m_configIfs.VerifyAndSet(...)
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // Expect QC_STATUS_BAD_ARGUMENTS
}

// --- Helpers for this pair of tests ---
static std::string BuildNonDynamicCfgJson( uint32_t w, uint32_t h, const char *inFmtStr,
                                           const char *outFmtStr, uint32_t numIn, uint32_t numOut )
{
    DataTree dt;
    dt.Set<std::string>( "name", "UNIT_VideoDecoder_NonDyn" );
    dt.Set<uint32_t>( "id", 101 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", w );
    dt.Set<uint32_t>( "height", h );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<bool>( "bInputDynamicMode",
                  false );   // <-- non-dynamic so CheckBuffer() loops actually run
    dt.Set<bool>( "bOutputDynamicMode", false );
    dt.Set<uint32_t>( "numInputBufferReq", numIn );
    dt.Set<uint32_t>( "numOutputBufferReq", numOut );
    dt.Set<std::string>( "inputImageFormat", inFmtStr );     // "h264" or "h265"
    dt.Set<std::string>( "outputImageFormat", outFmtStr );   // "nv12" / "p010"
    DataTree root;
    root.Set( "static", dt );
    return root.Dump();
}

static void
BuildBuffersVector( uint32_t w, uint32_t h, QCImageFormat_e inFmt, QCImageFormat_e outFmt,
                    uint32_t numIn, uint32_t numOut,
                    std::vector<VideoFrameDescriptor_t> &inObjs,    // owns storage
                    std::vector<VideoFrameDescriptor_t> &outObjs,   // owns storage
                    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &outRefs )
{
    inObjs.resize( numIn );
    outObjs.resize( numOut );
    outRefs.clear();
    outRefs.reserve( numIn + numOut );

    // INPUT descriptors: size must be >= negotiated input bufSize (set by
    // NegotiateBufferReq(INPUT)). Our unit stub exposes g_req_in.size (default 4096 in SetUp()) so
    // use at least that much.
    for ( uint32_t i = 0; i < numIn; ++i )
    {
        auto &f = inObjs[i];
        f.dmaHandle = 0x1000 + i;
        f.pBuf = reinterpret_cast<void *>( 0xA000 + 0x100 * i );
        f.size = 4096;   // >= required INPUT size
        f.width = w;
        f.height = h;
        f.format = inFmt;   // must match config.inFormat (compressed)
        f.pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( f ) );
    }

    // OUTPUT descriptors: AllocateBuffer() compares against m_bufSize[OUTPUT],
    // which is 0 during Initialize (no output negotiation yet), so any size >= 0 is fine.
    for ( uint32_t i = 0; i < numOut; ++i )
    {
        auto &f = outObjs[i];
        f.dmaHandle = 0x2000 + i;
        f.pBuf = reinterpret_cast<void *>( 0xB000 + 0x100 * i );
        f.size = 4096;   // ok (>= 0); actual output size checked only after reconfig
        f.width = w;
        f.height = h;
        f.format = outFmt;   // we can choose to match or mismatch to force CheckBuffer error
        f.pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( f ) );
    }
}

static std::string BuildNonDynamicOutputCfgJson( uint32_t w, uint32_t h, const char *inFmtStr,
                                                 const char *outFmtStr, uint32_t numIn,
                                                 uint32_t numOut )
{
    DataTree dt;
    dt.Set<std::string>( "name", "UNIT_VideoDecoder_NonDynOut" );
    dt.Set<uint32_t>( "id", 202 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", w );
    dt.Set<uint32_t>( "height", h );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<bool>( "bInputDynamicMode", true );     // input can remain dynamic
    dt.Set<bool>( "bOutputDynamicMode", false );   // <-- required to hit the branch
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );
    dt.Set<std::string>( "inputImageFormat", inFmtStr );     // "h264" or "h265"
    dt.Set<std::string>( "outputImageFormat", outFmtStr );   // "nv12" or "p010"
    DataTree root;
    root.Set( "static", dt );
    return root.Dump();
}

static void BuildBuffersVectorForNonDynamicOutput(
        uint32_t w, uint32_t h, QCImageFormat_e outFmt, uint32_t numOut,
        std::vector<VideoFrameDescriptor_t> &outObjs,
        std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> &refs_out )
{
    outObjs.resize( numOut );
    refs_out.clear();
    refs_out.reserve( numOut );

    // Make each output descriptor large enough for negotiated size.
    // Our default stub advertises g_req_out.size = 8192; use >=8192 to be safe.
    for ( uint32_t i = 0; i < numOut; ++i )
    {
        auto &f = outObjs[i];
        f.dmaHandle = 0x7000 + i;
        f.pBuf = reinterpret_cast<void *>( 0xBEE000 + 0x100 * i );
        f.size = 8192;   // >= g_req_out.size (stub default)
        f.width = w;
        f.height = h;
        f.format = outFmt;   // must match config.outFormat for ValidateBuffer to pass
        f.pid = 1234;
        refs_out.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( f ) );
    }
}

// 1) Input passes, Output fails (proves CheckBuffer() executed on OUTPUT path inside Initialize)
TEST_F( VideoDecoderTest, Initialize_NonDynamic_CallsCheckBuffer_OnOutput_FailsDueToFormat )
{
    // Make sure driver negotiation for INPUT can succeed.
    g_req_in.actual_count = 4;   // ask for 4
    g_req_in.size = 4096;

    const uint32_t W = 1280, H = 720;
    const uint32_t numIn = 4, numOut = 4;

    // Config: compressed input H264, EXPECT output NV12.
    // We'll deliberately provide OUTPUT descriptors with WRONG format (P010) so ValidateBuffer()
    // fails, which is called inside CheckBuffer(), proving the OUTPUT loop ran.
    auto cfgJson = BuildNonDynamicCfgJson( W, H, "h264", "nv12", numIn, numOut );

    // Build buffer arrays and the refs vector in the exact order Initialize expects:
    // first all INPUT descriptors, then OUTPUT descriptors.
    std::vector<VideoFrameDescriptor_t> inObjs, outObjs;
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> refs;
    BuildBuffersVector( W, H,
                        /*inFmt*/ QC_IMAGE_FORMAT_COMPRESSED_H264,
                        /*outFmt*/ QC_IMAGE_FORMAT_P010,   // WRONG vs configured "nv12"
                        numIn, numOut, inObjs, outObjs, refs );

    inObjs[0].size = 12288;
    inObjs[1].size = 12288;
    inObjs[2].size = 12288;
    inObjs[3].size = 12288;

    QCNodeInit_t init{};
    init.config = cfgJson;
    init.callback = TestOnDoneCb;
    init.buffers = refs;   // non-dynamic: Initialize() → AllocateBuffer() consumes these.

    // Initialize should proceed:
    //  - VerifyAndSet OK
    //  - OpenDriver + InitDrvProperty OK
    //  - NegotiateBufferReq(INPUT) OK
    //  - AllocateBuffer(INPUT) pushes m_inputBufferList; CheckBuffer(INPUT) loops and PASS
    //  - AllocateBuffer(OUTPUT) pushes m_outputBufferList; CheckBuffer(OUTPUT) loops and FAIL
    //  (format mismatch)
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // fails in OUTPUT CheckBuffer
    EXPECT_EQ( QC_OBJECT_STATE_ERROR,
               dec.GetState() );   // Initialize marks ERROR on any CheckBuffer failure.
}

// 2) Input fails immediately (proves CheckBuffer() executed on INPUT path inside Initialize)
TEST_F( VideoDecoderTest, Initialize_NonDynamic_CallsCheckBuffer_OnInput_FailsDueToFormat )
{
    g_req_in.actual_count = 4;
    g_req_in.size = 4096;

    const uint32_t W = 1280, H = 720;
    const uint32_t numIn = 4, numOut = 4;

    auto cfgJson = BuildNonDynamicCfgJson( W, H, "h264", "nv12", numIn, numOut );

    std::vector<VideoFrameDescriptor_t> inObjs, outObjs;
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> refs;
    BuildBuffersVector( W, H,
                        /*inFmt*/ QC_IMAGE_FORMAT_P010,
                        /*outFmt*/ QC_IMAGE_FORMAT_NV12,   // incorrect
                        numIn, numOut, inObjs, outObjs, refs );

    // Make the first INPUT descriptor invalid for CheckBuffer(INPUT):
    inObjs[0].size = 12288;
    inObjs[1].size = 12288;
    inObjs[2].size = 12288;
    inObjs[3].size = 12288;

    QCNodeInit_t init{};
    init.config = cfgJson;
    init.callback = TestOnDoneCb;
    init.buffers = refs;

    // Initialize fails during the INPUT CheckBuffer loop; OUTPUT loop is not reached
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, dec.GetState() );
}

TEST_F( VideoDecoderTest, Stop_Fails_WhenNotRunningState )
{
    auto init = MakeInit();
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    // State is READY — do not call Start()
    auto rc = dec.Stop();
    EXPECT_NE( QC_STATUS_OK, rc );   // BAD_STATE from VidcNodeBase::Stop
    // VideoDecoder::Stop() else-branch executed ("dec stop failed")
}

TEST_F( VideoDecoderTest, InitDrvProperty_Selects_HEVC_When_InputIsH265 )
{
    // Build an H.265 config (key change is inputImageFormat = "h265")
    DataTree dt;
    dt.Set<std::string>( "name", "UNIT_VideoDecoder_HEVC" );
    dt.Set<uint32_t>( "id", 2 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", 1280 );
    dt.Set<uint32_t>( "height", 720 );
    dt.Set<uint32_t>( "frameRate", 30 );
    dt.Set<bool>( "bInputDynamicMode", true );
    dt.Set<bool>( "bOutputDynamicMode", true );
    dt.Set<uint32_t>( "numInputBufferReq", 4 );
    dt.Set<uint32_t>( "numOutputBufferReq", 4 );

    // 🔴 This is what forces InitDrvProperty() into its first 'if' branch:
    dt.Set<std::string>( "inputImageFormat", "h265" );   // → QC_IMAGE_FORMAT_COMPRESSED_H265
    dt.Set<std::string>( "outputImageFormat", "nv12" );

    DataTree root;
    root.Set( "static", dt );
    QCNodeInit_t init{};
    init.config = root.Dump();
    init.callback = TestOnDoneCb;

    // Initialize() → m_configIfs.VerifyAndSet(...) parses "h265" to COMPRESSED_H265,
    // InitDrvProperty() sets meta.codecType = VIDEO_CODEC_H265 and configures decoder props.
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );    // becomes READY after RESP_LOAD_RESOURCES
    EXPECT_EQ( QC_OBJECT_STATE_READY, dec.GetState() );   // InitDrvProperty branch executed
}

// Build a minimal "static" config JSON with overridable fields.
static std::string BuildCfgJson( uint32_t w, uint32_t h, uint32_t fps, const char *inFmtStr,
                                 const char *outFmtStr, bool inDyn, bool outDyn, uint32_t numIn,
                                 uint32_t numOut, const char *name = "ValidateCfg" )
{
    DataTree dt;
    dt.Set<std::string>( "name", name );
    dt.Set<uint32_t>( "id", 123 );
    dt.Set<std::string>( "logLevel", "ERROR" );
    dt.Set<uint32_t>( "width", w );
    dt.Set<uint32_t>( "height", h );
    dt.Set<uint32_t>( "frameRate", fps );
    dt.Set<bool>( "bInputDynamicMode", inDyn );
    dt.Set<bool>( "bOutputDynamicMode", outDyn );
    dt.Set<uint32_t>( "numInputBufferReq", numIn );
    dt.Set<uint32_t>( "numOutputBufferReq", numOut );
    dt.Set<std::string>( "inputImageFormat", inFmtStr );
    dt.Set<std::string>( "outputImageFormat", outFmtStr );
    DataTree root;
    root.Set( "static", dt );
    return root.Dump();
}

TEST( Decoder_ValidateConfig, NullConfigPointer_FailsEarly )
{
    QC::Node::VideoDecoder dec;
    // Invalid formatss -> VidcNodeBaseConfigIfs::VerifyAndSet returns BAD_ARGUMENTS
    // and VideoDecoder skips assigning m_pConfig (remains nullptr).
    auto badCfg = BuildCfgJson( 1280, 720, 30, "bogus_in_fmt", "bogus_out_fmt", true, true, 4, 4,
                                "NullCfg" );
    QCNodeInit_t init{ .config = badCfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // fails before driver; m_pConfig stayed null
}

TEST( Decoder_ValidateConfig, ResolutionBelowMin_Fails )
{
    QC::Node::VideoDecoder dec;
    // width/height below 128 -> should fail
    auto cfg = BuildCfgJson( 127, 127, 30, "h264", "nv12", true, true, 4, 4, "ResLow" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // MIN=128 enforced
}

TEST( Decoder_ValidateConfig, ResolutionAboveMax_Fails )
{
    QC::Node::VideoDecoder dec;
    auto cfg = BuildCfgJson( 8193, 9000, 30, "h264", "nv12", true, true, 4, 4, "ResHigh" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // MAX=8192 enforced
}

TEST( Decoder_ValidateConfig, UnsupportedInputFormat_Fails )
{
    QC::Node::VideoDecoder dec;
    // "nv12" is not a compressed input format; ValidateConfig rejects
    auto cfg = BuildCfgJson( 1280, 720, 30, "nv12", "nv12", true, true, 4, 4, "BadInFmt" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST( Decoder_ValidateConfig, UnsupportedOutputFormat_Fails )
{
    QC::Node::VideoDecoder dec;
    // "bgr888" not allowed as decoder output per ValidateConfig
    auto cfg = BuildCfgJson( 1280, 720, 30, "h264", "bgr888", true, true, 4, 4, "BadOutFmt" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST( Decoder_ValidateConfig, InputBufferCountTooSmall_Fails )
{
    QC::Node::VideoDecoder dec;
    auto cfg = BuildCfgJson( 1280, 720, 30, "h264", "nv12", true, true, 3, 4, "InCntSmall" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST( Decoder_ValidateConfig, InputBufferCountTooLarge_Fails )
{
    QC::Node::VideoDecoder dec;
    auto cfg = BuildCfgJson( 1280, 720, 30, "h264", "nv12", true, true, 65, 4, "InCntLarge" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST( Decoder_ValidateConfig, OutputBufferCountTooSmall_Fails )
{
    QC::Node::VideoDecoder dec;
    auto cfg = BuildCfgJson( 1280, 720, 30, "h264", "nv12", true, true, 4, 3, "OutCntSmall" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST( Decoder_ValidateConfig, OutputBufferCountTooLarge_Fails )
{
    QC::Node::VideoDecoder dec;
    auto cfg = BuildCfgJson( 1280, 720, 30, "h264", "nv12", true, true, 4, 65, "OutCntLarge" );
    QCNodeInit_t init{ .config = cfg, .callback = TestOnDoneCb };
    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST_F( VideoDecoderTest, Initialize_NonDynamic_InputCheckBuffer_SizeZeroBranch )
{
    // Driver advertises sane input reqs so negotiation passes
    g_req_in.actual_count = 4;
    g_req_in.size = 4096;

    const uint32_t W = 1280, H = 720;
    const uint32_t numIn = 4, numOut = 4;

    // Non-dynamic so Initialize() will iterate input/output lists and call CheckBuffer()
    auto cfgJson = BuildNonDynamicCfgJson( W, H, "h264", "nv12", numIn, numOut );

    // Build buffer vectors in the exact order Initialize expects:
    //  [all input descs] + [all output descs]
    std::vector<VideoFrameDescriptor_t> inObjs, outObjs;
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> refs;
    BuildBuffersVector( W, H,
                        /*inFmt*/ QC_IMAGE_FORMAT_COMPRESSED_H264,
                        /*outFmt*/ QC_IMAGE_FORMAT_NV12, numIn, numOut, inObjs, outObjs, refs );

    // Force the input-branch in CheckBuffer(): set first INPUT descriptor size to 0
    inObjs[0].size = 0;   // <-- hits: if (0 == frameDesc.size) for VIDEO_CODEC_BUF_INPUT

    QCNodeInit_t init{};
    init.config = cfgJson;
    init.callback = TestOnDoneCb;
    init.buffers = refs;

    auto rc = dec.Initialize( init );
    EXPECT_NE( QC_STATUS_OK, rc );   // fails in CheckBuffer(INPUT)
    EXPECT_EQ( QC_OBJECT_STATE_ERROR,
               dec.GetState() );   // Initialize marks ERROR on CheckBuffer failure
}

TEST_F( VideoDecoderTest, ProcessFrameDescriptor_InputCheckBuffer_SizeZeroBranch )
{
    // Normal dynamic config (Initialize + Start)
    auto init = MakeInit();   // from your existing unit fixture (input=h264, output=nv12, dynamic)
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Build an INPUT frame with size=0 but otherwise valid metadata
    VideoFrameDescriptor_t in{};
    in.dmaHandle = 0xA55;
    in.pBuf = reinterpret_cast<void *>( 0xCAFEB000 );
    in.size = 0;   // <-- hits input zero-size branch
    in.width = 1280;
    in.height = 720;
    in.format = QC_IMAGE_FORMAT_COMPRESSED_H264;   // must match config so ValidateBuffer passes
    in.pid = 1234;

    NodeFrameDescriptor fd( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID + 1 );
    fd.Clear();
    ASSERT_EQ( QC_STATUS_OK, fd.SetBuffer( QC_NODE_VIDEO_DECODER_INPUT_BUFF_ID, in ) );

    // Process: SubmitInputFrame -> CheckBuffer(INPUT) -> size==0 branch triggers invalid buffer
    auto rc = dec.ProcessFrameDescriptor( fd );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST_F( VideoDecoderTest, ProcessFrameDescriptor_InvalidInputBuffer )
{
    // Normal dynamic config (Initialize + Start)
    auto init = MakeInit();   // from your existing unit fixture (input=h264, output=nv12, dynamic)
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Build an INPUT frame with size=0 but otherwise valid metadata
    VideoFrameDescriptor_t in{};
    in.dmaHandle = 0xA55;
    in.pBuf = reinterpret_cast<void *>( 0xCAFEB000 );
    in.size = 0;   // <-- hits input zero-size branch
    in.width = 1280;
    in.height = 720;
    in.format = QC_IMAGE_FORMAT_COMPRESSED_H264;   // must match config so ValidateBuffer passes
    in.pid = 1234;

    NodeFrameDescriptor fd( 0 );
    fd.Clear();

    // Process: SubmitInputFrame -> CheckBuffer(INPUT) -> size==0 branch triggers invalid buffer
    auto rc = dec.ProcessFrameDescriptor( fd );
    EXPECT_NE( QC_STATUS_OK, rc );
}

TEST_F( VideoDecoderTest, HandleOutputReconfig_SecondEventWhileInProgress_TakesInProgressBranch )
{
    g_use_mock = true;
    g_emit_start_output_done = false;

    // 1) Normal init → READY
    auto init = MakeInit();   // from the fixture (input="h264", output="nv12", dynamic)

    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );

    // 2) Start decoder → RUNNING (Start uses START_INPUT, event RESP_START_INPUT_DONE)
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // 3) Emit first OUTPUT_RECONFIG:
    //    - VideoDecoder::EventCallback receives VIDEO_CODEC_EVT_OUTPUT_RECONFIG
    //    - HandleOutputReconfig() sets m_OutputReconfigInprogress = true,
    //      negotiates output and calls StartDriver(OUTPUT) (which will later generate
    //      RESP_START_OUTPUT_DONE)
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );

    // 4) Immediately emit a second OUTPUT_RECONFIG before RESP_START_OUTPUT_DONE arrives.
    //    This forces HandleOutputReconfig() into: if (m_OutputReconfigInprogress) { ret =
    //    QC_STATUS_BAD_STATE; }
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );

    // 5) As coded, EventCallback marks ERROR when HandleOutputReconfig() fails.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, dec.GetState() );
}

TEST_F( VideoDecoderTest, HandleOutputReconfig_CallsSetBuffer_WhenOutputNonDynamic )
{
    // Arrange: output non-dynamic; provide output descriptors via init.buffers
    const uint32_t W = 1280, H = 720;
    const uint32_t numOut = 4;
    // Ensure the driver’s output requirement is reasonable
    g_req_out.actual_count = numOut;
    g_req_out.size = 8192;

    auto cfgJson = BuildNonDynamicOutputCfgJson( W, H, /*in*/ "h264", /*out*/ "nv12",
                                                 /*numIn*/ 4, /*numOut*/ numOut );

    // Build a refs vector that contains ONLY output descriptors (because input stays dynamic)
    std::vector<VideoFrameDescriptor_t> outObjs;
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> outRefs;
    BuildBuffersVectorForNonDynamicOutput( W, H, QC_IMAGE_FORMAT_NV12, numOut, outObjs, outRefs );

    QCNodeInit_t init{};
    init.config = cfgJson;
    init.callback = TestOnDoneCb;
    init.buffers = outRefs;   // VidcNodeBase::AllocateBuffer(OUTPUT) will collect these.

    // Initialize → READY (no SetBuffer(OUTPUT) yet, because that happens only on reconfig)
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_OBJECT_STATE_READY, dec.GetState() );

    // Start → RUNNING
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Reset counter, then trigger a single reconfig
    g_set_buffer_out_calls = 0;

    // Act: OUTPUT_RECONFIG → HandleOutputReconfig() will:
    //  - set in-progress flag
    //  - NegotiateBufferReq(OUTPUT)  (ret == OK by our stub)
    //  - because bOutputDynamicMode == false -> call VidcNodeBase::SetBuffer(OUTPUT)
    //  → real VidcDrvClient::SetBuffer(...) → VIDC_IOCTL_SET_BUFFER per descriptor
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );

    // Allow the default stub to emit RESP_START_OUTPUT_DONE
    // (which will call FinishOutputReconfig() and clear the flag).
    // No need to manipulate g_emit_start_output_done here.

    // Assert: we saw one SET_BUFFER per output descriptor during reconfig
    EXPECT_EQ( numOut, g_set_buffer_out_calls.load() );
}

TEST_F( VideoDecoderTest, HandleOutputReconfig_Fails_WhenStartOutputIoctlFails )
{
    // Normal init → READY
    auto init = MakeInit();   // input: "h264", output: "nv12", dynamic by default
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );

    // Start decoder → RUNNING
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // Force StartDriver(OUTPUT) to fail by failing the IOCTL in the stub.
    g_start_output_ioctl_should_fail = true;

    // Emit one OUTPUT_RECONFIG event:
    //   HandleOutputReconfig() sets in-progress, negotiates,
    //   calls StartDriver(OUTPUT) → IOCTL returns VIDC_ERR_FAIL → ret != OK
    //   => m_OutputReconfigInprogress = false; error logged; EventCallback sets ERROR.
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );

    // The node should be in ERROR as EventCallback reacts to the failure.
    EXPECT_EQ( QC_OBJECT_STATE_ERROR, dec.GetState() );

    g_start_output_ioctl_should_fail = false;
}

TEST_F( VideoDecoderTest, FinishOutputReconfig_BreaksOnFirstSubmitFailure )
{
    // --- Arrange non-dynamic output and provide two output descriptors ---
    const uint32_t W = 1280, H = 720;
    const uint32_t numOut = 4;

    // Build config: input dynamic; output non-dynamic so FinishOutputReconfig() will iterate
    // m_outputBufferList
    auto cfgJson = BuildNonDynamicOutputCfgJson( W, H, /*in*/ "h264", /*out*/ "nv12",
                                                 /*numIn*/ 4, /*numOut*/ numOut );

    // Two output buffers: first is "small" (8K), second is "large" (16K).
    // After reconfig we will require 12K so: first fails, second would pass if tried.
    std::vector<VideoFrameDescriptor_t> outObjs( numOut );
    std::vector<std::reference_wrapper<QCBufferDescriptorBase_t>> outRefs;
    {
        // small
        outObjs[0].dmaHandle = 0x7100;
        outObjs[0].pBuf = reinterpret_cast<void *>( 0xBEE000 );
        outObjs[0].size = 8 * 1024;   // < 12K -> will fail CheckBuffer after reconfig
        outObjs[0].width = W;
        outObjs[0].height = H;
        outObjs[0].format = QC_IMAGE_FORMAT_NV12;
        outObjs[0].pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( outObjs[0] ) );

        // medium
        outObjs[1].dmaHandle = 0x7101;
        outObjs[1].pBuf = reinterpret_cast<void *>( 0xBEE100 );
        outObjs[1].size = 12 * 1024;   // >= 12K -> would pass if loop didn't break
        outObjs[1].width = W;
        outObjs[1].height = H;
        outObjs[1].format = QC_IMAGE_FORMAT_NV12;
        outObjs[1].pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( outObjs[1] ) );

        // medium
        outObjs[2].dmaHandle = 0x7102;
        outObjs[2].pBuf = reinterpret_cast<void *>( 0xBEE100 );
        outObjs[2].size = 12 * 1024;   // >= 12K -> would pass if loop didn't break
        outObjs[2].width = W;
        outObjs[2].height = H;
        outObjs[2].format = QC_IMAGE_FORMAT_NV12;
        outObjs[2].pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( outObjs[2] ) );

        // large
        outObjs[3].dmaHandle = 0x7103;
        outObjs[3].pBuf = reinterpret_cast<void *>( 0xBEE100 );
        outObjs[3].size = 16 * 1024;   // >= 12K -> would pass if loop didn't break
        outObjs[3].width = W;
        outObjs[3].height = H;
        outObjs[3].format = QC_IMAGE_FORMAT_NV12;
        outObjs[3].pid = 1234;
        outRefs.push_back( reinterpret_cast<QCBufferDescriptorBase_t &>( outObjs[3] ) );
    }

    QCNodeInit_t init{};
    init.config = cfgJson;
    init.callback = TestOnDoneCb;
    init.buffers = outRefs;   // non-dynamic OUTPUT: Initialize collects into m_outputBufferList

    // Initialize → READY (OUTPUT CheckBuffer uses m_bufSize[OUT]=0 at this stage, so OK)
    ASSERT_EQ( QC_STATUS_OK, dec.Initialize( init ) );
    ASSERT_EQ( QC_OBJECT_STATE_READY, dec.GetState() );

    // Start → RUNNING so ValidateFrameSubmission will pass in SubmitOutputFrame
    // [3](https://qualcomm-my.sharepoint.com/personal/yabraham_qti_qualcomm_com/Documents/Microsoft%20Copilot%20Chat%20Files/VideoDecoder.cpp)
    ASSERT_EQ( QC_STATUS_OK, dec.Start() );
    ASSERT_EQ( QC_OBJECT_STATE_RUNNING, dec.GetState() );

    // --- Make re-negotiated output size bigger than the first buffer but smaller than the second
    // ---
    g_req_out.actual_count = numOut;
    g_req_out.size = 12 * 1024;   // HandleOutputReconfig updates m_bufSize[OUT] to this

    // Reset counter: we expect NO output Fill IOCTLs if break triggers at first failure
    g_fill_output_calls = 0;

    // Act: trigger OUTPUT_RECONFIG. HandleOutputReconfig does:
    //  - set m_OutputReconfigInprogress=true
    //  - NegotiateBufferReq(OUTPUT) -> m_bufSize[OUT]=12K
    //  - because output non-dynamic -> VidcNodeBase::SetBuffer(OUTPUT)
    //  - StartDriver(OUTPUT) -> stub emits RESP_START_OUTPUT_DONE
    //  - FinishOutputReconfig(): loop over m_outputBufferList, first SubmitOutputFrame fails at
    //  CheckBuffer,
    //    so if (ret != OK) { break; } executes.
    EmitVidcEvent( VIDC_EVT_OUTPUT_RECONFIG );

    // Assert: FillBuffer was never called -> loop broke on the first (small) descriptor
    EXPECT_EQ( 0, g_fill_output_calls.load() );
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
