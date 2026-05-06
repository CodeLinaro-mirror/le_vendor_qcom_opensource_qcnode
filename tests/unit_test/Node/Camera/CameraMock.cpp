// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "CameraMock.hpp"

typedef QCarCamRet_e ( *QCarCamInitializeFn_t )( const QCarCamInit_t *pInitParams );
typedef QCarCamRet_e ( *QCarCamUninitializeFn_t )( void );
typedef QCarCamRet_e ( *QCarCamQueryInputsFn_t )( QCarCamInput_t *pInputs, const uint32_t size,
                                                  uint32_t *pRetSize );
typedef QCarCamRet_e ( *QCarCamQueryInputModesFn_t )( const uint32_t inputId,
                                                      QCarCamInputModes_t *pInputModes );
typedef QCarCamRet_e ( *QCarCamOpenFn_t )( const QCarCamOpen_t *pOpenParams, QCarCamHndl_t *pHndl );
typedef QCarCamRet_e ( *QCarCamCloseFn_t )( const QCarCamHndl_t hndl );
typedef QCarCamRet_e ( *QCarCamRegisterEventCallbackFn_t )(
        const QCarCamHndl_t hndl, const QCarCamEventCallback_t callbackFunc, void *pPrivateData );
typedef QCarCamRet_e ( *QCarCamSetParamFn_t )( const QCarCamHndl_t hndl,
                                               const QCarCamParamType_e param, const void *pValue,
                                               const uint32_t size );
typedef QCarCamRet_e ( *QCarCamReserveFn_t )( const QCarCamHndl_t hndl );
typedef QCarCamRet_e ( *QCarCamReleaseFn_t )( const QCarCamHndl_t hndl );
typedef QCarCamRet_e ( *QCarCamStartFn_t )( const QCarCamHndl_t hndl );
typedef QCarCamRet_e ( *QCarCamStopFn_t )( const QCarCamHndl_t hndl );
typedef QCarCamRet_e ( *QCarCamSetBuffersFn_t )( const QCarCamHndl_t hndl,
                                                 const QCarCamBufferList_t *pBuffers );
typedef QCarCamRet_e ( *QCarCamGetBuffersFn_t )( const QCarCamHndl_t hndl,
                                                 QCarCamBufferList_t *pBuffers );
typedef QCarCamRet_e ( *QCarCamSubmitRequestFn_t )( const QCarCamHndl_t hndl,
                                                    const QCarCamRequest_t *pRequest );
typedef QCarCamRet_e ( *QCarCamGetFrameFn_t )( const QCarCamHndl_t hndl,
                                               QCarCamFrameInfo_t *pFrameInfo,
                                               const uint64_t timeout, const uint32_t flags );
typedef QCarCamRet_e ( *QCarCamReleaseFrameFn_t )( const QCarCamHndl_t hndl, const uint32_t id,
                                                   const uint32_t bufferIndex );

static void *s_hDll = nullptr;
static QCarCamInitializeFn_t s_QCarCamInitializeFn = nullptr;
static QCarCamUninitializeFn_t s_QCarCamUninitializeFn = nullptr;
static QCarCamQueryInputsFn_t s_QCarCamQueryInputsFn = nullptr;
static QCarCamQueryInputModesFn_t s_QCarCamQueryInputModesFn = nullptr;
static QCarCamOpenFn_t s_QCarCamOpenFn = nullptr;
static QCarCamCloseFn_t s_QCarCamCloseFn = nullptr;
static QCarCamRegisterEventCallbackFn_t s_QCarCamRegisterEventCallbackFn = nullptr;
static QCarCamSetParamFn_t s_QCarCamSetParamFn = nullptr;
static QCarCamReserveFn_t s_QCarCamReserveFn = nullptr;
static QCarCamReleaseFn_t s_QCarCamReleaseFn = nullptr;
static QCarCamStartFn_t s_QCarCamStartFn = nullptr;
static QCarCamStopFn_t s_QCarCamStopFn = nullptr;
static QCarCamSetBuffersFn_t s_QCarCamSetBuffersFn = nullptr;
static QCarCamGetBuffersFn_t s_QCarCamGetBuffersFn = nullptr;
static QCarCamSubmitRequestFn_t s_QCarCamSubmitRequestFn = nullptr;
static QCarCamGetFrameFn_t s_QCarCamGetFrameFn = nullptr;
static QCarCamReleaseFrameFn_t s_QCarCamReleaseFrameFn = nullptr;

class QCXClientLoader
{
public:
    QCXClientLoader()
    {
        s_hDll = dlopen( "libqcxclient.so", RTLD_LAZY );
        if ( nullptr == s_hDll )
        {
            std::cerr << "Failed to load libqcxclient.so: " << dlerror() << std::endl;
        }
        else
        {
            std::cout << "Successfully loaded libqcxclient.so" << std::endl;
        }

#define LOAD_SYMBOL( ptr, symName, type )                                                          \
    ptr = (type) dlsym( s_hDll, #symName );                                                        \
    if ( nullptr == ptr )                                                                          \
        std::cerr << "Failed to load symbol " << #symName << ": " << dlerror() << std::endl;       \
    else                                                                                           \
        std::cout << "Successfully loaded symbol " << #symName << std::endl;

        LOAD_SYMBOL( s_QCarCamInitializeFn, QCarCamInitialize, QCarCamInitializeFn_t );
        LOAD_SYMBOL( s_QCarCamUninitializeFn, QCarCamUninitialize, QCarCamUninitializeFn_t );
        LOAD_SYMBOL( s_QCarCamQueryInputsFn, QCarCamQueryInputs, QCarCamQueryInputsFn_t );
        LOAD_SYMBOL( s_QCarCamQueryInputModesFn, QCarCamQueryInputModes,
                     QCarCamQueryInputModesFn_t );
        LOAD_SYMBOL( s_QCarCamOpenFn, QCarCamOpen, QCarCamOpenFn_t );
        LOAD_SYMBOL( s_QCarCamCloseFn, QCarCamClose, QCarCamCloseFn_t );
        LOAD_SYMBOL( s_QCarCamRegisterEventCallbackFn, QCarCamRegisterEventCallback,
                     QCarCamRegisterEventCallbackFn_t );
        LOAD_SYMBOL( s_QCarCamSetParamFn, QCarCamSetParam, QCarCamSetParamFn_t );
        LOAD_SYMBOL( s_QCarCamReserveFn, QCarCamReserve, QCarCamReserveFn_t );
        LOAD_SYMBOL( s_QCarCamReleaseFn, QCarCamRelease, QCarCamReleaseFn_t );
        LOAD_SYMBOL( s_QCarCamStartFn, QCarCamStart, QCarCamStartFn_t );
        LOAD_SYMBOL( s_QCarCamStopFn, QCarCamStop, QCarCamStopFn_t );
        LOAD_SYMBOL( s_QCarCamSetBuffersFn, QCarCamSetBuffers, QCarCamSetBuffersFn_t );
        LOAD_SYMBOL( s_QCarCamGetBuffersFn, QCarCamGetBuffers, QCarCamGetBuffersFn_t );
        LOAD_SYMBOL( s_QCarCamSubmitRequestFn, QCarCamSubmitRequest, QCarCamSubmitRequestFn_t );
        LOAD_SYMBOL( s_QCarCamGetFrameFn, QCarCamGetFrame, QCarCamGetFrameFn_t );
        LOAD_SYMBOL( s_QCarCamReleaseFrameFn, QCarCamReleaseFrame, QCarCamReleaseFrameFn_t );
    }

    ~QCXClientLoader()
    {
        if ( nullptr != s_hDll )
        {
            dlclose( s_hDll );
            std::cout << "Library unloaded." << std::endl;
        }
    }
};

static QCXClientLoader s_qcxclientLoader;

static MockControlParam_t s_MockParams[MOCK_API_MAX];
static bool s_isErrorPassive = false;

static QCarCamEventCallback_t s_registeredCallback = nullptr;
static void *s_registeredPrivateData = nullptr;
static QCarCamHndl_t s_registeredHndl = QCARCAM_HNDL_INVALID;

extern "C" void MockApi_Control( MockAPI_ID_e apiId, MockAPI_Action_e action, void *param )
{
    if ( apiId < MOCK_API_MAX )
    {
        s_MockParams[apiId].action = action;
        s_MockParams[apiId].param = param;
    }
}

extern "C" void MockApi_SetErrorPassive( bool active )
{
    s_isErrorPassive = active;
}

// QCarCamInitialize
QCarCamRet_e QCarCamInitialize( const QCarCamInit_t *pInitParams )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    (void) s_qcxclientLoader;
    if ( nullptr != s_QCarCamInitializeFn )
    {
        ret = s_QCarCamInitializeFn( pInitParams );
    }
    else
    {
        std::cerr << "QCarCamInitialize function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_INITIALIZE].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_INITIALIZE].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_INITIALIZE].param;
        }
        s_MockParams[MOCK_API_QCARCAM_INITIALIZE].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamUninitialize
QCarCamRet_e QCarCamUninitialize( void )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamUninitializeFn )
    {
        ret = s_QCarCamUninitializeFn();
    }
    else
    {
        std::cerr << "QCarCamUninitialize function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_UNINITIALIZE].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_UNINITIALIZE].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_UNINITIALIZE].param;
        }
        s_MockParams[MOCK_API_QCARCAM_UNINITIALIZE].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamQueryInputs
QCarCamRet_e QCarCamQueryInputs( QCarCamInput_t *pInputs, const uint32_t size, uint32_t *pRetSize )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamQueryInputsFn )
    {
        ret = s_QCarCamQueryInputsFn( pInputs, size, pRetSize );
    }
    else
    {
        std::cerr << "QCarCamQueryInputs function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
        if ( pRetSize )
        {
            *pRetSize = 1;
        }
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].action )
    {
        switch ( s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].action )
        {
            case MOCK_CONTROL_API_RETURN:
                ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].param;
                break;
            case MOCK_CONTROL_API_OUT_PARAM1:   // pInputs
                if ( pInputs )
                {
                    *pInputs =
                            *(QCarCamInput_t *) s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].param;
                }
                if ( pRetSize )
                {
                    *pRetSize = 1;
                }
                break;
            case MOCK_CONTROL_API_OUT_PARAM2:   // pRetSize is index 2
                if ( pRetSize )
                    *pRetSize = *(uint32_t *) s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].param;
                break;
            default:
                break;
        }
        if ( false == s_isErrorPassive )
        { /* don't consume it for error passive mode */
            s_MockParams[MOCK_API_QCARCAM_QUERY_INPUTS].action = MOCK_CONTROL_API_NONE;
        }
    }
    return ret;
}

// QCarCamQueryInputModes
QCarCamRet_e QCarCamQueryInputModes( const uint32_t inputId, QCarCamInputModes_t *pInputModes )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamQueryInputModesFn )
    {
        ret = s_QCarCamQueryInputModesFn( inputId, pInputModes );
    }
    else
    {
        std::cerr << "QCarCamQueryInputModes function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_QUERY_INPUT_MODES].action )
    {
        switch ( s_MockParams[MOCK_API_QCARCAM_QUERY_INPUT_MODES].action )
        {
            case MOCK_CONTROL_API_RETURN:
                ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_QUERY_INPUT_MODES].param;
                break;
            case MOCK_CONTROL_API_OUT_PARAM1:   // pInputModes is index 1
                if ( pInputModes )
                {
                    // CAUTION: Shallow copy struct, deep copy might be needed depending on test
                    *pInputModes = *(QCarCamInputModes_t *)
                                            s_MockParams[MOCK_API_QCARCAM_QUERY_INPUT_MODES]
                                                    .param;
                }
                break;
            default:
                break;
        }
        if ( false == s_isErrorPassive )
        { /* don't consume it for error passive mode */
            s_MockParams[MOCK_API_QCARCAM_QUERY_INPUT_MODES].action = MOCK_CONTROL_API_NONE;
        }
    }
    return ret;
}

// QCarCamOpen
QCarCamRet_e QCarCamOpen( const QCarCamOpen_t *pOpenParams, QCarCamHndl_t *pHndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamOpenFn )
    {
        ret = s_QCarCamOpenFn( pOpenParams, pHndl );
    }
    else
    {
        std::cerr << "QCarCamOpen function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_OPEN].action )
    {
        switch ( s_MockParams[MOCK_API_QCARCAM_OPEN].action )
        {
            case MOCK_CONTROL_API_RETURN:
                ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_OPEN].param;
                break;
            case MOCK_CONTROL_API_OUT_PARAM1:   // pHndl is index 1
                if ( pHndl ) *pHndl = *(QCarCamHndl_t *) s_MockParams[MOCK_API_QCARCAM_OPEN].param;
                break;
            default:
                break;
        }
        s_MockParams[MOCK_API_QCARCAM_OPEN].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamClose
QCarCamRet_e QCarCamClose( const QCarCamHndl_t hndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamCloseFn )
    {
        ret = s_QCarCamCloseFn( hndl );
    }
    else
    {
        std::cerr << "QCarCamClose function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_CLOSE].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_CLOSE].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_CLOSE].param;
        }
        s_MockParams[MOCK_API_QCARCAM_CLOSE].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamRegisterEventCallback
QCarCamRet_e QCarCamRegisterEventCallback( const QCarCamHndl_t hndl,
                                           const QCarCamEventCallback_t callbackFunc,
                                           void *pPrivateData )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamRegisterEventCallbackFn )
    {
        ret = s_QCarCamRegisterEventCallbackFn( hndl, callbackFunc, pPrivateData );
    }
    else
    {
        std::cerr << "QCarCamRegisterEventCallback function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK].action ==
             MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK].param;
        }
        s_MockParams[MOCK_API_QCARCAM_REGISTER_EVENT_CALLBACK].action = MOCK_CONTROL_API_NONE;
    }

    // Store the callback and private data for event triggering
    if ( QCARCAM_RET_OK == ret )
    {
        s_registeredCallback = callbackFunc;
        s_registeredPrivateData = pPrivateData;
        s_registeredHndl = hndl;
    }

    return ret;
}

extern "C" void MockApi_TriggerEvent( uint32_t eventId, const QCarCamEventPayload_t *pPayload,
                                      bool useNullPrivateData )
{
    if ( nullptr != s_registeredCallback )
    {
        void *pData = useNullPrivateData ? nullptr : s_registeredPrivateData;
        s_registeredCallback( s_registeredHndl, eventId, pPayload, pData );
    }
}

// QCarCamSetParam
QCarCamRet_e QCarCamSetParam( const QCarCamHndl_t hndl, const QCarCamParamType_e param,
                              const void *pValue, const uint32_t size )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamSetParamFn )
    {
        ret = s_QCarCamSetParamFn( hndl, param, pValue, size );
    }
    else
    {
        std::cerr << "QCarCamSetParam function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    switch ( param )
    {
        case QCARCAM_STREAM_CONFIG_PARAM_EVENT_MASK:
            if ( MOCK_CONTROL_API_NONE !=
                 s_MockParams[MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK].action )
            {
                if ( s_MockParams[MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK].action ==
                     MOCK_CONTROL_API_RETURN )
                {
                    ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK]
                                   .param;
                }
                s_MockParams[MOCK_API_QCARCAM_SET_PARAM_EVENT_MASK].action = MOCK_CONTROL_API_NONE;
            }
            break;
        case QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE:
            if ( MOCK_CONTROL_API_NONE !=
                 s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE].action )
            {
                if ( s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE].action ==
                     MOCK_CONTROL_API_RETURN )
                {
                    ret = *(QCarCamRet_e *) s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE]
                                   .param;
                }
                s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_ISP_USECASE].action =
                        MOCK_CONTROL_API_NONE;
            }
            break;
        case QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL:
            if ( MOCK_CONTROL_API_NONE !=
                 s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL].action )
            {
                if ( s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL].action ==
                     MOCK_CONTROL_API_RETURN )
                {
                    ret = *(QCarCamRet_e *)
                                   s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL]
                                           .param;
                }
                s_MockParams[QCARCAM_STREAM_CONFIG_PARAM_FRAME_DROP_CONTROL].action =
                        MOCK_CONTROL_API_NONE;
            }
            break;
        default:
            break;
    }

    return ret;
}

// QCarCamReserve
QCarCamRet_e QCarCamReserve( const QCarCamHndl_t hndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamReserveFn )
    {
        ret = s_QCarCamReserveFn( hndl );
    }
    else
    {
        std::cerr << "QCarCamReserve function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_RESERVE].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_RESERVE].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_RESERVE].param;
        }
        s_MockParams[MOCK_API_QCARCAM_RESERVE].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamRelease
QCarCamRet_e QCarCamRelease( const QCarCamHndl_t hndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamReleaseFn )
    {
        ret = s_QCarCamReleaseFn( hndl );
    }
    else
    {
        std::cerr << "QCarCamRelease function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_RELEASE].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_RELEASE].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_RELEASE].param;
        }
        s_MockParams[MOCK_API_QCARCAM_RELEASE].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamStart
QCarCamRet_e QCarCamStart( const QCarCamHndl_t hndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamStartFn )
    {
        ret = s_QCarCamStartFn( hndl );
    }
    else
    {
        std::cerr << "QCarCamStart function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_START].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_START].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_START].param;
        }
        s_MockParams[MOCK_API_QCARCAM_START].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamStop
QCarCamRet_e QCarCamStop( const QCarCamHndl_t hndl )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamStopFn )
    {
        ret = s_QCarCamStopFn( hndl );
    }
    else
    {
        std::cerr << "QCarCamStop function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_STOP].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_STOP].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_STOP].param;
        }
        s_MockParams[MOCK_API_QCARCAM_STOP].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamSetBuffers
QCarCamRet_e QCarCamSetBuffers( const QCarCamHndl_t hndl, const QCarCamBufferList_t *pBuffers )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamSetBuffersFn )
    {
        ret = s_QCarCamSetBuffersFn( hndl, pBuffers );
    }
    else
    {
        std::cerr << "QCarCamSetBuffers function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_SET_BUFFERS].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_SET_BUFFERS].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_SET_BUFFERS].param;
        }
        s_MockParams[MOCK_API_QCARCAM_SET_BUFFERS].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamGetBuffers
QCarCamRet_e QCarCamGetBuffers( const QCarCamHndl_t hndl, QCarCamBufferList_t *pBuffers )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamGetBuffersFn )
    {
        ret = s_QCarCamGetBuffersFn( hndl, pBuffers );
    }
    else
    {
        std::cerr << "QCarCamGetBuffers function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_GET_BUFFERS].action )
    {
        switch ( s_MockParams[MOCK_API_QCARCAM_GET_BUFFERS].action )
        {
            case MOCK_CONTROL_API_RETURN:
                ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_GET_BUFFERS].param;
                break;
            case MOCK_CONTROL_API_OUT_PARAM1:   // pBuffers is index 1
                if ( pBuffers != nullptr )
                {
                    QCarCamBufferList_t *pBufferList =
                            (QCarCamBufferList_t *) s_MockParams[MOCK_API_QCARCAM_GET_BUFFERS]
                                    .param;
                    pBuffers->id = pBufferList->id;
                    pBuffers->colorFmt = pBufferList->colorFmt;
                    pBuffers->nBuffers = pBufferList->nBuffers;
                    pBuffers->flags = pBufferList->flags;
                    for ( uint32_t i = 0; i < pBuffers->nBuffers; i++ )
                    {
                        uint32_t planeNum = pBufferList->pBuffers[i].numPlanes;
                        pBuffers->pBuffers[i].numPlanes = planeNum;
                        for ( uint32_t k = 0; k < planeNum; k++ )
                        {
                            pBuffers->pBuffers[i].planes[k].width =
                                    pBufferList->pBuffers[i].planes[k].width;
                            pBuffers->pBuffers[i].planes[k].height =
                                    pBufferList->pBuffers[i].planes[k].height;
                            pBuffers->pBuffers[i].planes[k].stride =
                                    pBufferList->pBuffers[i].planes[k].stride;
                            pBuffers->pBuffers[i].planes[k].size =
                                    pBufferList->pBuffers[i].planes[k].size;
                            pBuffers->pBuffers[i].planes[k].offset =
                                    pBufferList->pBuffers[i].planes[k].offset;
                            pBuffers->pBuffers[i].planes[k].memHndl =
                                    pBufferList->pBuffers[i].planes[k].memHndl;
                        }
                    }
                }
                break;
            default:
                break;
        }
        s_MockParams[MOCK_API_QCARCAM_GET_BUFFERS].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamSubmitRequest
QCarCamRet_e QCarCamSubmitRequest( const QCarCamHndl_t hndl, const QCarCamRequest_t *pRequest )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamSubmitRequestFn )
    {
        ret = s_QCarCamSubmitRequestFn( hndl, pRequest );
    }
    else
    {
        std::cerr << "QCarCamSubmitRequest function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_SUBMIT_REQUEST].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_SUBMIT_REQUEST].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_SUBMIT_REQUEST].param;
        }
        s_MockParams[MOCK_API_QCARCAM_SUBMIT_REQUEST].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamGetFrame
QCarCamRet_e QCarCamGetFrame( const QCarCamHndl_t hndl, QCarCamFrameInfo_t *pFrameInfo,
                              const uint64_t timeout, const uint32_t flags )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamGetFrameFn )
    {
        ret = s_QCarCamGetFrameFn( hndl, pFrameInfo, timeout, flags );
    }
    else
    {
        std::cerr << "QCarCamGetFrame function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_GET_FRAME].action )
    {
        switch ( s_MockParams[MOCK_API_QCARCAM_GET_FRAME].action )
        {
            case MOCK_CONTROL_API_RETURN:
                ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_GET_FRAME].param;
                break;
            case MOCK_CONTROL_API_OUT_PARAM1:   // pFrameInfo is index 1
                if ( pFrameInfo )
                    *pFrameInfo =
                            *(QCarCamFrameInfo_t *) s_MockParams[MOCK_API_QCARCAM_GET_FRAME].param;
                break;
            default:
                break;
        }
        s_MockParams[MOCK_API_QCARCAM_GET_FRAME].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}

// QCarCamReleaseFrame
QCarCamRet_e QCarCamReleaseFrame( const QCarCamHndl_t hndl, const uint32_t id,
                                  const uint32_t bufferIndex )
{
    QCarCamRet_e ret = QCARCAM_RET_FAILED;
    if ( nullptr != s_QCarCamReleaseFrameFn )
    {
        ret = s_QCarCamReleaseFrameFn( hndl, id, bufferIndex );
    }
    else
    {
        std::cerr << "QCarCamReleaseFrame function not loaded." << std::endl;
        ret = QCARCAM_RET_FAILED;
    }

    if ( s_isErrorPassive )
    {
        ret = QCARCAM_RET_OK;
    }

    if ( MOCK_CONTROL_API_NONE != s_MockParams[MOCK_API_QCARCAM_RELEASE_FRAME].action )
    {
        if ( s_MockParams[MOCK_API_QCARCAM_RELEASE_FRAME].action == MOCK_CONTROL_API_RETURN )
        {
            ret = *(QCarCamRet_e *) s_MockParams[MOCK_API_QCARCAM_RELEASE_FRAME].param;
        }
        s_MockParams[MOCK_API_QCARCAM_RELEASE_FRAME].action = MOCK_CONTROL_API_NONE;
    }
    return ret;
}