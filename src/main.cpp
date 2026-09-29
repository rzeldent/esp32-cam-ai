#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <esp_camera.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc_caps.h>
#include <driver/i2c.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include <mcp.h>
#include <libb64/cdecode.h>

#include "board_config.h"
#include "settings.h"
#include "mcp_schema_tool.h"
#include "mcp_schema_response.h"
#include "mcp_schema_error.h"

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

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

static const std::map<std::string, framesize_t> frame_sizes = {
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

static const std::map<std::string, pixformat_t> pixel_formats = {
    {"RGB565", PIXFORMAT_RGB565},
    {"YUV422", PIXFORMAT_YUV422},
    {"YUV420", PIXFORMAT_YUV420},
    {"Grayscale", PIXFORMAT_GRAYSCALE},
    {"JPEG", PIXFORMAT_JPEG},
    {"RGB888", PIXFORMAT_RGB888},
    {"RAW", PIXFORMAT_RAW},
    {"RGB444", PIXFORMAT_RGB444},
    {"RGB555", PIXFORMAT_RGB555}};

static const std::map<std::string, int> camera_wb_modes = {
    {"Auto", 0},
    {"Sunny", 1},
    {"Cloudy", 2},
    {"Office", 3},
    {"Home", 4}};

// ---------------------------------------------------------------------------
// GPIO control tool configuration
// ---------------------------------------------------------------------------
// Which pins the "gpio" tool accepts is derived from the selected board
// (boards/*.json) and the chip capabilities instead of a fixed list:
//   - the pin must exist on the chip and be usable (digitalPinIsValid),
//   - the pins wired to the camera (CAMERA_CONFIG_PIN_*) are reserved,
//   - each mode needs its own capability: an ADC channel for "ai", an output
//     capable pin for "do"/"ao" (digitalPinToAnalogChannel / digitalPinCanOutput).
// The led and flash tools own USER_LED_GPIO / FLASH_LED_GPIO and are unaffected.

// True when the pin is part of the camera wiring (DVP data/clock/sync or SCCB).
static bool is_camera_pin(int pin)
{
  return pin == CAMERA_CONFIG_PIN_PWDN || pin == CAMERA_CONFIG_PIN_RESET ||
         pin == CAMERA_CONFIG_PIN_XCLK || pin == CAMERA_CONFIG_PIN_SCCB_SDA ||
         pin == CAMERA_CONFIG_PIN_SCCB_SCL || pin == CAMERA_CONFIG_PIN_PCLK ||
         pin == CAMERA_CONFIG_PIN_VSYNC || pin == CAMERA_CONFIG_PIN_HREF ||
         pin == CAMERA_CONFIG_PIN_Y2 || pin == CAMERA_CONFIG_PIN_Y3 ||
         pin == CAMERA_CONFIG_PIN_Y4 || pin == CAMERA_CONFIG_PIN_Y5 ||
         pin == CAMERA_CONFIG_PIN_Y6 || pin == CAMERA_CONFIG_PIN_Y7 ||
         pin == CAMERA_CONFIG_PIN_Y8 || pin == CAMERA_CONFIG_PIN_Y9;
}

// True when the "gpio" tool may address the pin at all.
static bool gpio_pin_available(int pin)
{
  return pin >= 0 && pin < SOC_GPIO_PIN_COUNT && digitalPinIsValid(pin) && !is_camera_pin(pin);
}

// Comma separated list of the pins this board exposes to the "gpio" tool.
static std::string gpio_pin_list()
{
  std::string list;
  for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; ++pin)
  {
    if (!gpio_pin_available(pin))
      continue;
    if (!list.empty())
      list += ", ";
    list += std::to_string(pin);
  }
  return list.empty() ? "none" : list;
}

// LEDC (PWM) channel per pin for analog output mode, allocated on first use. The
// highest channels are used first so the low ones stay free for the camera XCLK
// PWM, which is set up in low speed mode (CAMERA_CONFIG_LEDC_TIMER/_CHANNEL).
static std::map<int, uint8_t> gpio_pwm_channels;

static int gpio_pwm_channel_for(int pin)
{
  auto channel_it = gpio_pwm_channels.find(pin);
  if (channel_it != gpio_pwm_channels.end())
    return channel_it->second;

  for (int channel = SOC_LEDC_CHANNEL_NUM - 1; channel >= 0; --channel)
  {
    if (channel == CAMERA_CONFIG_LEDC_CHANNEL)
      continue;

    auto in_use = false;
    for (const auto &entry : gpio_pwm_channels)
      if (entry.second == channel)
      {
        in_use = true;
        break;
      }

    if (!in_use)
    {
      gpio_pwm_channels[pin] = (uint8_t)channel;
      return channel;
    }
  }
  return -1;
}

// PWM settings for analog output mode (duty cycle given as a percentage).
// 13-bit resolution (duty 0..8191) makes sub-1% values usable: 0.1% -> ~8 steps.
// 13 bits is the max resolution that still supports 5 kHz on the 80 MHz APB clock
// (max freq = 80 MHz / 2^13 ~ 9.7 kHz; 14 bits would cap at ~4.8 kHz).
static constexpr uint32_t GPIO_PWM_FREQ = 5000;                                // Hz
static constexpr uint8_t GPIO_PWM_RESOLUTION = 13;                             // bits -> duty 0..8191
static constexpr uint32_t GPIO_PWM_MAX_DUTY = (1U << GPIO_PWM_RESOLUTION) - 1; // 8191
// Full-scale ADC reference (mV) used to express an analog input as a percentage.
static constexpr uint32_t GPIO_ADC_REFERENCE_MV = 3300;

// Formats a float for display, dropping trailing zeros (e.g. "0.5", "1", "12.34").
static std::string format_float(float value)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%g", value);
  return std::string(buf);
}

// Minimal base64 encoder (RFC 4648), used to carry the SRTP master key and salt in the "a=crypto" attribute.
std::string base64_encode(const uint8_t *data, size_t len)
{
  static const char table_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3)
  {
    auto n = (uint32_t)data[i] << 16;
    if (i + 1 < len)
      n |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len)
      n |= data[i + 2];

    out += table_b64[(n >> 18) & 0x3f];
    out += table_b64[(n >> 12) & 0x3f];
    out += (i + 1 < len) ? table_b64[(n >> 6) & 0x3f] : '=';
    out += (i + 2 < len) ? table_b64[n & 0x3f] : '=';
  }
  return out;
}

WebServer server;
// Hex-encoded MAC address (Arduino boundary: ESP.getEfuseMac() -> hex String) used for a unique mDNS hostname.
auto macAddress = std::string(String(ESP.getEfuseMac(), 16).c_str());
auto thingName = "esp-" + macAddress;

// Configured MCP API credentials from settings.h.
static const std::string mcp_api_user(MCP_API_USER);
static const std::string mcp_api_password(MCP_API_PASSWORD);

// Generic Accept-Encoding check
static bool client_accepts(const char *encoding)
{
  if (!server.hasHeader("Accept-Encoding"))
    return false;
  auto accept = std::string(server.header("Accept-Encoding").c_str());
  std::transform(accept.begin(), accept.end(), accept.begin(), ::tolower);
  return accept.find(encoding) != std::string::npos;
}

// Strips leading/trailing ASCII whitespace (mirrors Arduino String::trim()).
static std::string trim_whitespace(const std::string &value)
{
  auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
    return "";
  auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

// Returns true if the request is authorized. Authentication is disabled when
// MCP_API_USER is empty; otherwise the request must present valid HTTP Basic
// credentials (Authorization: Basic base64(user:password)).
static bool request_authorized()
{
  if (mcp_api_user.empty())
    return true;

  auto auth = std::string(server.header("Authorization").c_str());
  if (auth.rfind("Basic ", 0) != 0)
    return false;

  auto encoded = trim_whitespace(auth.substr(6));

  auto expected = mcp_api_user + ":" + mcp_api_password;
  auto decoded_len = base64_decode_expected_len(encoded.length()) + 1;
  std::unique_ptr<char[]> decoded(new char[decoded_len]);
  auto len = base64_decode_chars(encoded.c_str(), static_cast<int>(encoded.length()), decoded.get());
  if (len < 0)
    return false;
  decoded[len] = '\0';
  return expected == decoded.get();
}

#ifdef ENABLE_GZIP
// Optional deflate (zlib) compression using miniz. Returns true on success and writes binary data to output.
static bool deflate_compress(const std::string &input, std::string &output)
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

  output.assign(reinterpret_cast<const char *>(buf.get()), static_cast<size_t>(out_len));
  return true;
}
#endif

void handle_initialize(mcp_response &response)
{
  mcp_response_schema r(response);
  auto result = r.result_object();
  result["protocolVersion"] = MCP_PROTOCOL_VERSION;
  result["capabilities"]["tools"]["listChanged"] = false;
  result["serverInfo"]["name"] = MCP_NAME;
  result["serverInfo"]["version"] = MCP_VERSION;
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
  tool_schema led_tool(tools.add<JsonObject>(), "led", "Controls the ESP32-CAM LED state");
  led_tool
      .boolean("on", "LED on", false)
      .required("on");

  // Add flash control tool
  tool_schema flash_tool(tools.add<JsonObject>(), "flash", "Controls the ESP32-CAM Flash");
  flash_tool
      .number("duration", "Flash duration in milliseconds", 5, 100, 50);

  // Add camera capture tool
  tool_schema camera_tool(tools.add<JsonObject>(), "capture", "Captures a photo from the ESP32-CAM");
  camera_tool
      .boolean("flash", "Use flash when capturing", false)
      .enum_table("frame_size", "Resolution to use for the captured photo", frame_sizes, [](framesize_t size)
                  { return size == MCP_CAPTURE_FRAMESIZE; })
      .number("quality", "JPEG quality for the captured photo (1-100)", 1, 100, MCP_CAPTURE_JPEG_QUALITY)
      .enum_table("whitebalance", "White balance mode for the captured photo", camera_wb_modes, [](int mode)
                  { return mode == MCP_CAPTURE_WB_MODE; })
      .enum_table("pixelformat", "Pixel format for the captured photo", pixel_formats, [](pixformat_t fmt)
                  { return fmt == MCP_CAPTURE_PIXELFORMAT; });

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
                          "internal_temperature_c (number, °C; only on boards with an internal temperature sensor)");

  // Add GPIO control tool. The usable pins depend on the selected board, so the
  // list is resolved here; the strings are static because the const char* handed
  // to the schema has to stay valid for the lifetime of the document.
  static const std::string gpio_pins = gpio_pin_list();
  static const std::string gpio_tool_description =
      "Controls the GPIO pins of the ESP32-CAM. Pins that do not exist on the chip, "
      "pins wired to the camera and pins lacking the capability required by the mode "
      "are rejected. Available pins: " +
      gpio_pins +
      ". "
      "Modes: di (digital input, returns value true/false from the logical level), "
      "ai (analog input, returns value 0-100, percentage of the calibrated max input), "
      "do (digital output, sets value true/false), "
      "ao (analog output, sets value 0-100, PWM duty cycle percentage). "
      "Note: analog input pins are ADC2 channels which are unavailable while Wi-Fi is "
      "active (readings may be 0).";
  static const std::string gpio_pin_description =
      "GPIO pin to control. Available on this board: " + gpio_pins;

  tool_schema gpio_tool(tools.add<JsonObject>(), "gpio", gpio_tool_description.c_str());
  gpio_tool
      .number("pin", gpio_pin_description.c_str())
      .enum_string("mode", "Pin mode", {"di", "ai", "do", "ao"})
      .any("value", "Required for output modes. do: true (HIGH) or false (LOW). ao: 0-100 (duty cycle percentage; float accepted, e.g. 0.5 = 0.5%).")
      .required("pin")
      .required("mode");
}

void tool_led(JsonObject arguments, mcp_response &response)
{
  if (!has_user_led)
  {
    mcp_schema_error(response)
        .code(error_code::internal_error)
        .message("This board does not have a user LED.");
    return;
  }

  mcp_response_schema r(response);
  if (arguments["on"].as<bool>())
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL);
    r.text("LED turned on");
  }
  else
  {
    digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW);
    r.text("LED turned off");
  }
}

void tool_flash(JsonObject arguments, mcp_response &response)
{
  if (!has_flash_led)
  {
    mcp_schema_error(response)
        .code(error_code::internal_error)
        .message("This board does not have a flash LED.");
    return;
  }

  auto duration = arguments["duration"].is<int>() ? arguments["duration"].as<int>() : 50; // Default to 50ms if not provided
  digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL);
  delay(duration); // 5-100ms
  digitalWrite(FLASH_GPIO, !FLASH_ON_LEVEL);
  mcp_response_schema(response).text("Flash executed");
}

void tool_capture(JsonObject arguments, mcp_response &response)
{
  // Resolve the requested capture resolution from the frame_size argument (enum)
  auto capture_framesize = MCP_CAPTURE_FRAMESIZE;
  if (!arguments["frame_size"].isNull())
  {
    auto frame_size_it = frame_sizes.find(arguments["frame_size"].as<std::string>());
    if (frame_size_it == frame_sizes.end())
    {
      std::string valid_options;
      for (const auto &entry : frame_sizes)
      {
        if (!valid_options.empty())
          valid_options += ", ";
        valid_options += entry.first;
      }
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Invalid frame_size. Valid options: " + valid_options + ".");
      return;
    }
    capture_framesize = frame_size_it->second;
  }

  // Resolve the requested JPEG quality (1-100) from the quality argument
  auto capture_quality = MCP_CAPTURE_JPEG_QUALITY;
  if (!arguments["quality"].isNull())
  {
    auto quality = arguments["quality"].as<int>();
    if (quality < 1 || quality > 100)
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Invalid quality. Must be between 1 and 100.");
      return;
    }
    capture_quality = quality;
  }

  // Resolve the requested white balance mode from the wb_mode argument (enum)
  auto capture_whitebalance = MCP_CAPTURE_WB_MODE;
  if (!arguments["whitebalance"].isNull())
  {
    auto wb_mode_it = camera_wb_modes.find(arguments["whitebalance"].as<std::string>());
    if (wb_mode_it == camera_wb_modes.end())
    {
      std::string valid_options;
      for (const auto &entry : camera_wb_modes)
      {
        if (!valid_options.empty())
          valid_options += ", ";
        valid_options += entry.first;
      }
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Invalid whitebalance. Valid options: " + valid_options + ".");
      return;
    }
    capture_whitebalance = wb_mode_it->second;
  }

  // Resolve the requested pixel format from the pixelformat argument (enum)
  auto capture_pixelformat = MCP_CAPTURE_PIXELFORMAT;
  if (!arguments["pixelformat"].isNull())
  {
    auto pixel_format_it = pixel_formats.find(arguments["pixelformat"].as<std::string>());
    if (pixel_format_it == pixel_formats.end())
    {
      std::string valid_options;
      for (const auto &entry : pixel_formats)
      {
        if (!valid_options.empty())
          valid_options += ", ";
        valid_options += entry.first;
      }
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Invalid pixelformat. Valid options: " + valid_options + ".");
      return;
    }
    capture_pixelformat = pixel_format_it->second;
  }

  // Build the camera configuration with the requested parameters and (re)initialize
  // Camera configuration for the selected board.
  // The default frame size, pixel format and JPEG quality are overridden per capture request by the MCP "capture" tool (see MCP_CAPTURE_* in settings.h).
  constexpr camera_config_t config = {
      .pin_pwdn = CAMERA_CONFIG_PIN_PWDN,
      .pin_reset = CAMERA_CONFIG_PIN_RESET,
      .pin_xclk = CAMERA_CONFIG_PIN_XCLK,
      .pin_sccb_sda = CAMERA_CONFIG_PIN_SCCB_SDA,
      .pin_sccb_scl = CAMERA_CONFIG_PIN_SCCB_SCL,
      .pin_d7 = CAMERA_CONFIG_PIN_Y9,
      .pin_d6 = CAMERA_CONFIG_PIN_Y8,
      .pin_d5 = CAMERA_CONFIG_PIN_Y7,
      .pin_d4 = CAMERA_CONFIG_PIN_Y6,
      .pin_d3 = CAMERA_CONFIG_PIN_Y5,
      .pin_d2 = CAMERA_CONFIG_PIN_Y4,
      .pin_d1 = CAMERA_CONFIG_PIN_Y3,
      .pin_d0 = CAMERA_CONFIG_PIN_Y2,
      .pin_vsync = CAMERA_CONFIG_PIN_VSYNC,
      .pin_href = CAMERA_CONFIG_PIN_HREF,
      .pin_pclk = CAMERA_CONFIG_PIN_PCLK,
      .xclk_freq_hz = CAMERA_CONFIG_CLK_FREQ_HZ,
      .ledc_timer = CAMERA_CONFIG_LEDC_TIMER,
      .ledc_channel = CAMERA_CONFIG_LEDC_CHANNEL,
      .pixel_format = MCP_CAPTURE_PIXELFORMAT,
      .frame_size = MCP_CAPTURE_FRAMESIZE,
      .jpeg_quality = MCP_CAPTURE_JPEG_QUALITY,
      .fb_count = CAMERA_CONFIG_FB_COUNT,
      .fb_location = CAMERA_CONFIG_FB_LOCATION,
      .sccb_i2c_port = CAMERA_CONFIG_SCCB_I2C_PORT};

  auto camera_init_result = esp_camera_init(&config);
  if (camera_init_result != ESP_OK)
  {
    mcp_schema_error(response)
        .code(error_code::internal_error)
        .message("Camera initialization failed (0x" + std::string(String(camera_init_result, 16).c_str()) + ")");
    return;
  }

  auto sensor = esp_camera_sensor_get();
  if (sensor)
    sensor->set_wb_mode(sensor, capture_whitebalance);

  log_d("Capture: free heap before capture: %d", ESP.getFreeHeap());

  if (has_flash_led && arguments["flash"].as<bool>())
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
  if (has_flash_led)
    digitalWrite(FLASH_GPIO, !FLASH_ON_LEVEL);

  if (!fb)
  {
    esp_camera_deinit();
    mcp_schema_error(response)
        .code(error_code::internal_error)
        .message("Camera capture failed");
    return;
  }

  auto fb_len = fb->len;
  auto fb_width = fb->width;
  auto fb_height = fb->height;
  auto base64_image = base64_encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);
  esp_camera_deinit();
  log_d("Capture: JPEG %u bytes -> base64 %u bytes, free heap after: %d", (unsigned)fb_len, (unsigned)base64_image.length(), ESP.getFreeHeap());

  // Resolve display names for the summary (reverse lookup over the maps)
  const char *frame_size_name = "Unknown";
  for (const auto &entry : frame_sizes)
    if (entry.second == capture_framesize)
    {
      frame_size_name = entry.first.c_str();
      break;
    }
  const char *pixel_format_name = "Unknown";
  for (const auto &entry : pixel_formats)
    if (entry.second == capture_pixelformat)
    {
      pixel_format_name = entry.first.c_str();
      break;
    }
  const char *wb_mode_name = "Unknown";
  for (const auto &entry : camera_wb_modes)
    if (entry.second == capture_whitebalance)
    {
      wb_mode_name = entry.first.c_str();
      break;
    }

  // Human-readable text summary
  std::string text =
      "Image captured successfully. "
      "Pixel format: " +
      std::string(pixel_format_name) + ", Frame size: " +
      std::string(frame_size_name) + ", Quality: " +
      std::to_string(capture_quality) + ", White balance: " +
      std::string(wb_mode_name) + ", Flash: " +
      std::string(arguments["flash"].as<bool>() ? "on" : "off") + ", Dimensions: " +
      std::to_string(fb_width) + "x" + std::to_string(fb_height) + ", Size: " +
      std::to_string(base64_image.length()) + " bytes (base64 encoded)";

  mcp_response_schema r(response);
  r.text(text)
      .field("image", base64_image)
      .field("format", pixel_format_name)
      .field("width", fb_width)
      .field("height", fb_height)
      .field("mimeType", capture_pixelformat == PIXFORMAT_JPEG ? "image/jpeg" : "image/x-raw");
  // The JSON document already holds its own copy of the image data
  base64_image.clear();
}

void tool_wifi_status(mcp_response &response)
{
  // Human-readable text summary
  std::string text =
      "IP Address: " + std::string(WiFi.localIP().toString().c_str()) + "\n"
                                                                        "Signal Strength: " +
      std::to_string(WiFi.RSSI()) + " dBm\n"
                                    "MAC Address: " +
      std::string(WiFi.macAddress().c_str()) + "\n"
                                               "Gateway: " +
      std::string(WiFi.gatewayIP().toString().c_str()) + "\n"
                                                         "DNS: " +
      std::string(WiFi.dnsIP().toString().c_str()) + "\n";

  // Text summary plus the parameters individually as structuredContent (MCP 2025-06-18+)
  mcp_response_schema r(response);
  r.text(text)
      .field("ip_address", WiFi.localIP().toString())
      .field("signal_strength_dbm", WiFi.RSSI())
      .field("mac_address", WiFi.macAddress())
      .field("gateway", WiFi.gatewayIP().toString())
      .field("dns", WiFi.dnsIP().toString());
}

void tool_system_status(mcp_response &response)
{
  // Human-readable text summary
  std::string text =
      "Uptime: " + std::to_string(millis() / 1000) + " seconds\n"
                                                     "Free Heap: " +
      std::to_string(ESP.getFreeHeap()) + " bytes\n"
                                          "Min Free Heap: " +
      std::to_string(ESP.getMinFreeHeap()) + " bytes\n"
                                             "Max Alloc Heap: " +
      std::to_string(ESP.getMaxAllocHeap()) + " bytes\n"
                                              "CPU Frequency: " +
      std::to_string(getCpuFrequencyMhz()) + " MHz\n"
                                             "Flash Size: " +
      std::to_string(ESP.getFlashChipSize()) + " bytes\n"
                                               "Flash Speed: " +
      std::to_string(ESP.getFlashChipSpeed()) + " Hz\n"
                                                "Sketch Size: " +
      std::to_string(ESP.getSketchSize()) + " bytes\n"
                                            "Free Sketch Space: " +
      std::to_string(ESP.getFreeSketchSpace()) + " bytes\n"
                                                 "SDK Version: " +
      std::string(ESP.getSdkVersion()) + "\n"
                                         "Reset Reason: " +
      std::to_string(esp_reset_reason()) + "\n";

  // Not every board has a usable internal temperature sensor
  auto celsius = temperatureRead();
  if (!std::isnan(celsius))
    text += "Internal Temperature: " + std::string(String(celsius, 2).c_str()) + " °C\n";

  // Text summary plus the parameters individually as structuredContent (MCP 2025-06-18+)
  mcp_response_schema r(response);
  r.text(text)
      .field("uptime_seconds", millis() / 1000)
      .field("free_heap_bytes", ESP.getFreeHeap())
      .field("min_free_heap_bytes", ESP.getMinFreeHeap())
      .field("max_alloc_heap_bytes", ESP.getMaxAllocHeap())
      .field("cpu_frequency_mhz", getCpuFrequencyMhz())
      .field("flash_size_bytes", ESP.getFlashChipSize())
      .field("flash_speed_hz", ESP.getFlashChipSpeed())
      .field("sketch_size_bytes", ESP.getSketchSize())
      .field("free_sketch_space_bytes", ESP.getFreeSketchSpace())
      .field("sdk_version", ESP.getSdkVersion())
      .field("reset_reason", esp_reset_reason());
  if (!std::isnan(celsius))
    r.field("internal_temperature_c", celsius);
}

void tool_gpio(JsonObject arguments, mcp_response &response)
{
  // Resolve and validate the target pin: it must exist on the chip and must not be
  // part of the camera wiring on the selected board
  auto pin = arguments["pin"].as<int>();
  if (!gpio_pin_available(pin))
  {
    mcp_schema_error(response)
        .code(error_code::invalid_params)
        .message("Invalid or missing pin. Available pins: " + gpio_pin_list() + ".");
    return;
  }

  // Resolve and validate the mode
  if (arguments["mode"].isNull())
  {
    mcp_schema_error(response)
        .code(error_code::invalid_params)
        .message("Mode is required. Valid modes: di (digital input), ai (analog input), do (digital output), ao (analog output).");
    return;
  }
  auto mode = arguments["mode"].as<std::string>();

  if (mode == "di")
  {
    // Release the pin from any previously assigned PWM channel
    ledcDetachPin(pin);
    pinMode(pin, INPUT);
    auto state = digitalRead(pin) == HIGH;
    auto text = std::string("Pin GPIO") + std::to_string(pin) + " is digital input, level: " + (state ? "HIGH (true)" : "LOW (false)");
    mcp_response_schema r(response);
    r.text(text)
        .field("pin", pin)
        .field("mode", "di")
        .field("value", state);
  }
  else if (mode == "ai")
  {
    if (digitalPinToAnalogChannel(pin) < 0)
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Pin GPIO" + std::to_string(pin) + " has no ADC channel, so ai mode is not available for it.");
      return;
    }
    // Release the pin from any previously assigned PWM channel
    ledcDetachPin(pin);
    pinMode(pin, ANALOG);
    // analogReadMilliVolts applies the ADC calibration tables, giving a linear
    // millivolt reading (0-3300 mV) instead of the raw, non-linear ADC count.
    auto milli_volts = analogReadMilliVolts(pin);
    auto percent = (float)milli_volts / (float)GPIO_ADC_REFERENCE_MV * 100.0f;
    if (percent > 100.0f)
      percent = 100.0f;
    if (percent < 0.0f)
      percent = 0.0f;

    auto text = std::string("Pin GPIO") + std::to_string(pin) + " is analog input, value: " + std::to_string((int)(percent + 0.5f)) + "% (" + std::to_string(milli_volts) + " mV)";
    mcp_response_schema r(response);
    r.text(text)
        .field("pin", pin)
        .field("mode", "ai")
        .field("value", percent)
        .field("millivolts", milli_volts);
  }
  else if (mode == "do")
  {
    if (arguments["value"].isNull())
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Value (true/false) is required for do mode.");
      return;
    }
    if (!digitalPinCanOutput(pin))
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Pin GPIO" + std::to_string(pin) + " is input only, so do mode is not available for it.");
      return;
    }
    auto state = arguments["value"].as<bool>();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, state ? HIGH : LOW);

    auto text = std::string("Pin GPIO") + std::to_string(pin) + " set to " + (state ? "HIGH (true)" : "LOW (false)");
    mcp_response_schema r(response);
    r.text(text)
        .field("pin", pin)
        .field("mode", "do")
        .field("value", state);
  }
  else if (mode == "ao")
  {
    if (arguments["value"].isNull())
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Value (0-100, duty cycle percentage) is required for ao mode.");
      return;
    }
    auto percent = arguments["value"].as<float>();
    if (percent < 0.0f || percent > 100.0f)
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Value must be between 0 and 100 for ao mode.");
      return;
    }

    if (!digitalPinCanOutput(pin))
    {
      mcp_schema_error(response)
          .code(error_code::invalid_params)
          .message("Pin GPIO" + std::to_string(pin) + " is input only, so ao mode is not available for it.");
      return;
    }
    auto channel = gpio_pwm_channel_for(pin);
    if (channel < 0)
    {
      mcp_schema_error(response)
          .code(error_code::internal_error)
          .message("No free PWM channel available for GPIO" + std::to_string(pin) + ".");
      return;
    }
    ledcSetup(channel, GPIO_PWM_FREQ, GPIO_PWM_RESOLUTION);
    ledcAttachPin(pin, channel);
    // Duty cycle is kept as a float so sub-1% values stay representable; it is rounded to the nearest integer step only when written to the LEDC hardware.
    auto duty = percent / 100.0f * (float)GPIO_PWM_MAX_DUTY;
    auto duty_step = (uint32_t)(duty + 0.5f);
    if (duty_step > GPIO_PWM_MAX_DUTY)
      duty_step = GPIO_PWM_MAX_DUTY;
    ledcWrite(channel, duty_step);
    log_d("GPIO%d analog output: %.3f%% -> duty %.3f/%u, step %u", pin, percent, duty, (unsigned)GPIO_PWM_MAX_DUTY, (unsigned)duty_step);

    auto text = std::string("Pin GPIO") + std::to_string(pin) + " PWM duty set to " + format_float(percent) + "% (duty " + format_float(duty) + "/" + std::to_string(GPIO_PWM_MAX_DUTY) + ")";
    mcp_response_schema r(response);
    r.text(text)
        .field("pin", pin)
        .field("mode", "ao")
        .field("value", percent)
        .field("duty", duty)
        .field("duty_max", GPIO_PWM_MAX_DUTY);
  }
  else
  {
    mcp_schema_error(response)
        .code(error_code::invalid_params)
        .message("Invalid mode. Valid modes: di, ai, do, ao.");
  }
}

void handle_tools_call(const mcp_request &request, mcp_response &response)
{
  auto params = request.params();
  auto tool_name = params["name"].as<std::string>();
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
  else if (tool_name == "gpio")
    tool_gpio(arguments, response);
  else
  {
    // Tool not found, set error
    if (tool_name.empty())
      mcp_schema_error(response)
          .code(error_code::invalid_request)
          .message("Tool name is required");
    else
      mcp_schema_error(response)
          .code(error_code::method_not_found)
          .message("Unknown tool: " + tool_name);
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
    mcp_request mcp_request(std::string(server.arg("plain").c_str()));
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
      mcp_schema_error(mcp_response)
          .code(error_code::method_not_found)
          .message("Method not found: " + mcp_request.method());
  }
  catch (const mcp_exception &e)
  {
    mcp_schema_error(mcp_response)
        .code(e.code())
        .message(e.what());
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
    std::string deflated;
    if (deflate_compress(body, deflated))
    {
      server.sendHeader("Content-Encoding", "deflate");
      log_d("Sending deflate response: %d %s len=%u (deflate)", http_code, content_type, (unsigned)deflated.length());
      // Send with an explicit length: the deflated payload is binary and may contain
      // embedded NUL bytes, so the const char* overload (which uses strlen/String())
      // would truncate it. The String(const char*, unsigned int) overload preserves it.
      server.send(http_code, content_type, String(deflated.data(), static_cast<unsigned int>(deflated.size())));
      return;
    }
  }
#endif

  // Deflate not enabled or failed, fall back to plain text
  log_d("Sending response: %d %s len=%u", http_code, content_type, (unsigned)body.length());
  server.send(http_code, content_type, body.c_str());
}

void setup()
{
  // Disable brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Initialize the LED GPIOs, if the board has them fitted
  if (has_user_led)
  {
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LED_ON_LEVEL == LOW ? HIGH : LOW); // Start with LED off
  }
  if (has_flash_led)
  {
    pinMode(FLASH_GPIO, OUTPUT);
    digitalWrite(FLASH_GPIO, FLASH_ON_LEVEL == LOW ? HIGH : LOW); // Start with flash off
  }

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