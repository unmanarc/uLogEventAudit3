/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/events/audit_var.
 */
#ifndef VARCONTENT_H
#define VARCONTENT_H

#include <string>
#include <cstdint>

namespace UANLZ
{
namespace LOG2JSON
{
namespace AuditdEvents
{

/** @brief Own a decoded auditd field and its printable representation. */
class AuditVariable
{
public:
    /**
     * @brief Create an empty, invalid audit variable.
     */
    AuditVariable() = default;

    /**
     * @brief Append a decoded fragment while retaining both binary and display representations.
     * @param other Decoded fragment to append; this variable becomes invalid if the fragment is invalid.
     * @return False if the source fragment is invalid; otherwise true after concatenation.
     */
    bool append(const AuditVariable &other);

    /**
     * @brief Replace this variable with a quoted, hexadecimal or literal auditd field value.
     * @param eventType Auditd record type controlling interpretation and event completion.
     * @param variableName Auditd field name, including a fragment index when present.
     * @param variableValue Encoded auditd field text to decode.
     * @return True when the encoded field was decoded; false for an invalid field.
     */
    bool parse(const std::string &eventType, const std::string &variableName, const std::string &variableValue);

    /**
     * @brief Report whether this variable currently contains a successfully decoded value.
     * @return The current decoded-value validity flag.
     */
    bool isValid() const;
    /**
     * @brief Set and return the validity flag used by parsing and fragment reconstruction.
     * @param isValid Validity flag to store and return.
     * @return The flag supplied by the caller.
     */
    bool setValid(bool isValid);

    /**
     * @brief Return a borrowed buffer containing the decoded bytes, including any embedded NULs.
     * @return Borrowed data; use getRawValueSize() instead of strlen() for binary values.
     */
    const char *getRawValue() const;
    /**
     * @brief Return a borrowed display string with NUL and exclamation bytes replaced by spaces.
     * @return Reference valid until this variable is changed or destroyed.
     */
    const std::string &getDisplayValue() const;
    /**
     * @brief Return the number of decoded bytes, excluding the trailing string terminator.
     */
    size_t getRawValueSize() const;
    /**
     * @brief Convert the display string using the existing base-10 strtoul semantics.
     * @return Decimal conversion narrowed to uint32_t; this method does not validate overflow.
     */
    uint32_t asUint32() const;

private:
    /**
     * @brief Rebuild the display string from the decoded binary value.
     */
    void updateDisplayValue();

    bool valid = false;
    std::string displayValue;
    std::string rawValue;
};

} // namespace AuditdEvents
} // namespace LOG2JSON
} // namespace UANLZ

#endif // VARCONTENT_H
