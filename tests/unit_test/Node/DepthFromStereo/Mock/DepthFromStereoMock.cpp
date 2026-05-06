// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// SVCL shim/mock library for unit test coverage.
// Pattern mirrors the Camera/QNN mocks:
// - Export the same SV symbols DFS calls
// - dlopen() the real libsvcl.so and dlsym() real entry points
// - Allow tests to inject failures via DepthFromStereoMock::MockApi_Control()

#include "DepthFromStereoMock.hpp"

#include "svStereoDisparity.h"
#include "svUtils.h"

#include <atomic>
#include <climits>
#include <dlfcn.h>
#include <mutex>
#include <string.h>

namespace
{
static void* g_svclHandle = nullptr;
static std::once_flag g_svclOnce;

static DepthFromStereoMock::ControlParam g_params[DepthFromStereoMock::API_MAX];
static std::mutex g_paramsMutex;

// ---- ConfigMap::Set call-index mock ----------------------------------------
// g_configMapSetFailOnCall: 0-based index of the Set() call that should return
// EINCORRECTTYPE (via CheckTypeValidity returning false).  UINT32_MAX = disabled.
static std::atomic<uint32_t> g_configMapSetFailOnCall{UINT32_MAX};
static std::atomic<uint32_t> g_configMapSetCallCount{0};

// Returns true if the current Set() call should be forced to fail.
// Increments the call counter and consumes the mock when the target is hit.
static bool CheckConfigMapSetMock()
{
    uint32_t failOn = g_configMapSetFailOnCall.load(std::memory_order_relaxed);
    if (failOn == UINT32_MAX)
        return false;

    uint32_t count = g_configMapSetCallCount.fetch_add(1u, std::memory_order_relaxed);
    if (count == failOn)
    {
        // Consume: disable after one hit so subsequent calls succeed.
        g_configMapSetFailOnCall.store(UINT32_MAX, std::memory_order_relaxed);
        return true;
    }
    return false;
}

static void LoadRealSvcl()
{
    // We expect the real library to be discoverable via rpath/LD_LIBRARY_PATH.
    // In your setup it is copied to coverage/libsvcl.so, so tests can set LD_LIBRARY_PATH=coverage
    // or place it next to the test binary.
    const char* libName = "libsvcl.so";
    g_svclHandle = dlopen(libName, RTLD_NOW | RTLD_GLOBAL);
    if (g_svclHandle == nullptr)
    {
        // Avoid iostream; keep it minimal for QNX.
        // DFS will fail later when function pointers are nullptr.
    }
}

template <typename Fn>
Fn LoadSym(const char* sym)
{
    std::call_once(g_svclOnce, LoadRealSvcl);
    if (!g_svclHandle)
    {
        return nullptr;
    }
    dlerror(); // clear
    void* p = dlsym(g_svclHandle, sym);
    (void)dlerror();
    return reinterpret_cast<Fn>(p);
}

static DepthFromStereoMock::ControlParam ConsumeParam(DepthFromStereoMock::ApiId apiId)
{
    std::lock_guard<std::mutex> lock(g_paramsMutex);
    DepthFromStereoMock::ControlParam p = g_params[apiId];
    // consume
    g_params[apiId].action = DepthFromStereoMock::ACTION_NONE;
    g_params[apiId].param = nullptr;
    return p;
}

static bool ShouldReturnNull(DepthFromStereoMock::ApiId apiId)
{
    auto p = ConsumeParam(apiId);
    return p.action == DepthFromStereoMock::ACTION_RETURN_NULLPTR;
}

static bool ShouldReturnStatus(DepthFromStereoMock::ApiId apiId, SV::Status& out)
{
    auto p = ConsumeParam(apiId);
    if (p.action == DepthFromStereoMock::ACTION_RETURN_STATUS && p.param != nullptr)
    {
        out = *reinterpret_cast<SV::Status*>(p.param);
        return true;
    }
    return false;
}

static bool ShouldReturnConfigMapStatus(DepthFromStereoMock::ApiId apiId, SV::ConfigMapStatus& out)
{
    auto p = ConsumeParam(apiId);
    if (p.action == DepthFromStereoMock::ACTION_RETURN_STATUS && p.param != nullptr)
    {
        out = *reinterpret_cast<SV::ConfigMapStatus*>(p.param);
        return true;
    }
    return false;
}

class FakeStereoDisparity final : public SV::StereoDisparity
{
public:
    // SV::StereoDisparity has no default ctor; use the name-only ctor.
    explicit FakeStereoDisparity(SV::StereoDisparity* real) : SV::StereoDisparity("DepthFromStereoMockStereoDisparity"), m_real(real)
    {
    }

    ~FakeStereoDisparity() override = default;

    SV::Status SubmitSync(const SV::StereoDisparity::Input& input,
                          SV::StereoDisparity::Output& output,
                          const SV::StereoDisparity::ConfigMap& configMap) override
    {
        SV::Status forced{};
        if (ShouldReturnStatus(DepthFromStereoMock::API_STEREO_DISPARITY_SUBMIT_SYNC, forced))
        {
            return forced;
        }

        if (m_real)
        {
            return m_real->SubmitSync(input, output, configMap);
        }
        return SV::Status::EFAIL;
    }

    SV::Status Destroy() override
    {
        if (m_real)
        {
            SV::Status s = m_real->Destroy();
            m_real = nullptr;
            return s;
        }
        return SV::Status::EFAIL;
    }

private:
    SV::StereoDisparity* m_real{ nullptr };
};

} // namespace

void DepthFromStereoMock::MockApi_Control(DepthFromStereoMock::ApiId apiId, DepthFromStereoMock::Action action, void* param)
{
    if (apiId >= DepthFromStereoMock::API_MAX)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(g_paramsMutex);
    g_params[apiId].action = action;
    g_params[apiId].param = param;
}

void DepthFromStereoMock::MockApi_ResetAll()
{
    std::lock_guard<std::mutex> lock(g_paramsMutex);
    for (uint32_t i = 0; i < DepthFromStereoMock::API_MAX; ++i)
    {
        g_params[i].action = DepthFromStereoMock::ACTION_NONE;
        g_params[i].param = nullptr;
    }
    g_configMapSetCallCount.store(0u, std::memory_order_relaxed);
    g_configMapSetFailOnCall.store(UINT32_MAX, std::memory_order_relaxed);
}

void DepthFromStereoMock::MockApi_ConfigMapSet_FailOnCall(uint32_t callIndex)
{
    g_configMapSetCallCount.store(0u, std::memory_order_relaxed);
    g_configMapSetFailOnCall.store(callIndex, std::memory_order_relaxed);
}

void DepthFromStereoMock::MockApi_ConfigMapSet_Reset()
{
    g_configMapSetCallCount.store(0u, std::memory_order_relaxed);
    g_configMapSetFailOnCall.store(UINT32_MAX, std::memory_order_relaxed);
}

// ==============================================================================================
// Interposed SV entry points
// ==============================================================================================

// Interpose Session::Start / Stop / Destroy (virtual methods on SV::Session).
SV::Status SV::Session::Start()
{
    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_SESSION_START, forced))
    {
        return forced;
    }
    using Fn = SV::Status (*)(SV::Session*);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        // Mangled symbol from: nm -D coverage/libsvcl.so | grep -F 'Session::Start'
        realFn = LoadSym<Fn>("_ZN2SV7Session5StartEv");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(this);
}

SV::Status SV::Session::Stop()
{
    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_SESSION_STOP, forced))
    {
        return forced;
    }
    using Fn = SV::Status (*)(SV::Session*);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        // Mangled symbol from: nm -D coverage/libsvcl.so | grep -F 'Session::Stop'
        realFn = LoadSym<Fn>("_ZN2SV7Session4StopEv");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(this);
}

SV::Status SV::Session::Destroy()
{
    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_SESSION_DESTROY, forced))
    {
        return forced;
    }
    using Fn = SV::Status (*)(SV::Session*);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        // Mangled symbol from: nm -D coverage/libsvcl.so | grep -F 'Session::Destroy'
        realFn = LoadSym<Fn>("_ZN2SV7Session7DestroyEv");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(this);
}

// ==============================================================================================
// FeatureConfigMap<StereoDisparity::ConfigId>::CheckTypeValidity<T> specializations
//
// FeatureConfigMap::Set() calls CheckTypeValidity<T>(configId) before storing the value.
// If CheckTypeValidity returns false, Set() returns ConfigMapStatus::EINCORRECTTYPE.
//
// CheckTypeValidity is only *declared* in svConfigMap.h (not defined inline), so we can
// provide explicit specializations here that are linked in place of the SVCL library's
// definitions.  Each specialization delegates to CheckConfigMapSetMock(): when the mock
// is armed (via MockApi_ConfigMapSet_FailOnCall), the N-th Set() call returns false,
// causing Set() to return EINCORRECTTYPE and DFS to propagate QC_STATUS_FAIL.
// ==============================================================================================

// Specializing a member function template of a class template requires two template<> prefixes:
//   template<>  -- for the class template argument (StereoDisparity::ConfigId)
//   template<>  -- for the member function template argument (T)
// float32_t is typedef'd inside namespace SV on QNX, so use SV::float32_t.
#define DEFINE_CHECK_TYPE_VALIDITY(T)                                                                    \
    template <>                                                                                          \
    template <>                                                                                          \
    bool SV::FeatureConfigMap<SV::StereoDisparity::ConfigId>::CheckTypeValidity<T>(                     \
        SV::StereoDisparity::ConfigId) const                                                             \
    {                                                                                                    \
        return !CheckConfigMapSetMock();                                                                  \
    }

DEFINE_CHECK_TYPE_VALIDITY(uint32_t)
DEFINE_CHECK_TYPE_VALIDITY(bool)
DEFINE_CHECK_TYPE_VALIDITY(SV::float32_t)
DEFINE_CHECK_TYPE_VALIDITY(SV::PixelFormat)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::Mode)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::Precision)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::RefinementLevel)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::SearchDir)
DEFINE_CHECK_TYPE_VALIDITY(SV::ImageInfo*)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::FeatureNoiseToleranceScale*)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::FeatureNoiseToleranceOffset*)
DEFINE_CHECK_TYPE_VALIDITY(SV::StereoDisparity::Penalties*)

#undef DEFINE_CHECK_TYPE_VALIDITY

// Session::Create
SV::Session* SV::Session::Create(const SV::Session::ConfigMap& configMap,
                                void (*eventCallback)(SV::Session*, SV::Event, void*),
                                const void* pUserData)
{
    if (ShouldReturnNull(DepthFromStereoMock::API_SESSION_CREATE))
    {
        return nullptr;
    }

    using Fn = SV::Session* (*)(const SV::Session::ConfigMap&, void (*)(SV::Session*, SV::Event, void*), const void*);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        // Mangled symbol from: nm -D coverage/libsvcl.so | grep '_ZN2SV7Session6Create'
        realFn = LoadSym<Fn>("_ZN2SV7Session6CreateERKNS_16FeatureConfigMapINS0_8ConfigIdEEEPFvPS0_NS_5EventEPvEPKv");
    }
    if (!realFn)
    {
        return nullptr;
    }
    return realFn(configMap, eventCallback, pUserData);
}

// StereoDisparity::Create
SV::StereoDisparity* SV::StereoDisparity::Create(SV::Session* pSession,
                                                 const SV::StereoDisparity::ConfigMap& configMap,
                                                 void (*submitCallback)(SV::Status, SV::StereoDisparity::Output&, SV::StereoDisparity*, void*, void*),
                                                 const void* pUserData,
                                                 std::string outputTag)
{
    if (ShouldReturnNull(DepthFromStereoMock::API_STEREO_DISPARITY_CREATE))
    {
        return nullptr;
    }

    using Fn = SV::StereoDisparity* (*)(SV::Session*,
                                        const SV::StereoDisparity::ConfigMap&,
                                        void (*)(SV::Status, SV::StereoDisparity::Output&, SV::StereoDisparity*, void*, void*),
                                        const void*,
                                        std::string);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        // Mangled symbol from: nm -D coverage/libsvcl.so | grep -F 'StereoDisparity6Create'
        realFn = LoadSym<Fn>(
            "_ZN2SV15StereoDisparity6CreateEPNS_7SessionERKNS_16FeatureConfigMapINS0_8ConfigIdEEEPFvNS_6StatusERNS0_6OutputEPS0_PvSC_EPKvNSt3__212basic_stringIcNSH_11char_traitsIcEENSH_9allocatorIcEEEE");
    }
    if (!realFn)
    {
        return nullptr;
    }

    SV::StereoDisparity* real = realFn(pSession, configMap, submitCallback, pUserData, outputTag);

    // Wrap in fake object so tests can force SubmitSync to fail without interposing virtual dispatch.
    return new FakeStereoDisparity(real);
}

// Free functions
SV::Status BufferRegister(SV::Session* session, const SV::Buffer& buffer)
{
    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_BUFFER_REGISTER, forced))
    {
        return forced;
    }

    using Fn = SV::Status (*)(SV::Session*, const SV::Buffer&);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        realFn = LoadSym<Fn>("_ZN2SV13BufferRegisterEPNS_7SessionERKNS_6BufferE");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(session, buffer);
}

SV::Status BufferDeregister(SV::Session* session, const SV::Buffer& buffer)
{
    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_BUFFER_DEREGISTER, forced))
    {
        return forced;
    }

    using Fn = SV::Status (*)(SV::Session*, const SV::Buffer&);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        realFn = LoadSym<Fn>("_ZN2SV15BufferDeregisterEPNS_7SessionERKNS_6BufferE");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(session, buffer);
}

SV::Status ImageInfoQuery(SV::PixelFormat fmt, unsigned int width, unsigned int height, SV::ImageInfo& info)
{
    // IMPORTANT:
    // Depending on link ordering / symbol visibility, DFS may bind ImageInfoQuery to the real SVCL
    // symbol instead of this interposed one. To make the failure-path coverage deterministic
    // across builds, also allow overriding via an environment variable.
    //
    // This is intentionally test-only behavior (DepthFromStereoMock is only built/linked in unit tests).
    const char* env = getenv("SVCLMOCK_FORCE_IMAGE_INFO_QUERY_FAIL");
    if (env != nullptr && strcmp(env, "1") == 0)
    {
        return SV::Status::EFAIL;
    }

    SV::Status forced{};
    if (ShouldReturnStatus(DepthFromStereoMock::API_IMAGE_INFO_QUERY, forced))
    {
        return forced;
    }

    using Fn = SV::Status (*)(SV::PixelFormat, unsigned int, unsigned int, SV::ImageInfo&);
    static Fn realFn = nullptr;
    if (!realFn)
    {
        realFn = LoadSym<Fn>("_ZN2SV13ImageInfoQueryENS_10PixelFormatEjjRNS_9ImageInfoE");
    }
    if (!realFn)
    {
        return SV::Status::EFAIL;
    }
    return realFn(fmt, width, height, info);
}
