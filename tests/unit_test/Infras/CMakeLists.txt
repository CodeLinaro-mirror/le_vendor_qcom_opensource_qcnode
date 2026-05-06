add_subdirectory(Log)

if(NOT ENABLE_SIMNODE)
    add_subdirectory(Memory)
endif()