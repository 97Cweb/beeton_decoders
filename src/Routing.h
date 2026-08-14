#pragma once
#include <Arduino.h>
#include <cstdint>
#include <map>

struct BeetonPacket;

bool routingFindDestination(uint16_t thing, uint8_t id, String &outIp);
bool routingHandleLeaderPacket(const BeetonPacket &packet);

const std::map<uint32_t, String> &routingGetKnownDestinations();
