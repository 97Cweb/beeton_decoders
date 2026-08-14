#include "Routing.h"

#include "Beeton.h"
#include "BeetonConfig.h"
#include "WString.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace {
std::map<uint32_t, String> thingIdToIp;
uint32_t makeThingIdKey(uint16_t thing, uint8_t id) {
  return (static_cast<uint32_t>(thing) << 8) | id;
}

void registerThingOwner(uint16_t thing, uint8_t id, const String &ip) {
  thingIdToIp[makeThingIdKey(thing, id)] = ip;
}

/*
 * TODO: Replace announcement-only routing ownership
 *
 * The routing API is intentionally isolated in this file so the source of
 * routing information can be replaced without changing Beeton packet
 * handling.
 *
 * Currently, joiners populate a volatile (thing, id) -> IP table using
 * BEETON_LEADER_ACTION_ANNOUNCE.
 *
 * A future leader implementation should populate this table from the
 * authoritative device/thing assignment source, while preserving:
 *
 *   routingFindDestination()
 *   routingHandleLeaderPacket()
 *   routingGetKnownDestinations()
 *
 * The replacement should define:
 *   - how leader-local things are registered;
 *   - how ownership conflicts are resolved and reported;
 *   - how stale routes are expired or removed;
 *   - how address changes and device reassignment are handled;
 *   - whether routes survive a leader restart;
 *   - whether announcements are accepted directly or validated against
 *     configured assignments.
 */
bool handleAnnouncement(const BeetonPacket &packet) {
  if(packet.payload.size() % 3 != 0) {
    return true;
  }

  for(size_t i = 0; i < packet.payload.size(); i += 3) {
    const uint16_t thing = (static_cast<uint16_t>(packet.payload[i]) << 8) | packet.payload[i + 1];

    const uint8_t id = packet.payload[i + 2];

    registerThingOwner(thing, id, packet.originIp);
  }
  return true;
}
} // namespace

bool routingHandleLeaderPacket(const BeetonPacket &packet) {
  switch(packet.action) {
  case BEETON_LEADER_ACTION_ANNOUNCE:
    return handleAnnouncement(packet);
  default:
    return false;
  }
}

bool routingFindDestination(uint16_t thing, uint8_t id, String &outIp) {
  const auto it = thingIdToIp.find(makeThingIdKey(thing, id));
  if(it == thingIdToIp.end()) {
    return false;
  }
  outIp = it->second;
  return true;
}

const std::map<uint32_t, String> &routingGetKnownDestinations() { return thingIdToIp; }
