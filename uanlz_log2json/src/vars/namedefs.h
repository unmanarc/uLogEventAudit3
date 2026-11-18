/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/vars/namedefs.
 */
#ifndef VARNAMEDEFINITIONS_H
#define VARNAMEDEFINITIONS_H

#include <set>
#include <string>

namespace UANLZ
{
namespace LOG2JSON
{
namespace AuditdEvents
{

/** @brief Keep the auditd text-field and terminal-record classification tables. */
class NameDefs
{
public:
    /**
     * @brief Classify auditd variables and terminal record types.
     */
    NameDefs();
    /**
     * @brief Populate the auditd text-field and terminal-record lookup sets.
     */
    static void init();
    /**
     * @brief Report whether an unquoted field should be decoded from hexadecimal text.
     * @param variableName Auditd field name, including a fragment index when present.
     * @return True if unquoted field values use hexadecimal text encoding.
     */
    static bool isTextVar(const std::string &variableName);
    /**
     * @brief Report whether a record type normally terminates an auditd event.
     * @param eventType Auditd record type controlling interpretation and event completion.
     * @return True for record types considered terminal by the parser.
     */
    static bool isEndEventType(const std::string &eventType);

private:
    static std::set<std::string> textVariableNames;
    static std::set<std::string> terminalEventTypes;
};

} // namespace AuditdEvents
} // namespace LOG2JSON
} // namespace UANLZ

#endif // VARNAMEDEFINITIONS_H
