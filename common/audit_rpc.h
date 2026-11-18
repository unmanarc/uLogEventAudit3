#pragma once
#include "audit_json.h"
#include <Mantids30/Protocol/FastRPC1/fastrpc.h>

namespace UANLZ::JSON {
// The audit event/configuration format remains JSONCpp; Mantids30 transports use Boost.JSON.
template<auto Callback>
Mantids30::Protocol::FastRPC1::Method rpcMethod()
{
    return {+[](const std::shared_ptr<void> &, const std::string &key,
                const boost::json::value &input, const std::shared_ptr<void> &,
                const std::string &data) {
        return toMantids(Callback(nullptr, key, fromMantids(input), nullptr, data));
    }, nullptr};
}
}
