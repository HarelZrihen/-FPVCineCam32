#pragma once
#include <sdkconfig.h>

// Hardware profile, picked from the chip the firmware is built for.
// The MSP UART pins can be overridden per build with -D FCC_MSP_RX_PIN=n / -D FCC_MSP_TX_PIN=n.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define FCC_BOARD_NAME "ESP32-S3"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define FCC_BOARD_NAME "ESP32-C3"
#else
#error "FPVCineCam32 supports ESP32-C3 and ESP32-S3 only"
#endif

// GPIO6/GPIO7 are free general-purpose pins on both the C3 SuperMini and
// common S3 boards (DevKitC-1, S3 SuperMini/Zero), so wiring stays the same.
#ifndef FCC_MSP_RX_PIN
#define FCC_MSP_RX_PIN 6
#endif
#ifndef FCC_MSP_TX_PIN
#define FCC_MSP_TX_PIN 7
#endif

#define FCC_STR_(x) #x
#define FCC_STR(x) FCC_STR_(x)
