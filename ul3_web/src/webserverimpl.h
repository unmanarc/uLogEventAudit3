/**
 * @file
 * @brief Declarations and contracts for ul3_web/src/webserverimpl.
 */
#pragma once
namespace UL3::WEB {
/** @brief Create the authenticated REST console and its fixed backend route mappings. */
class WebServerImpl
{
public:
    /**
     * @brief Build and start the REST engine after synchronizing application access control.
     * @return True if the REST engine was created and started.
     */
    static bool createWebServer();
};
} // namespace UL3::WEB
