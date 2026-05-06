set(HEADERS_DIR ${PROJECT_SOURCE_DIR}/include)

set(QCNODE_SIMULATIONNODE_SOURCES
    SimulationNode.cpp
    SimulationNodeConfig.cpp
    SimulationNodeMonitoring.cpp
)

set(QCNODE_SIMULATIONNODE_DEFINITIONS QCNODE_SIMULATIONNODE_EXPORTS)

set(TARGET_LIBRARIES QCNodeBase)

add_library(QCNodeSimulationNode OBJECT ${QCNODE_SIMULATIONNODE_SOURCES})

set_property(TARGET QCNodeSimulationNode PROPERTY LINKER_LANGUAGE CXX)
set_property(TARGET QCNodeSimulationNode PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(QCNodeSimulationNode PUBLIC ${HEADERS_DIR})

target_link_libraries(QCNodeSimulationNode PUBLIC ${TARGET_LIBRARIES})

target_compile_definitions(QCNodeSimulationNode PRIVATE ${QCNODE_SIMULATIONNODE_DEFINITIONS})

install(FILES ${HEADERS_DIR}/QC/Node/SimulationNode.hpp
        DESTINATION include/QC/Node)
