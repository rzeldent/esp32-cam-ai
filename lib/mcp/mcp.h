#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <stdexcept>
#include <string>
#include <tuple>

enum error_code
{
    parse_error = -32700,        // Invalid JSON
    invalid_request = -32600,    // Invalid Request object
    method_not_found = -32601,   // Method not found
    invalid_params = -32602,     // Invalid method parameters
    internal_error = -32603,     // Internal JSON-RPC error
    server_error_start = -32000, // Server error start
    server_error_end = -32099    // Server error end
};

class mcp_exception : public std::runtime_error
{
public:
    mcp_exception(error_code code, const std::string &message);
    error_code code() const
    {
        return code_;
    }

private:
    error_code code_;
};

class mcp_request
{
public:
    mcp_request(const std::string &request);

    const std::string &jsonrpc() const { return jsonrpc_; }
    const JsonVariant &id() const { return id_; }
    const std::string &method() const { return method_; }
    const JsonObject &params() const { return params_; }

private:
    JsonDocument doc_;
    std::string jsonrpc_;
    JsonVariant id_;
    std::string method_;
    JsonObject params_;
};

struct mcp_response
{
    mcp_response(const std::string &jsonrpc = "2.0");

    mcp_response &set_id(const JsonVariant &id);
    JsonObject create_error();
    JsonObject create_result();

    std::tuple<int, const char *, std::string> get_http_response() const;

private:
    JsonDocument doc_;
    JsonObject root_;
};