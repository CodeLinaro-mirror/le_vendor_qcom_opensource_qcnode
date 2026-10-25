
set( THREAD_LIBRARIES )
if( NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "QNX" )
    list( APPEND THREAD_LIBRARIES pthread )
endif()

add_executable( gtest_QCQueue gtest_QCQueue.cpp )
target_link_libraries( gtest_QCQueue gtest QCNodeLogger )
install(TARGETS gtest_QCQueue DESTINATION bin)

add_executable( gtest_QCVector gtest_QCVector.cpp )
target_link_libraries( gtest_QCVector gtest QCNodeLogger )
install(TARGETS gtest_QCVector DESTINATION bin)

add_executable( gtest_QCThreadSafeQueue gtest_QCThreadSafeQueue.cpp )
target_link_libraries( gtest_QCThreadSafeQueue gtest QCNodeLogger ${THREAD_LIBRARIES} )
install(TARGETS gtest_QCThreadSafeQueue DESTINATION bin)

add_executable( gtest_QCThreadSafeVector gtest_QCThreadSafeVector.cpp )
target_link_libraries( gtest_QCThreadSafeVector gtest QCNodeLogger ${THREAD_LIBRARIES} )
install(TARGETS gtest_QCThreadSafeVector DESTINATION bin)

add_executable( gtest_QCString gtest_QCString.cpp )
target_link_libraries( gtest_QCString gtest QCNodeLogger )
install(TARGETS gtest_QCString DESTINATION bin)

add_executable( gtest_QCMap gtest_QCMap.cpp )
target_link_libraries( gtest_QCMap gtest QCNodeLogger )
install(TARGETS gtest_QCMap DESTINATION bin)

add_executable( gtest_QCThreadSafeMap gtest_QCThreadSafeMap.cpp )
target_link_libraries( gtest_QCThreadSafeMap gtest QCNodeLogger ${THREAD_LIBRARIES} )
install(TARGETS gtest_QCThreadSafeMap DESTINATION bin)
