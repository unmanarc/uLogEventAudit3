project(uanlz_alert VERSION ${SVERSION} LANGUAGES CXX DESCRIPTION "Audit analyzer uanlz_alert")
configure_file(src/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(uanlz_alert ${SOURCES})
audit_target(uanlz_alert
    Mantids30_Runtime_Service
    Mantids30_Runtime_Logs
    Mantids30_Core_Memory
    Mantids30_Core_Threads
    Mantids30_Core_Helpers
    Mantids30_Network_Sockets
    Mantids30_Protocol_FastRPC1
    Mantids30_DataFormat_JSON
    Mantids30_Protocol_LineRecv
    Mantids30_Scripting_JSONExprEval)
install(TARGETS uanlz_alert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY etc/uauditanalyzer3 DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
install(FILES etc/systemd/system/uanlz_alert.service DESTINATION lib/systemd/system)
