#pragma once
#include "network.h"

// --- W5500 SPI (ESP32-S3) ---
static const int PIN_ETH_MOSI = 11;
static const int PIN_ETH_MISO = 13;
static const int PIN_ETH_SCK = 12;
static const int PIN_ETH_CS = 10;
static const int PIN_ETH_RST = 14;

// --- JSN-SR04T (Mode 1: Trig/Echo). Pads often also say RX/TX — ignore those names. ---
static const int PIN_TRIG = 6; // pad labeled Trig / RX
static const int PIN_ECHO = 7; // pad labeled Echo / TX (via divider)

// Level sensor: brown 5V, blue GND, yellow GPIO4, black open.
// This board sees water as HIGH and dry as LOW.
static const int PIN_LEVEL = 4;

// Tank geometry: measure once and tune.
static const float TANK_EMPTY_CM = 140.0f;
static const float TANK_FULL_CM = 25.0f;
static const uint8_t TANK_ID = 1;

static const float WARN_PERCENT = 90.0f;
static const float FULL_PERCENT = 95.0f;
static const float CLEAR_PERCENT = 88.0f;

static const uint32_t SAMPLE_MS = 500;
static const uint32_t SEND_MS = 500;
static const uint8_t SAMPLE_AVG = 5;
