#pragma once
#include <cstdint>

enum class RoutingMessageType : uint8_t {
    ANNOUNCE_TABLE = 0x01
};

enum class RoutingDisposition: uint8_t{
  LOCAL,
  FORWARD,
  DROP
};
