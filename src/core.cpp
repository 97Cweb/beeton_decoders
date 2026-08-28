#include "Beeton.h"
#include "BeetonConfig.h"
#include "BeetonRouting.h"
#include "LightThread.h"
#include "LightThreadTypes.h"
#include "esp32-hal.h"
#include <cstdint>
#include <sys/types.h>
#include <utility>
// Initialize Beeton and register callbacks with LightThread
void Beeton::begin(LightThread &lt) {
  lightThread = &lt;
  networkReady = false;

  // Load name→ID mappings from SD card
  loadMappings();

  if(lightThread && lightThread->getRole() == Role::LEADER) {
    Serial.begin(BEETON_USB_BAUD);
    logBeeton(BEETON_LOG_INFO, "Serial Started for Leader");
  }

  // Register callback for all incoming UDP messages
  lightThread->registerUdpReceiveCallback(
      [this](const String &srcIp, const std::vector<uint8_t> &raw) {
        if(raw.size() < BEETON_HEADER_SIZE) {
          logBeeton(BEETON_LOG_DEBUG, "Ignored short packet from %s (len=%zu)", srcIp.c_str(),
                    raw.size());
          return;
        }

        BeetonPacket packet;

        // Parse the message and route it internally
        if(parsePacket(raw, packet)) {
          logBeeton(BEETON_LOG_INFO,
                    "Parsed: ver=%u flags=%02x seq=%u thing=%04x id=%02x action=%02x "
                    "payloadLen=%zu origin=%s",
                    packet.version, packet.flags, packet.seq, packet.thing, packet.id,
                    packet.action, packet.payload.size(), packet.originIp.c_str());

          handlePacket(raw, packet);
        } else {
          logBeeton(BEETON_LOG_WARN, "Invalid packet from %s", srcIp.c_str());
        }
      });

  isSetup = true;
  routingBegin();
}

// Forward update call to LightThread instance
void Beeton::update() {
  if(lightThread){
    lightThread->update();
  }


  routingUpdate();

  if(lightThread && lightThread->getRole() == Role::LEADER) {
    updateUsb();
  }
  pumpReliable();
}

bool Beeton::goDormant() {
  if(!isReady()) {
    logBeeton(BEETON_LOG_ERROR, "goDormant() called before Beeton network is ready");

    return false;
  }

  return lightThread->goDormant();
}


bool Beeton::sendPacket(bool reliable, uint16_t thing, uint8_t id, uint8_t action, const std::vector<uint8_t> &payload, bool requireNetworkReady){
 
  String nextHopIp;

  if(!routingFindNextHop(thing, id, nextHopIp)){
    logBeeton(BEETON_LOG_WARN, "Beeton: No next hop for thing %04X id %u", thing, id);
    return false;
  }

  return sendPacketToIp(reliable, nextHopIp, thing, id, action, payload, requireNetworkReady);
}

bool Beeton::sendPacketToIp(bool reliable, const String &nextHopIp, uint16_t thing, uint8_t id, uint8_t action, const std::vector<uint8_t> &payload, bool requireNetworkReady){
  uint8_t flags = 0;
  uint16_t seq = 0;

  if(!lightThread || !isSetup || !lightThread->isReady()) {
    logBeeton(BEETON_LOG_WARN, "Beeton: Transport is not ready; send blocked");
    return false;
  }


  if(requireNetworkReady && !networkReady) {
    logBeeton(BEETON_LOG_WARN, "Beeton: Network is not ready; send blocked");
    return false;
  }

  if(nextHopIp.length()== 0){
    logBeeton(BEETON_LOG_WARN, "Beeton: Cannot send to an empty IP address");
    return false;
  }

  if(reliable) {
    flags = BEETON_FLAG_RELIABLE;
    seq = allocSeq();
  }
  // Build once so retries preserve original packet contents
  std::vector<uint8_t> packet = buildPacket(flags, seq, thing, id, action, payload);
  
  const bool sent = lightThread->sendUdp(nextHopIp, packet);

  if(sent && reliable){
    Pending pendingPacket;
    pendingPacket.nextHopIp = nextHopIp;
    pendingPacket.thing = thing;
    pendingPacket.id = id;
    pendingPacket.action = action;
    pendingPacket.payload = payload;
    pendingPacket.seq = seq;
    pendingPacket.timeoutMs = BEETON_RETRY_INTERVAL_MS;
    pendingPacket.retriesLeft = BEETON_MAX_RETRIES;
    pendingPacket.nextDueMs = millis() + pendingPacket.timeoutMs;

    pending[seq] = std::move(pendingPacket);
  }
  return sent;
}

// set list of local things this device contains
void Beeton::defineThings(const std::vector<BeetonThing> &list) {
  localThings.assign(list.begin(), list.end());
}

// Construct a packet from components
std::vector<uint8_t> Beeton::buildPacket(uint8_t flags, uint16_t seq, uint16_t thing, uint8_t id,
                                         uint8_t action, const std::vector<uint8_t> &payload) {

  std::vector<uint8_t> out;
  // reserve full header
  out.reserve(1 + BEETON_ORIGIN_IP_SIZE + 1 + 2 + 2 + 1 + 1 + payload.size());
  //[0] Version
  out.push_back(BEETON_PROTOCOL_VERSION);
  //[1..16] Mesh-Local EID (source IP address)
  String ip = lightThread->getMyIp();
  auto origin = parseIpv6(ip);
  out.insert(out.end(), origin.begin(), origin.end());
  // [17] flags
  out.push_back(flags);

  // [18..19] seq
  appendUint16(out, seq);
  //[20..21] Thing
  appendUint16(out, thing);
  //[22] ID
  out.push_back(id);
  //[23] action
  out.push_back(action);

  //[24..end] Payload
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

// Attempt to parse a received packet
bool Beeton::parsePacket(const std::vector<uint8_t> &raw, BeetonPacket &packet) {

  if(raw.size() < BEETON_HEADER_SIZE) {
    return false;
  }

  size_t off = 0;
  //[0] version
  packet.version = raw[off++];

  if(packet.version != BEETON_PROTOCOL_VERSION) {
    logBeeton(BEETON_LOG_WARN, "Rejected packet with unsupported version %u expected %u",
              packet.version, BEETON_PROTOCOL_VERSION);
    return false;
  }

  //[1..16] Origin IPv6
  std::vector<uint8_t> origin(raw.begin() + off, raw.begin() + off + BEETON_ORIGIN_IP_SIZE);
  off += BEETON_ORIGIN_IP_SIZE;
  packet.originIp = formatIpv6(origin);
  // [17] flags
  packet.flags = raw[off++];
  // [18..19] seq
  packet.seq = readUint16(raw, off);
  off += 2;
  //[20..21] Thing ID (Big Endian)
  packet.thing = readUint16(raw, off);
  off += 2;
  //[22] ID
  packet.id = raw[off++];
  //[23] Action
  packet.action = raw[off++];
  //[24..end] Payload
  packet.payload.assign(raw.begin() + off, raw.end());
  return true;
}

void Beeton::handlePacket(const std::vector<uint8_t> &raw, const BeetonPacket &packet) {
  if(handleAckPacket(packet)) {
    return;
  }

  if(isLeaderAddress(packet) && packet.action == BEETON_LEADER_ACTION_ROUTING){
    if(handleReliablePacket(packet)){
      return;
    }
    routingHandlePacket(packet);
    return;
  }

  if(isLeaderAddress(packet)){
    if(handleReliablePacket(packet)){
      return;
    }
    handleLeaderControlPacket(packet);
    return;
  }

  if(!isReady()){
    logBeeton(BEETON_LOG_DEBUG, "Ignored data packet while Beeton network is not ready");
    return;
  }
  
  String nextHopIp;

  const RoutingDisposition disposition = routingClassifyPacket(packet, nextHopIp);

  switch(disposition){
    case RoutingDisposition::LOCAL:
      if(handleReliablePacket(packet)){
        return;
      }
      dispatchLocalPacket(packet);
      return;
    case RoutingDisposition::FORWARD:
      logBeeton(BEETON_LOG_INFO, "Forwarding thing=%04X id=%u action=%u to %s", packet.thing, packet.id, packet.action, nextHopIp.c_str());

      lightThread->sendUdp(nextHopIp, raw);
      return;
    case RoutingDisposition::DROP:
      return;
  }
}

bool Beeton::handleAckPacket(const BeetonPacket &packet) {
  if(!(packet.flags & BEETON_FLAG_ACK)) {
    return false;
  }

  auto it = pending.find(packet.seq);

  if(it == pending.end()) {
    logBeeton(BEETON_LOG_DEBUG, "Ignored unknown ACK seq=%u", packet.seq);
    return true;
  }

  const Pending &pendingPacket = it->second;

  if(packet.thing != pendingPacket.thing || packet.id != pendingPacket.id ||
     packet.action != pendingPacket.action) {
    logBeeton(BEETON_LOG_WARN,
              "Ignored mismatched ACK seq=%u "
              "expected=%04X:%u:%u received=%04X:%u:%u",
              packet.seq, pendingPacket.thing, pendingPacket.id, pendingPacket.action, packet.thing,
              packet.id, packet.action);
    return true;
  }
  Pending completed = std::move(it->second);
  pending.erase(it);

  logBeeton(BEETON_LOG_INFO, "ACK received seq=%u thing %04X id=%u action=%u", packet.seq,
            packet.thing, packet.id, packet.action);
  
  routingHandleAck(completed.thing, completed.id, completed.action, completed.seq, completed.payload);

  if(ackSuccessCb) {
    ackSuccessCb(completed.thing, completed.id, completed.action, completed.seq);
  }
  return true;
}

bool Beeton::handleReliablePacket(const BeetonPacket &packet) {
  if(!(packet.flags & BEETON_FLAG_RELIABLE)) {
    return false;
  }

  if(wasSeenAndMark(packet.originIp, packet.seq, millis())) {
    logBeeton(BEETON_LOG_INFO, "Duplicate reliable packet seq=%u from %s", packet.seq,
              packet.originIp.c_str());

    auto ack = buildPacket(BEETON_FLAG_ACK, packet.seq, packet.thing, packet.id, packet.action, {});
    lightThread->sendUdp(packet.originIp, ack);

    return true;
  }

  auto ack = buildPacket(BEETON_FLAG_ACK, packet.seq, packet.thing, packet.id, packet.action, {});
  lightThread->sendUdp(packet.originIp, ack);

  return false;
}

bool Beeton::handleLeaderControlPacket(const BeetonPacket &packet) {
  if(!isLeaderAddress(packet)) {
    return false;
  }

  // These internal behaviours only exist on the leader.
  if(!lightThread || lightThread->getRole() != Role::LEADER) {
    return true;
  }

  if(packet.action == BEETON_LEADER_ACTION_SERIAL){
    sendRemoteSerialPacket(packet);
    return true;
  }

  logBeeton(BEETON_LOG_WARN, "Ignored unknown reserved leader action %u", packet.action);
  return true;
}


void Beeton::dispatchLocalPacket(const BeetonPacket &packet) {
  if(!messageCallback){
    return;
  }
  
  BeetonPayload payload;

  if(!decodePayload(packet.payload, payload)){
    logBeeton(BEETON_LOG_WARN, "Rejected malformed payload thing= %04x id=%u action=%u len=%zu", packet.thing, packet.id, packet.action,packet.payload.size());
    return;
  }


  messageCallback(packet.thing, packet.id, packet.action, payload);
}

bool Beeton::isReady() { 
  return lightThread && 
    isSetup && 
    lightThread->isReady() &&
    networkReady; 
}

bool Beeton::isNetworkReady() const {
  return networkReady;
}

void Beeton::setNetworkReady(bool ready){
  if(networkReady == ready){
    return;
  }
  networkReady = ready;
  logBeeton(BEETON_LOG_INFO, "Beeton network marked %s", ready ? "ready": "not ready");
}
