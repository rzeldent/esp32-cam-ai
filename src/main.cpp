#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>
#include <soc/rtc_cntl_reg.h>

#include <mcp.h>
#include <base64.h>
#include <libb64/cdecode.h>

#include "camera_config.h"
#include "settings.h"
#include "tool_schema.h"

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

// Temperature export (funny; has a typo!)
#ifdef __cplusplus
extern "C"
{
#endif
  uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

WebServer server;
auto macAddress = String(ESP.getEfuseMac(), 16);
auto thingName = String("esp") + "-" + macAddress;

// Configured MCP API credentials from settings.h.
static const String mcp_api_user(MCP_API_USER);
static const String mcp_api_password(MCP_API_PASSWORD);

// Generic Accept-Encoding check
static bool client_accepts(const char *encoding)
{
  if (!server.hasHeader("Accept-Encoding"))
    return false;
  auto accept = server.header("Accept-Encoding");
  accept.toLowerCase();
  return accept.indexOf(encoding) >= 0;
}

// Returns true if the request is authorized. Authentication is disabled when
// MCP_API_USER is empty; otherwise the request must present valid HTTP Basic
// credentials (Authorization: Basic base64(user:password)).
static bool request_authorized()
{
  if (mcp_api_user.length() == 0)
    return true;

  auto auth = server.header("Authorization");
  if (!auth.startsWith("Basic "))
    return false;

  auto encoded = auth.substring(6);
  encoded.trim();

  auto expected = mcp_api_user + ":" + mcp_api_password;
  auto decoded_len = base64_decode_expected_len(encoded.length()) + 1;
  std::unique_ptr<char[]> decoded(new char[decoded_len]);
  auto len = base64_decode_chars(encoded.c_str(), encoded.length(), decoded.get());
  if (len < 0)
    return false;
  decoded[len] = '\0';
  return String(decoded.get()) == expected;
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

// pixformat_t and the PIXFORMAT_* values are provided by esp_camera.h (sensor.h);
// see the pixel_formats lookup table below.

typedef struct frame_size_entry
{
    const char name[17];
    const framesize_t frame_size;
} frame_size_entry_t;

constexpr const frame_size_entry_t frame_sizes[] = {
    {"QQVGA (160x120)", FRAMESIZE_QQVGA},
    {"QCIF (176x144)", FRAMESIZE_QCIF},
    {"HQVGA (240x176)", FRAMESIZE_HQVGA},
    {"240x240", FRAMESIZE_240X240},
    {"QVGA (320x240)", FRAMESIZE_QVGA},
    {"CIF (400x296)", FRAMESIZE_CIF},
    {"HVGA (480x320)", FRAMESIZE_HVGA},
    {"VGA (640x480)", FRAMESIZE_VGA},
    {"SVGA (800x600)", FRAMESIZE_SVGA},
    {"XGA (1024x768)", FRAMESIZE_XGA},
    {"HD (1280x720)", FRAMESIZE_HD},
    {"SXGA (1280x1024)", FRAMESIZE_SXGA},
    {"UXGA (1600x1200)", FRAMESIZE_UXGA}};

const framesize_t lookup_frame_size(const char *pin)
{
    // Lookup table for the frame name to framesize_t
    for (const auto &entry : frame_sizes)
        if (strncmp(entry.name, pin, sizeof(entry.name)) == 0)
            return entry.frame_size;

    return FRAMESIZE_INVALID;
}

const char *lookup_frame_size_name(framesize_t size)
{
    // Lookup table for framesize_t to a display name
    for (const auto &entry : frame_sizes)
        if (entry.frame_size == size)
            return entry.name;

    return "Unknown";
}

typedef struct pixel_format_entry
{
    const char name[12];
    const pixformat_t pixel_format;
} pixel_format_entry_t;

constexpr const pixel_format_entry_t pixel_formats[] = {
    {"RGB565", PIXFORMAT_RGB565},
    {"YUV422", PIXFORMAT_YUV422},
    {"YUV420", PIXFORMAT_YUV420},
    {"Grayscale", PIXFORMAT_GRAYSCALE},
    {"JPEG", PIXFORMAT_JPEG},
    {"RGB888", PIXFORMAT_RGB888},
    {"RAW", PIXFORMAT_RAW},
    {"RGB444", PIXFORMAT_RGB444},
    {"RGB555", PIXFORMAT_RGB555}};

constexpr auto PIXFORMAT_INVALID = static_cast<pixformat_t>(0xff);

const pixformat_t lookup_pixel_format(const char *pin)
{
    // Lookup table for the pixel format name to pixformat_t
    for (const auto &entry : pixel_formats)
        if (strncmp(entry.name, pin, sizeof(entry.name)) == 0)
            return entry.pixel_format;

    return PIXFORMAT_INVALID;
}

const char *lookup_pixel_format_name(pixformat_t fmt)
{
    // Lookup table for pixformat_t to a display name
    for (const auto &entry : pixel_formats)
        if (entry.pixel_format == fmt)
            return entry.name;

    return "Unknown";
}

typedef struct
{
    const char name[7];
    const int value;
} camera_wb_mode_entry_t;

constexpr const camera_wb_mode_entry_t camera_wb_modes[] = {
    {"Auto", 0},
    {"Sunny", 1},
    {"Cloudy", 2},
    {"Office", 3},
    {"Home", 4}};

const int lookup_camera_wb_mode(const char *name)
{
    // Lookup table for the white balance name to wb_mode int
    for (const auto &entry : camera_wb_modes)
        if (strncmp(entry.name, name, sizeof(entry.name)) == 0)
            return entry.value;

    return -1; // Not found
}

const char *lookup_camera_wb_mode_name(int mode)
{
    // Lookup table for wb_mode int to a display name
    for (const auto &entry : camera_wb_modes)
        if (entry.value == mode)
            return entry.name;

    return "Unknown";
}

void handle_tools_list(mcp_response &response)
{
  auto result = response.create_result();
  auto tools = result["tools"].to<JsonArray>();

  // Add LED control tool
  tool_schema led_tool(tools.add<JsonObject>(), "led", "Controls the ESP32-CAM LED state");
  led_tool.boolean("on", "LED on", false).required("on");

  // Add flash control tool
  tool_schema flash_tool(tools.add<JsonObject>(), "flash", "Controls the ESP32-CAM Flash");
  flash_tool.number("duration", "Flash duration in milliseconds", 5, 100, 50);

  // Add camera capture tool
  tool_schema camera_tool(tools.add<JsonObject>(), "capture", "Captures a photo from the ESP32-CAM");
  camera_tool
      .boolean("flash", "Use flash when capturing", false)
      .enum_table("frame_size", "Resolution to use for the captured photo", frame_sizes,
                  [](const frame_size_entry_t &entry) { return entry.frame_size == MCP_CAPTURE_FRAMESIZE; })
      .number("quality", "JPEG quality for the captured photo (1-100)", 1, 100, MCP_CAPTURE_QUALITY)
      .enum_table("whitebalance", "White balance mode for the captured photo", camera_wb_modes,
                  [](const camera_wb_mode_entry_t &entry) { return entry.value == MCP_CAPTURE_WB_MODE; })
      .enum_table("pixelformat", "Pixel format for the captured photo", pixel_formats,
                  [](const pixel_format_entry_t &entry) { return entry.pixel_format == MCP_CAPTURE_PIXELFORMAT; });

  // Add WiFi status tool
  tool_schema wifi_tool(tools.add<JsonObject>(), "wifi_status",
                        "Gets current WiFi connection status and network information. "
                        "Returns: ip_address (string, IPv4), signal_strength_dbm (integer, dBm), "
                        "mac_address (string), gateway (string, IPv4), dns (string, IPv4)");

  // Add system status tool
  tool_schema system_tool(tools.add<JsonObject>(), "system_status",
                          "Gets comprehensive system status including memory, uptime, and hardware info. "
                          "Returns: uptime_seconds (integer), free_heap_bytes (integer), min_free_heap_bytes (integer), "
                          "max_alloc_heap_bytes (integer), cpu_frequency_mhz (integer), flash_size_bytes (integer), "
                          "flash_speed_hz (integer), sketch_size_bytes (integer), free_sketch_space_bytes (integer), "
                          "sdk_version (string), reset_reason (integer), camera_initialized (boolean), "
                          "internal_temperature_c (number, °C)");
}

void tool_led(JsonObject arguments, mcp_response &response)
{
  if (arguments["on"].as<bool>())
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL);
    auto result = response.create_result();
    auto result_content = result["content"].to<JsonArray>();
    auto result_content_item = result_content.add<JsonObject>();
    result_content_item["type"] = "text";
    result_content_item["text"] = "LED turned on";
  }
  else
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW);
    auto result = response.create_result();
    auto result_content = result["content"].to<JsonArray>();
    auto result_content_item = result_content.add<JsonObject>();
    result_content_item["type"] = "text";
    result_content_item["text"] = "LED turned off";
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
  // Resolve the requested capture resolution from the frame_size argument (enum)
  auto requested_framesize = FRAMESIZE_INVALID;
  if (!arguments["frame_size"].isNull())
  {
    requested_framesize = lookup_frame_size(arguments["frame_size"].as<const char *>());
    if (requested_framesize == FRAMESIZE_INVALID)
    {
      String valid_options;
      for (const auto &entry : frame_sizes)
      {
        if (!valid_options.isEmpty())
          valid_options += ", ";
        valid_options += entry.name;
      }
      auto error = response.create_error();
      error["code"] = error_code::invalid_params;
      error["message"] = "Invalid frame_size. Valid options: " + valid_options + ".";
      return;
    }
  }
  auto capture_framesize = requested_framesize == FRAMESIZE_INVALID ? MCP_CAPTURE_FRAMESIZE : requested_framesize;

  // Resolve the requested JPEG quality (1-100) from the quality argument
  auto capture_quality = MCP_CAPTURE_QUALITY;
  if (!arguments["quality"].isNull())
  {
    auto quality = arguments["quality"].as<int>();
    if (quality < 1 || quality > 100)
    {
      auto error = response.create_error();
      error["code"] = error_code::invalid_params;
      error["message"] = "Invalid quality. Must be between 1 and 100.";
      return;
    }
    capture_quality = quality;
  }

  // Resolve the requested white balance mode from the wb_mode argument (enum)
  auto capture_whitebalance = MCP_CAPTURE_WB_MODE;
  if (!arguments["whitebalance"].isNull())
  {
    auto wb_mode = lookup_camera_wb_mode(arguments["whitebalance"].as<const char *>());
    if (wb_mode < 0)
    {
      String valid_options;
      for (const auto &entry : camera_wb_modes)
      {
        if (!valid_options.isEmpty())
          valid_options += ", ";
        valid_options += entry.name;
      }
      auto error = response.create_error();
      error["code"] = error_code::invalid_params;
      error["message"] = "Invalid whitebalance. Valid options: " + valid_options + ".";
      return;
    }
    capture_whitebalance = wb_mode;
  }

  // Resolve the requested pixel format from the pixelformat argument (enum)
  auto capture_pixelformat = MCP_CAPTURE_PIXELFORMAT;
  if (!arguments["pixelformat"].isNull())
  {
    auto pixel_format = lookup_pixel_format(arguments["pixelformat"].as<const char *>());
    if (pixel_format == PIXFORMAT_INVALID)
    {
      String valid_options;
      for (const auto &entry : pixel_formats)
      {
        if (!valid_options.isEmpty())
          valid_options += ", ";
        valid_options += entry.name;
      }
      auto error = response.create_error();
      error["code"] = error_code::invalid_params;
      error["message"] = "Invalid pixelformat. Valid options: " + valid_options + ".";
      return;
    }
    capture_pixelformat = pixel_format;
  }

  // Build the camera configuration with the requested parameters and (re)initialize
  camera_config_t config = esp32cam_aithinker_settings;
  config.frame_size = capture_framesize;
  config.pixel_format = capture_pixelformat;
  config.jpeg_quality = capture_quality;

  auto camera_init_result = esp_camera_init(&config);
  if (camera_init_result != ESP_OK)
  {
    auto error = response.create_error();
    error["code"] = error_code::internal_error;
    error["message"] = "Camera initialization failed (0x" + String(camera_init_result, 16) + ")";
    return;
  }

  auto sensor = esp_camera_sensor_get();
  if (sensor)
    sensor->set_wb_mode(sensor, capture_whitebalance);

  log_d("Capture: free heap before capture: %d", ESP.getFreeHeap());

  if (arguments["flash"].as<bool>())
  {
    digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL);
    delay(20); // Allow flash to stabilize
  }

  // Discard 1-2 warm-up frames to ensure a fresh capture
  auto fb = esp_camera_fb_get();
  if (fb)
    esp_camera_fb_return(fb);
  else
    log_w("Capture: warm-up frame failed");

  fb = esp_camera_fb_get();
  if (fb)
    esp_camera_fb_return(fb);
else
    log_w("Capture: warm-up frame failed");

  // Take the actual frame
  fb = esp_camera_fb_get();
  if (fb)
    log_d("Capture: captured frame: %u bytes, free heap after capture: %d", (unsigned)fb->len, ESP.getFreeHeap());
  else
    log_w("Capture: failed to capture frame, free heap after capture: %d", ESP.getFreeHeap());
  
  // Turn flash off immediately after capture attempt
  digitalWrite(FLASH_GPIO, !FLASH_ON_LEVEL);

  if (!fb)
  {
    esp_camera_deinit();
    auto error = response.create_error();
    error["code"] = error_code::internal_error;
    error["message"] = "Camera capture failed";
    return;
  }

  auto fb_len = fb->len;
  auto fb_width = fb->width;
  auto fb_height = fb->height;
  auto base64_image = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  esp_camera_deinit();
  log_d("Capture: JPEG %u bytes -> base64 %u bytes, free heap after: %d", (unsigned)fb_len, (unsigned)base64_image.length(), ESP.getFreeHeap());

  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();
  auto result_content_item = result_content.add<JsonObject>();
  result_content_item["type"] = "text";
  auto capture_text = String("Image captured successfully. ");
  capture_text += "Pixel format: " + String(lookup_pixel_format_name(capture_pixelformat)) + ", ";
  capture_text += "Frame size: " + String(lookup_frame_size_name(capture_framesize)) + ", ";
  capture_text += "Quality: " + String(capture_quality) + ", ";
  capture_text += "White balance: " + String(lookup_camera_wb_mode_name(capture_whitebalance)) + ", ";
  capture_text += "Flash: " + String(arguments["flash"].as<bool>() ? "on" : "off") + ", ";
  capture_text += "Dimensions: " + String(fb_width) + "x" + String(fb_height) + ", ";
  capture_text += "Size: " + String(base64_image.length()) + " bytes (base64 encoded)";
  result_content_item["text"] = capture_text;

  auto structured = result["structuredContent"].to<JsonObject>();
  structured["image"] = base64_image;
  structured["format"] = lookup_pixel_format_name(capture_pixelformat);
  structured["width"] = fb_width;
  structured["height"] = fb_height;
  structured["mimeType"] = capture_pixelformat == PIXFORMAT_JPEG ? "image/jpeg" : "image/x-raw";
  // Free the intermediate base64 String; the JSON document already holds its own copy
  base64_image = String();
}

void tool_wifi_status(mcp_response &response)
{
  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();

  // Human-readable text summary
  auto summary_item = result_content.add<JsonObject>();
  summary_item["type"] = "text";
  auto status_text = String();
  status_text.reserve(256); // Pre-allocate to avoid repeated reallocations
  status_text += "IP Address: " + WiFi.localIP().toString() + "\n"
                 "Signal Strength: " + String(WiFi.RSSI()) + " dBm\n"
                 "MAC Address: " + WiFi.macAddress() + "\n"
                 "Gateway: " + WiFi.gatewayIP().toString() + "\n"
                 "DNS: " + WiFi.dnsIP().toString() + "\n";
  summary_item["text"] = status_text;

  // Return the parameters individually as structuredContent (MCP 2025-06-18+)
  auto structured = result["structuredContent"].to<JsonObject>();
  structured["ip_address"] = WiFi.localIP().toString();
  structured["signal_strength_dbm"] = WiFi.RSSI();
  structured["mac_address"] = WiFi.macAddress();
  structured["gateway"] = WiFi.gatewayIP().toString();
  structured["dns"] = WiFi.dnsIP().toString();
}

void tool_system_status(mcp_response &response)
{
  auto result = response.create_result();
  auto result_content = result["content"].to<JsonArray>();

  auto internal_temperature = (temprature_sens_read() - 32) / 1.8;

  // Human-readable text summary
  auto summary_item = result_content.add<JsonObject>();
  summary_item["type"] = "text";
  auto status_text = String();
  status_text.reserve(1024); // Pre-allocate to avoid repeated reallocations
  status_text += "Uptime: " + String(millis() / 1000) + " seconds\n"
                 "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n"
                 "Min Free Heap: " + String(ESP.getMinFreeHeap()) + " bytes\n"
                 "Max Alloc Heap: " + String(ESP.getMaxAllocHeap()) + " bytes\n"
                 "CPU Frequency: " + String(getCpuFrequencyMhz()) + " MHz\n"
                 "Flash Size: " + String(ESP.getFlashChipSize()) + " bytes\n"
                 "Flash Speed: " + String(ESP.getFlashChipSpeed()) + " Hz\n"
                 "Sketch Size: " + String(ESP.getSketchSize()) + " bytes\n"
                 "Free Sketch Space: " + String(ESP.getFreeSketchSpace()) + " bytes\n"
                 "SDK Version: " + String(ESP.getSdkVersion()) + "\n"
                 "Reset Reason: " + String(esp_reset_reason()) + "\n"
                 "Internal Temperature: " + String(internal_temperature, 2) + " °C\n";
  summary_item["text"] = status_text;

  // Return the parameters individually as structuredContent (MCP 2025-06-18+)
  auto structured = result["structuredContent"].to<JsonObject>();
  structured["uptime_seconds"] = millis() / 1000;
  structured["free_heap_bytes"] = ESP.getFreeHeap();
  structured["min_free_heap_bytes"] = ESP.getMinFreeHeap();
  structured["max_alloc_heap_bytes"] = ESP.getMaxAllocHeap();
  structured["cpu_frequency_mhz"] = getCpuFrequencyMhz();
  structured["flash_size_bytes"] = ESP.getFlashChipSize();
  structured["flash_speed_hz"] = ESP.getFlashChipSpeed();
  structured["sketch_size_bytes"] = ESP.getSketchSize();
  structured["free_sketch_space_bytes"] = ESP.getFreeSketchSpace();
  structured["sdk_version"] = ESP.getSdkVersion();
  structured["reset_reason"] = esp_reset_reason();
  structured["internal_temperature_c"] = internal_temperature;
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

  // Enforce optional token authentication
  if (!request_authorized())
  {
    server.sendHeader("WWW-Authenticate", String("Bearer realm=\"") + MCP_API_REALM + "\"");
    log_w("Unauthorized request from %s", server.client().remoteIP().toString().c_str());
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  // Reject oversized request bodies before parsing them
  auto content_length = server.clientContentLength();
  if (content_length > MAX_MCP_REQUEST_SIZE)
  {
    log_w("Rejecting request: Content-Length %d exceeds %d bytes limit", content_length, MAX_MCP_REQUEST_SIZE);
    server.send(413, "text/plain", "Payload Too Large");
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

  log_i("Local IP address: %s", WiFi.localIP().toString().c_str());
  log_d("Signal strength: %d dBm", WiFi.RSSI());

  auto hostName = "esp32-" + macAddress + ".local";
  log_i("mDNS hostname: %s", hostName.c_str());
  MDNS.begin(hostName.c_str());
  MDNS.addService("_jsonrpc", "_tcp", 80);
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "version", "2.0");
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "protocol", "http");
  MDNS.addServiceTxt("_jsonrpc", "_tcp", "path", "/");

  // Allow over the air updates (optionally password-protected)
  ArduinoOTA.setPassword(MCP_OTA_PASSWORD);
  ArduinoOTA.begin();

  // Camera is initialized on demand inside tool_capture with the requested parameters

  // Register request headers to collect so hasHeader()/header() work for Accept-Encoding content negotiation
  const char *headerkeys[] = {"Accept-Encoding"};
  server.collectHeaders(headerkeys, 1);

  server.on("/", HTTP_ANY, handleRoot);
  server.begin();
}

void loop()
{
  // Handle web server requests only if WiFi is connected
  if (WiFi.status() == WL_CONNECTED)
    server.handleClient();

  // Handle OTA (works even with WiFi issues for recovery)
  ArduinoOTA.handle();
}