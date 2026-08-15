#include "Beeton.h"
#include "BeetonConfig.h"
#include "LightThreadTypes.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace {
std::map<uint32_t, String> thingIdToIp;
std::map<uint32_t, bool> localDestinations;


enum class RoutingState{
  IDLE,
  WAITING_FOR_TRANSPORT,
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
} // namespace


void Beeton::routingBegin(){
  thingIdToIp.clear();
  localDestinations.clear();
  routingState = RoutingState::IDLE;

  if(!lightThread || lightThread->getRole() != Role::LEADER){
    setNetworkReady(false);
    return;
  }

  setNetworkReady(true);

  const String localIp = lightThread->getMyIp();

  for(const BeetonThing &thing : localThings){
    registerThingOwner(thing.thing, thing.id, localIp, true);
  }
}

void Beeton::routingUpdate(){
  if(!lightThread){
    return;
  } 
  if(lightThread->getRole() == Role::JOINER){
    if(routingState != RoutingState::WAITING_FOR_TRANSPORT || !lightThread->isReady()){
      return;
    }

    std::vector<uint8_t> payload;
    payload.reserve(localThings.size()*3);
    
    for(const BeetonThing &thing : localThings){
      payload.push_back(static_cast<uint8_t>(thing.thing >> 8));
      payload.push_back(static_cast<uint8_t>(thing.thing & 0xFF));
      payload.push_back(thing.id);
    }

    const bool sent = sendPacket(true, BEETON_LEADER_THING, BEETON_LEADER_ID, BEETON_LEADER_ACTION_ANNOUNCE, payload, false);

    if(sent){
      routingState = RoutingState::WAITING_FOR_ACK;
      logBeeton(BEETON_LOG_INFO, "Joiner sent Thing announcement");
    }
    return;
  }

  if(!lightThread->isReady()|| lightThread->getRole() != Role::LEADER){
    return;
  }
  const String localIp = lightThread->getMyIp();
  for(const BeetonThing &thing: localThings){
    registerThingOwner(thing.thing, thing.id, localIp, true);
  }

  if(!isNetworkReady()){
    setNetworkReady(true);
  }
}

void Beeton::routingHandleJoin(){
  setNetworkReady(false);

  routingState = RoutingState::WAITING_FOR_TRANSPORT;
}

void Beeton::routingHandleAck(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  if(thing != BEETON_LEADER_THING ||
      id != BEETON_LEADER_ID ||
      action != BEETON_LEADER_ACTION_ANNOUNCE){
    return;
  }

  routingState = RoutingState::IDLE;

  logBeeton(BEETON_LOG_INFO, "Thing announcement acknowledged seq=%u", seq);
  setNetworkReady(true);
}

bool Beeton::routingHandleLeaderPacket(const BeetonPacket &packet){
  switch(packet.action){
    case BEETON_LEADER_ACTION_ANNOUNCE:
      if(packet.payload.size() % 3 != 0){
        logBeeton(BEETON_LOG_WARN,"Ignored malformed Thing announcement length %zu",packet.payload.size());
        return true;
      }
      for(size_t i = 0; i < packet.payload.size(); i+=3){
        const uint16_t thing = (static_cast<uint16_t>(packet.payload[i]) << 8) | packet.payload[i+1];

        const uint8_t id = packet.payload[i+ 2];
        registerThingOwner(thing, id, packet.originIp, false);

        logBeeton(BEETON_LOG_INFO, "Registered remote Thing %04X:%u at %s", thing, id, packet.originIp.c_str());
      }
      return true;
    default:
      return false;
  }
}

void Beeton::routingHandleAckFailure(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  if(thing != BEETON_LEADER_THING ||
      id != BEETON_LEADER_ID ||
      action != BEETON_LEADER_ACTION_ANNOUNCE){
    return;
  }
  routingState = RoutingState::WAITING_FOR_TRANSPORT;
  logBeeton(BEETON_LOG_WARN, "Thing announcement failed seq=%u; scheduling another attempt", seq);
}

bool Beeton::routingFindDestination(uint16_t thing, uint8_t id, String &outIp) {
  const auto it = thingIdToIp.find(makeThingIdKey(thing, id));
  if(it == thingIdToIp.end()) {
    return false;
  }
  outIp = it->second;
  return true;
}

bool Beeton::routingIsLocalDestination(uint16_t thing, uint8_t id){
  const auto it = localDestinations.find(makeThingIdKey(thing, id));
  return it != localDestinations.end() && it->second;
}

const std::map<uint32_t, String> & Beeton::routingGetKnownDestinations() { return thingIdToIp; }
