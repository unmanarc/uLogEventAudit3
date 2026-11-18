#include "globals.h"

using namespace UANLZ::JSONALERT;

Mantids30::Runtime::Logs::AppLog * Globals::applog = nullptr;
std::mutex Globals::mDatabase, Globals::mDirs;
boost::property_tree::ptree Globals::config_main;

Globals::Globals()
{
}

Mantids30::Runtime::Logs::AppLog *Globals::getAppLog()
{
    return applog;
}

void Globals::setAppLog(Mantids30::Runtime::Logs::AppLog *value)
{
    applog = value;
}

boost::property_tree::ptree *Globals::getConfig_main()
{
    return &config_main;
}

