
set(HEADERS_DIR ${PROJECT_SOURCE_DIR}/include)

set(MEMORY_HEADERS
    ${HEADERS_DIR}/QC/Infras/Memory/BufferDescriptor.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/ImageDescriptor.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/TensorDescriptor.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/CameraBufferDescriptor.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryAllocatorIfs.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryDefs.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryManagerIfs.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryPoolIfs.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryUtilsIfs.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/HeapAllocator.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/ManagerLocal.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/UtilsBase.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/VideoFrameDescriptor.hpp
    ${HEADERS_DIR}/QC/Infras/Memory/Ifs/QCMemoryFactory.hpp
)

# Use the ENABLE_SIMNODE option to conditionally include source files
if(ENABLE_SIMNODE)
    # For simulation build, only include HeapAllocator.cpp
    set(MEMORY_SOURCES
        HeapAllocator.cpp
    )

    # Set minimal libraries for simulation
    set(TARGET_LIBRARIES QCNodeCommon)
else()
    # For on-target build, include all source files
    set(MEMORY_SOURCES
        BufferDescriptor.cpp
        ImageDescriptor.cpp
        TensorDescriptor.cpp
        CameraBufferDescriptor.cpp
        ManagerLocal.cpp
        HeapAllocator.cpp
        UtilsBase.cpp
        VideoFrameDescriptor.cpp
        QCMemoryFactory.cpp
    )

    # Set full libraries for on-target build
    set(TARGET_LIBRARIES QCNodeCommon apdf aosal)
endif()

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
    list(APPEND MEMORY_HEADERS
        ${HEADERS_DIR}/QC/Infras/Memory/DMABUFFAllocator.hpp
        ${HEADERS_DIR}/QC/Infras/Memory/DMABUFFUtils.hpp
    )
else()
    list(APPEND MEMORY_HEADERS
        ${HEADERS_DIR}/QC/Infras/Memory/PMEMAllocator.hpp
        ${HEADERS_DIR}/QC/Infras/Memory/PMEMUtils.hpp
    )
endif()

# Handle platform-specific source files - only include if not in simulation mode
if(NOT ENABLE_SIMNODE)
    if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
        list(APPEND MEMORY_SOURCES
            DMABUFFAllocator.cpp
            DMABUFFUtils.cpp
        )

        # Add Linux-specific libraries and include directories
        list(APPEND TARGET_LIBRARIES kiumd)
        list(APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/WF)
        list(APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/linux-ark)
    else()
        list(APPEND MEMORY_SOURCES
            PMEMAllocator.cpp
            PMEMUtils.cpp
        )

        # Add QNX-specific libraries
        list(APPEND TARGET_LIBRARIES pmem_client pmemext fastrpc_pmem mmap_peer OSAbstraction)
    endif()
endif()

add_library(QCNodeMemory OBJECT ${MEMORY_SOURCES})

set_property(TARGET QCNodeMemory PROPERTY LINKER_LANGUAGE CXX)
set_property(TARGET QCNodeMemory PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(QCNodeMemory PUBLIC ${HEADERS_DIR})

target_link_libraries(QCNodeMemory PUBLIC ${TARGET_LIBRARIES})

target_compile_options(QCNodeMemory PRIVATE -Wall)

install(FILES ${MEMORY_HEADERS} DESTINATION include/QC/Infras/Memory)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    target_include_directories(QCNodeMemory PUBLIC ${CMAKE_SYSROOT}/usr/include/mm-osal/include)
    target_compile_definitions(QCNodeMemory PUBLIC _VIDC_LRH_LINUX_)
endif()