#pragma once

#include <Arduino.h>

// Packet layout
static constexpr size_t BEETON_ORIGIN_IP_SIZE = 16;
static constexpr size_t BEETON_HEADER_SIZE = 24;

// Leader/control address
static constexpr uint16_t BEETON_LEADER_THING = 0xFFFF;
static constexpr uint8_t  BEETON_LEADER_ID = 0xFF;
constexpr uint8_t BEETON_LEADER_ACTION_SERIAL = 0xFE;
constexpr uint8_t BEETON_LEADER_ACTION_ANNOUNCE = 0xFF;

// Packet flags
static constexpr uint8_t BEETON_FLAG_ACK = 0x01;
static constexpr uint8_t BEETON_FLAG_RELIABLE = 0x02;
// Reliable delivery
static constexpr unsigned long BEETON_RETRY_INTERVAL_MS = 250;
static constexpr uint8_t BEETON_MAX_RETRIES = 5;
static constexpr unsigned long BEETON_SEEN_PACKET_TTL_MS = 10000;
static constexpr size_t BEETON_SEEN_PACKET_MAX = 32;

// USB
static constexpr uint32_t BEETON_USB_BAUD = 115200;

// UDP / protocol
static constexpr uint16_t BEETON_DEFAULT_UDP_PORT = 12345;

//Logging
static constexpr size_t BEETON_LOG_BUFFER_SIZE = 256;
static constexpr size_t BEETON_IPV6_TEXT_BUFFER_SIZE = 40;
