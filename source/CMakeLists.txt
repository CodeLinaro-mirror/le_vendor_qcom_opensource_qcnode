add_subdirectory(Common)
add_subdirectory(Infras)
add_subdirectory(Library)
add_subdirectory(Node)

set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

file( WRITE ${CMAKE_BINARY_DIR}/null.cpp "" )

set( QCNODE_SOURCES ${CMAKE_BINARY_DIR}/null.cpp )

set( TARGET_LIBRARIES
    QCNodeCommon
    QCNodeMemory
    QCNodeBase
)

# Add regular nodes if not in simulation mode
if( NOT ENABLE_SIMNODE )
    list( APPEND TARGET_LIBRARIES
        QCNodeCamera
        QCNodeCL2DFlex
        QCNodeVideoEncoder
        QCNodeVideoDecoder
    )

    if ( ENABLE_EVA )
      list( APPEND TARGET_LIBRARIES
                QCNodeOpticalFlow
                QCNodeDepthFromStereo
      )
    endif()

    if ( ENABLE_FADAS )
      list( APPEND TARGET_LIBRARIES
                QCNodeRemap
                QCNodeVoxelization
      )
    endif()

    if ( ENABLE_RADAR )
      list( APPEND TARGET_LIBRARIES
                QCNodeNodeRadar
      )
    endif()

    if( NOT TARGET QCNodeQNN )
      message(STATUS "build QCNode without QNN")
    else()
      list( APPEND TARGET_LIBRARIES QCNodeQNN )
    endif()
else()
    # Add SimulationNode when in simulation mode
    list( APPEND TARGET_LIBRARIES QCNodeSimulationNode )
endif()

add_library( QCNode SHARED ${QCNODE_SOURCES} )

set_property( TARGET QCNode PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNode PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNode PUBLIC ${HEADERS_DIR} )

target_link_libraries( QCNode PUBLIC ${TARGET_LIBRARIES} )

install( TARGETS QCNode DESTINATION lib )
