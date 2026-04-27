#include "Beeton.h"
// Initialize Beeton and register callbacks with LightThread
void Beeton::begin(LightThread &lt) {
    lightThread = &lt;

    // Load name→ID mappings from SD card
    loadMappings();

    if(lightThread && lightThread->getRole() == Role::LEADER) {
        Serial.begin(115200);
        usbConnected = true;
        logBeeton(BEETON_LOG_INFO, "Serial Started for Leader");
    }

    // Register callback for all incoming UDP messages
    lightThread->registerUdpReceiveCallback(
        [this](const String &srcIp, const bool lightThreadReliable, const std::vector<uint8_t> &raw) {
            if(raw.size() < 24) {
                logBeeton(BEETON_LOG_DEBUG, "Ignored short packet from %s (len=%d)", srcIp.c_str(),
                          raw.size());
                return;
            }

            BeetonPacket packet;

            // Parse the message and route it internally
            if(parsePacket(raw, packet)) {
                logBeeton(BEETON_LOG_INFO,
                      "Parsed: ver=%u flags=%02x seq=%u thing=%04x id=%02x action=%02x payloadLen=%u origin=%s",
                      packet.version, packet.flags, packet.seq, packet.thing, packet.id, packet.action, packet.payload.size(), packet.originIp.c_str());

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
            payload.push_back((entry.thing >> 8) & 0xff);
            payload.push_back(entry.thing & 0xff);
            payload.push_back(entry.id);
        }

        this->send(true, BEETON::BEETON_LEADER_THING,BEETON::BEETON_LEADER_ID,BEETON::BEETON_LEADER_ACTION,payload);
        logBeeton(BEETON_LOG_INFO, "Joiner Sent WHO_AM_I automatically");
    });
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

// Overload for sending a message without payload
bool Beeton::send(bool reliable, uint16_t thing, uint8_t id, uint8_t action) {
    std::vector<uint8_t> payload; // empty vector
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
    if(reliable){
        flags = BEETON_FLAG_RELIABLE;
        seq = allocSeq();
    }
    // Build packet ONCE (source of truth)
    std::vector<uint8_t> packet = buildPacket(flags, seq, thing, id, action, payload);
    
    if (lightThread->getRole() == Role::LEADER) {
        uint32_t key = makeThingIdKey(thing, id);

        if (!thingIdToIp.count(key)) {
            logBeeton(BEETON_LOG_WARN, "Beeton: No IP for thing %u id %u", thing, id);
            return false;
        }

        // TRANSPORT reliability OFF; Beeton handles it now
        bool ok = lightThread->sendUdp(thingIdToIp[key], /*lightThreadReliable=*/false, packet);

        // Track pending if we requested ACK
        if (ok && reliable) {
            Pending p;
            p.destIp = thingIdToIp[key];
            p.originIp = lightThread->getMyIp();
            p.thing = thing; p.id = id; p.action = action;
            p.payload = payload;
            p.seq = seq;
            p.timeoutMs = 200;
            p.retriesLeft = 3;
            p.nextDueMs = millis() + p.timeoutMs;
            pending[seq] = std::move(p);
        }
        return ok;
    }
    else if (lightThread->getRole() == Role::JOINER) {
        // Send to leader; leader forwards (must preserve packet as-is)
        bool ok = lightThread->sendUdp(lightThread->getLeaderIp(), /*lightThreadReliable=*/false, packet);

        if (ok && reliable) {
            Pending p;
            p.destIp = lightThread->getLeaderIp();      // first hop is leader
            p.originIp = lightThread->getMyIp();
            p.thing = thing; p.id = id; p.action = action;
            p.payload = payload;
            p.seq = seq;
            p.timeoutMs = 200;
            p.retriesLeft = 3;
            p.nextDueMs = millis() + p.timeoutMs;
            pending[seq] = std::move(p);
        }
        return ok;
    }

    logBeeton(BEETON_LOG_WARN, "Beeton: Unknown role, cannot send");
    return false;
}


// Provide list of local things this device represents
void Beeton::defineThings(const std::vector<BeetonThing> &list) {
    localThings.assign(list.begin(), list.end());
}

// Construct a packet from components
std::vector<uint8_t> Beeton::buildPacket(uint8_t flags, uint16_t seq, uint16_t thing, uint8_t id, uint8_t action,
                                         const std::vector<uint8_t> &payload) {
    uint8_t version = 1;
    
    std::vector<uint8_t> out;
    //reserve full header 
    out.reserve(1+16+1+2+2+1+1+payload.size());
    //[0] Version
    out.push_back(version);
    //[1..16] Mesh-Local EID (source IP address)
    String ip = lightThread->getMyIp();
    auto origin = parseIpv6(ip);
    out.insert(out.end(),origin.begin(),origin.end());
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

    static constexpr size_t BEETON_HEADER_SIZE = 24;
    if (raw.size() < BEETON_HEADER_SIZE){
        return false;
    }    

    size_t off = 0;
    //[0] version
    packet.version = raw[off++];
    //[1..16] Origin IPv6
    std::vector<uint8_t> origin(raw.begin() + off, raw.begin() + off + 16);
    off += 16;
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

    if(handleReliablePacket(raw, packet)) {
        return;
    }

    if(handleLeaderControlPacket(packet)) {
        return;
    }

    if(forwardPacketIfLeader(raw, packet)) {
        return;
    }

    dispatchLocalPacket(packet);
}

bool Beeton::handleAckPacket(const BeetonPacket &packet) {
    if(!(packet.flags & BEETON_FLAG_ACK)) {
        return false;
    }

    auto it = pending.find(packet.seq);

    if(it != pending.end()) {
        auto p = it->second;
        pending.erase(it);
        logBeeton(BEETON_LOG_INFO, "ACK received seq=%u", packet.seq);
        if(ackSuccessCb) ackSuccessCb(p.thing, p.id, p.action, p.seq);
    }

    return true;
}

bool Beeton::handleReliablePacket(const std::vector<uint8_t> &raw, const BeetonPacket &packet) {
    if(!(packet.flags & BEETON_FLAG_RELIABLE)) {
        return false;
    }

    if(wasSeenAndMark(packet.originIp, packet.seq, millis())) {
        logBeeton(BEETON_LOG_INFO, "Duplicate reliable packet seq=%u from %s",
                  packet.seq, packet.originIp.c_str());

        auto ack = buildPacket(BEETON_FLAG_ACK, packet.seq, packet.thing, packet.id, packet.action, {});
        lightThread->sendUdp(packet.originIp, false, ack);

        return true;
    }

    auto ack = buildPacket(BEETON_FLAG_ACK, packet.seq, packet.thing, packet.id, packet.action, {});
    lightThread->sendUdp(packet.originIp, false, ack);

    return false;
}

bool Beeton::handleLeaderControlPacket(const BeetonPacket &packet) {
    if(!isLeaderControlPacket(packet)) {
        return false;
    }

    for(size_t i = 0; i + 2 < packet.payload.size(); i += 3) {
        uint16_t thing = readUint16(packet.payload, i);
        uint8_t id = packet.payload[i + 2];

        uint32_t key = makeThingIdKey(thing, id);
        thingIdToIp[key] = packet.originIp;

        logBeeton(BEETON_LOG_INFO,
                  "Registered thing=%04X id=%u at %s",
                  thing, id, packet.originIp.c_str());
    }

    return true;
}

bool Beeton::forwardPacketIfLeader(const std::vector<uint8_t> &raw, const BeetonPacket &packet) {
    if(!lightThread || lightThread->getRole() != Role::LEADER) {
        return false;
    }

    uint32_t key = makeThingIdKey(packet.thing, packet.id);

    auto it = thingIdToIp.find(key);

    if(it == thingIdToIp.end()) {
        logBeeton(BEETON_LOG_WARN,
                  "Leader has no destination for thing=%04X id=%u",
                  packet.thing,
                  packet.id);
        return false;
    }

    const String &destIp = it->second;

    if(destIp.equals(packet.originIp)) {
        return false;
    }

    logBeeton(BEETON_LOG_INFO,
              "Leader forwarding thing=%04X id=%u action=%u to %s",
              packet.thing,
              packet.id,
              packet.action,
              destIp.c_str());

    lightThread->sendUdp(destIp, false, raw);
    return true;
}

void Beeton::dispatchLocalPacket(const BeetonPacket &packet) {
    if(messageCallback) {
        messageCallback(packet.thing, packet.id, packet.action, packet.payload);
    }
}
