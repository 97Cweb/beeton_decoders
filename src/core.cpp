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

        this->send(true, 0xFFFF,0xFF,0xFF,payload);
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
    uint8_t seq = 0;
    if(reliable){
        flags = 1;
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
    out.push_back((seq >> 8) & 0xFF);
    out.push_back(seq & 0xFF);
    //[20..21] Thing
    out.push_back((thing >> 8) & 0xff);   // high byte
    out.push_back(thing & 0xff); // low byte
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
    packet.seq = (uint16_t(raw[off]) << 8) | uint16_t(raw[off + 1]);
    off += 2;
    //[20..21] Thing ID (Big Endian)
    packet.thing = (uint16_t(raw[off]) << 8) | uint16_t(raw[off+1]);
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

    logBeeton(BEETON_LOG_INFO, "handlePacket: thing=%04x id=%02x action=%02x flags=%02x",
          packet.thing, packet.id, packet.action, packet.flags);

    // Handle packet directed to 0xFF (leader): WHO_I_AM messages from joiners
    if(packet.thing == 0xFFFF && packet.id == 0xFF && packet.action == 0xFF) {
        if(lightThread && lightThread->getRole() == Role::LEADER) {
            logBeeton(BEETON_LOG_INFO, "WHO_AM_I received from %s, %u bytes", packet.originIp.c_str(), packet.payload.size());

            // Map each (thing, id) to the joiner's IP
            for(size_t i = 0; i + 2 < packet.payload.size(); i += 3) {
                uint16_t t = (packet.payload[i] << 8) | packet.payload[i + 1];

                uint8_t i_ = packet.payload[i + 2];
                uint32_t key = makeThingIdKey(t, i_);
                thingIdToIp[key] = packet.originIp;
                logBeeton(BEETON_LOG_INFO, "[Leader] Mapped %04X:%d to %s\n", t, i_, packet.originIp.c_str());
            }
        }
        return;
    }

    // handle messages coming from controllers seeking things
    else if(lightThread && lightThread->getRole() == Role::LEADER) {
        uint32_t key = makeThingIdKey(packet.thing, packet.id);
        String dest = thingIdToIp[key];
        
        // If the sender is not the registered owner of the thing, forward the message
        if(!dest.equals(packet.originIp)) {
            logBeeton(BEETON_LOG_INFO, "forwarding message to %04X:%d at %s\n", packet.thing, packet.id,
                      dest.c_str());
            lightThread->sendUdp(dest,/*lightThreadReliable=*/false, raw);
            return;
        }

        // Otherwise, fall through and invoke the leader's local message callback
    }
    
    //ack finish and stop
    if(packet.flags & BEETON_FLAG_ACK){
        auto it = pending.find(packet.seq);
        if(it!= pending.end()){
            auto p = it->second;
            pending.erase(it);
            if(ackSuccessCb) ackSuccessCb(p.thing, p.id, p.action, p.seq);
        }
        return;
    }
    
    //if reliable request, send if new
    if(packet.flags & BEETON_FLAG_RELIABLE){
        if(wasSeenAndMark(packet.originIp, packet.seq, millis())){
            //already processed
            return;
        }
        auto ack = buildPacket(BEETON_FLAG_ACK, packet.seq, packet.thing, packet.id, packet.action, {});
        lightThread->sendUdp(packet.originIp, /*lightThreadReliable=*/false, ack);
    }

    // Call user callback only for normal messages
    if(messageCallback) {
        messageCallback(packet.thing, packet.id, packet.action, packet.payload);
    }
}
