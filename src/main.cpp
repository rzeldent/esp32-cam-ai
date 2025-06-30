#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>
#include <esp_task_wdt.h>
#include <soc/rtc_cntl_reg.h>

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

// WiFi reconnection settings
const unsigned long WIFI_REBOOT_DELAY = 60000;       // 60 seconds
const unsigned long WIFI_RECONNECT_INTERVAL = 30000; // 30 seconds
const unsigned long WIFI_CHECK_INTERVAL = 5000;      // 5 seconds
const int MAX_RECONNECT_ATTEMPTS = 5;

const unsigned long WATCHDOG_TIMEOUT = 30000; // 30 seconds

// WiFi status tracking
unsigned long lastWiFiCheck = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;
bool wifiConnected = false;

// Result of camera initialization
esp_err_t camera_init_result = ESP_OK;
// Temperature export (funny; has a typo!)
#ifdef __cplusplus
extern "C"
{
#endif
  uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

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

void handle_notifications_initialized(mcp_response &response)
{
  // For notifications, we don't need to send a response body
  // Set empty result to indicate successful notification processing
  auto result = response.create_result();
  result["acknowledged"] = true;
}

void handle_tools_list(mcp_response &response)
{
  auto result = response.create_result();
  auto tools = result["tools"].to<JsonArray>();

  // Add LED control tool
  auto led_tool = tools.add<JsonObject>();
  led_tool["name"] = "led";
  led_tool["description"] = "Controls the ESP32-CAM LED state";
  auto led_tool_input_schema = led_tool["inputSchema"].to<JsonObject>();
  led_tool_input_schema["type"] = "object";
  auto led_tool_input_schema_properties = led_tool_input_schema["properties"].to<JsonObject>();
  auto led_tool_input_schema_properties_state = led_tool_input_schema_properties["state"].to<JsonObject>();
  led_tool_input_schema_properties_state["type"] = "string";
  led_tool_input_schema_properties_state["description"] = "LED state";
  auto led_tool_input_schema_properties_state_enum_array = led_tool_input_schema_properties_state["enum"].to<JsonArray>();
  led_tool_input_schema_properties_state_enum_array.add("on");
  led_tool_input_schema_properties_state_enum_array.add("off");
  auto led_tool_input_schema_properties_state_required = led_tool_input_schema_properties_state["required"].to<JsonArray>();
  led_tool_input_schema_properties_state_required.add("state");
  led_tool_input_schema["additionalProperties"] = false;

  // Add Flash control tool
  auto flash_tool = tools.add<JsonObject>();
  flash_tool["name"] = "flash";
  flash_tool["description"] = "Controls the ESP32-CAM Flash";
  auto flash_tool_input_schema = flash_tool["inputSchema"].to<JsonObject>();
  flash_tool_input_schema["type"] = "object";
  auto flash_tool_input_schema_properties = flash_tool_input_schema["properties"].to<JsonObject>();
  auto flash_tool_input_schema_properties_duration = led_tool_input_schema_properties["duration"].to<JsonObject>();
  flash_tool_input_schema_properties_duration["description"] = "Flash duration in milliseconds";
  flash_tool_input_schema_properties_duration["type"] = "number";
  flash_tool_input_schema_properties_duration["minimum"] = 5;
  flash_tool_input_schema_properties_duration["maximum"] = 100;
  flash_tool_input_schema_properties_duration["default"] = 50;
  flash_tool_input_schema["additionalProperties"] = false;

  // Add camera capture tool
  auto camera_tool = tools.add<JsonObject>();
  camera_tool["name"] = "capture";
  camera_tool["description"] = "Captures a photo from the ESP32-CAM";
  auto camera_tool_input_schema = camera_tool["inputSchema"].to<JsonObject>();
  camera_tool_input_schema["type"] = "object";
  auto camera_tool_input_schema_properties = camera_tool_input_schema["properties"].to<JsonObject>();
  auto camera_tool_input_schema_properties_flash = camera_tool_input_schema_properties["flash"].to<JsonObject>();
  camera_tool_input_schema_properties_flash["type"] = "string";
  camera_tool_input_schema_properties_flash["description"] = "Use flash when capturing";
  auto camera_tool_input_schema_properties_flash_enum_array = camera_tool_input_schema_properties_flash["enum"].to<JsonArray>();
  camera_tool_input_schema_properties_flash_enum_array.add("on");
  camera_tool_input_schema_properties_flash_enum_array.add("off");
  camera_tool_input_schema["additionalProperties"] = false;

  // Add WiFi status tool
  auto wifi_tool = tools.add<JsonObject>();
  wifi_tool["name"] = "wifi_status";
  wifi_tool["description"] = "Gets current WiFi connection status and network information";
  auto wifi_tool_input_schema = wifi_tool["inputSchema"].to<JsonObject>();
  wifi_tool_input_schema["type"] = "object";
  auto wifi_tool_input_schema_properties = wifi_tool_input_schema["properties"].to<JsonObject>();
  wifi_tool_input_schema["additionalProperties"] = false;

  // Add system status tool
  auto system_tool = tools.add<JsonObject>();
  system_tool["name"] = "system_status";
  system_tool["description"] = "Gets comprehensive system status including memory, uptime, and hardware info";
  auto system_tool_input_schema = system_tool["inputSchema"].to<JsonObject>();
  system_tool_input_schema["type"] = "object";
  auto system_tool_input_schema_properties = system_tool_input_schema["properties"].to<JsonObject>();
  system_tool_input_schema["additionalProperties"] = false;
}

void checkWiFiConnection()
{
  auto now = millis();
  // Check WiFi status periodically
  if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL)
  {
    lastWiFiCheck = now;
    bool currentStatus = WiFi.status() == WL_CONNECTED;
    // Status changed
    if (currentStatus != wifiConnected)
    {
      wifiConnected = currentStatus;
      if (wifiConnected)
      {
        log_i("WiFi reconnected! IP: %s", WiFi.localIP().toString().c_str());
        log_i("Signal strength: %d dBm", WiFi.RSSI());
        reconnectAttempts = 0; // Reset counter on successful connection
      }
      else
      {
        log_w("WiFi disconnected!");
      }
    }

    // Attempt reconnection if disconnected
    if (!wifiConnected && (now - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL))
    {
      lastReconnectAttempt = now;
      if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS)
      {
        reconnectAttempts++;
        log_i("WiFi reconnection attempt %d/%d", reconnectAttempts, MAX_RECONNECT_ATTEMPTS);

        WiFi.disconnect();
        delay(1000);
        WiFi.begin(STR(WIFI_SSID), STR(WIFI_PASSWORD));

        // Wait for connection with timeout
        auto connectStart = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 10000)
          delay(500);

        if (WiFi.status() == WL_CONNECTED)
        {
          wifiConnected = true;
          log_i("WiFi reconnected successfully! IP: %s", WiFi.localIP().toString().c_str());
          reconnectAttempts = 0;
        }
        else
        {
          log_w("WiFi reconnection attempt %d failed", reconnectAttempts);
        }
      }
      else
      {
        log_e("Max WiFi reconnection attempts reached. Will restart in 60 seconds...");
        // Reset attempt counter and wait longer before restart
        if (now - lastReconnectAttempt >= WIFI_REBOOT_DELAY)
        {
          log_e("Restarting ESP32 due to WiFi connection failure...");
          ESP.restart();
        }
      }
    }
  }
}

void tool_led(JsonObject arguments, mcp_response &response)
{
  auto state = arguments["state"].as<String>();
  if (state == "on")
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL);
    auto result = response.create_result();
    auto result_content = result["content"].to<JsonArray>();
    auto result_content_item = result_content.add<JsonObject>();
    result_content_item["type"] = "text";
    result_content_item["text"] = "LED turned on";
  }
  else if (state == "off")
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW);
    auto result = response.create_result();
    auto result_content = result["content"].to<JsonArray>();
    auto result_content_item = result_content.add<JsonObject>();
    result_content_item["type"] = "text";
    result_content_item["text"] = "LED turned off";
  }
  else
  {
    auto error = response.create_error();
    error["code"] = error_code::invalid_params;
    error["message"] = "Invalid LED state. Use 'on' or 'off'.";
  }
}

void tool_flash(JsonObject arguments, mcp_response &response)
{
  auto duration = arguments["duration"].is<int>() ? arguments["duration"].as<int>() : 50; // Default to 50ms if not provided
  digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL);
  delay(duration); // 5-100ms
  digitalWrite(FLASH_GPIO, !FLASH_ON_LEVEL);
  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();
  auto result_content_item = result_content.add<JsonObject>();
  result_content_item["type"] = "text";
  result_content_item["text"] = "Flash executed";
}

void tool_capture(JsonObject arguments, mcp_response &response)
{
  if (camera_init_result != ESP_OK)
  {
    auto error = response.create_error();
    error["code"] = error_code::internal_error;
    error["message"] = "Camera not initialized or failed to initialize";
    return;
  }

  auto flash = arguments["flash"].as<String>();
  if (flash == "on")
  {
    digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL);
    delay(20); // Allow flash to stabilize
  }

  // Discard previous frames to ensure we get a fresh capture
  auto fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);

  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);

  digitalWrite(FLASH_GPIO, !FLASH_ON_LEVEL);

  if (!fb)
  {
    auto error = response.create_error();
    error["code"] = error_code::internal_error;
    error["message"] = "Camera capture failed";
    return;
  }

  auto base64_image = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();
  auto result_content_item = result_content.add<JsonObject>();
  result_content_item["type"] = "text";
  result_content_item["text"] = "Image captured successfully. Size: " + String(base64_image.length()) + " bytes (base64 encoded)";

  auto result_content_image_item = result_content.add<JsonObject>();
  result_content_image_item["type"] = "image";
  result_content_image_item["data"] = base64_image;
  result_content_image_item["mimeType"] = "image/jpeg";
}

void tool_wifi_status(mcp_response &response)
{
  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();
  auto result_content_item = result_content.add<JsonObject>();
  result_content_item["type"] = "text";

  String status_text;
  status_text += "IP Address: " + WiFi.localIP().toString() + "\n";
  status_text += "Signal Strength: " + String(WiFi.RSSI()) + " dBm\n";
  status_text += "MAC Address: " + WiFi.macAddress() + "\n";
  status_text += "Gateway: " + WiFi.gatewayIP().toString() + "\n";
  status_text += "DNS: " + WiFi.dnsIP().toString() + "\n";
  result_content_item["text"] = status_text;
}

void tool_system_status(mcp_response &response)
{
  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();
  auto result_content_item = result_content.add<JsonObject>();
  result_content_item["type"] = "text";

  // Get the tem

  String status_text = "System Status:\n";
  status_text += "Uptime: " + String(millis() / 1000) + " seconds\n";
  status_text += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
  status_text += "Min Free Heap: " + String(ESP.getMinFreeHeap()) + " bytes\n";
  status_text += "Max Alloc Heap: " + String(ESP.getMaxAllocHeap()) + " bytes\n";
  status_text += "CPU Frequency: " + String(getCpuFrequencyMhz()) + " MHz\n";
  status_text += "Flash Size: " + String(ESP.getFlashChipSize()) + " bytes\n";
  status_text += "Flash Speed: " + String(ESP.getFlashChipSpeed()) + " Hz\n";
  status_text += "Sketch Size: " + String(ESP.getSketchSize()) + " bytes\n";
  status_text += "Free Sketch Space: " + String(ESP.getFreeSketchSpace()) + " bytes\n";
  status_text += "SDK Version: " + String(ESP.getSdkVersion()) + "\n";
  status_text += "Reset Reason: " + String(esp_reset_reason()) + "\n";
  status_text += "Camera initialized: " + String(camera_init_result == ESP_OK ? "Yes" : "No (code = 0x" + String(camera_init_result, 16) + ")") + "\n";
  auto internal_temperature = (temprature_sens_read() - 32) / 1.8;
  status_text += "Internal Temperature: " + String(internal_temperature, 2) + " °C\n";
  result_content_item["text"] = status_text;
}

void handle_tools_call(const mcp_request &request, mcp_response &response)
{
  auto params = request.params();
  auto tool_name = params["name"].as<String>();
  auto arguments = params["arguments"].as<JsonObject>();

  if (tool_name == "led")
    tool_led(arguments, response);
  else if (tool_name == "flash")
    tool_flash(arguments, response);
  else if (tool_name == "capture")
    tool_capture(arguments, response);
  else if (tool_name == "wifi_status")
    tool_wifi_status(response);
  else if (tool_name == "system_status")
    tool_system_status(response);
  else
  {
    // Tool not found, set error
    auto error = response.create_error();
    if (tool_name.isEmpty())
    {
      error["code"] = error_code::invalid_request;
      error["message"] = "Tool name is required";
    }
    else
    {
      error["code"] = error_code::method_not_found;
      error["message"] = "Unknown tool: " + tool_name;
    }
  }
}

void handleRoot()
{
  // Add CORS headers for all requests
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
  server.sendHeader("Access-Control-Max-Age", "86400");

  if (server.method() == HTTP_OPTIONS)
  {
    // Handle preflight CORS requests
    server.send(200, "text/plain", "OK");
    return;
  }

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
      handle_initialize(mcp_response);
    else if (mcp_request.method() == "notifications/initialized")
      handle_notifications_initialized(mcp_response);
    else if (mcp_request.method() == "tools/list")
      handle_tools_list(mcp_response);
    else if (mcp_request.method() == "tools/call")
      handle_tools_call(mcp_request, mcp_response);
    else
    {
      auto error = mcp_response.create_error();
      error["code"] = error_code::method_not_found;
      error["message"] = "Method not found: " + mcp_request.method();
    }
  }
  catch (const mcp_exception &e)
  {
    auto error = mcp_response.create_error();
    error["code"] = e.code();
    error["message"] = e.what();
  }

  auto response = mcp_response.get_http_response();
  // Http Code, Content-Type, and Body
  log_d("Sending response: %d %s %s", std::get<0>(response), std::get<1>(response), std::get<2>(response).c_str());
  server.send(std::get<0>(response), std::get<1>(response), std::get<2>(response));
}

// WiFi event handlers
void onWiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    log_d("WiFi connected to SSID: %s", WiFi.SSID().c_str());
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    log_d("WiFi got IP address: %s", WiFi.localIP().toString().c_str());
    wifiConnected = true;
    reconnectAttempts = 0;
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    log_d("WiFi disconnected!");
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    log_d("WiFi lost IP address");
    wifiConnected = false;
    break;
  }
}

void setup()
{
  // Disable brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Initialize watchdog timer
  esp_task_wdt_init(WATCHDOG_TIMEOUT / 1000, true);
  esp_task_wdt_add(NULL);

  // Initialize LED GPIOs
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off
  pinMode(FLASH_GPIO, OUTPUT);
  digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off

  log_d("CPU Freq: %d Mhz", getCpuFrequencyMhz());
  log_d("Free heap: %d bytes", ESP.getFreeHeap());

  // Setup WiFi event handlers
  WiFi.onEvent(onWiFiEvent);

  // Configure WiFi for better stability
  WiFi.setAutoReconnect(false); // We handle reconnection manually
  WiFi.persistent(true);        // Save WiFi config to flash

  log_d("WiFi.begin() with SSID: %s", STR(WIFI_SSID));
  WiFi.begin(STR(WIFI_SSID), STR(WIFI_PASSWORD));
  auto connection_result = WiFi.waitForConnectResult();
  if (connection_result != WL_CONNECTED)
  {
    log_e("Failed to connect to WiFi. Error code: %d. Restarting...", connection_result);
    ESP.restart();
  }

  wifiConnected = true; // Set initial status
  log_i("Local IP address: %s", WiFi.localIP().toString().c_str());
  log_d("Signal strength: %d dBm", WiFi.RSSI());

  auto hostName = "esp32-" + WiFi.macAddress() + ".local";
  hostName.replace(":", "");
  hostName.toLowerCase();
  log_i("mDNS hostname: %s", hostName.c_str());
  MDNS.begin(hostName.c_str());
  MDNS.addService("_jsonrpc", "_tcp", 80);
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "version", "2.0");
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "protocol", "http");
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "path", "/");

  // Allow over the air updates
  ArduinoOTA.begin();

  // Initialize camera
  camera_init_result = esp_camera_init(&esp32cam_aithinker_settings);
  if (camera_init_result == ESP_OK)
    log_i("Camera initialized successfully");
  else
    log_e("Camera init failed with error 0x%x", camera_init_result);

  server.on("/", HTTP_ANY, handleRoot);
  server.begin();
}

void loop()
{
  // Reset watchdog timer
  esp_task_wdt_reset();

  // Check WiFi connection status and handle reconnection first
  checkWiFiConnection();

  // Handle web server requests only if WiFi is connected
  if (wifiConnected)
    server.handleClient();

  // Handle OTA (works even with WiFi issues for recovery)
  ArduinoOTA.handle();
}