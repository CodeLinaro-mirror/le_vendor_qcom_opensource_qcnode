set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( NODEBASE_HEADERS
    ${HEADERS_DIR}/QC/Node/NodeBase.hpp
    ${HEADERS_DIR}/QC/Node/NodeConfigBase.hpp
    ${HEADERS_DIR}/QC/Node/NodeFrameDescriptor.hpp
    ${HEADERS_DIR}/QC/Node/NodeFrameDescriptorPool.hpp
    ${HEADERS_DIR}/QC/Node/Ifs/QCNodeFactory.hpp
)

set( NODEBASE_SOURCES
    NodeBase.cpp
    DataTree.cpp
    NodeFrameDescriptor.cpp
    NodeFrameDescriptorPool.cpp
    QCNodeFactory.cpp
)

# Set libraries based on build type
if(ENABLE_SIMNODE)
    # For simulation build, only include QCNodeCommon
    set( TARGET_LIBRARIES QCNodeCommon )
else()
    # For on-target build, include QCNodeCommon and QCNodeVideoCodec
    set( TARGET_LIBRARIES QCNodeCommon QCNodeVideoCodec )
endif()

if ( ENABLE_TRACE )
    list( APPEND TARGET_LIBRARIES QCNodeTrace )
endif()

add_library( QCNodeBase STATIC ${NODEBASE_SOURCES} )
set_property( TARGET QCNodeBase PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeBase PROPERTY POSITION_INDEPENDENT_CODE ON )

find_package(nlohmann_json 3.2.0 REQUIRED)
list( APPEND TARGET_LIBRARIES nlohmann_json::nlohmann_json)

target_link_libraries( QCNodeBase PUBLIC ${TARGET_LIBRARIES} )
target_compile_options( QCNodeBase PRIVATE -Wall )

install( FILES ${NODEBASE_HEADERS} DESTINATION include/QC/Node )
install( FILES  ${HEADERS_DIR}/QC/Common/DataTree.hpp
                ${HEADERS_DIR}/QC/Common/QCDefs.hpp
                DESTINATION include/QC/Common )
