set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( QC_NODE_RADAR_HEADERS
    ${HEADERS_DIR}/QC/Node/Radar.hpp
)

set( TARGET_LIBRARIES
    QCNodeBase
)

# Platform-specific libraries
if( NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" )
    list( APPEND TARGET_LIBRARIES c )
endif()

set( QC_NODE_RADAR_SOURCES
    Radar.cpp
    RadarImpl.cpp
)

add_library( QCNodeNodeRadar OBJECT ${QC_NODE_RADAR_SOURCES} )

set_property( TARGET QCNodeNodeRadar PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeNodeRadar PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNodeNodeRadar PUBLIC ${HEADERS_DIR} )
target_include_directories( QCNodeNodeRadar PRIVATE ${PROJECT_SOURCE_DIR}/source/Library/RadarIface )

target_link_libraries( QCNodeNodeRadar PUBLIC ${TARGET_LIBRARIES} )

install( FILES ${QC_NODE_RADAR_HEADERS} DESTINATION include/QC/Node )
