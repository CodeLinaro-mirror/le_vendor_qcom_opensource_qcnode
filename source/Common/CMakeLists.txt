set( HEADERS_DIR ${PROJECT_SOURCE_DIR}/include )

set( COMMON_HEADERS
    ${HEADERS_DIR}/QC/Common/QCMap.hpp
    ${HEADERS_DIR}/QC/Common/QCQueue.hpp
    ${HEADERS_DIR}/QC/Common/QCRingPool.hpp
    ${HEADERS_DIR}/QC/Common/QCRingPoolAllocator.hpp
    ${HEADERS_DIR}/QC/Common/QCString.hpp
    ${HEADERS_DIR}/QC/Common/QCThreadSafeMap.hpp
    ${HEADERS_DIR}/QC/Common/QCThreadSafeQueue.hpp
    ${HEADERS_DIR}/QC/Common/QCThreadSafeVector.hpp
    ${HEADERS_DIR}/QC/Common/QCVector.hpp
    ${HEADERS_DIR}/QC/Common/Types.hpp
)

install( FILES ${COMMON_HEADERS} DESTINATION include/QC/Common )
