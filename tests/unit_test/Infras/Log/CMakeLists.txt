
add_executable( gtest_Logger gtest_Logger.cpp )
target_link_libraries( gtest_Logger gtest QCNodeLogger )

if( NOT ENABLE_CTC )
    target_link_libraries( gtest_Logger QCNodeTestMockCLib )
else()
    target_compile_definitions( gtest_Logger PRIVATE __CTC__ )
endif()

install(TARGETS gtest_Logger DESTINATION bin)
