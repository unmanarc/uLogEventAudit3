#include "globals.h"

using namespace UANLZ::JSONALERT;

std::unique_ptr<Mantids30::Runtime::Logs::AppLog> Globals::applog;
std::mutex Globals::mDatabase, Globals::mDirs;
boost::property_tree::ptree Globals::config_main;

Globals::Globals()
{
}

Mantids30::Runtime::Logs::AppLog *Globals::getAppLog()
{
    return applog.get();
}

void Globals::setAppLog(std::unique_ptr<Mantids30::Runtime::Logs::AppLog> value)
{
    applog = std::move(value);
}

boost::property_tree::ptree *Globals::getConfig_main()
{
    return &config_main;
}

