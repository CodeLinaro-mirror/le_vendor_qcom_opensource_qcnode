# Build OpticalFlowMock as a static library so the gtest binary has no runtime dependency on libOpticalFlowMock.so.
# (We still dlopen("libsvcl.so") at runtime for pass-through.)
add_library( OpticalFlowMock STATIC OpticalFlowMock.cpp )
set_property( TARGET OpticalFlowMock PROPERTY LINKER_LANGUAGE CXX )

target_include_directories( OpticalFlowMock PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/tests/utils
    ${CMAKE_CURRENT_LIST_DIR}
)

# No install needed for static-only unit-test linkage; keep it local to the build.
