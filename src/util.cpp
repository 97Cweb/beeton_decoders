#include "Beeton.h"
#include "BeetonConfig.h"
#include <IPAddress.h>
#include <cstddef>
#include <cstdint>
#include <esp_random.h>
#include <vector>

namespace {
RTC_DATA_ATTR uint32_t retainedSeqMagic = 0;
RTC_DATA_ATTR uint16_t retainedNextSeq = 0;

constexpr uint32_t SEQ_MAGIC = 0xBEE70001;
} // namespace

void Beeton::logBeeton(BeetonLogLevel level, const char *fmt, ...) {
  char buffer[BEETON_LOG_BUFFER_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  switch(level) {
  case BEETON_LOG_DEBUG:
    log_d("[Beeton] %s", buffer);
    break;
  case BEETON_LOG_INFO:
    log_i("[Beeton] %s", buffer);
    break;
  case BEETON_LOG_WARN:
    log_w("[Beeton] %s", buffer);
    break;
  case BEETON_LOG_ERROR:
    log_e("[Beeton] %s", buffer);
    break;
  }
}

std::vector<String> Beeton::splitCsv(const String &input) {
  std::vector<String> result;
  int start = 0;

  while(true) {
    const int end = input.indexOf(',', start);

    if(end == -1) {
      result.push_back(input.substring(start));
      break;
    }
    result.push_back(input.substring(start, end));
    start = end + 1;
  }
  return result;
}

bool Beeton::parseUnsignedField(const String &field, uint32_t maximum, uint32_t &result) {
  String value = field;
  value.trim();

  if(value.length() == 0) {
    return false;
  }

  uint32_t parsed = 0;

  for(size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];

    if(c < '0' || c > '9') {
      return false;
    }
    const uint32_t digit = static_cast<uint32_t>(c - '0');

    if(parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10)) {
      return false;
    }
    parsed = parsed * 10 + digit;
  }
  result = parsed;
  return true;
}

String Beeton::formatPayload(const std::vector<uint8_t> &payload) {
  String result;
  for(size_t i = 0; i < payload.size(); ++i) {
    if(i > 0)
      result += " ";
    result += String(payload[i], DEC);
  }
  return result;
}

std::vector<uint8_t> Beeton::parseIpv6(const String &ip) {
  std::vector<uint8_t> bytes(BEETON_ORIGIN_IP_SIZE, 0);

  IPAddress addr;

  if(!addr.fromString(ip)) {
    logBeeton(BEETON_LOG_WARN, "Invalid IPv6 address: %s", ip.c_str());
    return bytes;
  }

  for(int i = 0; i < BEETON_ORIGIN_IP_SIZE; i++) {
    bytes[i] = addr[i];
  }

  return bytes;
}

// Turn 16 bytes back into "xxxx:xxxx:..." compressed form
String Beeton::formatIpv6(const std::vector<uint8_t> &bytes) {
  char buf[BEETON_IPV6_TEXT_BUFFER_SIZE];
  snprintf(buf, sizeof(buf), "%x:%x:%x:%x:%x:%x:%x:%x", (bytes[0] << 8) | bytes[1],
           (bytes[2] << 8) | bytes[3], (bytes[4] << 8) | bytes[5], (bytes[6] << 8) | bytes[7],
           (bytes[8] << 8) | bytes[9], (bytes[10] << 8) | bytes[11], (bytes[12] << 8) | bytes[13],
           (bytes[14] << 8) | bytes[15]);
  return String(buf);
}

uint16_t Beeton::allocSeq() {
  if(retainedSeqMagic != SEQ_MAGIC) {
    retainedSeqMagic = SEQ_MAGIC;

    // Begin at an unpredictable position after a cold boot.
    retainedNextSeq = static_cast<uint16_t>(esp_random());

    if(retainedNextSeq == 0) {
      retainedNextSeq = 1;
    }
  }

  ++retainedNextSeq;

  if(retainedNextSeq == 0) {
    retainedNextSeq = 1;
  }

  return retainedNextSeq;
}

bool Beeton::wasSeenAndMark(const String &origin, uint16_t seq, uint32_t nowMs) {
  // simple small dedupe window
  for(auto &e : seen) {
    if(e.first.origin == origin && e.first.seq == seq) {
      e.second = nowMs;
      return true;
    }
  }
  if(seen.size() >= BEETON_SEEN_PACKET_MAX)
    seen.erase(seen.begin());
  seen.push_back({SeqKey{origin, seq}, nowMs});
  return false;
}

void Beeton::pumpReliable() {
  if(!lightThread) {
    return;
  }
  uint32_t now = millis();
  std::vector<uint16_t> done;

  for(auto &kv : pending) {
    auto &p = kv.second;
    if((int32_t)(now - p.nextDueMs) < 0)
      continue;

    if(p.retriesLeft == 0) {
      routingHandleAckFailure(p.thing, p.id, p.action, p.seq, p.payload);
      if(ackFailCb)
        ackFailCb(p.thing, p.id, p.action, p.seq);
      done.push_back(kv.first);
      continue;
    }

    // resend same packet bytes (rebuild with same flags/seq)
    auto raw = buildPacket(BEETON_FLAG_RELIABLE, p.seq, p.thing, p.id, p.action, p.payload);
    lightThread->sendUdp(p.nextHopIp, raw);

    p.retriesLeft--;
    p.nextDueMs = now + p.timeoutMs;
  }
  for(auto s : done)
    pending.erase(s);

  // trim dedupe entries
  auto it = seen.begin();
  while(it != seen.end()) {
    if(now - it->second > BEETON_SEEN_PACKET_TTL_MS)
      it = seen.erase(it);
    else
      ++it;
  }
}

uint16_t Beeton::keyToThing(uint32_t key) { return uint16_t((key >> 8) & 0xFFFF); }

uint8_t Beeton::keyToId(uint32_t key) { return uint8_t(key & 0xFF); }

bool Beeton::isLeaderAddress(const BeetonPacket &packet) {
  return packet.thing == BEETON_LEADER_THING && packet.id == BEETON_LEADER_ID;
}

void Beeton::appendUint16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(uint8_t((value >> 8) & 0xFF));
  out.push_back(uint8_t(value & 0xFF));
}

uint16_t Beeton::readUint16(const std::vector<uint8_t> &data, size_t offset) {
  return (uint16_t(data[offset]) << 8) | uint16_t(data[offset + 1]);
}
