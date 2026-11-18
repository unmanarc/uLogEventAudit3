#include "audit_var.h"
#include "../vars/namedefs.h"
#include <boost/algorithm/string.hpp>

using namespace boost::algorithm;
using namespace std;
using namespace UANLZ::LOG2JSON::AuditdEvents;


bool Audit_Var::append(const Audit_Var &v)
{
    if (!v.getValid()) return setValid(false);
    rawPure += v.rawPure;
    fancy += v.fancy;
    return setValid(true);
}

inline bool isHexUpperString(const char *value)
{
    for ( ;value && *value;value++ )
    {
        if (!(*value>='A' && *value<='F') && !(*value>='0' && *value<='9'))
            return false;
    }
    return true;
}

inline bool isNumeric(const char value)
{
    return (value>='0' && value<='9');
}


inline unsigned char fromHex8(const char value)
{
    if (value>='A' && value<='F') return (value-'A')+10;
    else if (value>='0' && value<='9') return (value-'0');
    else if (value>='a' && value<='f') return (value-'a')+10;
    return 0;
}


inline bool isATextVar( const string &auditdEventType, const string &varName )
{
    return (auditdEventType == "EXECVE" && varName.size()>=2 && varName.at(0)=='a' && isNumeric(varName.at(1)) && !ends_with(varName,"n"));
}

unsigned char fromHex16(const char *value)
{
    return fromHex8(value[0])*0x10 + fromHex8(value[1]);
}

void Audit_Var::createFancyString()
{
    fancy = rawPure;
    for (char &byte : fancy)
    {
        if (byte == '\0' || byte == '!') byte = ' ';
    }
}

size_t Audit_Var::getRawPureSize() const
{
    return rawPure.size();
}

uint32_t Audit_Var::asUInt32() const
{
    return strtoul(fancy.c_str(),0,10);
}

const string &Audit_Var::getFancy() const
{
    return fancy;
}


const char *Audit_Var::getRawPure() const
{
    return rawPure.c_str();
}


bool Audit_Var::getValid() const
{
    return valid;
}

bool Audit_Var::setValid(bool value)
{
    valid = value;
    return valid;
}

bool Audit_Var::parse(const string &auditdEventType, const string &varName, const string &varValue)
{
    rawPure.clear();
    fancy.clear();
    if (varValue.empty()) return setValid(false);

    if (    (varValue.at(0) == '\'' && varValue.size()>2 && varValue.at(varValue.size()-1)=='\'')
            || (varValue.at(0) == '\"' && varValue.size()>2 && varValue.at(varValue.size()-1)=='\"')
            || (varValue.at(0) == '(' && varValue.size()>2 && varValue.at(varValue.size()-1)==')')
            || (varValue.at(0) == '{' && varValue.size()>2 && varValue.at(varValue.size()-1)=='}'
                )
            )
    {
        rawPure = varValue.substr(1, varValue.size() - 2);

        createFancyString();

        return setValid(true);
    }
    else if ( NameDefs::isTextVar(varName) || isATextVar( auditdEventType, varName ) ) // at this point, if this is text var, then should be HEX (previous if statement check any using "'{ ).
    {
        if (isHexUpperString(varValue.c_str()) && varValue.size()%2==0)
        {
            rawPure.resize(varValue.size() / 2);
            for (size_t i = 0; i < rawPure.size(); ++i)
                rawPure[i] = fromHex16(varValue.data() + 2 * i);

            createFancyString();
            return setValid(true);
        }
        else
        {
            return setValid(false);
        }
    }
    else // Take the value as is.
    {
        rawPure = varValue;
        createFancyString();
        return setValid(true);
    }

    return setValid(false);
}
