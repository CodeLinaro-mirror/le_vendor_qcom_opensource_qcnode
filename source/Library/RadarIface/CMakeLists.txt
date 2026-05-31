add_library( QCNodeRadarIface INTERFACE )

target_include_directories( QCNodeRadarIface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} )

set( RADAR_IFACE_HEADERS
    IRadarIf.hpp
    RadarIf.hxx
)

install( FILES ${RADAR_IFACE_HEADERS} DESTINATION include/QC/Library/RadarIface )
