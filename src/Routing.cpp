#include "Beeton.h"
#include "BeetonConfig.h"
#include "BeetonRouting.h"
#include "LightThreadTypes.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>

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
} // namespace


void Beeton::routingBegin(){
  thingIdToIp.clear();
  localDestinations.clear();
  routingState = RoutingState::IDLE;
  routingLightThreadWasReady = false;

  setNetworkReady(false);

}

void Beeton::routingOnLightThreadReady(){
  logBeeton(BEETON_LOG_INFO, "LightThread ready, starting beeting routing setup");

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

void Beeton::routingUpdate(){
  if(!lightThread){
    return;
  }

  const bool lightThreadReady = lightThread->isReady();

  if(lightThreadReady != routingLightThreadWasReady){
    routingLightThreadWasReady = lightThreadReady;

    if(lightThreadReady){
      routingOnLightThreadReady();
    }
    else{
      routingOnLightThreadLost();
    }
  }

  if(!lightThreadReady){
    return;
  }

  if(lightThread->getRole() != Role::JOINER){
    return;
  }

  if(routingState != RoutingState::READY_TO_ANNOUNCE){
    return;
  }

  std::vector<uint8_t> payload;
  payload.reserve(localThings.size() * 3 + 1);

  payload.push_back(static_cast<uint8_t>(RoutingMessageType::ANNOUNCE_TABLE));
  for(const BeetonThing &thing : localThings){
    payload.push_back(static_cast<uint8_t>(thing.thing >> 8));
    payload.push_back(static_cast<uint8_t>(thing.thing & 0xFF));
    payload.push_back(thing.id);
  }

  const bool sent = sendPacket(true, BEETON_LEADER_THING, BEETON_LEADER_ID, BEETON_LEADER_ACTION_ROUTING, payload, false);

  if(sent){
    routingState = RoutingState::WAITING_FOR_ACK;
    logBeeton(BEETON_LOG_INFO, "Joiner sent Thing announcement");
  }
  return;
}



void Beeton::routingHandleAck(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  if(thing != BEETON_LEADER_THING ||
      id != BEETON_LEADER_ID ||
      action != BEETON_LEADER_ACTION_ROUTING){
    return;
  }

  routingState = RoutingState::IDLE;

  logBeeton(BEETON_LOG_INFO, "Thing announcement acknowledged seq=%u", seq);
  setNetworkReady(true);
}

bool Beeton::routingHandlePacket(const BeetonPacket &packet){
  if(packet.action != BEETON_LEADER_ACTION_ROUTING){
    return false;
  }
  if(packet.payload.empty()){
    logBeeton(BEETON_LOG_WARN, "Ignored empty routing packet");
    return true;
  }

  const RoutingMessageType messageType = static_cast<RoutingMessageType>(packet.payload[0]);

  switch (messageType) {

    case RoutingMessageType::ANNOUNCE_TABLE:{

      const size_t tableLength = packet.payload.size() -1;

      if(tableLength % 3 != 0){
        logBeeton(BEETON_LOG_WARN, "Ignored malformed Thing announcement length %zu", tableLength);
        return true;
      }

      for(size_t i = 1; i < packet.payload.size(); i += 3){
        const uint16_t thing = (static_cast<uint16_t>(packet.payload[i]) << 8) | packet.payload[i + 1];

        const uint8_t id = packet.payload[i + 2];

        registerThingOwner(thing, id, packet.originIp, false);

        logBeeton(BEETON_LOG_INFO, "Registered remote Thing %04X:%u at %s",
            thing, id, packet.originIp.c_str());
      }
      return  true;

    }
    default:
      logBeeton(BEETON_LOG_WARN, "Ignored unknown routing message type %u", packet.payload[0]);
      return true;
  }
}

void Beeton::routingHandleAckFailure(uint16_t thing, uint8_t id, uint8_t action, uint16_t seq){
  if(thing != BEETON_LEADER_THING ||
      id != BEETON_LEADER_ID ||
      action != BEETON_LEADER_ACTION_ROUTING){
    return;
  }

  routingState = RoutingState::READY_TO_ANNOUNCE;
  logBeeton(BEETON_LOG_WARN, "Thing announcement failed seq=%u; scheduling another attempt", seq);
}

bool Beeton::routingFindNextHop( uint16_t thing, uint8_t id, String& outIp){
  if(!lightThread){
    return false;
  }
  if(lightThread->getRole() == Role::JOINER){
    outIp = lightThread ->getLeaderIp();
    return outIp.length() > 0;
  }

  if(lightThread->getRole() == Role::LEADER){
    return routingFindDestination(thing, id, outIp);
  }
  return false;
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
