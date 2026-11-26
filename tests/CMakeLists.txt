add_executable(audit_modernization_test
    modernization_test.cpp
    ../ul3_log2json/src/events/auditd_field.cpp
    ../ul3_log2json/src/events/auditd_record.cpp
    ../ul3_log2json/src/events/auditd_event.cpp
    ../ul3_log2json/src/vars/namedefs.cpp
    ../ul3_log2json/src/globals.cpp)
target_include_directories(audit_modernization_test PRIVATE ../ul3_log2json/src)
audit_target(audit_modernization_test
    Mantids30_Core_Helpers
    Mantids30_Runtime_Logs
    Mantids30_Core_Threads
    Mantids30_DataFormat_JSON)
add_test(NAME audit_modernization COMMAND audit_modernization_test)

add_executable(audit_line_parser_test
    audit_line_parser_test.cpp
    ../ul3_log2json/src/input/auditlineparser.cpp)
target_include_directories(audit_line_parser_test PRIVATE ../ul3_log2json/src)
target_link_libraries(audit_line_parser_test PRIVATE Boost::regex)
add_test(NAME audit_line_parser COMMAND audit_line_parser_test)

find_program(AUDIT_NODE_EXECUTABLE node)
if(AUDIT_NODE_EXECUTABLE)
    add_test(NAME audit_web_client COMMAND "${AUDIT_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/web_client.test.js")
endif()

add_executable(syslog_modules_test
    syslog_modules_test.cpp
    ../ul3_log2json/src/input/auditlineparser.cpp
    ../ul3_log2json/src/input/syslogmodules.cpp
    ../ul3_log2json/src/events/event.cpp)
target_include_directories(syslog_modules_test PRIVATE ../ul3_log2json/src)
audit_target(syslog_modules_test Mantids30_DataFormat_JSON)
add_test(NAME syslog_modules COMMAND syslog_modules_test)
# The installed JSON library also loads Mantids30 dependencies from this prefix.
set_tests_properties(syslog_modules PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${Mantids30_DataFormat_JSON_LIBDIR}:$ENV{LD_LIBRARY_PATH}")

add_executable(endpoint_config_test
    endpoint_config_test.cpp
    ../ul3_log2json/src/endpointconfig.cpp)
target_include_directories(endpoint_config_test PRIVATE ../ul3_log2json/src)
audit_target(endpoint_config_test Mantids30_DataFormat_JSON)
add_test(NAME endpoint_config COMMAND endpoint_config_test)
set_tests_properties(endpoint_config PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${Mantids30_DataFormat_JSON_LIBDIR}:$ENV{LD_LIBRARY_PATH}")
if(AUDIT_NODE_EXECUTABLE)
    add_test(NAME endpoint_config_web COMMAND "${AUDIT_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/endpoint_config_web.test.js")
endif()
