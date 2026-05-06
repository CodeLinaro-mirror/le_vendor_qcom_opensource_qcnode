set( CMAKE_SYSTEM_NAME QNX )
set( CMAKE_SYSTEM_PROCESSOR aarch64 )

set( arch gcc_ntoaarch64le )

set( CMAKE_C_COMPILER aarch64-unknown-nto-qnx8.0.0-gcc )
set( CMAKE_C_COMPILER_TARGET ${arch} )
set( CMAKE_CXX_COMPILER aarch64-unknown-nto-qnx8.0.0-g++ )
set( CMAKE_CXX_COMPILER_TARGET ${arch} )

set( CMAKE_SYSROOT $ENV{QNX_TARGET}/aarch64le/ )

if( DEFINED ENV{BSP_ROOT} )
add_link_options("--sysroot=$ENV{BSP_ROOT}/install/aarch64le")
endif()

set( CMAKE_FIND_LIBRARY_PREFIXES lib )
set( CMAKE_FIND_LIBRARY_SUFFIXES .so )

# Usage:
#   find_header_dir(
#       OUT_VAR          <output variable name>
#       BASE_DIR         <base directory to search under>
#       NAMES            <header1.h> [header2.h ...]
#   )
#
# Behavior:
#   - OUT_VAR: list of *unique* directories that contain any of the headers
#   - If none found, OUT_VAR is set to empty (no error)

function(find_header_dir)
    set(options)
    set(oneValueArgs OUT_VAR BASE_DIR)
    set(multiValueArgs NAMES)
    cmake_parse_arguments(FHDR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT FHDR_OUT_VAR)
        message(FATAL_ERROR "find_header_dir: OUT_VAR is required")
    endif()
    if(NOT FHDR_BASE_DIR)
        message(FATAL_ERROR "find_header_dir: BASE_DIR is required")
    endif()
    if(NOT FHDR_NAMES)
        message(FATAL_ERROR "find_header_dir: NAMES is required")
    endif()

    # Build glob patterns for each header name
    set(_patterns)
    foreach(_hdr ${FHDR_NAMES})
        list(APPEND _patterns "${FHDR_BASE_DIR}/${_hdr}")
    endforeach()

    # Recursively search
    file(GLOB_RECURSE _FOUND_HEADERS ${_patterns})

    set(_DIRS)
    foreach(_file ${_FOUND_HEADERS})
        get_filename_component(_dir "${_file}" DIRECTORY)
        list(APPEND _DIRS "${_dir}")
    endforeach()

    # Remove duplicates
    if(_DIRS)
        list(REMOVE_DUPLICATES _DIRS)
        set(${FHDR_OUT_VAR} "${_DIRS}" PARENT_SCOPE)
    else()
        # No matches: return empty list
        set(${FHDR_OUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

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
if( DEFINED ENV{BSP_ROOT} )
list( APPEND QC_LIB_PATHS
    $ENV{BSP_ROOT}/install/aarch64le/lib
    $ENV{BSP_ROOT}/install/aarch64le/usr/lib
    $ENV{BSP_ROOT}/install/aarch64le/lib/camera_qcx
)
endif()

# common header files and libraries
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss )
include_directories( $ENV{BSP_ROOT}/install/aarch64le/usr/include )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/lib" )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/usr/lib" )
endif()

# pmem
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/inc )
endif()

# apdf
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/apdf/ )
include_directories( $ENV{QSDP_FIXME_ROOT}/target/qnx/usr/include )
include_directories( $ENV{QSDP_FIXME_ROOT}/target/qnx/usr/include/WF )
endif()

# c2d
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/graphics/include/private/C2D/ )
include_directories( $ENV{BSP_ROOT}/AMSS/inc/graphics/include/private/C2D/ )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/usr/lib/graphics/qc/" )
add_link_options( "-L$ENV{BSP_ROOT}/install_remote/aarch64le/lib" )
endif()
set( QC_C2D_LIBS c2d30 OSUser GSLUser planedef )

# vidc
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/inc )

find_header_dir(
    OUT_VAR VIDC_INCLUDE_DIR
    BASE_DIR $ENV{BSP_ROOT}/AMSS/multimedia/video
    NAMES vidc_ioctl.h vidc_types.h vidc_client.h
)
include_directories( ${VIDC_INCLUDE_DIR} )

find_header_dir(
    OUT_VAR VIDC_FILE_DEMUX_INCLUDE_DIR
    BASE_DIR $ENV{BSP_ROOT}/AMSS/multimedia/video
    NAMES filesource.h parserinternaldefs.h
)
include_directories( ${VIDC_FILE_DEMUX_INCLUDE_DIR} )

find_header_dir(
    OUT_VAR VIDC_FILE_DEMUX_INCLUDE_DIR
    BASE_DIR $ENV{BSP_ROOT}/test/multimedia/experimental/video
    NAMES filesource.h parserinternaldefs.h
)
include_directories( ${VIDC_FILE_DEMUX_INCLUDE_DIR} )

endif()
set( QC_VIDC_LIBS ioctlClient )
set( QC_VIDC_FILEDEMUX_LIBS
    FileDemux_Common
    FileBaseLib
    FileSource
    dal
    dalconfig
    AACParserLib
    AC3ParserLib
    AMRNBParserLib
    AMRWBParserLib
    ASFParserLib
    AVIParserLib
    FileDemux_Common
    EVRCBParserLib
    EVRCWBParserLib
    FLACParserLib
    ID3Lib
    MP2ParserLib
    MP3ParserLib
    OGGParserLib
    QCPParserLib
    RawParserLib
    SeekLib
    SeekTableLib
    VideoFMTReaderLib
    WAVParserLib
    ISOBaseFileLib
    MKAVParserLib
    AIFFParserLib
)

# qcarcam
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/qcamera/camera_qcx/cdk_qcx/api/qcarcam/ )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/camera_qcx )
add_link_options( "-L$ENV{BSP_ROOT}/install/aarch64le/lib/camera_qcx/" )
endif()

set( QC_CAMERA_EXTRA_LIBS )
find_and_append_library(
    OUT_LIST QC_CAMERA_EXTRA_LIBS
    NAMES xml2 memorylogger
    PATHS ${QC_LIB_PATHS}
)

# fadas
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/fadas/ )
include_directories( $ENV{BSP_ROOT}/prebuilt/usr/include/amss/multimedia/fadas/ )
endif()
set( QC_FADAS_EXTRA_LIBS )
find_and_append_library(
    OUT_LIST QC_FADAS_EXTRA_LIBS
    NAMES safe_xml
    PATHS ${QC_LIB_PATHS}
)

# OpenCL
if( DEFINED ENV{BSP_ROOT} )
include_directories( $ENV{BSP_ROOT}/AMSS/inc/graphics-fusa/include/public )
include_directories( $ENV{BSP_ROOT}/AMSS/multimedia/graphics-fusa-binaries/include/public )
endif()

# sv
if( DEFINED ENV{BSP_ROOT} AND ENABLE_EVA )
find_header_dir(
    OUT_VAR SV_AUTO_INCLUDE_DIR
    BASE_DIR $ENV{BSP_ROOT}/AMSS/multimedia/compute/sv
    NAMES svUtils.h svStereoDisparity.h svLme.h
)
include_directories( ${SV_AUTO_INCLUDE_DIR} )

include_directories( $ENV{BSP_ROOT}/install/usr/include/amss/multimedia/sv )
set( QC_SV_EXTRA_LIBS softsku smmu_client pm_client )
endif()

# c2c
set( QC_C2C_EXTRA_LIBS )
find_and_append_library(
    OUT_LIST QC_C2C_EXTRA_LIBS
    NAMES rc_client ep_client mhi_client
    PATHS ${QC_LIB_PATHS}
)

# gtest
include_directories( $ENV{QCNODE_INSTALL_DIR}/opt/qcnode/include )
add_link_options( "-L$ENV{QCNODE_INSTALL_DIR}/opt/qcnode/lib" )
link_libraries( regex )
