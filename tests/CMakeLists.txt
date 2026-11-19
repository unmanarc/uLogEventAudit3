add_executable(audit_modernization_test
    modernization_test.cpp
    ../uanlz_log2json/src/events/audit_var.cpp
    ../uanlz_log2json/src/events/audit_class.cpp
    ../uanlz_log2json/src/events/audit_event.cpp
    ../uanlz_log2json/src/vars/namedefs.cpp
    ../uanlz_log2json/src/globals.cpp)
target_include_directories(audit_modernization_test PRIVATE ../uanlz_log2json/src)
audit_target(audit_modernization_test
    Mantids30_Core_Helpers
    Mantids30_Runtime_Logs
    Mantids30_Core_Threads
    Mantids30_DataFormat_JSON)
add_test(NAME audit_modernization COMMAND audit_modernization_test)

add_executable(audit_line_parser_test
    audit_line_parser_test.cpp
    ../uanlz_log2json/src/input/auditlineparser.cpp)
target_include_directories(audit_line_parser_test PRIVATE ../uanlz_log2json/src)
target_link_libraries(audit_line_parser_test PRIVATE Boost::regex)
add_test(NAME audit_line_parser COMMAND audit_line_parser_test)
