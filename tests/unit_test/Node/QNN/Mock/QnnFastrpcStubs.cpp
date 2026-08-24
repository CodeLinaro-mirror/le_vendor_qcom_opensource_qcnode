// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

// Mock override of remote_system_request for QNN unit tests.

#include "remote.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>

typedef int ( *RemoteSystemRequestFn_t )( system_req_payload *req );

static void *s_hDll = nullptr;
static RemoteSystemRequestFn_t s_remoteSystemRequestFn = nullptr;

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

        // Call real qmock platform simulation
        int ret = 0;
        if ( nullptr != s_remoteSystemRequestFn )
        {
            ret = s_remoteSystemRequestFn( req );
        }

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
