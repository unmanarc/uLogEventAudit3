/**
 * @file
 * @brief Implementation for uanlz_log2json/src/vars/namedefs.
 */
#include "namedefs.h"

using namespace UANLZ::LOG2JSON::AuditdEvents;

std::set<std::string> NameDefs::textVariableNames;
std::set<std::string> NameDefs::terminalEventTypes;

NameDefs::NameDefs() {}

void NameDefs::init()
{
    textVariableNames.insert("acct");
    textVariableNames.insert("cmd");
    textVariableNames.insert("comm");
    textVariableNames.insert("cwd");

    textVariableNames.insert("exe");
    textVariableNames.insert("grp");
    textVariableNames.insert("key");
    textVariableNames.insert("name");
    textVariableNames.insert("path");
    textVariableNames.insert("proctitle");
    textVariableNames.insert("reason");

    // Keep the legacy completion table: multi-record events commonly end with PROCTITLE.
    terminalEventTypes.insert("SYSTEM_BOOT");
    terminalEventTypes.insert("ADD_USER");
    terminalEventTypes.insert("ANOM_ABEND");

    //  AVC->ENDS WITH PROCTITLE.
    //  BPRM_FCAPS->ENDS WITH PROCTITLE.
    terminalEventTypes.insert("CONFIG_CHANGE");
    terminalEventTypes.insert("CRED_ACQ");
    terminalEventTypes.insert("CRED_DISP");
    terminalEventTypes.insert("CRED_REFR");
    terminalEventTypes.insert("CRYPTO_KEY_USER");
    terminalEventTypes.insert("CRYPTO_SESSION");
    terminalEventTypes.insert("DAEMON_START");

    terminalEventTypes.insert("LOGIN");

    terminalEventTypes.insert("PROCTITLE");
    terminalEventTypes.insert("SERVICE_START");
    terminalEventTypes.insert("SERVICE_STOP");

    terminalEventTypes.insert("USER_ACCT");
    terminalEventTypes.insert("USER_AUTH");
    terminalEventTypes.insert("USER_CHAUTHTOK");
    terminalEventTypes.insert("USER_CMD");
    terminalEventTypes.insert("USER_END");
    terminalEventTypes.insert("USER_ERR");
    terminalEventTypes.insert("USER_LOGIN");
    terminalEventTypes.insert("USER_LOGOUT");
    terminalEventTypes.insert("USER_MGMT");
    terminalEventTypes.insert("USER_ROLE_CHANGE");
    terminalEventTypes.insert("USER_START");
}

bool NameDefs::isTextVar(const std::string &variableName)
{
    return textVariableNames.find(variableName) != textVariableNames.end();
}

bool NameDefs::isEndEventType(const std::string &eventType)
{
    return terminalEventTypes.find(eventType) != terminalEventTypes.end();
}
