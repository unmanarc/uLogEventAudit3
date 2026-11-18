#ifndef VARCONTENT_H
#define VARCONTENT_H

#include <string>
#include <cstdint>

namespace UANLZ { namespace LOG2JSON { namespace AuditdEvents {

class Audit_Var
{
public:
    Audit_Var() = default;

    // append.
    bool append(const Audit_Var & v);
    // parse.
    bool parse(const std::string &auditdEventType, const std::string & varName, const std::string & varValue);

    bool getValid() const;
    bool setValid(bool value);

    const char *getRawPure() const;
    const std::string &getFancy() const;
    size_t getRawPureSize() const;
    uint32_t asUInt32() const;

private:
    void createFancyString();

    bool valid = false;
    std::string fancy;
    std::string rawPure;
};

}}}

#endif // VARCONTENT_H
