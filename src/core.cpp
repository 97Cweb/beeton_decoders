#include "Beeton.h"
#include "Routing.h"
// Initialize Beeton and register callbacks with LightThread
void Beeton::begin(LightThread &lt) {
  lightThread = &lt;

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

  // Register callback for join events (only runs on joiner)
  lightThread->registerJoinCallback([this](const String &ip, const String &hashmac) {
    // Only announce if we’re the joiner
    if(lightThread->getRole() != Role::JOINER)
      return;

    // Package all local things into a WHO_AM_I announcement
    std::vector<uint8_t> payload;
    for(const auto &entry : localThings) {
      logBeeton(BEETON_LOG_INFO, "Joiner adding thing id: %04X:%d", entry.thing, entry.id);
      appendUint16(payload, entry.thing);
      payload.push_back(entry.id);
    }

    this->send(true, BEETON_LEADER_THING, BEETON_LEADER_ID, BEETON_LEADER_ACTION_ANNOUNCE, payload);
    logBeeton(BEETON_LOG_INFO, "Joiner Sent WHO_AM_I automatically");
  });
  isSetup = true;
}

// Forward update call to LightThread instance
void Beeton::update() {
  if(lightThread)
    lightThread->update();

  if(lightThread && lightThread->getRole() == Role::LEADER) {
    updateUsb();
  }
  pumpReliable();
}

bool Beeton::goDormant() {
  if(!isReady()) {
    logBeeton(BEETON_LOG_ERROR, "goDormant() called before Beeton::begin()");

    return false;
  }

  return lightThread->goDormant();
}

// Overload for sending a message without payload
bool Beeton::send(bool reliable, uint16_t thing, uint8_t id, uint8_t action) {
  std::vector<uint8_t> payload;
  return send(reliable, thing, id, action, payload);
}

// Overload for sending a message with a single byte payload
bool Beeton::send(bool reliable, uint16_t thing, uint8_t id, uint8_t action, uint8_t payloadByte) {
  std::vector<uint8_t> payload = {payloadByte};
  return send(reliable, thing, id, action, payload);
}

// Send message to a known (thing, id) destination, if its IP is known
bool Beeton::send(bool reliable, uint16_t thing, uint8_t id, uint8_t action,
                  const std::vector<uint8_t> &payload) {
  uint8_t flags = 0;
  uint16_t seq = 0;

  if(!isReady()) {
    return false;
  }

  if(reliable) {
    flags = BEETON_FLAG_RELIABLE;
    seq = allocSeq();
  }
  // Build once so retries preserve original packet contents
  std::vector<uint8_t> packet = buildPacket(flags, seq, thing, id, action, payload);

  if(lightThread->getRole() == Role::LEADER) {

    String destIp;

    if(!routingFindDestination(thing, id, destIp)) {
      logBeeton(BEETON_LOG_WARN, "Beeton: No IP for thing %04X id %u", thing, id);
      return false;
    }

    bool ok = lightThread->sendUdp(destIp, packet);

    // Track pending if we requested ACK
    if(ok && reliable) {
      Pending p;
      p.destIp = destIp;
      p.thing = thing;
      p.id = id;
      p.action = action;
      p.payload = payload;
      p.seq = seq;
      p.timeoutMs = BEETON_RETRY_INTERVAL_MS;
      p.retriesLeft = BEETON_MAX_RETRIES;
      p.nextDueMs = millis() + p.timeoutMs;
      pending[seq] = std::move(p);
    }
    return ok;
  } else if(lightThread->getRole() == Role::JOINER) {
    // Send to leader; leader forwards (must preserve packet as-is)
    bool ok = lightThread->sendUdp(lightThread->getLeaderIp(), packet);

    if(ok && reliable) {
      Pending p;
      p.destIp = lightThread->getLeaderIp(); // first hop is leader
      p.originIp = lightThread->getMyIp();
      p.thing = thing;
      p.id = id;
      p.action = action;
      p.payload = payload;
      p.seq = seq;
      p.timeoutMs = BEETON_RETRY_INTERVAL_MS;
      p.retriesLeft = BEETON_MAX_RETRIES;
      p.nextDueMs = millis() + p.timeoutMs;
      pending[seq] = std::move(p);
    }
    return ok;
  }

  logBeeton(BEETON_LOG_WARN, "Beeton: Unknown role, cannot send");
  return false;
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

  // leader is router for things not itself. Forward unconditionally, no ack
  if(lightThread && lightThread->getRole() == Role::LEADER && !isLeaderAddress(packet)) {
    forwardPacketIfLeader(raw, packet);
    return;
  }

  // final destination, do your own duplicate detection
  if(handleReliablePacket(packet)) {
    return;
  }

  if(handleLeaderControlPacket(packet)) {
    return;
  }

  dispatchLocalPacket(packet);
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
    return false;
  }

  switch(packet.action) {
  case BEETON_LEADER_ACTION_SERIAL:
    sendRemoteSerialPacket(packet);
    return true;

  default:
    // Ordinary user-defined leader action.
    // Allow dispatchLocalPacket() to receive it.
    return routingHandleLeaderPacket(packet);
  }
}

bool Beeton::forwardPacketIfLeader(const std::vector<uint8_t> &raw, const BeetonPacket &packet) {
  if(!isReady() || lightThread->getRole() != Role::LEADER) {
    return false;
  }

  if(isLeaderAddress(packet)) {
    return false;
  }

  String destIp;

  if(!routingFindDestination(packet.thing, packet.id, destIp)) {
    logBeeton(BEETON_LOG_WARN, "Leader has no destination for thing=%04X id=%u", packet.thing,
              packet.id);
    return false;
  }

  logBeeton(BEETON_LOG_INFO, "Leader forwarding thing=%04X id=%u action=%u to %s", packet.thing,
            packet.id, packet.action, destIp.c_str());

  return lightThread->sendUdp(destIp, raw);
}

void Beeton::dispatchLocalPacket(const BeetonPacket &packet) {
  if(messageCallback) {
    messageCallback(packet.thing, packet.id, packet.action, packet.payload);
  }
}

bool Beeton::isReady() { return lightThread && isSetup && lightThread->isReady(); }
