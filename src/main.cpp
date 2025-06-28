#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>
#include <esp_task_wdt.h>

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
const unsigned long WIFI_RECONNECT_INTERVAL = 30000; // 30 seconds
const unsigned long WIFI_CHECK_INTERVAL = 5000;      // 5 seconds
const int MAX_RECONNECT_ATTEMPTS = 5;

const unsigned long WATCHDOG_TIMEOUT = 10000; // 10 seconds

// WiFi status tracking
unsigned long lastWiFiCheck = 0;
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;
bool wifiConnected = false;

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
  auto camera_properties = camera_input_schema["properties"].to<JsonObject>();
  camera_input_schema["additionalProperties"] = false;

  // Add WiFi status tool
  auto wifi_tool = tools.add<JsonObject>();
  wifi_tool["name"] = "wifi_status";
  wifi_tool["description"] = "Gets current WiFi connection status and network information";
  auto wifi_input_schema = wifi_tool["inputSchema"].to<JsonObject>();
  wifi_input_schema["type"] = "object";
  auto wifi_properties = wifi_input_schema["properties"].to<JsonObject>();
  wifi_input_schema["additionalProperties"] = false;

  // Add system status tool
  auto system_tool = tools.add<JsonObject>();
  system_tool["name"] = "system_status";
  system_tool["description"] = "Gets comprehensive system status including memory, uptime, and hardware info";
  auto system_input_schema = system_tool["inputSchema"].to<JsonObject>();
  system_input_schema["type"] = "object";
  auto system_properties = system_input_schema["properties"].to<JsonObject>();
  system_input_schema["additionalProperties"] = false;
}

void checkWiFiConnection()
{
  unsigned long currentTime = millis();

  // Check WiFi status periodically
  if (currentTime - lastWiFiCheck >= WIFI_CHECK_INTERVAL)
  {
    lastWiFiCheck = currentTime;

    bool currentStatus = (WiFi.status() == WL_CONNECTED);

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
    if (!wifiConnected && (currentTime - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL))
    {
      lastReconnectAttempt = currentTime;

      if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS)
      {
        reconnectAttempts++;
        log_i("WiFi reconnection attempt %d/%d", reconnectAttempts, MAX_RECONNECT_ATTEMPTS);

        WiFi.disconnect();
        delay(1000);
        WiFi.begin(STR(WIFI_SSID), STR(WIFI_PASSWORD));

        // Wait for connection with timeout
        unsigned long connectStart = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 10000)
        {
          delay(500);
        }

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
        if (currentTime - lastReconnectAttempt >= 60000)
        {
          log_e("Restarting ESP32 due to WiFi connection failure...");
          ESP.restart();
        }
      }
    }
  }
}

void getWiFiStatus(mcp_response &response)
{
  auto result = response.create_result();
  auto content = result["content"].to<JsonArray>();
  auto item = content.add<JsonObject>();
  item["type"] = "text";

  String status_text = "WiFi Status:\n";
  status_text += "Connected: " + String(wifiConnected ? "Yes" : "No") + "\n";

  if (wifiConnected)
  {
    status_text += "IP Address: " + WiFi.localIP().toString() + "\n";
    status_text += "Signal Strength: " + String(WiFi.RSSI()) + " dBm\n";
    status_text += "MAC Address: " + WiFi.macAddress() + "\n";
    status_text += "Gateway: " + WiFi.gatewayIP().toString() + "\n";
    status_text += "DNS: " + WiFi.dnsIP().toString() + "\n";
  }
  else
  {
    status_text += "Reconnect Attempts: " + String(reconnectAttempts) + "/" + String(MAX_RECONNECT_ATTEMPTS) + "\n";
    status_text += "Last Attempt: " + String((millis() - lastReconnectAttempt) / 1000) + " seconds ago\n";
  }

  item["text"] = status_text;
}

void getSystemStatus(mcp_response &response)
{
  auto result = response.create_result();
  auto content = result["content"].to<JsonArray>();
  auto item = content.add<JsonObject>();
  item["type"] = "text";

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

  // Camera status
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb)
  {
    status_text += "Camera: Working (Last capture: " + String(fb->len) + " bytes)\n";
    esp_camera_fb_return(fb);
  }
  else
  {
    status_text += "Camera: Error - Unable to capture\n";
  }

  item["text"] = status_text;
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
  else if (tool_name == "wifi_status")
    getWiFiStatus(response);
  else if (tool_name == "system_status")
    getSystemStatus(response);
  else
    response.set_error(error_code::method_not_found, "Unknown tool: " + tool_name);
}

void handleRoot()
{
  // Check if WiFi is connected before processing requests
  if (!wifiConnected)
  {
    server.send(503, "text/plain", "Service Unavailable - WiFi not connected");
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
    else if (mcp_request.method() == "tools/list")
      handle_tools_list(mcp_response);
    else if (mcp_request.method() == "tools/call")
      handle_tools_call(mcp_request, mcp_response);
    else
      mcp_response.set_error(error_code::method_not_found, "Method not found: " + mcp_request.method());
  }
  catch (const mcp_exception &e)
  {
    mcp_response.set_error(e.code(), e.what());
  }

  auto response = mcp_response.get_http_response();
log_i("response 0: %d", std::get<0>(response));
log_i("response 1: %s", std::get<1>(response).c_str());
log_i("response 2: %s", std::get<2>(response).c_str());
log_i("response 1: %s", std::get<1>(response).c_str());
  log_i("Sending response: %d %s %s", std::get<0>(response), std::get<1>(response).c_str(), std::get<2>(response).c_str());
  server.send(std::get<0>(response), std::get<1>(response), std::get<2>(response));
}

// WiFi event handlers
void onWiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    log_i("WiFi connected to SSID: %s", WiFi.SSID().c_str());
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    log_i("WiFi got IP address: %s", WiFi.localIP().toString().c_str());
    wifiConnected = true;
    reconnectAttempts = 0;
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    log_w("WiFi disconnected!");
    wifiConnected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    log_w("WiFi lost IP address");
    wifiConnected = false;
    break;
  }
}

void setup()
{
  Serial.begin(115200);

  // Initialize watchdog timer
  esp_task_wdt_init(WATCHDOG_TIMEOUT / 1000, true);
  esp_task_wdt_add(NULL);

  // Initialize LED GPIO
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off

  log_i("CPU Freq: %d Mhz", getCpuFrequencyMhz());
  log_i("Free heap: %d bytes", ESP.getFreeHeap());

  // Setup WiFi event handlers
  WiFi.onEvent(onWiFiEvent);

  // Configure WiFi for better stability
  WiFi.setAutoReconnect(false); // We handle reconnection manually
  WiFi.persistent(true);        // Save WiFi config to flash

  log_i("WiFi.begin() with SSID: %s", STR(WIFI_SSID));
  WiFi.begin(STR(WIFI_SSID), STR(WIFI_PASSWORD));
  auto connection_result = WiFi.waitForConnectResult();
  if (connection_result != WL_CONNECTED)
  {
    log_e("Failed to connect to WiFi. Error code: %d. Restarting...", connection_result);
    ESP.restart();
  }

  wifiConnected = true; // Set initial status
  log_i("Local IP address: %s", WiFi.localIP().toString().c_str());
  log_i("Signal strength: %d dBm", WiFi.RSSI());

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
