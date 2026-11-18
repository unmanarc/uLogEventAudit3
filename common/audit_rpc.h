#pragma once
#include "audit_json.h"
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>

namespace UANLZ::JSON {
// Adapt the application callback context to the FastRPC1 method signature.
template<auto Callback>
Mantids30::Protocol::FastRPC1::Method rpcMethod()
{
    return {+[](const std::shared_ptr<void> &, const std::string &key,
                const boost::json::value &input, const std::shared_ptr<void> &,
                const std::string &data) {
        return Callback(nullptr, key, input, nullptr, data);
    }, nullptr};
}
}
