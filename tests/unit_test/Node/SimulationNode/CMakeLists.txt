if( NOT TARGET QCNodeSimulationNode )
    return()
endif()

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)


set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/utils)
add_executable( gtest_NodeSimulationNode gtest_SimulationNode_parameterized.cpp )

target_include_directories(gtest_NodeSimulationNode PUBLIC
    ${GTEST_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries( gtest_NodeSimulationNode
    ${GTEST_LIBRARIES}
    QCNode
    QCNodeMemory
    Threads::Threads
)

install(TARGETS gtest_NodeSimulationNode DESTINATION bin)
