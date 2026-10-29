set( HEADERS_DIR
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library( NodeFrameDescriptorPool STATIC NodeFrameDescriptorPool.cpp )

target_include_directories( NodeFrameDescriptorPool PUBLIC ${HEADERS_DIR} ${CMAKE_CURRENT_SOURCE_DIR} )

set_property( TARGET NodeFrameDescriptorPool PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET NodeFrameDescriptorPool PROPERTY POSITION_INDEPENDENT_CODE ON )

target_link_libraries( NodeFrameDescriptorPool PUBLIC QCNodeBase )
target_compile_options( NodeFrameDescriptorPool PRIVATE -Wall )
