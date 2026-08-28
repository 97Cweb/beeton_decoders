#include "Beeton.h"
#include "BeetonConfig.h"
#include "BeetonRouting.h"
#include "LightThreadTypes.h"
#include <cstddef>
#include <cstdint>
#include <map>


/*
 * Routing Strategy Implementation
 * This file has the behaviour that changes between strategies
 * The routing envelope and lighthread lifecycle are in RoutingCommon.cpp
 *
 * Routing strategy responsibilities:
 * - maintain routing state and destination info
 * - Reset state when routing begins
 * - respond when lightthread starts or stops
 * - setup before Beeton is ready
 * - process routing message types
 * - process routing ack success and failures
 * - select next hop for application packets
 * - classify incoming application packets as local, forward, or drop
 * - expose known destinations for leader USB reporting
 *
 * current strategy has joiners announce local Thing/Id table to leader.
 * Joiner sends application traffic through leader, leader forwards to
 * correct spot based on collected destination table
 *
 */

// Strategy owned state and private helpers
namespace {
std::map<uint32_t, String> thingIdToIp;
std::map<uint32_t, bool> localDestinations;


enum class RoutingState{
  IDLE,
  READY_TO_ANNOUNCE,
  WAITING_FOR_ACK
};

RoutingState routingState = RoutingState::IDLE;

uint32_t makeThingIdKey(uint16_t thing, uint8_t id) {
  return (static_cast<uint32_t>(thing) << 8) | id;
}

void registerThingOwner(uint16_t thing, uint8_t id, const String &ip, bool local) {
  const uint32_t key = makeThingIdKey(thing, id);
  thingIdToIp[key] = ip;
  localDestinations[key] = local;
}

bool findThingOwner(
    uint16_t thing,
    uint8_t id,
    String &outIp
) {
  const auto it =
      thingIdToIp.find(makeThingIdKey(thing, id));

  if(it == thingIdToIp.end()) {
    return false;
  }

  outIp = it->second;
  return true;
}

bool isLocalDestination(
    uint16_t thing,
    uint8_t id
) {
  const auto it =
      localDestinations.find(makeThingIdKey(thing, id));

  return it != localDestinations.end() && it->second;
}
} // namespace

//strategy lifecycle
//
void Beeton::routingReset(){
  thingIdToIp.clear();
  localDestinations.clear();
  routingState = RoutingState::IDLE;

}

void Beeton::routingOnLightThreadReady(){
  logBeeton(BEETON_LOG_INFO, "LightThread ready, starting Beeton routing setup");

  setNetworkReady(false);

  thingIdToIp.clear();
  localDestinations.clear();

  if(lightThread->getRole() == Role::LEADER){
    const String localIp = lightThread->getMyIp();

    for(const BeetonThing &thing:localThings){
      registerThingOwner(thing.thing, thing.id, localIp, true);
    }

    routingState = RoutingState::IDLE;
    setNetworkReady(true);
    return;
    
  }

  if(lightThread->getRole() == Role::JOINER){
    routingState = RoutingState::READY_TO_ANNOUNCE;
    return;
  }

  routingState = RoutingState::IDLE;

  logBeeton(BEETON_LOG_WARN, "LightThread ready with unsupported routing role");
}

void Beeton::routingOnLightThreadLost(){
  logBeeton(BEETON_LOG_INFO, "LightThread no longer ready");

  setNetworkReady(false);

  thingIdToIp.clear();
  localDestinations.clear();

  routingState = RoutingState::IDLE;
}

void Beeton::routingStrategyUpdate() {
  if(lightThread->getRole() != Role::JOINER) {
    return;
  }

  if(routingState != RoutingState::READY_TO_ANNOUNCE) {
    return;
  }

  std::vector<uint8_t> tablePayload;
  tablePayload.reserve(localThings.size() * 3);

  for(const BeetonThing &thing : localThings) {
    tablePayload.push_back(
        static_cast<uint8_t>(thing.thing >> 8));
    tablePayload.push_back(
        static_cast<uint8_t>(thing.thing & 0xFF));
    tablePayload.push_back(thing.id);
  }

  const bool sent = sendRoutingPacket(
      true,
      lightThread->getLeaderIp(),
      RoutingMessageType::ANNOUNCE_TABLE,
      tablePayload);

  if(sent) {
    routingState = RoutingState::WAITING_FOR_ACK;

    logBeeton(
        BEETON_LOG_INFO,
        "Joiner sent Thing announcement");
  }
}

//routing control message behaviour


bool Beeton::routingHandleMessage(
    RoutingMessageType type,
    const BeetonPacket &packet
) {
  switch(type) {
    case RoutingMessageType::ANNOUNCE_TABLE: {
      if(!lightThread ||
         lightThread->getRole() != Role::LEADER) {
        logBeeton(
            BEETON_LOG_WARN,
            "Ignored Thing announcement on non-leader");
        return true;
      }

      const size_t tableLength =
          packet.payload.size() - 1;

      if(tableLength % 3 != 0) {
        logBeeton(
            BEETON_LOG_WARN,
            "Ignored malformed Thing announcement length %zu",
            tableLength);
        return true;
      }

      for(size_t i = 1;
          i < packet.payload.size();
          i += 3) {
        const uint16_t thing =
            (static_cast<uint16_t>(packet.payload[i]) << 8) |
            packet.payload[i + 1];

        const uint8_t id = packet.payload[i + 2];

        registerThingOwner(
            thing,
            id,
            packet.originIp,
            false);

        logBeeton(
            BEETON_LOG_INFO,
            "Registered remote Thing %04X:%u at %s",
            thing,
            id,
            packet.originIp.c_str());
      }

      return true;
    }

    default:
      logBeeton(
          BEETON_LOG_WARN,
          "Ignored unknown routing message type %u",
          static_cast<uint8_t>(type));
      return true;
  }
}


void Beeton::routingHandleAckMessage(
    RoutingMessageType type,
    uint16_t seq
) {
  switch(type) {
    case RoutingMessageType::ANNOUNCE_TABLE:
      routingState = RoutingState::IDLE;

      logBeeton(
          BEETON_LOG_INFO,
          "Thing announcement acknowledged seq=%u",
          seq);

      setNetworkReady(true);
      return;

    default:
      logBeeton(
          BEETON_LOG_WARN,
          "ACK for unknown routing message type %u seq=%u",
          static_cast<uint8_t>(type),
          seq);
      return;
  }
}

void Beeton::routingHandleFailureMessage(
    RoutingMessageType type,
    uint16_t seq
) {
  switch(type) {
    case RoutingMessageType::ANNOUNCE_TABLE:
      routingState = RoutingState::READY_TO_ANNOUNCE;

      logBeeton(
          BEETON_LOG_WARN,
          "Thing announcement failed seq=%u; scheduling another attempt",
          seq);
      return;

    default:
      logBeeton(
          BEETON_LOG_WARN,
          "Failure for unknown routing message type %u seq=%u",
          static_cast<uint8_t>(type),
          seq);
      return;
  }
}

//application packet routing policy

bool Beeton::routingFindNextHop( uint16_t thing, uint8_t id, String& outIp){
  if(!lightThread){
    return false;
  }
  if(lightThread->getRole() == Role::JOINER){
    outIp = lightThread ->getLeaderIp();
    return outIp.length() > 0;
  }

  if(lightThread->getRole() == Role::LEADER){
    return findThingOwner(thing, id, outIp);
  }
  return false;
}


RoutingDisposition Beeton::routingClassifyPacket(const BeetonPacket &packet, String &outNextHopIp){
  if(!lightThread){
    return RoutingDisposition::DROP;
  }
  if(lightThread->getRole() == Role::JOINER){
    return RoutingDisposition::LOCAL;
  }
  if(lightThread->getRole()!= Role::LEADER){
    return RoutingDisposition::DROP;
  }
  if(isLocalDestination(packet.thing, packet.id)){
    return RoutingDisposition::LOCAL;
  }

  if(!findThingOwner(packet.thing, packet.id, outNextHopIp)){
    logBeeton(BEETON_LOG_WARN, "Leader has no destination for thing %04X id=%u", packet.thing, packet.id);

    return RoutingDisposition::DROP;
  }
  return RoutingDisposition::FORWARD;
}

//strategy info exposed to other beeton services
const std::map<uint32_t, String> & Beeton::routingGetKnownDestinations() { return thingIdToIp; }

