// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP
#define QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "QC/Common/QCDefs.hpp"
#include "QC/Node/Ifs/QCFrameDescriptorNodeIfs.hpp"
#include "QC/Node/Ifs/QCNodeDefs.hpp"

namespace QC
{
namespace Node
{

// ---------------------------------------------------------------------------
// FrameDescriptor_CreateFunction_t
//
// Signature of the factory function that each frame descriptor implementation
// provides and registers via REGISTER_FRAME_DESCRIPTOR().
//
// numOfBuffers: the number of buffer slots the descriptor should hold.
//
// Mirrors Node_CreateFunction_t in NodeBase.hpp, extended with numOfBuffers
// so that the same concrete class (e.g. NodeFrameDescriptor) can be reused
// across descriptor types with different buffer counts.
// ---------------------------------------------------------------------------
typedef std::unique_ptr<QCFrameDescriptorNodeIfs> ( *FrameDescriptor_CreateFunction_t )(
        uint32_t numOfBuffers );

// ---------------------------------------------------------------------------
// RegisterFrameDescriptorCreator
//
// Associates a QCNodeFrameDescType_e key with a FrameDescriptor_CreateFunction_t
// so that QCNodeFrameDescriptorFactory::CreateFrameDescriptor() can instantiate
// the corresponding descriptor.
//
// Returns QC_STATUS_OK when the (type, createFnc) pair was registered, or
// QC_STATUS_ALREADY when a creator was already registered for the same type
// (duplicate registration).  The QCStatus_e return value lets
// REGISTER_FRAME_DESCRIPTOR() initialise a file-scope static constant with it,
// which both forces the registration to run at static-initialisation time and
// avoids unused-variable warnings.
// ---------------------------------------------------------------------------
QCStatus_e RegisterFrameDescriptorCreator( QCNodeFrameDescType_e type,
                                           FrameDescriptor_CreateFunction_t createFnc );

// ---------------------------------------------------------------------------
// QCNodeFrameDescriptorFactory
//
// Registry-based static factory for QCFrameDescriptorNodeIfs, following the
// same convention as QCNodeFactory:
//
//   CreateFrameDescriptor                — selected by QCNodeFrameDescType_e via registry
//   GetSupportedNodeFrameDescriptorTypes — populates a vector of types constructible on this build
//
// New frame descriptor types are registered at static-initialisation time
// via REGISTER_FRAME_DESCRIPTOR() without modifying this class, ensuring
// the interface remains stable across future API extensions.
//
// ISO 26262:6 Table 8 §1b : out-parameter is null on error.
// ---------------------------------------------------------------------------
class QCNodeFrameDescriptorFactory
{
public:
    // Creates a frame descriptor for the given type using the registered
    // creator function.
    // Returns QC_STATUS_UNSUPPORTED if no creator is registered for the type;
    //   outDescriptor is null.
    // Returns QC_STATUS_BAD_ARGUMENTS if numOfBuffers is 0; outDescriptor is null.
    // Returns QC_STATUS_FAIL on allocation failure; outDescriptor is null.
    static QCStatus_e
    CreateFrameDescriptor( QCNodeFrameDescType_e type, uint32_t numOfBuffers,
                           std::unique_ptr<QCFrameDescriptorNodeIfs> &outDescriptor );

    // Populates outTypes with frame descriptor types registered via REGISTER_FRAME_DESCRIPTOR().
    // Returns QC_STATUS_OK on success; outTypes is valid only on QC_STATUS_OK.
    static QCStatus_e
    GetSupportedNodeFrameDescriptorTypes( std::vector<QCNodeFrameDescType_e> &outTypes );

private:
    QCNodeFrameDescriptorFactory() = delete;
};

}   // namespace Node
}   // namespace QC

// ---------------------------------------------------------------------------
// REGISTER_FRAME_DESCRIPTOR( type, class_name )
//
// Registers a QCFrameDescriptorNodeIfs-derived class with
// QCNodeFrameDescriptorFactory so that
// QCNodeFrameDescriptorFactory::CreateFrameDescriptor( type, numOfBuffers, ... )
// can instantiate it.
//
// Usage (in a frame descriptor implementation .cpp file):
//
//   REGISTER_FRAME_DESCRIPTOR( QC_NODE_FRAME_DESC_TYPE_NODE_FRAME_DESCRIPTOR,
//                               NodeFrameDescriptor )
//
// Requirement: class_name must have a constructor that accepts
//   uint32_t numOfBuffers.
//
// Mechanism (mirrors REGISTER_NODE in NodeBase.hpp):
//   1. A factory function CreateFD<class_name>_<line>(uint32_t) is defined as
//      a file-scope static; it heap-allocates the descriptor and wraps it in a
//      unique_ptr<QCFrameDescriptorNodeIfs>.
//   2. A file-scope static const QCStatus_e g_registeredFD<class_name>_<line>
//      is initialised with the return value of RegisterFrameDescriptorCreator().
//      Evaluating its initialiser at static-initialisation time (before main())
//      performs the registration; storing the returned status keeps the
//      variable genuinely used and suppresses unused-variable diagnostics.
//
// The __LINE__-based suffix (via the three-level indirection below) ensures
// that the generated symbols are unique even when the same class_name is
// registered for multiple descriptor types in the same translation unit.
//
// ISO 26262:6 Table 8 §1b : outDescriptor is null when type is not registered.
// ---------------------------------------------------------------------------

// Level 1 (innermost): performs the actual token pasting.
// uid is used with ##, so it must already be a fully-expanded integer token
// by the time this macro is invoked.
#define REGISTER_FRAME_DESCRIPTOR_IMPL( type, class_name, uid )                                    \
    static std::unique_ptr<QC::QCFrameDescriptorNodeIfs> CreateFD##class_name##_##uid(             \
            uint32_t numOfBuffers )                                                                \
    {                                                                                              \
        return std::make_unique<class_name>( numOfBuffers );                                       \
    }                                                                                              \
    static const QC::QCStatus_e g_registeredFD##class_name##_##uid =                               \
            QC::Node::RegisterFrameDescriptorCreator( type, CreateFD##class_name##_##uid );

// Level 2 (intermediate): uid is used as a plain argument here (no ## or #),
// so the preprocessor fully expands it — turning __LINE__ into its integer
// value — before passing it on to REGISTER_FRAME_DESCRIPTOR_IMPL.
#define REGISTER_FRAME_DESCRIPTOR_EXPAND( type, class_name, uid )                                  \
    REGISTER_FRAME_DESCRIPTOR_IMPL( type, class_name, uid )

// Level 3 (public API): passes __LINE__ to the EXPAND helper so that
// __LINE__ is evaluated in the caller's context and then expanded before
// reaching the ## operator in REGISTER_FRAME_DESCRIPTOR_IMPL.
#define REGISTER_FRAME_DESCRIPTOR( type, class_name )                                              \
    REGISTER_FRAME_DESCRIPTOR_EXPAND( type, class_name, __LINE__ )

#endif   // QC_NODE_FRAME_DESCRIPTOR_FACTORY_HPP
