project(ul3_log2json VERSION ${SVERSION} LANGUAGES CXX DESCRIPTION "uLogEventAudit3 ul3_log2json")
configure_file(src/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(ul3_log2json ${SOURCES})
target_link_libraries(ul3_log2json PRIVATE audit_resource_sensing)
audit_target(ul3_log2json
    Mantids30_Runtime_Service
    Mantids30_Runtime_Logs
    Mantids30_Core_Memory
    Mantids30_Core_Threads
    Mantids30_Core_Helpers
    Mantids30_Network_Sockets
    Mantids30_Protocol_FastRPC1
    Mantids30_DataFormat_JSON
    Mantids30_Protocol_LineRecv)
install(TARGETS ul3_log2json RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY etc/ulogeventaudit3 DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
install(FILES etc/systemd/system/ul3_log2json.service DESTINATION lib/systemd/system)
