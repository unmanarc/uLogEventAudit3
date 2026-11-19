/**
 * @file
 * @brief Declarations and contracts for common/audit_rpc.
 */
#pragma once
#include "audit_json.h"
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>

namespace UL3::JSON {

/**
 * @brief Adapt an application callback to the Mantids30 FastRPC1 method signature.
 * @tparam Callback Application handler receiving the legacy context and payload arguments.
 * @return Mantids30 method entry with unused application contexts set to nullptr.
 */
template<auto Callback>
Mantids30::Protocol::FastRPC1::Method rpcMethod()
{
    return {+[](const std::shared_ptr<void> &, const std::string &key, const boost::json::value &input, const std::shared_ptr<void> &, const std::string &data) -> boost::json::value
            { return Callback(nullptr, key, input, nullptr, data); },
            nullptr};
}
} // namespace UL3::JSON
