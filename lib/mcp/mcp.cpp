#include "mcp.h"

mcp_exception::mcp_exception(error_code code, const String &message)
    : std::runtime_error(message.c_str()), code_(code)
{
}

mcp_request::mcp_request(const String &request)
    : jsonrpc_("2.0")
{
    JsonDocument doc;
    auto error = deserializeJson(doc, request);
    if (error)
        throw mcp_exception(error_code::parse_error, "Failed to parse JSON request: " + String(error.c_str()));

    if (doc.is<JsonObject>())
    {
        auto request = doc.as<JsonObject>();
        if (request["jsonrpc"].is<String>())
            jsonrpc_ = request["jsonrpc"].as<String>();
        if (request["id"].is<JsonVariant>())
            id_ = request["id"];
        if (request["method"].is<String>())
            method_ = request["method"].as<String>();
        if (request["params"].is<JsonObject>())
            params_ = request["params"].as<JsonObject>();
    }
}

mcp_response::mcp_response(const String &jsonrpc /*= "2.0"*/)
{
    root_ = doc_.to<JsonObject>();
    root_["jsonrpc"] = jsonrpc;
}

mcp_response &mcp_response::set_id(const JsonVariant &id)
{
    root_["id"] = id;
    return *this;
}

mcp_response &mcp_response::set_error(error_code code, const String &message)
{
    auto error = root_["error"].as<JsonObject>();
    error["code"] = static_cast<int>(code);
    error["message"] = message;
    root_["error"] = error;
    return *this;
}

mcp_response &mcp_response::set_result(const JsonObject &result)
{
    root_["result"] = result;
    return *this;
}

JsonObject mcp_response::create_result()
{
    return root_["result"].to<JsonObject>();
}

std::tuple<int, const String &, const String &> mcp_response::get_http_response() const
{
    String json;
    try
    {
        serializeJson(doc_, json);
    }
    catch (const std::exception &e)
    {
        return {500, "text/plain", "Internal Server Error: " + String(e.what())}; // Internal Server Error
    }

    if (root_["error"].is<JsonObject>())
        return {400, "application/json", json}; // Bad Request

    return {200, "application/json", json}; // OK
}
