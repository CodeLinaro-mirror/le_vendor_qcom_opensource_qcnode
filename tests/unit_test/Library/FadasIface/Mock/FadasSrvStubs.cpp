// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

// Stubs for FastRPC and rpcmem symbols needed by FadasSrv.cpp
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Replicate the exact layout of remote.h structures so that
 * remote_system_request can properly populate the payload.
 * MAX_DOMAIN_NAMELEN = 30 (from remote.h)
 * ================================================================ */
#define STUB_MAX_DOMAIN_NAMELEN 30

typedef enum {
    STUB_FASTRPC_ALL_DOMAINS = 0,
    STUB_FASTRPC_NSP         = 1,
    STUB_FASTRPC_LPASS       = 2,
    STUB_FASTRPC_SDSP        = 3,
    STUB_FASTRPC_MDSP        = 4,
    STUB_FASTRPC_HPASS       = 5,
} stub_fastrpc_domain_type;

typedef struct {
    int id;
    int instance_id;
    char name[STUB_MAX_DOMAIN_NAMELEN];
    stub_fastrpc_domain_type type;   /* at same offset as real fastrpc_domain_type */
    int status;
    uint32_t card;
    uint32_t soc_id;
    uint32_t reserved[10];
} stub_fastrpc_domain;

typedef struct {
    stub_fastrpc_domain *domains;   /* pointer first — matches fastrpc_domains_info */
    int max_domains;
    int num_domains;
    uint64_t flags;
} stub_fastrpc_domains_info;

typedef struct {
    int id;   /* system_req_id enum, 4 bytes */
    union {
        stub_fastrpc_domains_info sys;
    };
} stub_system_req_payload;

/* ================================================================
 * Mock control for remote_system_request
 * ================================================================ */
static int g_rsr_fail_first  = 0;   /* non-zero → fail the 1st call  */
static int g_rsr_fail_second = 0;   /* non-zero → fail the 2nd call  */
static int g_rsr_domain_type = STUB_FASTRPC_ALL_DOMAINS;
/* Default 3: HTP0 uses domains[0], HTP1 uses domains[0],
   HTP2 uses domains[1], HTP3 uses domains[2] — need at least 3 */
static int g_rsr_num_domains = 3;

void MockRemoteSystemRequest_SetFailFirst( int fail )  { g_rsr_fail_first  = fail; }
void MockRemoteSystemRequest_SetFailSecond( int fail ) { g_rsr_fail_second = fail; }
void MockRemoteSystemRequest_SetDomainType( int type ) { g_rsr_domain_type = type; }
void MockRemoteSystemRequest_SetNumDomains( int n )    { g_rsr_num_domains = n; }
void MockRemoteSystemRequest_Reset( void )
{
    g_rsr_fail_first  = 0;
    g_rsr_fail_second = 0;
    g_rsr_domain_type = STUB_FASTRPC_ALL_DOMAINS;
    g_rsr_num_domains = 3;
}

int remote_system_request( void *req_void )
{
    stub_system_req_payload *req = (stub_system_req_payload *)req_void;

    if ( req->sys.domains == NULL )
    {
        /* First call: client queries number of available domains */
        if ( g_rsr_fail_first )
        {
            return -1;
        }
        req->sys.num_domains = g_rsr_num_domains;
        return 0;
    }
    else
    {
        /* Second call: client has allocated domains array, populate it */
        if ( g_rsr_fail_second )
        {
            return -1;
        }
        int count = req->sys.max_domains < g_rsr_num_domains
                        ? req->sys.max_domains
                        : g_rsr_num_domains;
        for ( int i = 0; i < count; i++ )
        {
            req->sys.domains[i].id           = i;
            req->sys.domains[i].instance_id  = i;
            req->sys.domains[i].type         = (stub_fastrpc_domain_type)g_rsr_domain_type;
            req->sys.domains[i].status       = 1;
            strlcpy( req->sys.domains[i].name, "test_domain", STUB_MAX_DOMAIN_NAMELEN );
            req->sys.domains[i].name[STUB_MAX_DOMAIN_NAMELEN - 1] = '\0';
        }
        return 0;
    }
}

int fastrpc_munmap( int domain, int fd, void *addr, size_t length ) { return 0; }
int  get_extended_domains_id( int domain, int session ) { return 0; }
void remote_register_buf_v2( int ext_domain_id, void *buf, int size, int fd ) {}
void remote_register_buf_attr_v2( int ext_domain_id, void *buf, int size, int fd, int attr ) {}
int  remote_session_control( uint32_t req, void *data, uint32_t datalen ) { return 0; }

#ifdef __cplusplus
}
#endif
