#pragma once

// Hardware configuration is provided by the board definition selected in platformio.ini
// (`board = <name>`), which is backed by the files in boards/. These board definitions
// are shared with rzeldent/esp32cam-rtsp and describe the hardware through build flags:
//
//   ESP32CAM_*                        board identification
//   USER_LED_GPIO, USER_LED_ON_LEVEL  user (status) LED
//   FLASH_LED_GPIO                    flash LED
//   CAMERA_CONFIG_*                   camera pin-out, see camera_config.h
//
// This header maps those flags onto the LED_GPIO / LED_ON_LEVEL / FLASH_GPIO /
// FLASH_ON_LEVEL flags used throughout this project. A board without a user LED or
// flash LED (or one that declares the pin as GPIO_NUM_NC) resolves to -1, meaning the
// LED is not fitted.

#include <driver/gpio.h>

// User (status) LED
#ifndef LED_GPIO
#ifdef USER_LED_GPIO
#define LED_GPIO USER_LED_GPIO
#else
#define LED_GPIO GPIO_NUM_NC
#endif
#endif

#ifndef LED_ON_LEVEL
#ifdef USER_LED_ON_LEVEL
#define LED_ON_LEVEL USER_LED_ON_LEVEL
#else
#define LED_ON_LEVEL LOW
#endif
#endif

// Flash LED
#ifndef FLASH_GPIO
#ifdef FLASH_LED_GPIO
#define FLASH_GPIO FLASH_LED_GPIO
#else
#define FLASH_GPIO GPIO_NUM_NC
#endif
#endif

#ifndef FLASH_ON_LEVEL
#ifdef FLASH_LED_ON_LEVEL
#define FLASH_ON_LEVEL FLASH_LED_ON_LEVEL
#else
#define FLASH_ON_LEVEL HIGH
#endif
#endif

// Whether the selected board actually has a user LED / flash LED fitted
constexpr bool has_user_led = LED_GPIO >= 0;
constexpr bool has_flash_led = FLASH_GPIO >= 0;

#if !defined(USER_LED_GPIO) && !defined(FLASH_LED_GPIO)
#warning "No board definition (boards/*.json) selected; user LED and flash LED are disabled."
#endif
