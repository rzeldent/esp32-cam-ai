#pragma once

// Fluent helper for building MCP tool input schemas (JSON Schema) with ArduinoJson.
//
// ArduinoJson already exposes a "fluent" member-proxy API (doc["a"]["b"] = v), which
// auto-creates nested objects on assignment. However, declaring tool schemas still
// requires a lot of boilerplate. This small builder wraps that API so each property
// can be declared on a single line.
//
// Example:
//   auto tool = tools.add<JsonObject>();
//   tool_schema schema(tool, "capture", "Captures a photo from the ESP32-CAM");
//   schema
//     .boolean("flash", "Use flash when capturing", false)
//     .number("quality", "JPEG quality for the captured photo (1-100)", 1, 100, 20)
//     .enum_table("frame_size", "Resolution to use for the captured photo", frame_sizes,
//                 [](const frame_size_entry_t &e) { return e.frame_size == MCP_CAPTURE_FRAMESIZE; });

#include <ArduinoJson.h>

class tool_schema
{
public:
    // Starts a tool definition with a JSON Schema input ("type": "object",
    // "additionalProperties": false) and an empty "properties" container.
    tool_schema(JsonObject tool, const char *name, const char *description)
    {
        tool["name"] = name;
        tool["description"] = description;
        auto schema = tool["inputSchema"].to<JsonObject>();
        schema["type"] = "object";
        schema["additionalProperties"] = false;
        properties_ = schema["properties"].to<JsonObject>();
    }

    // Declares a string property with a fixed list of enum values.
    tool_schema &enum_string(const char *key, const char *description, std::initializer_list<const char *> values)
    {
        auto property = properties_[key].to<JsonObject>();
        property["type"] = "string";
        property["description"] = description;
        auto enum_values = property["enum"].to<JsonArray>();
        for (const char *value : values)
            enum_values.add(value);
        return *this;
    }

    // Declares a string property whose enum values (and optional default) are derived
    // from a lookup table of { name, value } entries (e.g. frame_sizes). is_default
    // receives each entry and returns true for the entry that should be the default.
    template <typename T, size_t N, typename IsDefault>
    tool_schema &enum_table(const char *key, const char *description, const T (&table)[N], IsDefault is_default)
    {
        auto property = properties_[key].to<JsonObject>();
        property["type"] = "string";
        property["description"] = description;
        auto enum_values = property["enum"].to<JsonArray>();
        for (size_t i = 0; i < N; ++i)
        {
            enum_values.add(table[i].name);
            if (is_default(table[i]))
                property["default"] = table[i].name;
        }
        return *this;
    }

    // Declares a number property with min/max/default constraints.
    tool_schema &number(const char *key, const char *description, long minimum, long maximum, long default_value)
    {
        auto property = properties_[key].to<JsonObject>();
        property["type"] = "number";
        property["description"] = description;
        property["minimum"] = minimum;
        property["maximum"] = maximum;
        property["default"] = default_value;
        return *this;
    }

    // Declares a boolean property (true/false) with an optional default.
    tool_schema &boolean(const char *key, const char *description, bool default_value = false)
    {
        auto property = properties_[key].to<JsonObject>();
        property["type"] = "boolean";
        property["description"] = description;
        property["default"] = default_value;
        return *this;
    }

    // Marks a property as required.
    tool_schema &required(const char *key)
    {
        auto required = properties_[key]["required"].to<JsonArray>();
        required.add(key);
        return *this;
    }

private:
    JsonObject properties_;
};
