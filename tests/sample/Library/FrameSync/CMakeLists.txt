
set( HEADERS_DIR
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include )

add_library( TimestampSync STATIC TimestampSync.cpp )
target_include_directories( TimestampSync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} )
set_property( TARGET TimestampSync PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET TimestampSync PROPERTY POSITION_INDEPENDENT_CODE ON )
target_compile_options( TimestampSync PRIVATE -Wall )

add_executable( gtest_FrameSync gtest_FrameSync.cpp )
target_include_directories( gtest_FrameSync PRIVATE ${HEADERS_DIR} )
target_link_libraries( gtest_FrameSync gtest_main gtest TimestampSync )
install( TARGETS gtest_FrameSync DESTINATION bin )
