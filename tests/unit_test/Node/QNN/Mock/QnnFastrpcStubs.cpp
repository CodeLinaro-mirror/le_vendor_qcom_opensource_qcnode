// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

// Mock override of remote_system_request for QNN unit tests.
// Hybrid: the real platform API is called first so that domain ids used by
// actual QNN model execution stay correct; the reported topology is then padded
// up to a minimum of 4 NSP / 3 HPASS so that tests asserting on those counts
// pass regardless of how many accelerators the host exposes.

#include "remote.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Minimum domain topology the unit tests are written against.
#define MOCK_MIN_NSP_DOMAINS 4
#define MOCK_MIN_HPASS_DOMAINS 3

// Base ids used only for padded (non-existent) domains, matching the qmock
// reference implementation in qmock/source/cdsprpc.cpp.
#define MOCK_PAD_NSP_BASE_ID 10
#define MOCK_PAD_HPASS_BASE_ID 20

typedef int ( *RemoteSystemRequestFn_t )( system_req_payload *req );

static void *s_hDll = nullptr;
static RemoteSystemRequestFn_t s_remoteSystemRequestFn = nullptr;

// Number of domains the real platform reported for the pending request; cached
// on the count query so the populate call knows where padding must start.
static int s_realNumDomains = 0;

// Mock control state — one action armed at a time
typedef enum
{
    RSR_ACTION_NONE = 0,
    RSR_ACTION_RETURN,                 // inject return value on the NEXT call
    RSR_ACTION_OUT_NUM_DOMAINS,        // override req->sys.num_domains on second call
    RSR_ACTION_FAIL_SECOND,            // succeed first call (count), fail second call
    RSR_ACTION_OVERRIDE_DOMAIN_TYPE,   // override domain[].type on second call (populate)
    RSR_ACTION_OVERRIDE_DOMAIN_ID      // override domain[].id = baseId + i on second call
} RsrAction_e;

static RsrAction_e s_action = RSR_ACTION_NONE;
static int s_intParam = 0;
static bool s_firstDone = false;
static fastrpc_domain_type s_domainTypeOverride = FASTRPC_NSP;
static int s_domainIdBase = 0;

// ================================================================
// Hybrid FastRPC platform simulation.
//
// A pure simulation would hand fabricated domain ids to tests that really
// execute a QNN model on HTP, so the real platform API is always called first
// and its domain ids are kept as-is. Only the *shortfall* is synthesised: a
// host exposing fewer than 4 NSP (or 3 HPASS) domains gets the remaining slots
// filled with reasonable values so count-based assertions still hold.
//
//   NSP   → at least 4 domains; padded ids continue from 10 + index
//   HPASS → at least 3 domains; padded ids continue from 20 + index
//   other → left exactly as the platform reported it
// ================================================================
static void MockGetMinTopology( const system_req_payload *req, int &minDomains, int &padBaseId,
                                fastrpc_domain_type &domainType )
{
    // Extract requested domain type from flags
    int type = (int) ( req->sys.flags & ( ( 1 << DOMAINS_LIST_FLAGS_TYPE_POS ) - 1 ) );

    if ( type == (int) NSP )
    {
        minDomains = MOCK_MIN_NSP_DOMAINS;
        padBaseId = MOCK_PAD_NSP_BASE_ID;
        domainType = NSP;
    }
    else if ( type == (int) HPASS )
    {
        minDomains = MOCK_MIN_HPASS_DOMAINS;
        padBaseId = MOCK_PAD_HPASS_BASE_ID;
        domainType = HPASS;
    }
    else
    {
        // Unknown type: no padding, report whatever the platform said.
        minDomains = 0;
        padBaseId = 0;
        domainType = FASTRPC_ALL_DOMAINS;
    }
}

static int remote_system_request_mock( system_req_payload *req )
{
    if ( nullptr == req )
    {
        return -1;
    }

    int minDomains = 0;
    int padBaseId = 0;
    fastrpc_domain_type domainType = FASTRPC_ALL_DOMAINS;
    MockGetMinTopology( req, minDomains, padBaseId, domainType );

    // The caller may ask for the padded count, which the real platform would
    // reject as out of range. Ask it only for the domains it actually has and
    // restore the caller's values afterwards.
    const int reqNumDomains = req->sys.num_domains;
    const int reqMaxDomains = req->sys.max_domains;
    const bool isPopulate = ( nullptr != req->sys.domains );

    if ( isPopulate && ( reqNumDomains > s_realNumDomains ) )
    {
        req->sys.num_domains = s_realNumDomains;
        req->sys.max_domains = s_realNumDomains;
    }

    // Always call the real platform first so genuine domain ids are preserved.
    int ret = 0;
    if ( nullptr != s_remoteSystemRequestFn )
    {
        ret = s_remoteSystemRequestFn( req );
    }
    else
    {
        // No platform library available (pure x86 host): nothing real to report.
        req->sys.num_domains = 0;
    }

    if ( isPopulate )
    {
        // Restore what the caller allocated for, so padding can fill the rest.
        req->sys.num_domains = reqNumDomains;
        req->sys.max_domains = reqMaxDomains;
    }

    if ( 0 != ret )
    {
        return ret;
    }

    if ( !isPopulate )
    {
        // Count query: remember what the platform reported, then raise the
        // count to the minimum the tests expect.
        s_realNumDomains = req->sys.num_domains;
        if ( req->sys.num_domains < minDomains )
        {
            req->sys.num_domains = minDomains;
        }
        return 0;
    }

    // Populate call: the real platform filled in s_realNumDomains entries;
    // synthesise the remaining slots up to the (padded) count.
    int count = ( req->sys.max_domains < req->sys.num_domains ) ? req->sys.max_domains
                                                                : req->sys.num_domains;
    for ( int i = s_realNumDomains; i < count; i++ )
    {
        req->sys.domains[i].id = padBaseId + i;
        req->sys.domains[i].instance_id = i;
        req->sys.domains[i].type = domainType;
        req->sys.domains[i].status = 1;
        (void) snprintf( req->sys.domains[i].name, MAX_DOMAIN_NAMELEN, "%s", "qmock_domain" );
    }
    return 0;
}

class CdsprpcLoader
{
public:
    CdsprpcLoader()
    {
        s_hDll = dlopen( "libcdsprpc.so", RTLD_LAZY );
        if ( nullptr == s_hDll )
        {
            printf( "Failed to load libcdsprpc.so: %s\n", dlerror() );
        }
        else
        {
            printf( "Successfully loaded libcdsprpc.so\n" );
        }

        s_remoteSystemRequestFn =
                (RemoteSystemRequestFn_t) dlsym( s_hDll, "remote_system_request" );
        if ( nullptr == s_remoteSystemRequestFn )
        {
            printf( "Failed to load symbol remote_system_request: %s\n", dlerror() );
        }
        else
        {
            printf( "Successfully loaded symbol remote_system_request\n" );
        }
    }

    ~CdsprpcLoader()
    {
        if ( nullptr != s_hDll )
        {
            dlclose( s_hDll );
        }
    }
};

static CdsprpcLoader s_cdsprpcLoader;

extern "C"
{

    void MockRemoteSystemRequest_SetReturnCode( int errCode )
    {
        s_action = RSR_ACTION_RETURN;
        s_intParam = errCode;
    }

    void MockRemoteSystemRequest_SetNumDomains( int n )
    {
        s_action = RSR_ACTION_OUT_NUM_DOMAINS;
        s_intParam = n;
        s_firstDone = false;
    }

    void MockRemoteSystemRequest_SetFailSecondCall()
    {
        s_action = RSR_ACTION_FAIL_SECOND;
        s_firstDone = false;
    }

    // Override domain[].type on the second call (populate).
    // Use e.g. MockRemoteSystemRequest_SetOverrideDomainType(NSP) when testing HTP1/2/3
    // to make HPASS == type False, exercising the type-mismatch → else branch.
    void MockRemoteSystemRequest_SetOverrideDomainType( int domainType )
    {
        s_action = RSR_ACTION_OVERRIDE_DOMAIN_TYPE;
        s_domainTypeOverride = (fastrpc_domain_type) domainType;
        s_firstDone = false;
    }

    // Override domain[].id = baseId + i on the second call (populate).
    // This makes id assertions platform-independent: the test asserts
    // against the known baseId rather than real hardware domain ids.
    void MockRemoteSystemRequest_SetOverrideDomainID( int baseId )
    {
        s_action = RSR_ACTION_OVERRIDE_DOMAIN_ID;
        s_domainIdBase = baseId;
        s_firstDone = false;
    }

    void MockRemoteSystemRequest_Reset()
    {
        s_action = RSR_ACTION_NONE;
        s_intParam = 0;
        s_firstDone = false;
        s_domainTypeOverride = FASTRPC_NSP;
        s_domainIdBase = 0;
        s_realNumDomains = 0;
    }

    int remote_system_request( system_req_payload *req )
    {
        (void) s_cdsprpcLoader;

        if ( RSR_ACTION_RETURN == s_action )
        {
            int ret = s_intParam;
            s_action = RSR_ACTION_NONE;
            return ret;
        }

        if ( RSR_ACTION_FAIL_SECOND == s_action )
        {
            if ( !s_firstDone )
            {
                s_firstDone = true;
                // fall through to real implementation for count query
            }
            else
            {
                s_action = RSR_ACTION_NONE;
                s_firstDone = false;
                return -1;
            }
        }

        // Call the real platform, padded up to the minimum test topology
        int ret = remote_system_request_mock( req );

        if ( RSR_ACTION_OUT_NUM_DOMAINS == s_action )
        {
            if ( !s_firstDone )
            {
                // First call (count query): override num_domains
                if ( nullptr != req )
                {
                    req->sys.num_domains = s_intParam;
                }
                s_firstDone = true;
            }
            else
            {
                s_action = RSR_ACTION_NONE;
                s_firstDone = false;
            }
        }

        if ( RSR_ACTION_OVERRIDE_DOMAIN_TYPE == s_action )
        {
            if ( !s_firstDone )
            {
                // First call (count query): let through unchanged
                s_firstDone = true;
            }
            else
            {
                // Second call (populate): override all domain types
                if ( ( nullptr != req ) && ( nullptr != req->sys.domains ) )
                {
                    for ( int i = 0; i < req->sys.num_domains; i++ )
                    {
                        req->sys.domains[i].type = s_domainTypeOverride;
                    }
                }
                s_action = RSR_ACTION_NONE;
                s_firstDone = false;
            }
        }

        if ( RSR_ACTION_OVERRIDE_DOMAIN_ID == s_action )
        {
            if ( !s_firstDone )
            {
                s_firstDone = true;
            }
            else
            {
                // Second call (populate): override all domain ids = baseId + i
                if ( ( nullptr != req ) && ( nullptr != req->sys.domains ) )
                {
                    for ( int i = 0; i < req->sys.num_domains; i++ )
                    {
                        req->sys.domains[i].id = s_domainIdBase + i;
                    }
                }
                s_action = RSR_ACTION_NONE;
                s_firstDone = false;
            }
        }

        return ret;
    }

}   // extern "C"
