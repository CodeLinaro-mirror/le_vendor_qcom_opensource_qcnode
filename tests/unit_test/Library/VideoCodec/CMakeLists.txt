set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/include/QC/Library/VideoCodec/ ${PROJECT_SOURCE_DIR}/source/Library/VideoCodec  )

set( VIDEOCODEC_HEADERS
    ${PROJECT_SOURCE_DIR}/source/Library/VideoCodec/VidcDrvClient.hpp
    ${PROJECT_SOURCE_DIR}/source/Library/VideoCodec/VidcNodeBase.hpp
    ${PROJECT_SOURCE_DIR}/tests/unit_test/Library/VideoCodec/vidc_driver_mockup.hpp
)

set( VIDEOCODEC_SOURCES
    ${PROJECT_SOURCE_DIR}/source/Library/VideoCodec/VidcDrvClient.cpp
    ${PROJECT_SOURCE_DIR}/source/Library/VideoCodec/VidcNodeBase.cpp
)

set( TARGET_LIBRARIES QCNodeMemory )

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND TARGET_LIBRARIES vidc vidc_client mmosal )
endif()

if( DEFINED QC_VIDC_LIBS )
    list( APPEND TARGET_LIBRARIES ${QC_VIDC_LIBS} )
endif()

add_library( QCNodeVideoCodecMockup SHARED ${VIDEOCODEC_SOURCES} vidc_driver_mockup.cpp )

target_link_options(QCNodeVideoCodecMockup PRIVATE
  -Wl,--wrap=device_open
  -Wl,--wrap=device_ioctl
  -Wl,--wrap=device_close
  -Wl,--wrap=MM_Timer_Sleep
)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
target_include_directories( QCNodeVideoCodecMockup PUBLIC ${CMAKE_SYSROOT}/usr/include/mm-osal/include )
target_compile_definitions( QCNodeVideoCodecMockup PUBLIC _VIDC_LRH_LINUX_ )
endif()

set_property( TARGET QCNodeVideoCodecMockup PROPERTY LINKER_LANGUAGE CXX )
set_property( TARGET QCNodeVideoCodecMockup PROPERTY POSITION_INDEPENDENT_CODE ON )

target_include_directories( QCNodeVideoCodecMockup PUBLIC ${HEADERS_DIR} )
target_link_libraries( QCNodeVideoCodecMockup PUBLIC ${TARGET_LIBRARIES} )
target_compile_options( QCNodeVideoCodecMockup PRIVATE -Wno-error=deprecated-declarations )

set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/tests/utils ${PROJECT_SOURCE_DIR}/tests/sample/Library/VidcDemuxer/include)

install(TARGETS QCNodeVideoCodecMockup DESTINATION lib)

add_executable( gtest_VidcDrvClient gtest_VidcDrvClient.cpp )
target_link_libraries( gtest_VidcDrvClient gtest QCNodeVideoCodecMockup QCNodeCommon QCNode QCNodeTestUtils )
install(TARGETS gtest_VidcDrvClient DESTINATION bin)

add_executable( gtest_VidcNodeBase gtest_VidcNodeBase.cpp )
target_link_libraries( gtest_VidcNodeBase gtest QCNodeVideoCodecMockup QCNodeCommon QCNode QCNodeTestUtils )
install(TARGETS gtest_VidcNodeBase DESTINATION bin)