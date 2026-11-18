#ifndef GLOBALS_H
#define GLOBALS_H

#include <boost/property_tree/ini_parser.hpp>
#include <Mantids30/Runtime/Logs/applog.h>
#include <mutex>
#include <memory>
#include <list>

#define LOG_APP Globals::getAppLog()

namespace UANLZ { namespace LOG2JSON {

class Globals
{
public:
    Globals();

    static Mantids30::Runtime::Logs::AppLog *getAppLog();
    static void setAppLog(std::unique_ptr<Mantids30::Runtime::Logs::AppLog> value);

    static std::mutex * getDatabaseMutex();

    static boost::property_tree::ptree * getConfig_main();


private:
    static std::mutex mDatabase,mDirs;
    static boost::property_tree::ptree config_main;
    static std::unique_ptr<Mantids30::Runtime::Logs::AppLog> applog;

};


}}


#endif // GLOBALS_H
