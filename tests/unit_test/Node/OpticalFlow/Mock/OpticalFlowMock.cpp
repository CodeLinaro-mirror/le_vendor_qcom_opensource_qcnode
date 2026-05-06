// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// LME shim/mock library for OpticalFlow unit test coverage.
// Pattern mirrors DepthFromStereoMock used for DepthFromStereo:
// - Export the same SV symbols OpticalFlow calls
// - dlopen() the real libsvcl.so and dlsym() real entry points
// - Allow tests to inject failures via OpticalFlowMock::MockApi_Control()

#include "OpticalFlowMock.hpp"

#include "svLme.h"
#include "svUtils.h"

#include <atomic>
#include <climits>
#include <dlfcn.h>
#include <mutex>
#include <string.h>

namespace
{
static void*           g_svclHandle = nullptr;
static std::once_flag  g_svclOnce;

static OpticalFlowMock::ControlParam g_params[OpticalFlowMock::API_MAX];
static std::mutex                    g_paramsMutex;

// ---- ConfigMap::Set call-index mock ----------------------------------------
// g_configMapSetFailOnCall: 0-based index of the Set() call that should return
// EINCORRECTTYPE (via CheckTypeValidity returning false).  UINT32_MAX = disabled.
static std::atomic<uint32_t> g_configMapSetFailOnCall{ UINT32_MAX };
static std::atomic<uint32_t> g_configMapSetCallCount{ 0 };

// Returns true if the current Set() call should be forced to fail.
// Increments the call counter and consumes the mock when the target is hit.
static bool CheckConfigMapSetMock()
{
    uint32_t failOn = g_configMapSetFailOnCall.load( std::memory_order_relaxed );
    if ( failOn == UINT32_MAX )
        return false;

    uint32_t count = g_configMapSetCallCount.fetch_add( 1u, std::memory_order_relaxed );
    if ( count == failOn )
    {
        // Consume: disable after one hit so subsequent calls succeed.
        g_configMapSetFailOnCall.store( UINT32_MAX, std::memory_order_relaxed );
        return true;
    }
    return false;
}

static void LoadRealSvcl()
{
    const char* libName = "libsvcl.so";
    g_svclHandle        = dlopen( libName, RTLD_NOW | RTLD_GLOBAL );
}

template <typename Fn>
Fn LoadSym( const char* sym )
{
    std::call_once( g_svclOnce, LoadRealSvcl );
    if ( !g_svclHandle )
        return nullptr;
    dlerror();
    void* p = dlsym( g_svclHandle, sym );
    (void)dlerror();
    return reinterpret_cast<Fn>( p );
}

static OpticalFlowMock::ControlParam ConsumeParam( OpticalFlowMock::ApiId apiId )
{
    std::lock_guard<std::mutex> lock( g_paramsMutex );
    OpticalFlowMock::ControlParam p = g_params[apiId];
    // consume
    g_params[apiId].action = OpticalFlowMock::ACTION_NONE;
    g_params[apiId].param  = nullptr;
    return p;
}

static bool ShouldReturnNull( OpticalFlowMock::ApiId apiId )
{
    auto p = ConsumeParam( apiId );
    return p.action == OpticalFlowMock::ACTION_RETURN_NULLPTR;
}

static bool ShouldReturnStatus( OpticalFlowMock::ApiId apiId, SV::Status& out )
{
    auto p = ConsumeParam( apiId );
    if ( p.action == OpticalFlowMock::ACTION_RETURN_STATUS && p.param != nullptr )
    {
        out = *reinterpret_cast<SV::Status*>( p.param );
        return true;
    }
    return false;
}

// FakeLME wraps the real LME instance and allows SubmitSync to be intercepted.
class FakeLME final : public SV::LME
{
public:
    explicit FakeLME( SV::LME* real ) : SV::LME( "OpticalFlowMockLME" ), m_real( real ) {}
    ~FakeLME() override = default;

    SV::Status SubmitSync( const SV::LME::Input&     sInput,
                           SV::LME::Output&           sOutputFrwd,
                           SV::LME::Output&           sOutputBkwd,
                           const SV::LME::ConfigMap&  sConfigMap ) override
    {
        SV::Status forced{};
        if ( ShouldReturnStatus( OpticalFlowMock::API_LME_SUBMIT_SYNC, forced ) )
            return forced;
        if ( m_real )
            return m_real->SubmitSync( sInput, sOutputFrwd, sOutputBkwd, sConfigMap );
        return SV::Status::EFAIL;
    }

    SV::Status Destroy() override
    {
        if ( m_real )
        {
            SV::Status s = m_real->Destroy();
            m_real       = nullptr;
            return s;
        }
        return SV::Status::EFAIL;
    }

private:
    SV::LME* m_real{ nullptr };
};

}   // namespace

// ==============================================================================================
// OpticalFlowMock public API
// ==============================================================================================

void OpticalFlowMock::MockApi_Control( OpticalFlowMock::ApiId apiId, OpticalFlowMock::Action action, void* param )
{
    if ( apiId >= OpticalFlowMock::API_MAX )
        return;
    std::lock_guard<std::mutex> lock( g_paramsMutex );
    g_params[apiId].action = action;
    g_params[apiId].param  = param;
}

void OpticalFlowMock::MockApi_ResetAll()
{
    std::lock_guard<std::mutex> lock( g_paramsMutex );
    for ( uint32_t i = 0; i < OpticalFlowMock::API_MAX; ++i )
    {
        g_params[i].action = OpticalFlowMock::ACTION_NONE;
        g_params[i].param  = nullptr;
    }
    g_configMapSetCallCount.store( 0u, std::memory_order_relaxed );
    g_configMapSetFailOnCall.store( UINT32_MAX, std::memory_order_relaxed );
}

void OpticalFlowMock::MockApi_ConfigMapSet_FailOnCall( uint32_t callIndex )
{
    g_configMapSetCallCount.store( 0u, std::memory_order_relaxed );
    g_configMapSetFailOnCall.store( callIndex, std::memory_order_relaxed );
}

void OpticalFlowMock::MockApi_ConfigMapSet_Reset()
{
    g_configMapSetCallCount.store( 0u, std::memory_order_relaxed );
    g_configMapSetFailOnCall.store( UINT32_MAX, std::memory_order_relaxed );
}

// ==============================================================================================
// Interposed SV Session entry points
// ==============================================================================================

SV::Status SV::Session::Start()
{
    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_SESSION_START, forced ) )
        return forced;
    using Fn      = SV::Status ( * )( SV::Session* );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV7Session5StartEv" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( this );
}

SV::Status SV::Session::Stop()
{
    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_SESSION_STOP, forced ) )
        return forced;
    using Fn      = SV::Status ( * )( SV::Session* );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV7Session4StopEv" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( this );
}

SV::Status SV::Session::Destroy()
{
    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_SESSION_DESTROY, forced ) )
        return forced;
    using Fn      = SV::Status ( * )( SV::Session* );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV7Session7DestroyEv" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( this );
}

// ==============================================================================================
// FeatureConfigMap<LME::ConfigId>::CheckTypeValidity<T> specializations
//
// FeatureConfigMap::Set() calls CheckTypeValidity<T>(configId) before storing the value.
// If CheckTypeValidity returns false, Set() returns ConfigMapStatus::EINCORRECTTYPE.
//
// CheckTypeValidity is only *declared* in svConfigMap.h (not defined inline), so we can
// provide explicit specializations here that are linked in place of the SVCL library's
// definitions.  Each specialization delegates to CheckConfigMapSetMock(): when the mock
// is armed (via MockApi_ConfigMapSet_FailOnCall), the N-th Set() call returns false,
// causing Set() to return EINCORRECTTYPE and OpticalFlow to propagate QC_STATUS_FAIL.
// ==============================================================================================

// Specializing a member function template of a class template requires two template<> prefixes:
//   template<>  -- for the class template argument (LME::ConfigId)
//   template<>  -- for the member function template argument (T)
// float32_t is typedef'd inside namespace SV on QNX, so use SV::float32_t.
#define DEFINE_CHECK_TYPE_VALIDITY( T )                                                            \
    template <>                                                                                    \
    template <>                                                                                    \
    bool SV::FeatureConfigMap<SV::LME::ConfigId>::CheckTypeValidity<T>(                           \
        SV::LME::ConfigId ) const                                                                  \
    {                                                                                              \
        return !CheckConfigMapSetMock();                                                           \
    }

DEFINE_CHECK_TYPE_VALIDITY( uint32_t )
DEFINE_CHECK_TYPE_VALIDITY( uint64_t )
DEFINE_CHECK_TYPE_VALIDITY( bool )
DEFINE_CHECK_TYPE_VALIDITY( SV::float32_t )
DEFINE_CHECK_TYPE_VALIDITY( SV::PixelFormat )
DEFINE_CHECK_TYPE_VALIDITY( SV::ImageInfo* )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::MotionMapUpscale )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::MotionMapStepSize )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::MotionDirection )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::RefinementLevel )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::ComputationAccuracy )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::FeatureNoiseTolerances* )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::Penalties* )
DEFINE_CHECK_TYPE_VALIDITY( SV::LME::LightingCondition )

#undef DEFINE_CHECK_TYPE_VALIDITY

// ==============================================================================================
// Session::Create
// ==============================================================================================

SV::Session* SV::Session::Create( const SV::Session::ConfigMap&                    configMap,
                                   void ( *eventCallback )( SV::Session*, SV::Event, void* ),
                                   const void*                                       pUserData )
{
    if ( ShouldReturnNull( OpticalFlowMock::API_SESSION_CREATE ) )
        return nullptr;

    using Fn = SV::Session* ( * )( const SV::Session::ConfigMap&,
                                   void ( * )( SV::Session*, SV::Event, void* ),
                                   const void* );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>(
            "_ZN2SV7Session6CreateERKNS_16FeatureConfigMapINS0_8ConfigIdEEEPFvPS0_NS_5EventEPvEPKv" );
    if ( !realFn )
        return nullptr;
    return realFn( configMap, eventCallback, pUserData );
}

// ==============================================================================================
// LME::Create
// ==============================================================================================

SV::LME* SV::LME::Create( SV::Session*              pSession,
                            const SV::LME::ConfigMap& sConfigMap,
                            const SV::LME::CallbackFn pCallbackFn,
                            const void*               pInstanceCbData,
                            const std::string         sName )
{
    if ( ShouldReturnNull( OpticalFlowMock::API_LME_CREATE ) )
        return nullptr;

    using Fn = SV::LME* ( * )( SV::Session*,
                                const SV::LME::ConfigMap&,
                                const SV::LME::CallbackFn,
                                const void*,
                                const std::string );
    static Fn realFn = nullptr;
    if ( !realFn )
    {
        // Mangled name: nm -D coverage/libsvcl.so | grep 'LME.*Create'
        realFn = LoadSym<Fn>(
            "_ZN2SV3LME6CreateEPNS_7SessionERKNS_16FeatureConfigMapINS0_8ConfigIdEEEPFvNS_6StatusERNS0_6OutputERSA_PS0_PvSF_EPKvNSt3__212basic_stringIcNSK_11char_traitsIcEENSK_9allocatorIcEEEE" );
    }
    if ( !realFn )
        return nullptr;

    SV::LME* real = realFn( pSession, sConfigMap, pCallbackFn, pInstanceCbData, sName );
    return new FakeLME( real );
}

// ==============================================================================================
// Free functions: BufferRegister, BufferDeregister, ImageInfoQuery
// ==============================================================================================

SV::Status BufferRegister( SV::Session* session, const SV::Buffer& buffer )
{
    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_BUFFER_REGISTER, forced ) )
        return forced;

    using Fn      = SV::Status ( * )( SV::Session*, const SV::Buffer& );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV13BufferRegisterEPNS_7SessionERKNS_6BufferE" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( session, buffer );
}

SV::Status BufferDeregister( SV::Session* session, const SV::Buffer& buffer )
{
    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_BUFFER_DEREGISTER, forced ) )
        return forced;

    using Fn      = SV::Status ( * )( SV::Session*, const SV::Buffer& );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV15BufferDeregisterEPNS_7SessionERKNS_6BufferE" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( session, buffer );
}

SV::Status ImageInfoQuery( SV::PixelFormat fmt,
                            unsigned int    width,
                            unsigned int    height,
                            SV::ImageInfo&  info )
{
    // Allow environment-variable override for deterministic failure injection.
    const char* env = getenv( "SVCLMOCK_FORCE_IMAGE_INFO_QUERY_FAIL" );
    if ( env != nullptr && strcmp( env, "1" ) == 0 )
        return SV::Status::EFAIL;

    SV::Status forced{};
    if ( ShouldReturnStatus( OpticalFlowMock::API_IMAGE_INFO_QUERY, forced ) )
        return forced;

    using Fn      = SV::Status ( * )( SV::PixelFormat, unsigned int, unsigned int, SV::ImageInfo& );
    static Fn realFn = nullptr;
    if ( !realFn )
        realFn = LoadSym<Fn>( "_ZN2SV13ImageInfoQueryENS_10PixelFormatEjjRNS_9ImageInfoE" );
    if ( !realFn )
        return SV::Status::EFAIL;
    return realFn( fmt, width, height, info );
}
