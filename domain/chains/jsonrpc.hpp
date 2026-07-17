#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace izan::chains {

// Error object returned by the node ({"error":{"code":…,"message":…}}).
struct RpcError : std::runtime_error {
    int64_t code;

    RpcError(int64_t errorCode, const std::string& message)
        : std::runtime_error(message)
        , code(errorCode)
    {
    }
};

// Builds a JSON-RPC 2.0 request envelope. `params_json` is a raw JSON
// value (usually an array) supplied by the caller; it is parsed here
// first so a malformed fragment fails loudly at build time, not as a
// confusing node-side error.
std::string make_request(
    std::string_view method, std::string_view params_json, uint64_t id);

// Validates the envelope (object, id echo) and returns the raw JSON
// text of "result". A node-reported error throws RpcError; a malformed
// or mismatched body throws std::runtime_error.
std::string result_of(std::string_view body, uint64_t expect_id);

// Same, for the common case of a string result ("0x1a" quantities,
// hex blobs): returns the string itself, unquoted.
std::string result_string(std::string_view body, uint64_t expect_id);

}
