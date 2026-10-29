# gtest_QCMemoryFactory.cpp is part of gtest_Memory instead of being a separate
# executable. As a standalone binary its CTC counters were flushed by its own
# main() via ctc_append_all(), so whenever that process did not terminate
# cleanly the whole QCMemoryFactory.cpp translation unit was reported as 0%
# coverage. Linking it into gtest_Memory makes its counters part of that
# process, which is already exercised by the coverage run.
#
# Both gtest_Memory.cpp and gtest_QCMemoryFactory.cpp define main() guarded by
# #ifndef GTEST_QCNODE, so GTEST_QCNODE is defined below to keep exactly one
# main() (the one in gtest_Memory.cpp) and avoid a duplicate symbol at link time.
if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    add_executable( gtest_Memory
        gtest_Memory.cpp
        gtest_QCMemoryManager.cpp
        gtest_QCHEAPMemoryAllocator.cpp
        gtest_QCDMABUFFMemoryAllocator.cpp
        gtest_QCMemoryPool.cpp
        gtest_QCMemoryUtilsBase.cpp
        gtest_QCMemoryFactory.cpp)
else()
    add_executable( gtest_Memory
        gtest_Memory.cpp
        gtest_QCMemoryManager.cpp
        gtest_QCHEAPMemoryAllocator.cpp
        gtest_QCPMEMMemoryAllocator.cpp
        gtest_QCMemoryPool.cpp
        gtest_QCMemoryUtilsBase.cpp
        gtest_QCMemoryFactory.cpp)
endif()

# Suppress the main() in gtest_QCMemoryFactory.cpp; gtest_Memory.cpp provides it.
set_source_files_properties( gtest_QCMemoryFactory.cpp
    PROPERTIES COMPILE_DEFINITIONS GTEST_QCNODE )

target_link_libraries( gtest_Memory gtest QCNodeLogger QCNodeMemory QCNodeTestMockCLib BufferManager )
install(TARGETS gtest_Memory DESTINATION bin)
