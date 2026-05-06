
set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include
                 ${PROJECT_SOURCE_DIR}/tests/utils 
                 ${PROJECT_SOURCE_DIR}/source/Library/FadasIface
                 ${CMAKE_CURRENT_SOURCE_DIR}/Mock
)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR  ${CMAKE_SYSROOT}/usr/include/amss/multimedia/fadas )
endif()

add_executable( gtest_FadasIfaceCore
    gtest_FadasIfaceCore.cpp
    Mock/FadasMock.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface/src/FadasIfaceCore.cpp )
target_include_directories( gtest_FadasIfaceCore PUBLIC ${HEADERS_DIR} )
target_link_libraries( gtest_FadasIfaceCore gtest QCNodeTestMockCLib )
install( TARGETS gtest_FadasIfaceCore DESTINATION bin )
