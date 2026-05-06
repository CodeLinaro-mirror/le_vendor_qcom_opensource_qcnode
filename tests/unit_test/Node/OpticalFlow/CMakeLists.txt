if( NOT TARGET QCNodeOpticalFlow )
    return()
endif()

add_subdirectory(Mock)

set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/utils)
add_executable( gtest_NodeOpticalFlow gtest_OpticalFlow.cpp )
target_include_directories( gtest_NodeOpticalFlow PUBLIC ${HEADERS_DIR})
target_link_libraries( gtest_NodeOpticalFlow gtest QCNode QCNodeTestUtils BufferManager)
install(TARGETS gtest_NodeOpticalFlow DESTINATION bin)

# OpticalFlow Code Coverage test
add_executable( gtest_NodeOpticalFlow_CodeCoverage gtest_OpticalFlow_CodeCoverage.cpp )
target_include_directories( gtest_NodeOpticalFlow_CodeCoverage PUBLIC ${HEADERS_DIR})
target_link_libraries( gtest_NodeOpticalFlow_CodeCoverage gtest QCNode QCNodeTestUtils BufferManager OpticalFlowMock)
install(TARGETS gtest_NodeOpticalFlow_CodeCoverage DESTINATION bin)
