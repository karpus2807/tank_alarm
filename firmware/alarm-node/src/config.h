#pragma once
#include "network.h"

// --- W5500 SPI (ESP32 DevKit V1) ---
static const int PIN_ETH_MOSI = 23;
static const int PIN_ETH_MISO = 19;
static const int PIN_ETH_SCK = 18;
// CS: avoid GPIO5 and GPIO15 (both ESP32 strapping pins). Use GPIO4.
static const int PIN_ETH_CS = 4;
static const int PIN_ETH_RST = 17;

// --- OLED I2C 1.3" SH1106 128x64 (same pins) ---
static const int PIN_SDA = 21;
static const int PIN_SCL = 22;

// --- Outputs / input ---
// 3-pin buzzer module: I/O LOW = sound, HIGH = quiet.
static const int PIN_BUZZER = 26;
static const int PIN_RELAY = 27;
// TTP223 capacitive touch: VCC 3.3V, GND, I/O -> GPIO33. Touch drives I/O HIGH.
static const int PIN_MUTE = 33;

static const uint32_t STALE_MS = 5000;
