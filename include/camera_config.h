#pragma once

#include <driver/i2c.h>
#include <esp_camera.h>

// The camera pin-out and timing are provided by the board definition selected in
// platformio.ini (`board = <name>`), which is backed by the files in boards/. These
// board definitions are shared with rzeldent/esp32cam-rtsp and describe the camera
// through the CAMERA_CONFIG_* build flags.
//
// The defaults below match the AI-Thinker ESP32-CAM and are only used when no board
// definition is selected, for example when the project is built outside PlatformIO.

#if !defined(CAMERA_CONFIG_PIN_PWDN) || !defined(CAMERA_CONFIG_PIN_XCLK)
#warning "No board definition (boards/*.json) selected; using the AI-Thinker ESP32-CAM camera defaults."
#endif

// Camera control and SCCB (I2C) pins
#ifndef CAMERA_CONFIG_PIN_PWDN
#define CAMERA_CONFIG_PIN_PWDN 32
#endif
#ifndef CAMERA_CONFIG_PIN_RESET
#define CAMERA_CONFIG_PIN_RESET -1
#endif
#ifndef CAMERA_CONFIG_PIN_XCLK
#define CAMERA_CONFIG_PIN_XCLK 0
#endif
#ifndef CAMERA_CONFIG_PIN_SCCB_SDA
#define CAMERA_CONFIG_PIN_SCCB_SDA 26
#endif
#ifndef CAMERA_CONFIG_PIN_SCCB_SCL
#define CAMERA_CONFIG_PIN_SCCB_SCL 27
#endif

// Data pins (Y2..Y9)
#ifndef CAMERA_CONFIG_PIN_Y9
#define CAMERA_CONFIG_PIN_Y9 35
#endif
#ifndef CAMERA_CONFIG_PIN_Y8
#define CAMERA_CONFIG_PIN_Y8 34
#endif
#ifndef CAMERA_CONFIG_PIN_Y7
#define CAMERA_CONFIG_PIN_Y7 39
#endif
#ifndef CAMERA_CONFIG_PIN_Y6
#define CAMERA_CONFIG_PIN_Y6 36
#endif
#ifndef CAMERA_CONFIG_PIN_Y5
#define CAMERA_CONFIG_PIN_Y5 21
#endif
#ifndef CAMERA_CONFIG_PIN_Y4
#define CAMERA_CONFIG_PIN_Y4 19
#endif
#ifndef CAMERA_CONFIG_PIN_Y3
#define CAMERA_CONFIG_PIN_Y3 18
#endif
#ifndef CAMERA_CONFIG_PIN_Y2
#define CAMERA_CONFIG_PIN_Y2 5
#endif

// Synchronization pins
#ifndef CAMERA_CONFIG_PIN_VSYNC
#define CAMERA_CONFIG_PIN_VSYNC 25
#endif
#ifndef CAMERA_CONFIG_PIN_HREF
#define CAMERA_CONFIG_PIN_HREF 23
#endif
#ifndef CAMERA_CONFIG_PIN_PCLK
#define CAMERA_CONFIG_PIN_PCLK 22
#endif

// Clock and LEDC (XCLK generation) configuration
#ifndef CAMERA_CONFIG_CLK_FREQ_HZ
#define CAMERA_CONFIG_CLK_FREQ_HZ 20000000
#endif
#ifndef CAMERA_CONFIG_LEDC_TIMER
#define CAMERA_CONFIG_LEDC_TIMER LEDC_TIMER_0
#endif
#ifndef CAMERA_CONFIG_LEDC_CHANNEL
#define CAMERA_CONFIG_LEDC_CHANNEL LEDC_CHANNEL_0
#endif

// Frame buffer configuration
#ifndef CAMERA_CONFIG_FB_COUNT
#define CAMERA_CONFIG_FB_COUNT 1
#endif
#ifndef CAMERA_CONFIG_FB_LOCATION
#define CAMERA_CONFIG_FB_LOCATION CAMERA_FB_IN_PSRAM
#endif

// I2C port used for SCCB when the pins are not used to set up a bus
#ifndef CAMERA_CONFIG_SCCB_I2C_PORT
#define CAMERA_CONFIG_SCCB_I2C_PORT I2C_NUM_0
#endif

// Camera configuration for the selected board.
//
// The default frame size, pixel format and JPEG quality are overridden per capture
// request by the MCP "capture" tool (see MCP_CAPTURE_* in settings.h).
constexpr camera_config_t esp32cam_settings = {
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
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 20,
    .fb_count = CAMERA_CONFIG_FB_COUNT,
    .fb_location = CAMERA_CONFIG_FB_LOCATION,
    .sccb_i2c_port = CAMERA_CONFIG_SCCB_I2C_PORT};
