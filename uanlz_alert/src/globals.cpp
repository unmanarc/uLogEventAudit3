/**
 * @file
 * @brief Implementation for uanlz_alert/src/globals.
 */
#include "globals.h"

using namespace UANLZ::JSONALERT;

std::unique_ptr<Mantids30::Runtime::Logs::AppLog> Globals::appLog;
std::mutex Globals::databaseMutex, Globals::directoriesMutex;
boost::property_tree::ptree Globals::mainConfig;

Globals::Globals() = default;

Mantids30::Runtime::Logs::AppLog *Globals::getAppLog()
{
    return appLog.get();
}

void Globals::setAppLog(std::unique_ptr<Mantids30::Runtime::Logs::AppLog> value)
{
    appLog = std::move(value);
}

boost::property_tree::ptree *Globals::getMainConfig()
{
    return &mainConfig;
}
