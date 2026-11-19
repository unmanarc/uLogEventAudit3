/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/audit_class.
 */
#pragma once

#include "audit_var.h"

#include <audit_json.h>

#include <map>
#include <set>

namespace UANLZ::LOG2JSON::AuditdEvents {

/** @brief Own one auditd record type and its parsed fields. */
class AuditClass
{
public:
    /**
     * @brief Create an empty auditd record container.
     */
    AuditClass() = default;

    /**
     * @brief Return mutable access to the owned auditd record text.
     * @return Reference valid for the lifetime of this record.
     */
    std::string &getRawInput();
    /**
     * @brief Replace the owned auditd record text.
     * @param value Record text moved into this container.
     */
    void setRawInput(std::string value);

    /**
     * @brief Reparse record fields and reconstruct any contiguous indexed fragments.
     * @return True after reconstruction; orphan fragments are reported in the log.
     */
    bool parseVariables();

    /**
     * @brief Return the auditd record type, such as EXECVE or PATH.
     */
    [[nodiscard]] std::string getClassTypeName() const;
    /**
     * @brief Set the auditd record type used to interpret field values.
     * @param value Auditd record type name.
     */
    void setClassTypeName(const std::string &value);

    /**
     * @brief Serialize record fields, reconstructing argv and cmdline for EXECVE records.
     */
    boost::json::object getJson();

private:
    /**
     * @brief Decode one field and derive address metadata for SOCKADDR records.
     * @param variableName Auditd field name, including a fragment index when present.
     * @param variableValue Encoded auditd field text to decode.
     */
    void parseVariable(const std::string &variableName, const std::string &variableValue);
    /**
     * @brief Join indexed fragments starting at [0] and log orphan fragments.
     * @return True after attempting to merge contiguous fragments.
     */
    bool mergeSplitVariables();

    /**
     * @brief Return base field names with an available [0] fragment.
     */
    std::set<std::string> getSplitVariableNames();
    /**
     * @brief Build an indexed fragment name such as a0[1].
     * @param variableName Auditd field name, including a fragment index when present.
     * @param position Zero-based rule or fragment index.
     */
    std::string getChunkVariableName(const std::string &variableName, const size_t &position);

    std::string rawInput;
    std::string classTypeName;
    std::map<std::string, AuditVariable> variablesByName;
};

} // namespace UANLZ::LOG2JSON::AuditdEvents
