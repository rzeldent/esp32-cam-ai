#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>

#include <mcp.h>
#include <base64.h>

#include "camera_config.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Please define it in your environment variables or in the code."
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined. Please define it in your environment variables or in the code."
#endif

#ifndef LED_GPIO
#error "LED_GPIO is not defined. Please define it in your build flags."
#endif

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)



WebServer server(80);

void handle_initialize(mcp_response &response)
{
  auto result = response.create_result();
  result["protocolVersion"] = "2024-11-05";
  auto capabilities = result["capabilities"].to<JsonObject>();
  auto tools = capabilities["tools"].to<JsonObject>();
  tools["listChanged"] = false;
  auto server_info = result["serverInfo"].to<JsonObject>();
  server_info["name"] = "ESP32-CAM-AI MCP Server";
  server_info["version"] = "1.0.0";
}

void handle_tools_list(mcp_response &response)
{
  auto result = response.create_result();
  auto tools = result["tools"].to<JsonArray>();
  
  // Add LED control tool
  auto led_tool = tools.add<JsonObject>();
  led_tool["name"] = "led";
  led_tool["description"] = "Controls the ESP32-CAM LED state";
  auto led_input_schema = led_tool["inputSchema"].to<JsonObject>();
  led_input_schema["type"] = "object";
  auto led_properties = led_input_schema["properties"].to<JsonObject>();
  auto state_prop = led_properties["state"].to<JsonObject>();
  state_prop["type"] = "string";
  state_prop["description"] = "LED state";
  auto enum_array = state_prop["enum"].to<JsonArray>();
  enum_array.add("on");
  enum_array.add("off");
  auto required = led_input_schema["required"].to<JsonArray>();
  required.add("state");
  led_input_schema["additionalProperties"] = false;
  
  // Add camera capture tool
  auto camera_tool = tools.add<JsonObject>();
  camera_tool["name"] = "capture";
  camera_tool["description"] = "Captures a photo from the ESP32-CAM";
  auto camera_input_schema = camera_tool["inputSchema"].to<JsonObject>();
  camera_input_schema["type"] = "object";
  camera_input_schema["properties"] = JsonObject();
  camera_input_schema["additionalProperties"] = false;
}

void handle_tools_call(const mcp_request &request, mcp_response &response)
{
  auto params = request.params();
  auto tool_name = params["name"].as<String>();
  auto arguments = params["arguments"].as<JsonObject>();
  
  if (tool_name == "led")
  {
    auto state = arguments["state"].as<String>();
    if (state == "on")
    {
      digitalWrite(LED_GPIO, LED_ON_LEVEL);
      auto result = response.create_result();
      auto content = result["content"].to<JsonArray>();
      auto item = content.add<JsonObject>();
      item["type"] = "text";
      item["text"] = "LED turned on";
    }
    else if (state == "off")
    {
      digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW);
      auto result = response.create_result();
      auto content = result["content"].to<JsonArray>();
      auto item = content.add<JsonObject>();
      item["type"] = "text";
      item["text"] = "LED turned off";
    }
    else
    {
      response.set_error(error_code::invalid_params, "Invalid LED state. Use 'on' or 'off'.");
    }
  }
  else if (tool_name == "capture")
  {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
      response.set_error(error_code::internal_error, "Camera capture failed");
      return;
    }
    
    // Convert to base64
    String base64_image = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    
    auto result = response.create_result();
    auto content = result["content"].to<JsonArray>();
    auto item = content.add<JsonObject>();
    item["type"] = "text";
    item["text"] = "Image captured successfully. Size: " + String(base64_image.length()) + " bytes (base64 encoded)";
    
    auto image_item = content.add<JsonObject>();
    image_item["type"] = "image";
    image_item["data"] = "data:image/jpeg;base64," + base64_image;
    image_item["mimeType"] = "image/jpeg";
  }
  else
  {
    response.set_error(error_code::method_not_found, "Unknown tool: " + tool_name);
  }
}

void handleRoot()
{
  if (server.method() != HTTP_POST)
  {
    server.send(405, "text/plain", "Only POST allowed");
    return;
  }

  mcp_response mcp_response;

  try
  {
    mcp_request mcp_request(server.arg("plain"));
    mcp_response.set_id(mcp_request.id());
    
    // Handle MCP methods
    if (mcp_request.method() == "initialize")
    {
      handle_initialize(mcp_response);
    }
    else if (mcp_request.method() == "tools/list")
    {
      handle_tools_list(mcp_response);
    }
    else if (mcp_request.method() == "tools/call")
    {
      handle_tools_call(mcp_request, mcp_response);
    }
    else
    {
      mcp_response.set_error(error_code::method_not_found, "Method not found: " + mcp_request.method());
    }
  }
  catch (const mcp_exception &e)
  {
    mcp_response.set_error(e.code(), e.what());
  }

  auto response = mcp_response.get_http_response();
  server.send(std::get<0>(response), std::get<1>(response), std::get<2>(response));
}

void setup()
{
  Serial.begin(115200);

  // Initialize LED GPIO
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off

  log_i("CPU Freq: %d Mhz", getCpuFrequencyMhz());
  log_i("Free heap: %d bytes", ESP.getFreeHeap());

  log_i("WiFi.begin() with SSID: %s", STR(WIFI_SSID));
  WiFi.begin(STR(WIFI_SSID), STR(WIFI_PASSWORD));
  auto connection_result = WiFi.waitForConnectResult();
  if (connection_result != WL_CONNECTED)
  {
    log_e("Failed to connect to WiFi. Error code: %d. Restarting...", connection_result);
    ESP.restart();
  }

  log_i("Local IP address: %s", WiFi.localIP().toString().c_str());

  auto hostName = "esp32-" + WiFi.macAddress() + ".local";
  hostName.replace(":", "");
  hostName.toLowerCase();
  log_i("MDNS hostname: %s", hostName.c_str());
  MDNS.begin(hostName.c_str());

  // Allow over the air updates
  ArduinoOTA.begin();

  auto camera_init_result = esp_camera_init(&esp32cam_aithinker_settings);
  if (camera_init_result != ESP_OK)
    log_e("Camera init failed with error 0x%x", camera_init_result);

  server.on("/", handleRoot);
  server.begin();
}

void loop()
{
  // Handle web server requests
  server.handleClient();
  // Handle OTA
  ArduinoOTA.handle();
}
