// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_RADAR_EXT_IFACE_HPP
#define QC_RADAR_EXT_IFACE_HPP

#include <stdint.h>

// ---------------------------------------------------------------------------
// Platform-specific includes — must be outside any namespace
// ---------------------------------------------------------------------------
#ifndef __linux__
#include <devctl.h>
#include <fcntl.h>
#include <sys/iofunc.h>
#include <sys/neutrino.h>
#include <unistd.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Protocol wire types — always defined on all platforms
// ---------------------------------------------------------------------------

#pragma pack( push, 1 )
typedef struct RadarMsgHeader
{
    uint32_t magic;         // 0x52414452 = 'RADR'
    uint16_t version;       // 1
    uint16_t msg_type;      // 1=req_dma, 2=resp
    uint32_t payload_len;   // bytes after this header
    uint32_t request_id;    // client-generated correlation id
} RadarMsgHeader_t;

typedef struct RadarReqDmaBuf
{
    size_t input_size;
    size_t output_max;
    uint32_t flags;
    uint32_t timeout_ms;
} RadarReqDmaBuf_t;

typedef struct RadarResp
{
    int32_t status;
    uint32_t output_size;
    uint32_t flags;
} RadarResp_t;
#pragma pack( pop )

namespace q
{
namespace interface
{

static constexpr uint32_t RADAR_MAGIC = 0x52414452u;   // 'RADR'
static constexpr uint16_t RADAR_PROTO_VERSION = 1;
static constexpr uint16_t RADAR_MSG_REQ_DMA = 1;
static constexpr uint16_t RADAR_MSG_RESP = 2;
static constexpr uint32_t RADAR_FLAG_INPUT_FD = ( 1u << 0 );
static constexpr uint32_t RADAR_FLAG_OUTPUT_FD = ( 1u << 1 );
static constexpr uint32_t RADAR_FLAG_OUT_OWNABLE = ( 1u << 2 );

// Return sentinels used by both platforms (0 = success, negative = error)
static constexpr int RADAR_OK = 0;
static constexpr int RADAR_ETIMEOUT = -1;
static constexpr int RADAR_EINVAL = -2;
static constexpr int RADAR_EPROTO = -3;

// ---------------------------------------------------------------------------
// QNX transport (devctl)
// ---------------------------------------------------------------------------
#ifndef __linux__

typedef struct CommandExecute
{
    uint64_t input_handle;
    uint64_t output_handle;
    size_t input_size;
    size_t output_size;
} CommandExecute_t;

constexpr int DCMD_EXECUTE_CMD = __DIOT( _DCMD_MISC, 1, CommandExecute_t );

class Radar
{
public:
    Radar( const char *device, uint32_t /*timeoutMs*/ = 5000 ) { m_fd = open( device, O_RDWR ); }

    bool IsOpen() const { return m_fd != -1; }

    ~Radar() { close( m_fd ); }

    Radar( const Radar & ) = delete;
    Radar &operator=( const Radar & ) = delete;
    Radar( Radar && ) = delete;
    Radar &operator=( Radar && ) = delete;

    int Execute( uint64_t input_handle, size_t input_size, uint64_t output_handle,
                 size_t output_size )
    {
        CommandExecute_t cmd{};
        cmd.input_handle = input_handle;
        cmd.output_handle = output_handle;
        cmd.input_size = input_size;
        cmd.output_size = output_size;
        int ret = devctl( m_fd, DCMD_EXECUTE_CMD, &cmd, sizeof( cmd ), NULL );
        return ret;
    }

private:
    int m_fd;
};

// ---------------------------------------------------------------------------
// Linux transport (AF_UNIX SOCK_SEQPACKET + SCM_RIGHTS)
// ---------------------------------------------------------------------------
#else

class Radar
{
public:
    Radar( const char *device, uint32_t timeoutMs = 5000 ) : m_timeoutMs( timeoutMs )
    {
        m_sockfd = socket( AF_UNIX, SOCK_SEQPACKET, 0 );
        if ( m_sockfd != -1 )
        {
            struct sockaddr_un addr
            {
            };
            addr.sun_family = AF_UNIX;
            strncpy( addr.sun_path, device, sizeof( addr.sun_path ) - 1 );
            int ret = connect( m_sockfd, reinterpret_cast<struct sockaddr *>( &addr ),
                               sizeof( addr ) );
            if ( ret == -1 )
            {
                close( m_sockfd );
                m_sockfd = -1;
            }
            else
            {
                struct timeval tv
                {
                };
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = ( timeoutMs % 1000 ) * 1000;
                setsockopt( m_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
            }
        }
    }

    bool IsOpen() const { return m_sockfd != -1; }

    ~Radar()
    {
        if ( m_sockfd != -1 )
        {
            close( m_sockfd );
        }
    }

    Radar( const Radar & ) = delete;
    Radar &operator=( const Radar & ) = delete;
    Radar( Radar && ) = delete;
    Radar &operator=( Radar && ) = delete;

    int Execute( uint64_t input_handle, size_t input_size, uint64_t output_handle,
                 size_t output_size )
    {
        int returnCode = RADAR_OK;
        int input_fd = static_cast<int>( input_handle );
        int output_fd = static_cast<int>( output_handle );
        uint32_t req_id = ++m_requestId;

        RadarMsgHeader_t hdr{};
        hdr.magic = RADAR_MAGIC;
        hdr.version = RADAR_PROTO_VERSION;
        hdr.msg_type = RADAR_MSG_REQ_DMA;
        hdr.payload_len = sizeof( RadarReqDmaBuf_t );
        hdr.request_id = req_id;

        RadarReqDmaBuf_t req{};
        req.input_size = input_size;
        req.output_max = output_size;
        req.flags = RADAR_FLAG_INPUT_FD | RADAR_FLAG_OUTPUT_FD | RADAR_FLAG_OUT_OWNABLE;
        req.timeout_ms = m_timeoutMs;

        struct iovec iov[2];
        iov[0].iov_base = &hdr;
        iov[0].iov_len = sizeof( hdr );
        iov[1].iov_base = &req;
        iov[1].iov_len = sizeof( req );

        int fds[2] = { input_fd, output_fd };
        char cmsg_buf[CMSG_SPACE( sizeof( fds ) )];
        memset( cmsg_buf, 0, sizeof( cmsg_buf ) );

        struct msghdr msg
        {
        };
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof( cmsg_buf );

        struct cmsghdr *cmsg = CMSG_FIRSTHDR( &msg );
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN( sizeof( fds ) );
        memcpy( CMSG_DATA( cmsg ), fds, sizeof( fds ) );

        ssize_t ret = sendmsg( m_sockfd, &msg, 0 );
        if ( ret != -1 )
        {
            returnCode = RecvResponse( req_id );
        }
        else
        {
            returnCode = RADAR_EPROTO;
        }

        return returnCode;
    }

private:
    int RecvResponse( uint32_t expected_req_id )
    {
        RadarMsgHeader_t resp_hdr{};
        RadarResp_t resp{};

        struct iovec iov[2];
        iov[0].iov_base = &resp_hdr;
        iov[0].iov_len = sizeof( resp_hdr );
        iov[1].iov_base = &resp;
        iov[1].iov_len = sizeof( resp );

        struct msghdr msg
        {
        };
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        int returnCode;

        ssize_t n = recvmsg( m_sockfd, &msg, 0 );
        if ( n == -1 )
        {
            if ( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                returnCode = RADAR_ETIMEOUT;
            }
            else
            {
                returnCode = RADAR_EPROTO;
            }
        }
        else if ( n < static_cast<ssize_t>( sizeof( resp_hdr ) + sizeof( resp ) ) )
        {
            returnCode = RADAR_EPROTO;
        }
        else if ( resp_hdr.magic != RADAR_MAGIC || resp_hdr.version != RADAR_PROTO_VERSION )
        {
            returnCode = RADAR_EPROTO;
        }
        else if ( resp_hdr.request_id != expected_req_id )
        {
            returnCode = RADAR_EPROTO;
        }
        else if ( resp_hdr.msg_type != RADAR_MSG_RESP )
        {
            returnCode = RADAR_EPROTO;
        }
        else if ( resp.status == 0 )
        {
            returnCode = RADAR_OK;
        }
        else if ( resp.status == -ETIMEDOUT )
        {
            returnCode = RADAR_ETIMEOUT;
        }
        else if ( resp.status == -EINVAL )
        {
            returnCode = RADAR_EINVAL;
        }
        else
        {
            returnCode = RADAR_EPROTO;
        }

        return returnCode;
    }

    int m_sockfd = -1;
    uint32_t m_requestId = 0;
    uint32_t m_timeoutMs;
    std::string m_sockPath;
};

#endif   // __linux__

}   // namespace interface
}   // namespace q

#endif   // QC_RADAR_EXT_IFACE_HPP
