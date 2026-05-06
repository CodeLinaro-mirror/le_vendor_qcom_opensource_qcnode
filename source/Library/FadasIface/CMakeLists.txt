list( APPEND HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )
list( APPEND HEADERS_DIR ${PROJECT_SOURCE_DIR}/source/Library/FadasIface )
if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
    list( APPEND HEADERS_DIR  ${CMAKE_SYSROOT}/usr/include/amss/multimedia/fadas )
endif()

set( FADASIFACE_HEADERS
    FadasIface.h
    FadasSrv.hpp
    FadasRemap.hpp
    FadasPlr.hpp
)

add_library(FadasIfaceStub SHARED)
target_sources(FadasIfaceStub PRIVATE FadasIfaceSafe.cpp FadasIfaceSafe.hpp FadasIface.h FadasIface.c)
target_include_directories(FadasIfaceStub PUBLIC ${HEADERS_DIR})
target_compile_definitions(FadasIfaceStub PUBLIC VARIANT_le_ BUILDENV_qss)

if( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
  target_link_libraries(FadasIfaceStub PUBLIC cdsprpc safetylibs)
else()
  target_link_libraries(FadasIfaceStub PUBLIC cdsprpc FuSa-CRC32)
endif()

install(TARGETS FadasIfaceStub DESTINATION lib)

add_library(FadasIface SHARED FadasSrv.cpp FadasRemap.cpp FadasPlr.cpp)
target_include_directories(FadasIface PUBLIC ${HEADERS_DIR})
target_compile_definitions(FadasIface PUBLIC USE_FADAS_DSP)

set(TARGET_LIBRARIES FadasIfaceStub fadas)
if( DEFINED QC_FADAS_EXTRA_LIBS )
    list( APPEND TARGET_LIBRARIES ${QC_FADAS_EXTRA_LIBS} )
endif()

target_link_libraries(FadasIface PUBLIC ${TARGET_LIBRARIES})
install(TARGETS FadasIface DESTINATION lib)

if(QC_TARGET_SOC STREQUAL "8797")
  if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    install(FILES prebuilt/dsp/v81/libFadasIface_skel.so DESTINATION lib/dsp)
  else()
    install(FILES prebuilt/dsp/v73/libFadasIface_skel.so DESTINATION lib/dsp)
  endif()
elseif(QC_TARGET_SOC STREQUAL "8650")
  install(FILES prebuilt/dsp/v73/libFadasIface_skel.so DESTINATION lib/dsp)
elseif(QC_TARGET_SOC STREQUAL "8295")
  install(FILES prebuilt/dsp/v68/libFadasIface_skel.so DESTINATION lib/dsp)
elseif(QC_TARGET_SOC STREQUAL "8775")
  install(FILES prebuilt/dsp/v75/libFadasIface_skel.so DESTINATION lib/dsp)
else()
  install(FILES prebuilt/dsp/v73/libFadasIface_skel.so DESTINATION lib/dsp)
endif()

install( FILES ${FADASIFACE_HEADERS} DESTINATION include/QC/Library/FadasIface )

