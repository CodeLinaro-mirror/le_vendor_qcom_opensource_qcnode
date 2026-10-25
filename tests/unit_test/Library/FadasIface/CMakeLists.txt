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
target_link_libraries( gtest_FadasIface gtest QCNodeTestMockCLib QCNodeLogger)
target_link_options(gtest_FadasIface PRIVATE -Wl,--wrap=dlopen -Wl,--wrap=dlsym)
include( CheckSymbolExists )
check_symbol_exists( strlcpy "string.h" HAVE_STRLCPY )
if( NOT HAVE_STRLCPY )
    target_compile_definitions( gtest_FadasIface PRIVATE strlcpy=strncpy )
endif()
if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    target_link_options(gtest_FadasIface PRIVATE -Wl,--wrap=sched_getaffinity)
else()
    target_link_options(gtest_FadasIface PRIVATE -Wl,--wrap=ThreadCtl)
endif()
install( TARGETS gtest_FadasIface DESTINATION bin )
