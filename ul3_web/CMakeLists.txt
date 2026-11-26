project(ul3_web VERSION ${SVERSION} LANGUAGES CXX DESCRIPTION "uLogEventAudit3 ul3_web")
configure_file(src/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_executable(ul3_web ${SOURCES})
target_link_libraries(ul3_web PRIVATE audit_resource_sensing)
audit_target(ul3_web
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
install(TARGETS ul3_web RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY etc/ulogeventaudit3 DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
install(FILES etc/systemd/system/ul3_web.service DESTINATION lib/systemd/system)
foreach(asset css/bootstrap.min.css js/bootstrap.bundle.min.js js/jquery.min.js)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/var/www/ulogeventaudit3/global/assets/${asset}")
        message(FATAL_ERROR "Missing local web asset ${asset}. Run npm ci && npm run build-assets in ul3_web/var/www")
    endif()
endforeach()
install(DIRECTORY var/www/ulogeventaudit3 DESTINATION /var/www)
