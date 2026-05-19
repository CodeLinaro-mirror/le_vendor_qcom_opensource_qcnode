set( HEADERS_DIR
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/utils
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface
    ${CMAKE_CURRENT_SOURCE_DIR}/Mock
    ${PROJECT_SOURCE_DIR}/tests/unit_test/Library/FadasIfaceCore/Mock
)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR ${CMAKE_SYSROOT}/usr/include/amss/multimedia/fadas )
endif()

add_executable( gtest_FadasIface
    gtest_FadasIface.cpp
    Mock/FadasMock.cpp
    Mock/FadasSrvStubs.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface/FadasIfaceSafe.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface/FadasRemap.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface/FadasPlr.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/FadasIface/FadasSrv.cpp
)
target_include_directories( gtest_FadasIface PUBLIC ${HEADERS_DIR} )
target_link_libraries( gtest_FadasIface gtest QCNodeTestMockCLib QCNodeCommon)
target_link_options(gtest_FadasIface PRIVATE -Wl,--wrap=dlopen -Wl,--wrap=dlsym)
install( TARGETS gtest_FadasIface DESTINATION bin )

