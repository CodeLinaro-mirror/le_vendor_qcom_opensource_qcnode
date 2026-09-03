// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_LOGGER_USER_HPP
#define QC_LOGGER_USER_HPP

#include "QC/Infras/Log/Ifs/QCLoggerBackendIfs.hpp"

namespace QC
{

/** @brief User-provided Logger backend callbacks. */
class LoggerUser : public QCLoggerBackendIfs
{
public:
    QCStatus_e Init( const char *pName, Logger_Level_e level ) override;
    QCStatus_e Deinit() override;
    void Log( Logger_Level_e level, const char *pFormat, va_list args ) const override;

    /**
     * @brief Setup user Logger backend function pointers.
     * @param[in] logFnc Function to log a message through the user backend.
     * @param[in] createFnc Function to create a user Logger handle.
     * @param[in] destroyFnc Function to destroy a user Logger handle.
     * @note This process-wide setup may be completed once. A repeated call with the same
     * callbacks is treated as already configured and returns QC_STATUS_OK; a repeated call with
     * different callbacks returns QC_STATUS_BAD_STATE. Complete setup before initializing any
     * Logger with LOGGER_BACKEND_USER.
     * @return QC_STATUS_OK on success, otherwise an error status.
     */
    static QCStatus_e Setup( Logger_Log_t logFnc, Logger_Create_t createFnc,
                             Logger_Destroy_t destroyFnc );

private:
    static QCStatus_e DummyCreate( const char *pName, Logger_Level_e level,
                                   Logger_Handle_t *pHandle );
    static void DummyLog( Logger_Handle_t hHandle, Logger_Level_e level, const char *pFormat,
                          va_list args );
    static void DummyDestroy( Logger_Handle_t hHandle );

    static Logger_Create_t s_createFnc;
    static Logger_Log_t s_logFnc;
    static Logger_Destroy_t s_destroyFnc;
    static bool s_isSetup;
    Logger_Handle_t m_hHandle = nullptr;
};

}   // namespace QC

#endif   // QC_LOGGER_USER_HPP
