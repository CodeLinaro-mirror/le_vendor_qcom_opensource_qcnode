// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_CL2D_PIPELINE_CONVERTUBWC_HPP
#define QC_CL2D_PIPELINE_CONVERTUBWC_HPP

#include "pipeline/CL2DPipelineBase.hpp"

namespace QC
{
namespace Node
{

class CL2DPipelineConvertUBWC : public CL2DPipelineBase
{
public:
    CL2DPipelineConvertUBWC();

    ~CL2DPipelineConvertUBWC() override;

    CL2DPipelineConvertUBWC( const CL2DPipelineConvertUBWC & ) = delete;
    CL2DPipelineConvertUBWC &operator=( const CL2DPipelineConvertUBWC & ) = delete;

    QCStatus_e
    Init( uint32_t inputId, cl_kernel *pKernel, CL2DFlex_Config_t *pConfig,
          OpenclSrv *pOpenclSrvObj,
          std::vector<std::reference_wrapper<QCBufferDescriptorBase>> &buffers ) override;

    QCStatus_e Deinit() override;

    QCStatus_e Execute( ImageDescriptor_t &input, ImageDescriptor_t &output ) override;

private:
    QCStatus_e ConvertUBWCFromNV12UBWCToNV12( ImageDescriptor_t &input, ImageDescriptor_t &output );

};   // class PipelineConvertUBWC

}   // namespace Node
}   // namespace QC

#endif   // QC_CL2D_PIPELINE_CONVERTUBWC_HPP
