#include "mcp.h"

mcp_exception::mcp_exception(error_code code, const std::string &message)
    : std::runtime_error(message), code_(code)
{
}

mcp_request::mcp_request(const std::string &request)
    : jsonrpc_("2.0")
{
    auto error = deserializeJson(doc_, request);
    if (error)
        throw mcp_exception(error_code::parse_error, "Failed to parse JSON request: " + std::string(error.c_str()));

    if (doc_.is<JsonObject>())
    {
        auto request = doc_.as<JsonObject>();
        if (request["jsonrpc"].is<std::string>())
            jsonrpc_ = request["jsonrpc"].as<std::string>();

        if (request["id"].is<JsonVariant>())
            id_ = request["id"];

        if (request["method"].is<std::string>())
            method_ = request["method"].as<std::string>();

        if (request["params"].is<JsonObject>())
            params_ = request["params"].as<JsonObject>();
    }
}

mcp_response::mcp_response(const std::string &jsonrpc /*= "2.0"*/)
{
    root_ = doc_.to<JsonObject>();
    root_["jsonrpc"] = jsonrpc;
}

mcp_response &mcp_response::set_id(const JsonVariant &id)
{
    root_["id"] = id;
    return *this;
}

JsonObject mcp_response::create_error()
{
    return root_["error"].to<JsonObject>();
}

JsonObject mcp_response::create_result()
{
    return root_["result"].to<JsonObject>();
}

std::tuple<int, const char *, std::string> mcp_response::get_http_response() const
{
    std::string json;
    try
    {
        serializeJson(doc_, json);
    }
    catch (const std::exception &e)
    {
        return {500, "text/plain", std::string("Internal Server Error: ") + e.what()}; // Internal Server Error
    }

    auto http_code = root_["error"].is<JsonObject>() ? 400 : 200; // If error is present, return 400 Bad Request, else 200 OK
    return {http_code, "application/json", json};                 // OK
}
