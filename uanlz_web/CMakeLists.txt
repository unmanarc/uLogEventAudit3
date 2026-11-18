project(uanlz_web VERSION ${SVERSION} LANGUAGES CXX DESCRIPTION "Audit analyzer uanlz_web")
configure_file(src/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(uanlz_web ${SOURCES})
audit_target(uanlz_web
    Mantids30_Runtime_Service
    Mantids30_Runtime_Logs
    Mantids30_Core_Memory
    Mantids30_Core_Threads
    Mantids30_Core_Helpers
    Mantids30_Network_Sockets
    Mantids30_Protocol_FastRPC1
    Mantids30_DataFormat_JSON
    Mantids30_Server_WebRESTFul
    Mantids30_ConfigBuilder_Server_WebRESTFul
    Mantids30_ConfigBuilder_Runtime_Logs
    Mantids30_Client_APISync)
install(TARGETS uanlz_web RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY etc/uauditanalyzer DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
install(FILES etc/systemd/system/uanlz_web.service DESTINATION lib/systemd/system)
install(DIRECTORY var/www/uauditweb DESTINATION /var/www)
