#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>
#include <soc/rtc_cntl_reg.h>

#include <mcp.h>
#include <base64.h>

#include "camera_config.h"
#include "settings.h"

#ifdef ENABLE_GZIP
// miniz is a small public-domain zlib/gzip alternative commonly available with ESP32 Arduino
#include <miniz.h>
#endif

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

constexpr auto WATCHDOG_TIMEOUT = 30000UL; // 30 seconds

// WiFi connection state, updated by WiFi events
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

// Generic Accept-Encoding check
static bool client_accepts(const char *encoding)
{
  if (!server.hasHeader("Accept-Encoding"))
    return false;
  auto accept = server.header("Accept-Encoding");
  accept.toLowerCase();
  return accept.indexOf(encoding) >= 0;
}

#ifdef ENABLE_GZIP
// Optional deflate (zlib) compression using miniz. Returns true on success and writes binary data to output.
static bool deflate_compress(const String &input, String &output)
{
  auto src = reinterpret_cast<const unsigned char *>(input.c_str());
  auto src_len = static_cast<mz_ulong>(input.length());
  auto bound = mz_compressBound(src_len);
  std::unique_ptr<unsigned char, decltype(&free)> buf(
      static_cast<unsigned char *>(malloc(bound)), &free);
  if (!buf)
    return false;
  auto out_len = bound;
  if (mz_compress2(buf.get(), &out_len, src, src_len, MZ_DEFAULT_LEVEL) != MZ_OK)
    return false;

    output = String(reinterpret_cast<const char *>(buf.get()), static_cast<unsigned int>(out_len));
  return true;
}
#endif

void handle_initialize(mcp_response &response)
{
  auto result = response.create_result();
  result["protocolVersion"] = MCP_PROTOCOL_VERSION;
  auto capabilities = result["capabilities"].to<JsonObject>();
  auto tools = capabilities["tools"].to<JsonObject>();
  tools["listChanged"] = false;
  auto server_info = result["serverInfo"].to<JsonObject>();
  server_info["name"] = MCP_NAME;
  server_info["version"] = MCP_VERSION;
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
  auto flash_tool_input_schema_properties_duration = flash_tool_input_schema_properties["duration"].to<JsonObject>();
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

  // Discard 1-2 warm-up frames to ensure a fresh capture
  auto fb  = esp_camera_fb_get();
  if (fb)
    esp_camera_fb_return(fb);

  fb = esp_camera_fb_get();
  if (fb)
    esp_camera_fb_return(fb);

  // Take the actual frame
  fb = esp_camera_fb_get();
  // Turn flash off immediately after capture attempt
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

  auto status_text = String();
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

  auto status_text = String("System Status:\n");
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
  // Content negotiation for caches and proxies
  server.sendHeader("Vary", "Accept-Encoding");

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
    {
      // JSON-RPC notification: do not return a JSON-RPC response body
      log_d("Notifications/initialized received; returning 204 No Content");
      server.send(204, "text/plain", "");
      return;
    }
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
  auto http_code = std::get<0>(response);
  auto content_type = std::get<1>(response);
  auto body = std::get<2>(response);

#ifdef ENABLE_GZIP
  // Try deflate if the client accepts it; fall back to plain text on any failure
  if (client_accepts("deflate"))
  {
    String deflated;
    if (deflate_compress(body, deflated))
    {
      server.sendHeader("Content-Encoding", "deflate");
      log_d("Sending deflate response: %d %s len=%u (deflate)", http_code, content_type, (unsigned)deflated.length());
      server.send(http_code, content_type, deflated);
      return;
    }
  }
#endif  

  // Deflate not enabled or failed, fall back to plain text
  log_d("Sending response: %d %s len=%u", http_code, content_type, (unsigned)body.length());
  server.send(http_code, content_type, body);
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

    // Initialize LED GPIOs
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off
  pinMode(FLASH_GPIO, OUTPUT);
  digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off

  log_d("CPU Freq: %d Mhz", getCpuFrequencyMhz());
  log_d("Free heap: %d bytes", ESP.getFreeHeap());

  // Setup WiFi event handlers
  WiFi.onEvent(onWiFiEvent);

  // Configure WiFi for automatic reconnection on disconnect
  WiFi.setAutoReconnect(true); // Auto-reconnect to the saved AP
  WiFi.persistent(true);       // Save WiFi config to flash

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

  // Register request headers to collect so hasHeader()/header() work for Accept-Encoding content negotiation
  const char* headerkeys[] = {"Accept-Encoding"};
  server.collectHeaders(headerkeys, 1);

  server.on("/", HTTP_ANY, handleRoot);
  server.begin();
}

void loop()
{
  // Handle web server requests only if WiFi is connected
  if (wifiConnected)
    server.handleClient();

  // Handle OTA (works even with WiFi issues for recovery)
  ArduinoOTA.handle();
}