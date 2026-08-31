#pragma once

// Fluent helper for building MCP responses with ArduinoJson.
//
// Wraps mcp_response so that setting the id, adding text content, populating
// structuredContent, or reporting errors can each be written on a single line
// and chained fluently. The "result" object (and its content/structuredContent
// containers) are created lazily, so only what you actually use is emitted.
//
// Example:
//   mcp_response response;
//   mcp_response_schema r(response);
//   r.id(request.id())                                  // mirror the request id
//    .text("Image captured: 640x480")                   // content[0] = { type: text }
//    .field("width", 640)                               // structuredContent.width
//    .field("height", 480);                             // structuredContent.height
//
//   r.error(error_code::internal_error, "Camera capture failed");
//
// text()/field()/error() return *this, so they can be chained indefinitely.

#include <ArduinoJson.h>
#include <string>
#include "mcp.h"

class mcp_response_schema
{
public:
    // Binds the helper to an existing mcp_response.
    mcp_response_schema(mcp_response &response) : response_(response) {}

    // Mirrors the incoming request's JSON-RPC id (e.g. request.id()).
    mcp_response_schema &id(const JsonVariant &value)
    {
        response_.set_id(value);
        return *this;
    }

    // Accepts scalar ids (numbers, strings) by buffering them in a local document.
    template <typename T>
    mcp_response_schema &id(const T &value)
    {
        id_buffer_["v"] = value;
        response_.set_id(id_buffer_["v"]);
        return *this;
    }

    // Explicitly begins the "result" object (text()/field() create it lazily too).
    mcp_response_schema &result()
    {
        result_ = response_.create_result();
        return *this;
    }

    // Adds a text content item: "content": [ { "type": "text", "text": ... } ].
    mcp_response_schema &text(const std::string &text)
    {
        ensure_result();
        if (!content_)
            content_ = result_["content"].to<JsonArray>();
        auto item = content_.add<JsonObject>();
        item["type"] = "text";
        item["text"] = text;
        return *this;
    }

    // Adds a structuredContent field (MCP 2025-06-18+). Accepts any value that
    // ArduinoJson can serialize (int, float, bool, const char*, std::string, ...).
    template <typename T>
    mcp_response_schema &field(const char *key, const T &value)
    {
        ensure_result();
        if (!structured_)
            structured_ = result_["structuredContent"].to<JsonObject>();
        structured_[key] = value;
        return *this;
    }

    // Raw access to the (lazily created) result object for advanced use.
    JsonObject result_object()
    {
        ensure_result();
        return result_;
    }

private:
    void ensure_result()
    {
        if (!result_)
            result();
    }

    mcp_response &response_;
    JsonDocument id_buffer_;
    JsonObject result_;
    JsonArray content_;
    JsonObject structured_;
};
