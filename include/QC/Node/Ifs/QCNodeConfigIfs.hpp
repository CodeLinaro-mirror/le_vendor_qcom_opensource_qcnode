// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_CONFIG_IFS_HPP
#define QC_NODE_CONFIG_IFS_HPP

#include "QC/Common/Types.hpp"
#include "QC/Infras/Memory/ImageDescriptor.hpp"
#include "QC/Node/Ifs/QCNodeDefs.hpp"
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace QC
{

/**
 * @brief Type-safe description of one node input or output buffer.
 * @param name The input or output name exposed by the node.
 * @param bufferType The type of buffer described by bufProps.
 * @param bufProps The concrete buffer properties owned by this descriptor.
 */
typedef struct QCBufferDescriptorInfo
{
    QCBufferDescriptorInfo() = default;

    QCBufferDescriptorInfo( std::string name, QCBufferType_e bufferType,
                            Memory::BufferProps_t props )
        : name( std::move( name ) ),
          bufferType( bufferType ),
          bufProps( std::in_place_type<Memory::BufferProps_t>, std::move( props ) )
    {}

    QCBufferDescriptorInfo( std::string name, QCBufferType_e bufferType,
                            Memory::TensorProps_t props )
        : name( std::move( name ) ),
          bufferType( bufferType ),
          bufProps( std::in_place_type<Memory::TensorProps_t>, std::move( props ) )
    {}

    QCBufferDescriptorInfo( std::string name, QCBufferType_e bufferType,
                            Memory::ImageBasicProps_t props )
        : name( std::move( name ) ),
          bufferType( bufferType ),
          bufProps( std::in_place_type<Memory::ImageBasicProps_t>, std::move( props ) )
    {}

    QCBufferDescriptorInfo( std::string name, QCBufferType_e bufferType,
                            Memory::ImageProps_t props )
        : name( std::move( name ) ),
          bufferType( bufferType ),
          bufProps( std::in_place_type<Memory::ImageProps_t>, std::move( props ) )
    {}

    std::string name;
    QCBufferType_e bufferType{ QC_BUFFER_TYPE_MAX };
    std::variant<Memory::BufferProps_t, Memory::TensorProps_t, Memory::ImageBasicProps_t,
                 Memory::ImageProps_t>
            bufProps;
} QCBufferDescriptorInfo_t;

typedef struct QCNodeConfigBase
{
    virtual ~QCNodeConfigBase() = default;
    QCNodeConfigBase() = default;
    QCNodeConfigBase( const QCNodeConfigBase & ) = default;
    QCNodeConfigBase &operator=( const QCNodeConfigBase & ) = default;
    QCNodeID_t nodeId{};
    QCExecuteMode_e executeMode{ QC_EXECUTE_MODE_SYNC };
    QCProcessorType_e processor{ QC_PROCESSOR_MAX };
    std::vector<uint32_t> coreIds;
    std::vector<QCBufferDescriptorInfo_t> inputs;
    std::vector<QCBufferDescriptorInfo_t> outputs;
    uint32_t numOfEntries{ 0U };
} QCNodeConfigBase_t;

class QCNodeConfigIfs
{

public:
    // Single lifetime execution methods
    ////////////////////////////////////
    // verify, migrate to structure and set internal data member
    // stringConfig is std:string to be able to work with different textual encoding (josn, yaml,
    // custom)
    virtual QCStatus_e VerifyAndSet( const std::string config, std::string &errors ) = 0;

    // Get details of parameters and ranges needed for essential functionality in run time
    // options is std:string to be able to work with different textual encoding (json, yaml,
    // custom) Information for pipeline builder (examples and rules )
    // Returns QC_STATUS_OK with options populated; an error code on failure (options set to "{}").
    virtual QCStatus_e GetOptions( std::string &options ) = 0;

    // Get configuration structure reference
    virtual const QCNodeConfigBase_t &Get() = 0;

protected:
    QCNodeConfigIfs() = default;
    QCNodeConfigIfs( const QCNodeConfigIfs & ) = default;
    QCNodeConfigIfs &operator=( const QCNodeConfigIfs & ) = default;
    ~QCNodeConfigIfs() = default;
    // In implementation a data member will be placed here from a type
    // which inherits "QCNodeConfigBase_t" as base
};

}   // namespace QC

#endif   // QC_NODE_CONFIG_IFS_HPP
