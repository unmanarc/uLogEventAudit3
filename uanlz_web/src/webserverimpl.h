/**
 * @file
 * @brief Declarations and contracts for uanlz_web/src/webserverimpl.
 */
#pragma once
namespace UANLZ::WEB {
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
} // namespace UANLZ::WEB
