// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * @file QCMemoryManagerIfs.hpp
 * @brief Interface definitions for the QC memory management system.
 *
 * This header defines the core interfaces and data structures for the QC memory management
 * subsystem. It provides:
 * - Handle types for identifying memory resources with proper comparison semantics
 * - Initialization structure for memory managers with copy semantics
 * - Abstract interface for memory manager implementations with state management
 *
 * The design emphasizes:
 * - Type safety through dedicated handle classes
 * - Clear ownership semantics via reference wrappers
 * - State management to prevent invalid operations
 * - Thread-safe operation requirements documented for each method
 *
 * @warning All handle types are process-specific and should not be shared across processes.
 *          The pid field in handles helps prevent accidental cross-process usage.
 */


#ifndef QC_MEMORY_MANAGER_IFS_HPP
#define QC_MEMORY_MANAGER_IFS_HPP

#include "QC/Common/QCDefs.hpp"
#include "QC/Node/Ifs/QCNodeDefs.hpp"
#include "QCMemoryAllocatorIfs.hpp"
#include "QCMemoryPoolIfs.hpp"
#include <array>
#include <functional>

namespace QC
{
namespace Memory
{

/**
 * @struct QCMemoryHandle
 * @brief Type-safe identifier for memory resources associated with nodes.
 *
 * This class provides a unique identifier for memory allocations with the following properties:
 * - Encapsulates component fields rather than using raw bit manipulation
 * - Provides proper comparison operators for container usage
 * - Maintains process isolation via PID inclusion
 * - Ensures type safety (prevents accidental mixing with other handle types)
 *
 * The handle structure:
 * - nodeType: Type of the node that owns the memory (e.g., AI, DSP)
 * - nodeCount: Instance count for nodes of the same type
 * - randomNumber: Random value to ensure handle uniqueness
 * - pid: Process ID to prevent cross-process handle confusion
 *
 * @note This implementation uses separate fields rather than bit-packed values for:
 *       - Better type safety
 *       - Easier debugging
 *       - More maintainable code
 *       - Natural comparison semantics
 *
 * @warning Handles are only valid within the process that created them. The pid field
 *          helps detect accidental cross-process usage but is not a security measure.
 */
typedef struct QCMemoryHandle
{
    /**
     * @brief Default constructor.
     *
     * Creates an invalid handle with all fields initialized to safe default values.
     * This handle should not be used for actual operations until properly initialized.
     */
    QCMemoryHandle()
        : nodeType( QC_NODE_TYPE_RESERVED ),
          nodeCount( 0 ),
          randomNumber( 0 ),
          pid( 0 )
    {}

    /**
     * @brief Copy constructor.
     */
    QCMemoryHandle( const QCMemoryHandle &other ) = default;

    /**
     * @brief Assignment operator.
     *
     * Copies all fields from the source handle to this handle.
     * Enables assignment operations like: handle1 = handle2;
     *
     * @param other The memory handle to copy from
     * @return Reference to this handle for assignment chaining
     *
     * @note This is a shallow copy that copies field values
     * @note Handles self-assignment safely
     */
    QCMemoryHandle &operator=( const QCMemoryHandle &other ) = default;

    /**
     * @brief Less-than comparison operator.
     *
     * Establishes a strict weak ordering of memory handles by comparing component
     * fields in priority order. This enables use as a key in sorted containers
     * like std::map and std::set.
     *
     * @param other The other memory handle to compare with
     * @return true if this handle is strictly less than the other handle
     *
     * @note Comparison hierarchy (most significant to least):
     *       1. nodeType
     *       2. nodeCount
     *       3. randomNumber
     *       4. pid
     *
     * @note This implementation provides the strict weak ordering required by
     *       the C++ standard for sorted containers.
     */
    bool operator<( const QCMemoryHandle &other ) const
    {
        if ( nodeType != other.nodeType )
        {
            return nodeType < other.nodeType;
        }
        if ( nodeCount != other.nodeCount )
        {
            return nodeCount < other.nodeCount;
        }
        if ( randomNumber != other.randomNumber )
        {
            return randomNumber < other.randomNumber;
        }
        return pid < other.pid;
    }

    /**
     * @brief Equality operator.
     *
     * Determines if two memory handles are identical by comparing all component fields.
     *
     * @param other The other memory handle to compare with
     * @return true if this handle equals the other handle
     */
    bool operator==( const QCMemoryHandle &other ) const
    {
        return ( nodeType == other.nodeType ) && ( nodeCount == other.nodeCount ) &&
               ( randomNumber == other.randomNumber ) && ( pid == other.pid );
    }

    /**
     * @brief Inequality operator.
     *
     * Determines if two memory handles are different.
     *
     * @param other The other memory handle to compare with
     * @return true if this handle is not equal to the other handle
     */
    bool operator!=( const QCMemoryHandle &other ) const { return !( *this == other ); }

    /**
     * @brief Gets the node type from the handle.
     *
     * @return The node type as a QCNodeType_e enum value
     *
     * @note This is a const method for safe use with const handles
     */
    QCNodeType_e GetNodeType() const noexcept { return nodeType; }

    /**
     * @brief Gets the node count from the handle.
     *
     * @return The instance count as an 8-bit unsigned integer
     *
     * @note This is a const method for safe use with const handles
     */
    uint8_t GetNodeCount() const noexcept { return nodeCount; }

    /**
     * @brief Gets the random number from the handle.
     *
     * @return The random number as a 32-bit unsigned integer
     *
     * @note This is a const method for safe use with const handles
     */
    uint32_t GetRandomNumber() const noexcept { return randomNumber; }

    /**
     * @brief Gets the process ID from the handle.
     *
     * @return The process ID as a pid_t type
     *
     * @note This is a const method for safe use with const handles
     * @warning The pid helps prevent cross-process handle confusion but is not a security measure
     */
    pid_t GetProcessId() const noexcept { return pid; }

    /**
     * @brief Sets the node type in the handle.
     *
     * @param nodeType_ The new node type to set
     * @return Reference to this handle for method chaining
     *
     * @note This method returns a non-const reference to enable fluent interface
     */
    QCMemoryHandle &SetNodeType( QCNodeType_e nodeType_ ) noexcept
    {
        nodeType = nodeType_;
        return *this;
    }

    /**
     * @brief Sets the node count in the handle.
     *
     * @param nodeCount_ The new node count to set
     * @return Reference to this handle for method chaining
     */
    QCMemoryHandle &SetNodeCount( uint8_t nodeCount_ ) noexcept
    {
        nodeCount = nodeCount_;
        return *this;
    }

    /**
     * @brief Sets the random number in the handle.
     *
     * @param randomNumber_ The new random number to set
     * @return Reference to this handle for method chaining
     */
    QCMemoryHandle &SetRandomNumber( uint32_t randomNumber_ ) noexcept
    {
        randomNumber = randomNumber_;
        return *this;
    }

    /**
     * @brief Sets the process ID in the handle.
     *
     * @param pid_ The new process ID to set
     * @return Reference to this handle for method chaining
     *
     * @warning Normally this should be the current process ID (getpid())
     *          Setting a different PID is generally not recommended
     */
    QCMemoryHandle &SetProcessId( pid_t pid_ ) noexcept
    {
        pid = pid_;
        return *this;
    }

private:
    QCNodeType_e nodeType;   ///< Type of node that owns the memory
    uint8_t nodeCount;       ///< Instance count for nodes of the same type
    uint32_t randomNumber;   ///< Random value to ensure handle uniqueness
    pid_t pid;               ///< Process ID for cross-process isolation
} QCMemoryHandle_t;

/**
 * @class QCMemoryPoolHandle
 * @brief Type-safe identifier for memory pools.
 *
 * This class provides a unique identifier for memory pools with the following properties:
 * - Contains a reference to the owning node's memory handle
 * - Includes pool-specific identifiers (count and random number)
 * - Provides proper comparison operators for container usage
 * - Maintains the relationship between pools and their owning nodes
 *
 * The handle structure:
 * - memoryHandle: Reference to the node that owns the pool
 * - poolCount: Count of pools for this node
 * - randomNumber: Random value to ensure handle uniqueness
 *
 * @note This implementation separates the node reference from pool-specific data
 *       for better modularity and maintainability.
 *
 * @warning Pool handles are only valid while their owning node is registered.
 *          Unregistering a node automatically invalidates all its pool handles.
 */
typedef struct QCMemoryPoolHandle
{
    /**
     * @brief Default constructor.
     *
     * Creates an invalid handle with default values.
     */
    QCMemoryPoolHandle() : poolCount( 0 ), randomNumber( 0 ) {}

    /**
     * @brief Copy constructor.
     */
    QCMemoryPoolHandle( const QCMemoryPoolHandle &other ) = default;

    /**
     * @brief Assignment operator.
     */
    QCMemoryPoolHandle &operator=( const QCMemoryPoolHandle &other ) = default;

    /**
     * @brief Less-than comparison operator.
     *
     * Establishes a strict weak ordering for pool handles.
     *
     * @param other The other pool handle to compare with
     * @return true if this handle is strictly less than the other handle
     *
     * @note Comparison hierarchy:
     *       1. memoryHandle (delegates to QCMemoryHandle comparison)
     *       2. poolCount
     *       3. randomNumber
     */
    bool operator<( const QCMemoryPoolHandle &other ) const
    {
        if ( memoryHandle != other.memoryHandle )
        {
            return memoryHandle < other.memoryHandle;
        }
        if ( poolCount != other.poolCount )
        {
            return poolCount < other.poolCount;
        }
        return randomNumber < other.randomNumber;
    }

    /**
     * @brief Equality operator.
     */
    bool operator==( const QCMemoryPoolHandle &other ) const
    {
        return ( poolCount == other.poolCount ) && ( randomNumber == other.randomNumber ) &&
               ( memoryHandle == other.memoryHandle );
    }

    /**
     * @brief Gets the pool count from the handle.
     */
    uint8_t GetPoolCount() const noexcept { return poolCount; }

    /**
     * @brief Gets the random number from the handle.
     */
    uint32_t GetRandomNumber() const noexcept { return randomNumber; }

    /**
     * @brief Gets the memory handle of the owning node.
     */
    const QCMemoryHandle_t &GetMemoryHandle() const noexcept { return memoryHandle; }


    /**
     * @brief Sets the pool count in the handle.
     */
    QCMemoryPoolHandle &SetPoolCount( uint8_t poolCount_ ) noexcept
    {
        poolCount = poolCount_;
        return *this;
    }

    /**
     * @brief Sets the random number in the handle.
     */
    QCMemoryPoolHandle &SetRandomNumber( uint32_t randomNumber_ ) noexcept
    {
        randomNumber = randomNumber_;
        return *this;
    }

    /**
     * @brief Sets the memory handle of the owning node.
     */
    QCMemoryPoolHandle &SetMemoryHandle( const QCMemoryHandle_t &memoryHandle_ ) noexcept
    {
        memoryHandle = memoryHandle_;
        return *this;
    }

private:
    uint8_t poolCount;               ///< Count of pools for this node
    uint32_t randomNumber;           ///< Random value for uniqueness
    QCMemoryHandle_t memoryHandle;   ///< Handle of the owning node
} QCMemoryPoolHandle_t;

/**
 * @struct QCMemoryManagerInit
 * @brief Configuration structure for initializing a memory manager.
 *
 * This structure provides the necessary information to initialize a memory manager instance.
 * It follows the copyable-configuration pattern where:
 * - The configuration can be copied and stored
 * - The actual resources (allocators) are referenced, not owned
 * - The configuration remains valid as long as referenced resources exist
 *
 * @note This is a value type with proper copy semantics, designed to be:
 *       - Easily stored in containers
 *       - Passed by value
 *       - Compared for equality
 *
 * @warning The memory manager does NOT take ownership of the allocators.
 *          The caller must ensure allocator objects outlive any memory manager
 *          instances that reference them.
 */
typedef struct QCMemoryManagerInit
{
    /**
     * @brief Default constructor.
     *
     * Creates an invalid configuration with 0 nodes and dummy allocators.
     * This configuration is only suitable for default construction needs
     * (e.g., in containers) and must be properly initialized before use.
     */
    QCMemoryManagerInit()
        : numOfNodes( 0 ),
          allocators( { s_dummyAllocator, s_dummyAllocator, s_dummyAllocator, s_dummyAllocator,
                        s_dummyAllocator, s_dummyAllocator, s_dummyAllocator } )
    {}

    /**
     * @brief Constructor for a valid configuration.
     *
     * @param nodeCount Number of nodes the manager should support
     * @param allocatorRefs Array of allocator references
     *
     * @warning The caller must ensure allocator objects outlive any memory manager
     *          instances that reference them.
     */
    QCMemoryManagerInit(
            uint8_t numOfNodes,
            std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST>
                    allocators )
        : numOfNodes( numOfNodes ),
          allocators( allocators )
    {}

    /**
     * @brief Copy constructor.
     */
    QCMemoryManagerInit( const QCMemoryManagerInit &other ) = default;

    /**
     * @brief Assignment operator.
     *
     * Performs a shallow copy of the configuration, maintaining references
     * to the same allocator objects.
     *
     * @param other Configuration to copy from
     * @return Reference to this configuration
     *
     * @note This is a shallow copy - both configurations will reference
     *       the same allocator objects.
     */
    QCMemoryManagerInit &operator=( const QCMemoryManagerInit &other ) = default;


    /**
     * @var numOfNodes
     * @brief The number of nodes in the system.
     */
    uint8_t numOfNodes = 0;

    /**
     * @var allocators
     * @brief The array of memory allocators.
     */
    std::array<std::reference_wrapper<QCMemoryAllocatorIfs>, QC_MEMORY_ALLOCATOR_LAST> allocators;

private:
    DummyAllocator s_dummyAllocator;

} QCMemoryManagerInit_t;

/**
 * @struct QCMemoryPoolInitConfig_t
 * @brief Structure for configuring a memory pool.
 * This structure provides the necessary information to configure a memory pool via memory manager
 * interface, including the properties of individual buffers in the pool, the maximum number of
 * elements, the name of the pool, and memory allocator enum used by the pool.
 */
typedef struct QCMemoryPoolInitConfig
{
    /**
     * @brief Deleted default constructor.
     * This constructor is deleted to prevent default initialization of the QCMemoryPoolConfig
     * structure.
     */
    QCMemoryPoolInitConfig() : maxElements( 0 ), allocator( QC_MEMORY_ALLOCATOR_LAST ) {};

    /**
     * @var buff
     * @brief Properties of individual buffers in the pool.
     * This structure specifies the properties of each buffer in the pool,
     * such as size, alignment, and cache attributes.
     */
    QCBufferPropBase_t buff;

    /**
     * @var maxElements
     * @brief Maximum number of elements in the pool.
     * This value specifies the maximum number of buffers that can be allocated from the pool.
     * A value of 0 indicates that the pool has no limit on the number of elements.
     */
    QCCount_t maxElements;

    /**
     * @var name
     * @brief Name of the pool.
     * This string specifies the name of the pool, which can be used for identification and
     * debugging purposes.
     */
    std::string name;

    /**
     * @var allocator
     * @brief enumerator indicationg the allocator used by the pool.
     * This enumerator specifies the allocator type that will be used to allocate and release memory
     * for the pool.
     */
    QCMemoryAllocator_e allocator;
} QCMemoryPoolInitConfig_t;


/**
 * @class QCMemoryManagerIfs
 * @brief Abstract interface for memory manager implementations.
 *
 * This interface defines the contract for memory manager implementations, providing methods for:
 * - System initialization and cleanup
 * - Node registration and management
 * - Memory pool creation and management
 * - Buffer allocation and deallocation
 * - Resource reclamation
 *
 * The interface follows these key design principles:
 * - State management to prevent invalid operations
 * - Handle-based resource identification
 * - Separation of memory pools from direct allocations
 * - Clear error reporting through QCStatus_e
 *
 * @note Implementations must be thread-safe unless otherwise documented.
 *       The interface does not specify internal synchronization mechanisms,
 *       but all public methods must be safe for concurrent use.
 *
 * @warning Memory managers do NOT take ownership of allocator objects.
 *          The caller must ensure allocator objects outlive the memory manager.
 *
 * @see ManagerLocal - A concrete implementation of this interface for local memory management
 */
class QCMemoryManagerIfs
{

public:
    /**
     * @brief Virtual destructor.
     *
     * Ensures proper cleanup of derived class resources when deleted
     * through a base class pointer.
     */
    virtual ~QCMemoryManagerIfs() = default;

    /**
     * @brief Initializes the memory manager.
     *
     * Sets up internal data structures based on the provided configuration.
     * This must be called before any other operations on the memory manager.
     *
     * Thread Safety: Must be thread-safe; implementations should handle concurrent initialization
     * attempts.
     *
     * @param init Configuration parameters for initialization
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note After successful initialization, the manager state becomes QC_OBJECT_STATE_READY
     * @warning Calling Initialize() on an already initialized manager returns QC_STATUS_BAD_STATE
     * @note The manager does NOT take ownership of allocator objects in the configuration
     */
    virtual QCStatus_e Initialize( const QCMemoryManagerInit_t &init ) = 0;

    /**
     * @brief Deinitializes the memory manager.
     *
     * Cleans up all resources and returns the manager to an uninitialized state.
     * After this call, Initialize() must be called again before using the manager.
     *
     * Thread Safety: Must be thread-safe; implementations should handle concurrent deinitialization
     * attempts.
     *
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note All registered nodes and pools are automatically destroyed during deinitialization
     * @warning Calling DeInitialize() on an uninitialized manager returns QC_STATUS_BAD_STATE
     * @note The manager will attempt to reclaim all resources before deinitialization
     */
    virtual QCStatus_e DeInitialize() = 0;

    /**
     * @brief Registers a node with the memory manager.
     *
     * Associates a node ID with a unique memory handle that can be used for subsequent operations.
     *
     * Thread Safety: Must be thread-safe; multiple threads may register nodes concurrently.
     *
     * @param node Node ID to register
     * @param handle Output parameter receiving the unique memory handle
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note The handle incorporates the node type, instance count, and random number
     * @warning Registering more nodes than configured (numOfNodes) returns QC_STATUS_BAD_ARGUMENTS
     * @note Each node must be unregistered before deinitialization
     */
    virtual QCStatus_e Register( const QCNodeID_t &node, QCMemoryHandle_t &handle ) = 0;

    /**
     * @brief Unregisters a node from the memory manager.
     *
     * Removes a previously registered node and cleans up associated resources.
     * Any pools or buffers associated with the node are automatically destroyed/freed.
     *
     * Thread Safety: Must be thread-safe; multiple threads may unregister nodes concurrently.
     *
     * @param handle Memory handle of the node to unregister
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @warning Unregistering a node with active allocations may return QC_STATUS_RESOURCE_IN_USE
     * @note The method automatically calls ReclaimResources() for the node
     */
    virtual QCStatus_e UnRegister( const QCMemoryHandle_t &memHandle ) = 0;

    /**
     * @brief Creates a memory pool.
     *
     * Creates a new memory pool with the specified configuration for the given node.
     *
     * Thread Safety: Must be thread-safe; multiple threads may create pools concurrently.
     *
     * @param handle Memory handle of the owning node
     * @param poolCfg Configuration for the new pool
     * @param poolHandle Output parameter receiving the unique pool handle
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note Pool handles incorporate the owning node's handle for context
     * @warning Creating more pools than UINT8_MAX per node returns QC_STATUS_OUT_OF_BOUND
     * @note The pool is automatically destroyed when the owning node is unregistered
     */
    virtual QCStatus_e CreatePool( const QCMemoryHandle_t &handle,
                                   const QCMemoryPoolInitConfig_t &poolCfg,
                                   QCMemoryPoolHandle_t &poolHandle ) = 0;

    /**
     * @brief Destroys a memory pool.
     *
     * Destroys a previously created memory pool and releases all associated resources.
     * Any buffers still allocated from the pool will be automatically freed.
     *
     * Thread Safety: Must be thread-safe; multiple threads may destroy pools concurrently.
     *
     * @param poolHandle Pool handle to destroy
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @warning Destroying a pool with active allocations returns QC_STATUS_RESOURCE_IN_USE
     * @note The method automatically frees all allocated buffers from the pool
     */
    virtual QCStatus_e DestroyPool( const QCMemoryPoolHandle_t &poolHandle ) = 0;

    /**
     * @brief Allocates a buffer from a memory pool.
     *
     * Retrieves an available buffer from the specified memory pool.
     *
     * Thread Safety: Must be thread-safe; multiple threads may allocate from the same pool
     * concurrently.
     *
     * @param poolHandle Pool handle to allocate from
     * @param buffer Output parameter receiving the buffer descriptor
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note The buffer descriptor contains all necessary information for usage and deallocation
     * @warning Allocating from a destroyed pool returns QC_STATUS_BAD_ARGUMENTS
     * @note The buffer must be returned to the same pool via PutBufferToPool()
     */
    virtual QCStatus_e AllocateBufferFromPool( const QCMemoryPoolHandle_t &poolHandle,
                                               QCBufferDescriptorBase_t &buff ) = 0;

    /**
     * @brief Returns a buffer to its memory pool.
     *
     * Returns a previously allocated buffer back to its originating pool.
     *
     * Thread Safety: Must be thread-safe; multiple threads may return buffers to the same pool
     * concurrently.
     *
     * @param poolHandle Pool handle the buffer belongs to
     * @param buffer Buffer descriptor to return
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @warning Returning a buffer to the wrong pool returns QC_STATUS_BAD_ARGUMENTS
     * @warning Returning an invalid buffer descriptor returns QC_STATUS_BAD_ARGUMENTS
     */
    virtual QCStatus_e PutBufferToPool( const QCMemoryPoolHandle_t &poolHandle,
                                        const QCBufferDescriptorBase_t &buff ) = 0;

    /**
     * @brief Allocates a buffer directly from an allocator.
     *
     * Allocates a new buffer using the specified allocator type.
     *
     * Thread Safety: Must be thread-safe; multiple threads may allocate concurrently.
     *
     * @param handle Memory handle of the requesting node
     * @param allocator Type of allocator to use
     * @param request Buffer properties specification
     * @param buffer Output parameter receiving the buffer descriptor
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note This is for long-lived allocations not suitable for pooling
     * @warning Direct allocations must be freed with FreeBuffer(), not PutBufferToPool()
     * @note The buffer descriptor contains allocator type for proper deallocation
     */
    virtual QCStatus_e AllocateBuffer( const QCMemoryHandle_t handle,
                                       const QCMemoryAllocator_e allocator,
                                       const QCBufferPropBase_t &request,
                                       QCBufferDescriptorBase_t &buff ) = 0;

    /**
     * @brief Frees a buffer allocated directly from an allocator.
     *
     * Releases a buffer that was previously allocated using AllocateBuffer.
     *
     * Thread Safety: Must be thread-safe; multiple threads may free buffers concurrently.
     *
     * @param handle Memory handle of the requesting node
     * @param buffer Buffer descriptor to free
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @warning Freeing a buffer allocated from a pool returns QC_STATUS_BAD_ARGUMENTS
     * @warning Freeing an invalid buffer descriptor returns QC_STATUS_BAD_ARGUMENTS
     */
    virtual QCStatus_e FreeBuffer( const QCMemoryHandle_t handle,
                                   const QCBufferDescriptorBase_t &buff ) = 0;

    /**
     * @brief Reclaims resources associated with a node.
     *
     * Attempts to reclaim resources that may be held by the specified node.
     * Implementation-specific behavior, but typically:
     * - Frees all direct allocations
     * - Destroys all pools (after freeing their allocations)
     *
     * Thread Safety: Must be thread-safe; multiple threads may request reclamation concurrently.
     *
     * @param handle Memory handle of the node to reclaim resources for
     * @return QC_STATUS_OK on success, appropriate error code otherwise
     *
     * @note This is effectively a "garbage collection" operation for the node
     * @warning May fail if resources are in active use (QC_STATUS_RESOURCE_IN_USE)
     * @note Called automatically during UnRegister() and DeInitialize()
     */
    virtual QCStatus_e ReclaimResources( const QCMemoryHandle_t &handle ) = 0;

    /**
     * @brief Gets the current state of the memory manager.
     *
     * @return Current state of the memory manager
     *
     * Valid states:
     * - QC_OBJECT_STATE_INITIAL: After construction, before Initialize()
     * - QC_OBJECT_STATE_INITIALIZING: During initialization
     * - QC_OBJECT_STATE_READY: Fully initialized and ready for use
     * - QC_OBJECT_STATE_DEINITIALIZING: During deinitialization
     * - QC_OBJECT_STATE_ERROR: An error has occurred
     *
     * @note This is a const method for safe use with const managers
     */
    QCObjectState_e GetState() const noexcept { return m_state; }

protected:
    /**
     * @brief Protected constructor to prevent direct instantiation.
     *
     * Memory managers must be implemented as concrete classes.
     */
    QCMemoryManagerIfs() : m_state( QC_OBJECT_STATE_INITIAL ) {}

    /**
     * @brief Sets the state of the memory manager.
     *
     * @param state New state to set
     */
    void SetState( QCObjectState_e state ) noexcept { m_state = state; }

private:
    /**
     * @var m_state
     * @brief Dsecribes the state of the memory manager.
     */
    QCObjectState_e m_state = QC_OBJECT_STATE_INITIAL;   ///< Creation state of the memory manager
};

}   // namespace Memory
}   // namespace QC

#endif   // QC_MEMORY_MANAGER_IFS_HPP
