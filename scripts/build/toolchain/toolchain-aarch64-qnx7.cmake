set( CMAKE_SYSTEM_NAME QNX )
set( CMAKE_SYSTEM_PROCESSOR aarch64 )

set( arch gcc_ntoaarch64le )

set( CMAKE_C_COMPILER aarch64-unknown-nto-qnx7.1.0-gcc )
set( CMAKE_C_COMPILER_TARGET ${arch} )
set( CMAKE_CXX_COMPILER aarch64-unknown-nto-qnx7.1.0-g++ )
set( CMAKE_CXX_COMPILER_TARGET ${arch} )

set( CMAKE_SYSROOT $ENV{QNX_TARGET}/aarch64le/ )

if( DEFINED ENV{BSP_ROOT} )
add_link_options("--sysroot=$ENV{BSP_ROOT}/install/aarch64le")
endif()

set( CMAKE_FIND_LIBRARY_PREFIXES lib )
set( CMAKE_FIND_LIBRARY_SUFFIXES .so )

# Usage:
#   find_and_append_library(
#       OUT_LIST         <output list variable>
#       NAMES            <library name> [library name ...]
#       PATHS            <paths to search>
#   )
function(find_and_append_library)
    set(options)
    set(oneValueArgs OUT_LIST)
    set(multiValueArgs NAMES PATHS)
    cmake_parse_arguments(FAL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT FAL_OUT_LIST)
        message(FATAL_ERROR "find_and_append_library: OUT_LIST is required")
    endif()
    if(NOT FAL_NAMES)
        message(FATAL_ERROR "find_and_append_library: NAMES is required")
    endif()

    foreach(_lib_name ${FAL_NAMES})
        string(TOUPPER "${_lib_name}" _VAR_NAME)
        set(_VAR_NAME "${_VAR_NAME}_LIB")

        find_library(${_VAR_NAME}
            NAMES ${_lib_name}
            PATHS ${FAL_PATHS}
        )

        if(${_VAR_NAME})
            list(APPEND ${FAL_OUT_LIST} "${${_VAR_NAME}}")
        else()
            message(INFO "${_lib_name} is not found, build without it")
        endif()
    endforeach()
    set(${FAL_OUT_LIST} "${${FAL_OUT_LIST}}" PARENT_SCOPE)
endfunction()

set( QC_LIB_PATHS
    ${CMAKE_SYSROOT}/lib
    ${CMAKE_SYSROOT}/usr/lib
)

# common header files
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/inc )
include_directories( $ENV{BSP_ROOT}/install/usr/include )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss )
endif()

# apdf
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/apdf/ )
include_directories( $ENV{BSP_ROOT}/install/usr/include/WF )
include_directories( $ENV{QNX_ROOT}_patches/target/qnx7/usr/include )
include_directories( $ENV{QNX_ROOT}_patches/target/qnx7/usr/include/WF )
endif()

# c2d
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/graphics/include/private/C2D/ )
include_directories( $ENV{BSP_ROOT}/AMSS/inc/graphics/include/private/C2D/ )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/usr/lib/graphics/qc/" )
add_link_options( "-L$ENV{BSP_ROOT}/install_remote/aarch64le/lib" )
endif()
set( QC_C2D_LIBS c2d30 OSUser GSLUser planedef )

# rsm_v2
if( ENABLE_RSM_V2 )
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss )
endif()
endif()

# vidc
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/inc/ )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/video/source/common/drivers/inc/ )
endif()
set( QC_VIDC_LIBS ioctlClient )
set( QC_VIDC_FILEDEMUX_LIBS
    FileDemux_Common FileSource dal
)

# qcarcam
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/qcamera/camera_qcx/cdk_qcx/api/qcarcam/ )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/camera_qcx )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/lib/camera_qcx/" )
endif()
set( QC_CAMERA_EXTRA_LIBS xml2 )

# gtest
include_directories( $ENV{QCNODE_INSTALL_DIR}/opt/qcnode/include )
add_link_options( "-L$ENV{QCNODE_INSTALL_DIR}/opt/qcnode/lib" )
link_libraries( regex )

# fadas
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/fadas/fadas/inc/ )
include_directories( $ENV{BSP_ROOT}/AMSS/platform/qal/clients/fastrpc_lib/inc )
include_directories( $ENV{BSP_ROOT}/install_remote/usr/include/amss/multimedia/fadas/ )
endif()
set( QC_FADAS_EXTRA_LIBS safe_xml )

# OpenCL
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/graphics/include/public )
include_directories( $ENV{BSP_ROOT}/AMSS/inc/graphics/include/public )
endif()
set( QC_CL_EXTRA_LIBS OSUser GSLUser )

# eva
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/eva )
include_directories( $ENV{BSP_ROOT}/install_remote/usr/include/amss/multimedia/eva/ )
endif()
set( QC_EVA_EXTRA_LIBS cdsprpc smmu_client npa_client clock_client icb_client )

# c2c
set( QC_C2C_EXTRA_LIBS )
find_and_append_library(
    OUT_LIST QC_C2C_EXTRA_LIBS
    NAMES rc_client ep_client mhi_client
    PATHS ${QC_LIB_PATHS}
)
