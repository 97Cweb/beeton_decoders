#include "Beeton.h"
#include "BeetonConfig.h"
#include "LightThread.h"

void Beeton::routingBegin() {
  routingLightThreadWasReady = false;
  setNetworkReady(false);
  routingReset();
}

void Beeton::routingUpdate() {
  if(!lightThread) {
    return;
  }

  const bool lightThreadReady = lightThread->isReady();

  if(lightThreadReady != routingLightThreadWasReady) {
    routingLightThreadWasReady = lightThreadReady;

    if(lightThreadReady) {
      routingOnLightThreadReady();
    } else {
      routingOnLightThreadLost();
    }
  }

  if(!lightThreadReady) {
    return;
  }

  routingStrategyUpdate();
}


bool Beeton::sendRoutingPacket(bool reliable, const String &destinationIp, RoutingMessageType type, const std::vector<uint8_t> &messagePayload){
  std::vector<uint8_t> payload;
  payload.reserve(messagePayload.size() + 1);
  payload.push_back(static_cast<uint8_t>(type));
  payload.insert(payload.end(), messagePayload.begin(), messagePayload.end());
  return sendPacketToIp(reliable, destinationIp, BEETON_LEADER_THING, BEETON_LEADER_ID, BEETON_LEADER_ACTION_ROUTING, payload, false);
}
bool Beeton::routingHandlePacket(
    const BeetonPacket &packet
) {
  if(packet.thing != BEETON_LEADER_THING || packet.id != BEETON_LEADER_ID || packet.action != BEETON_LEADER_ACTION_ROUTING) {
    return false;
  }

  if(packet.payload.empty()) {
    logBeeton(
        BEETON_LOG_WARN,
        "Ignored empty routing packet");
    return true;
  }

  const RoutingMessageType messageType =
      static_cast<RoutingMessageType>(packet.payload[0]);

  return routingHandleMessage(messageType, packet);
}

void Beeton::routingHandleAck(
    uint16_t thing,
    uint8_t id,
    uint8_t action,
    uint16_t seq,
    const std::vector<uint8_t> &payload
) {
  if(thing != BEETON_LEADER_THING ||
     id != BEETON_LEADER_ID ||
     action != BEETON_LEADER_ACTION_ROUTING) {
    return;
  }

  if(payload.empty()) {
    logBeeton(
        BEETON_LOG_WARN,
        "Routing ACK seq=%u has no original message type",
        seq);
    return;
  }

  const RoutingMessageType messageType =
      static_cast<RoutingMessageType>(payload[0]);

  routingHandleAckMessage(messageType, seq);
}

void Beeton::routingHandleAckFailure(
    uint16_t thing,
    uint8_t id,
    uint8_t action,
    uint16_t seq,
    const std::vector<uint8_t> &payload
) {
  if(thing != BEETON_LEADER_THING ||
     id != BEETON_LEADER_ID ||
     action != BEETON_LEADER_ACTION_ROUTING) {
    return;
  }

  if(payload.empty()) {
    logBeeton(
        BEETON_LOG_WARN,
        "Failed routing packet seq=%u has no message type",
        seq);
    return;
  }

  const RoutingMessageType messageType =
      static_cast<RoutingMessageType>(payload[0]);

  routingHandleFailureMessage(messageType, seq);
}
