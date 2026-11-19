/**
 * @file
 * @brief Declarations and contracts for uanlz_log2json/src/globals.
 */
#pragma once

#include <Mantids30/Runtime/Logs/applog.h>
#include <boost/property_tree/ini_parser.hpp>
#include <list>
#include <memory>
#include <mutex>

#define LOG_APP Globals::getAppLog()

namespace UANLZ::LOG2JSON {

/** @brief Hold the configuration, logs and shared resources used by this service. */
class Globals
{
public:
    /**
     * @brief Provide access to process-wide configuration and logging.
     */
    Globals();

    /**
     * @brief Return the process logger without transferring ownership.
     * @return Borrowed logger pointer, which may be null before logging initialization.
     */
    static Mantids30::Runtime::Logs::AppLog *getAppLog();
    /**
     * @brief Transfer ownership of the process logger into the global state.
     * @param value Unique owner of the logger to install.
     */
    static void setAppLog(std::unique_ptr<Mantids30::Runtime::Logs::AppLog> value);

    /**
     * @brief Return the shared database synchronization primitive.
     */
    static std::mutex *getDatabaseMutex();

    /**
     * @brief Return the shared configuration loaded before worker startup.
     * @return Borrowed configuration pointer; initialize it before starting workers.
     */
    static boost::property_tree::ptree *getMainConfig();

private:
    static std::mutex databaseMutex, directoriesMutex;
    static boost::property_tree::ptree mainConfig;
    static std::unique_ptr<Mantids30::Runtime::Logs::AppLog> appLog;
};

} // namespace UANLZ::LOG2JSON
