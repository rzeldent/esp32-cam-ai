#pragma once

// Fluent helper for building MCP JSON-RPC errors with ArduinoJson.
//
// Wraps mcp_response so that a JSON-RPC error object
// ("error": { "code": ..., "message": ..., "data": ... }) can be described
// fluently. The error object is created lazily on first use.
//
// Example:
//   mcp_schema_error err(response);
//   err.code(error_code::invalid_params)
//      .message("Invalid frame_size. Valid options: " + valid_options + ".")
//      .data("detail");                               // optional error data
//
//   mcp_schema_error(response).set(error_code::internal_error, "Camera capture failed");
//
// code()/message()/data()/set() return *this, so they can be chained indefinitely.

#include <ArduinoJson.h>
#include <string>
#include "mcp.h"

class mcp_schema_error
{
public:
    // Binds the helper to an existing mcp_response.
    mcp_schema_error(mcp_response &response) : response_(response) {}

    // Sets the JSON-RPC error code (error_code enum).
    mcp_schema_error &code(error_code value)
    {
        ensure_error()["code"] = value;
        return *this;
    }

    // Sets the human-readable error message.
    mcp_schema_error &message(const std::string &value)
    {
        ensure_error()["message"] = value;
        return *this;
    }

    // Sets the optional JSON-RPC error data (any JSON-serializable value).
    template <typename T>
    mcp_schema_error &data(const T &value)
    {
        ensure_error()["data"] = value;
        return *this;
    }

    // Sets code and message in a single call.
    mcp_schema_error &set(error_code value, const std::string &message_text)
    {
        auto error_object = ensure_error();
        error_object["code"] = value;
        error_object["message"] = message_text;
        return *this;
    }

    // Raw access to the (lazily created) error object for advanced use.
    JsonObject error_object()
    {
        return ensure_error();
    }

private:
    JsonObject ensure_error()
    {
        if (!error_)
            error_ = response_.create_error();
        return error_;
    }

    mcp_response &response_;
    JsonObject error_;
};
