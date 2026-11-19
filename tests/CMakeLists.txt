add_executable(audit_modernization_test
    modernization_test.cpp
    ../ul3_log2json/src/events/audit_var.cpp
    ../ul3_log2json/src/events/audit_class.cpp
    ../ul3_log2json/src/events/audit_event.cpp
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
