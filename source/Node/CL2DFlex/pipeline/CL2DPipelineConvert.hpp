// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear


#ifndef QC_CL2D_PIPELINE_CONVERT_HPP
#define QC_CL2D_PIPELINE_CONVERT_HPP

#include "pipeline/CL2DPipelineBase.hpp"

namespace QC
{
namespace Node
{

class CL2DPipelineConvert : public CL2DPipelineBase
{
public:
    CL2DPipelineConvert();

    ~CL2DPipelineConvert() override;

    CL2DPipelineConvert( const CL2DPipelineConvert & ) = delete;
    CL2DPipelineConvert &operator=( const CL2DPipelineConvert & ) = delete;

    QCStatus_e
    Init( uint32_t inputId, cl_kernel *pKernel, CL2DFlex_Config_t *pConfig,
          OpenclSrv *pOpenclSrvObj,
          std::vector<std::reference_wrapper<QCBufferDescriptorBase>> &buffers ) override;

    QCStatus_e Deinit() override;

    QCStatus_e Execute( ImageDescriptor_t &input, ImageDescriptor_t &output ) override;

private:
    QCStatus_e ConvertFromNV12ToRGB( cl_mem bufferSrc, uint32_t srcOffset, cl_mem bufferDst,
                                     uint32_t dstOffset, ImageDescriptor_t &input,
                                     ImageDescriptor_t &output );
    QCStatus_e ConvertFromUYVYToRGB( cl_mem bufferSrc, uint32_t srcOffset, cl_mem bufferDst,
                                     uint32_t dstOffset, ImageDescriptor_t &input,
                                     ImageDescriptor_t &output );
    QCStatus_e ConvertFromUYVYToNV12( cl_mem bufferSrc, uint32_t srcOffset, cl_mem bufferDst,
                                      uint32_t dstOffset, ImageDescriptor_t &input,
                                      ImageDescriptor_t &output );

};   // class PipelineConvert

}   // namespace Node
}   // namespace QC

#endif   // QC_CL2D_PIPELINE_CONVERT_HPP
