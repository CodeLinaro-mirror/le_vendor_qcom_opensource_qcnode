// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef QC_MEMORY_MANAGER_LOCAL_HPP
#define QC_MEMORY_MANAGER_LOCAL_HPP

#include "QC/Infras/Log/Logger.hpp"
#include "QC/Infras/Memory/Ifs/QCMemoryManagerIfs.hpp"
#include <map>
#include <memory>
#include <set>
#include <shared_mutex>
#include <vector>


namespace QC
{
namespace Memory
{

/**
 * @class ManagerLocal
 * @brief A concrete implementation of the QCMemoryManagerIfs interface for local memory management.
 *
 * This class provides methods for managing memory locally, including registration, unregistration,
 * pool creation and destruction, buffer allocation and deallocation, and resource reclamation.
 */
class ManagerLocal : public QCMemoryManagerIfs
{

public:
    /**
     * @brief Deleted copy constructor operator.
     * This operator is deleted to prevent copy constructor of ManagerLocal objects.
     * @param The object to assign from.
     * @return A reference to the current object.
     */
    ManagerLocal( const ManagerLocal & ) = delete;
    /**
     * @brief Deleted assignment operator.
     * This operator is deleted to prevent assignment of ManagerLocal objects.
     * @param The object to assign from.
     * @return A reference to the current object.
     */
    ManagerLocal &operator=( const ManagerLocal & ) = delete;

    /**
     * @brief Default constructor.
     * This constructor Intilizes members which do not require external parameters
     */
    ManagerLocal();

    /**
     * @brief Initializer the ManagerLocal class.
     * This function initializes the ManagerLocal object with the provided initialization
     * parameters, allocates memories for the internal database.
     * @param init The initialization parameters for the memory manager.
     * @return The status of the registration operation.
     */
    virtual QCStatus_e Initialize( const QCMemoryManagerInit_t &init );

    /**
     * @brief DeInitializes internal data bases and allocated resources.
     * This Function DEinitializes all allocated resoces and
     * heap memory for internal data base structures.
     * Validates the deallocation process success.
     * @return The status of the deinitialization operation.
     */
    virtual QCStatus_e DeInitialize();

    /**
     * @brief Destructor for the ManagerLocal class.
     * This destructor releases any resources allocated by the ManagerLocal object.
     */
    virtual ~ManagerLocal();

    /**
     * @brief Registers a node with the memory manager.
     * This method registers a node with the memory manager and returns a handle that can be used to
     * identify the node.
     * @param node The ID of the node to register.
     * @param handle The handle that will be used to identify the node.
     * @return The status of the registration operation.
     */
    virtual QCStatus_e Register( const QCNodeID_t &node, QCMemoryHandle_t &handle );

    /**
     * @brief Unregisters a node from the memory manager.
     * This method unregisters a node from the memory manager using the provided handle.
     * @param memHandle The handle of the node to unregister.
     * @return The status of the unregistration operation.
     */
    virtual QCStatus_e UnRegister( const QCMemoryHandle_t &memHandle );

    /**
     * @brief Creates a memory pool.
     * This method creates a memory pool using the provided handle, pool configuration, and returns
     * a handle that can be used to identify the pool.
     * @param handle The handle of the node that owns the pool.
     * @param poolCfg The configuration of the pool to create.
     * @param poolHandle The handle that will be used to identify the pool.
     * @return The status of the pool creation operation.
     */
    virtual QCStatus_e CreatePool( const QCMemoryHandle_t &handle,
                                   const QCMemoryPoolInitConfig_t &poolCfg,
                                   QCMemoryPoolHandle_t &poolHandle );

    /**
     * @brief Destroys a memory pool.
     * This method destroys a memory pool using the provided handle and pool handle.
     * @param poolHandle The handle of the pool to destroy.
     * @return The status of the pool destruction operation.
     */
    virtual QCStatus_e DestroyPool( const QCMemoryPoolHandle_t &poolHandle );

    /**
     * @brief Allocates a buffer from a memory pool.
     * This method allocates a buffer from a memory pool using the provided memory handle, pool
     * handle, and returns a buffer descriptor.
     * @param poolHandle The handle of the pool to allocate from.
     * @param buff The buffer descriptor that will be used to store the allocated buffer.
     * @return The status of the buffer allocation operation.
     */
    virtual QCStatus_e AllocateBufferFromPool( const QCMemoryPoolHandle_t &poolHandle,
                                               QCBufferDescriptorBase_t &buff );

    /**
     * @brief Puts a buffer back into a memory pool.
     * This method returns a buffer to a memory pool using the provided memory handle, pool handle,
     * and buffer descriptor.
     * @param poolHandle The handle of the pool to put the buffer back into.
     * @param buff The buffer descriptor of the buffer to put back into the pool.
     * @return The status of the buffer put operation.
     */
    virtual QCStatus_e PutBufferToPool( const QCMemoryPoolHandle_t &poolHandle,
                                        const QCBufferDescriptorBase_t &buff );

    /**
     * @brief Allocates a buffer.
     * This method allocates a buffer using the provided memory handle, allocator, buffer
     * properties, and returns a buffer descriptor.
     * @param handle The handle of the node that owns the buffer.
     * @param allocator The type of allocator to use.
     * @param request The properties of the buffer to allocate.
     * @param buff The buffer descriptor that will be used to store the allocated buffer.
     * @return The status of the buffer allocation operation.
     */
    virtual QCStatus_e AllocateBuffer( const QCMemoryHandle_t handle,
                                       const QCMemoryAllocator_e allocator,
                                       const QCBufferPropBase_t &request,
                                       QCBufferDescriptorBase_t &buff );

    /**
     * @brief Frees a buffer.
     * This method frees a buffer using the provided memory handle and buffer descriptor.
     * @param handle The handle of the node that owns the buffer.
     * @param buff The buffer descriptor of the buffer to free.
     * @return The status of the buffer free operation.
     */
    virtual QCStatus_e FreeBuffer( const QCMemoryHandle_t handle,
                                   const QCBufferDescriptorBase_t &buff );

    /**
     * @brief Reclaims resources.
     * This method reclaims resources using the provided memory handle.
     * @param handle The handle of the node that owns the resources to reclaim.
     * @return The status of the resource reclamation operation.
     */
    virtual QCStatus_e ReclaimResources( const QCMemoryHandle_t &handle );

#define IS_IN_DB_STATUS( dataBase, element )                                                       \
    ( ( ( dataBase ).find( element ) != ( dataBase ).end() ) ? QC_STATUS_OK : QC_STATUS_FAIL )
#define IS_NOT_IN_DB_STATUS( dataBase, element )                                                   \
    ( ( ( dataBase ).find( element ) == ( dataBase ).end() ) ? QC_STATUS_OK : QC_STATUS_FAIL )

private:
    /**
     * @brief Structure to hold pool map with its associated shared_mutex for reader-writer locking.
     */
    struct PoolMapWithMutex
    {
        std::map<QCMemoryPoolHandle_t, std::reference_wrapper<QCMemoryPoolIfs>> poolMap;
        mutable std::shared_mutex poolMutex;

        /**
         * @var poolSequenceCounter
         * @brief Per-node monotonic counter incremented on every CreatePool call.
         */
        uint8_t poolSequenceCounter{ 0 };

        PoolMapWithMutex() = default;

        // Delete copy constructor and assignment operator to prevent copying of mutex
        PoolMapWithMutex( const PoolMapWithMutex & ) = delete;
        PoolMapWithMutex &operator=( const PoolMapWithMutex & ) = delete;

        // Provide move constructor and assignment operator
        PoolMapWithMutex( PoolMapWithMutex &&other ) noexcept
            : poolMap( std::move( other.poolMap ) )
        {
            // Note: shared_mutex is not moved, each object gets its own mutex
        }

        PoolMapWithMutex &operator=( PoolMapWithMutex &&other ) noexcept
        {
            if ( this != &other )
            {
                poolMap = std::move( other.poolMap );
                // Note: shared_mutex is not moved, each object keeps its own mutex
            }
            return *this;
        }
    };

    /**
     * @brief Structure to hold allocation set with its associated shared_mutex for reader-writer
     * locking.
     */
    struct AllocationSetWithMutex
    {
        std::set<QCBufferDescriptorBase_t> allocationSet;
        mutable std::shared_mutex allocationMutex;

        AllocationSetWithMutex() = default;

        // Delete copy constructor and assignment operator to prevent copying of mutex
        AllocationSetWithMutex( const AllocationSetWithMutex & ) = delete;
        AllocationSetWithMutex &operator=( const AllocationSetWithMutex & ) = delete;

        // Provide move constructor and assignment operator
        AllocationSetWithMutex( AllocationSetWithMutex &&other ) noexcept
            : allocationSet( std::move( other.allocationSet ) )
        {
            // Note: shared_mutex is not moved, each object gets its own mutex
        }

        AllocationSetWithMutex &operator=( AllocationSetWithMutex &&other ) noexcept
        {
            if ( this != &other )
            {
                allocationSet = std::move( other.allocationSet );
                // Note: shared_mutex is not moved, each object keeps its own mutex
            }
            return *this;
        }
    };

    /**
     * @var m_handleToNodeIdInVector
     * @brief A mapping between memory handles and node IDs.
     */
    std::map<QCMemoryHandle_t, uint32_t> m_handleToNodeIdInVector;

    /**
     * @var m_pools
     * @brief A vector of nodes with mapping between pool handles and pool instances, each with its
     * own shared_mutex.
     */
    std::vector<PoolMapWithMutex> m_pools;

    /**
     * @var m_allocations
     * @brief A vector of length of nodes containing set of allocated buffer descriptors, each with
     * its own shared_mutex.
     */
    std::vector<AllocationSetWithMutex> m_allocations;

    /**
     * @brief Checks if a handle is legal.
     * This method checks if a handle is legal by verifying that it is present in the
     * handle-to-node-ID mapping.
     * @param handle The handle to check.
     * @param nodeId Reference to store the node ID if handle is found.
     * @return True if the handle is legal, false otherwise.
     */
    inline bool IsMemoryHandleRegistered( const QCMemoryHandle_t &handle, uint8_t &nodeId );

    /**
     * @brief Checks if a node index is unique.
     * This method checks if node index is allready registered in m_handleToNodeIdInVector.
     * @param node The node structure content to check.
     * @return True if the node index is unique, false otherwise
     */
    inline bool IsNodeIdUnique( const QCNodeID_t &node );

    /**
     * @brief Declare the logger for this class.
     */
    QC_DECLARE_LOGGER();

    /**
     * @var m_multiThreadLock
     * @brief A mutex for synchronizing access from multiple threads to functions which requires
     * single thread.
     */
    std::mutex m_multiThreadLock;

    /**
     * @var m_handle2NodeIdLock
     * @brief A mutex for synchronizing access to the handle-to-node-ID mapping.
     */
    mutable std::shared_mutex m_handle2NodeIdLock;

    /**
     * @var m_config
     * @brief Initial configuration passed by user.
     */
    QCMemoryManagerInit_t m_config;
};

}   // namespace Memory
}   // namespace QC

#endif   // QC_MEMORY_MANAGER_LOCAL_HPP
