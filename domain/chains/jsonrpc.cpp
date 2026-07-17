#include "domain/chains/jsonrpc.hpp"

#include <glaze/glaze.hpp>

namespace izan::chains {

namespace {

    std::string escape(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            default:
                if (uint8_t(c) < 0x20) {
                    static constexpr char digits[] = "0123456789abcdef";
                    out += "\\u00";
                    out += digits[uint8_t(c) >> 4];
                    out += digits[uint8_t(c) & 0xf];
                } else {
                    out += c;
                }
            }
        }
        return out;
    }

    glz::generic parse_envelope(std::string_view body, uint64_t expect_id)
    {
        glz::generic j;
        if (glz::read_json(j, body) || !j.is_object())
            throw std::runtime_error("jsonrpc: malformed response");
        if (!j.contains("id") || !j["id"].is_number()
            || uint64_t(j["id"].get_number()) != expect_id)
            throw std::runtime_error("jsonrpc: response id mismatch");
        if (j.contains("error")) {
            auto& e = j["error"];
            int64_t code = e.contains("code") && e["code"].is_number()
                ? int64_t(e["code"].get_number())
                : 0;
            std::string msg = e.contains("message") && e["message"].is_string()
                ? e["message"].get_string()
                : "unknown rpc error";
            throw RpcError(code, msg);
        }
        if (!j.contains("result"))
            throw std::runtime_error("jsonrpc: no result and no error");
        return j;
    }

}

std::string make_request(
    std::string_view method, std::string_view params_json, uint64_t id)
{
    glz::generic check;
    if (glz::read_json(check, params_json))
        throw std::invalid_argument("jsonrpc: params is not valid JSON");
    std::string out = "{\"jsonrpc\":\"2.0\",\"id\":";
    out += std::to_string(id);
    out += ",\"method\":\"";
    out += escape(method);
    out += "\",\"params\":";
    out += params_json;
    out += "}";
    return out;
}

std::string result_of(std::string_view body, uint64_t expect_id)
{
    glz::generic j = parse_envelope(body, expect_id);
    std::string out;
    if (glz::write_json(j["result"], out))
        throw std::runtime_error("jsonrpc: cannot re-serialize result");
    return out;
}

std::string result_string(std::string_view body, uint64_t expect_id)
{
    glz::generic j = parse_envelope(body, expect_id);
    if (!j["result"].is_string())
        throw std::runtime_error("jsonrpc: result is not a string");
    return j["result"].get_string();
}

}
