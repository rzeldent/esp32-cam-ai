#pragma once

constexpr auto MCP_PROTOCOL_VERSION = "2024-11-05";
constexpr auto MCP_NAME = "ESP32-CAM-AI MCP Server";
constexpr auto MCP_VERSION = "1.0.1";

// Maximum resolution for MCP "capture" tool photos (bounds JPEG size and memory)
constexpr auto MCP_CAPTURE_FRAMESIZE = FRAMESIZE_VGA;

constexpr auto WATCHDOG_TIMEOUT = 30000UL; // 30 seconds
// Maximum accepted MCP request body size (bytes)
constexpr auto MAX_MCP_REQUEST_SIZE = 16384; // 16 KB

// Optional API credentials for authenticating MCP requests (HTTP Basic).
// Set MCP_API_USER (and MCP_API_PASSWORD) here to enable authentication.
// Leave MCP_API_USER empty to disable authentication (open access).
constexpr auto MCP_API_USER = "";
constexpr auto MCP_API_PASSWORD = "";

constexpr auto MCP_API_REALM = "ESP32-CAM-AI MCP";

#define MCP_OTA_PASSWORD MCP_API_PASSWORD