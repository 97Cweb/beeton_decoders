#include "Beeton.h"

#include <cstddef>
#include <cstdint>
#include <vector>

bool BeetonPayload::hasValue() const{
  return type != Type::NONE && type != Type::BYTES;
}

bool BeetonPayload::hasBytes() const {
  return type == Type::BYTES;
}

int64_t BeetonPayload::getValue(int64_t fallback) const {
  if(!hasValue()){
    return fallback;
  }

  return value;
}

const std::vector<uint8_t> &BeetonPayload::getBytes() const{
  return bytes;
}

std::vector<uint8_t> Beeton::encodeValuePayload(uint8_t type, uint64_t value, uint8_t width){
  std::vector<uint8_t> encoded;
  encoded.reserve(static_cast<size_t>(width) + 1);
  encoded.push_back(type);

  for(uint8_t i = 0; i < width; ++i){
    const uint8_t shift = static_cast<uint8_t>((width-i-1) * 8);

    encoded.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
  }
  return encoded;
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action) {
  const std::vector<uint8_t> encoded = {
      static_cast<uint8_t>(BeetonPayload::Type::NONE)
  };

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, bool value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::BOOL),
          value ? 1 : 0, 1);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, uint8_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::UINT8),
          value, 1);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, uint16_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::UINT16),
          value, 2);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, uint32_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::UINT32),
          value, 4);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, int8_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::INT8),
          static_cast<uint8_t>(value), 1);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, int16_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::INT16),
          static_cast<uint16_t>(value), 2);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action, int32_t value) {
  const auto encoded =
      encodeValuePayload(
          static_cast<uint8_t>(BeetonPayload::Type::INT32),
          static_cast<uint32_t>(value), 4);

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::send(bool reliable, uint16_t thing, uint8_t id,
                  uint8_t action,
                  const std::vector<uint8_t> &bytes) {
  std::vector<uint8_t> encoded;
  encoded.reserve(bytes.size() + 1);

  encoded.push_back(
      static_cast<uint8_t>(BeetonPayload::Type::BYTES));

  encoded.insert(encoded.end(), bytes.begin(), bytes.end());

  return sendPacket(reliable, thing, id, action, encoded, true);
}

bool Beeton::decodePayload(const std::vector<uint8_t> &encoded, BeetonPayload & decoded){
  decoded.type = BeetonPayload::Type::NONE;
  decoded.value = 0;
  decoded.bytes.clear();

  if(encoded.empty()){
    return false;
  }

  decoded.type = static_cast<BeetonPayload::Type>(encoded[0]);

  auto readUnsigned = [&encoded](uint8_t width, uint64_t & result) -> bool{
    if(encoded.size() != static_cast<size_t>(width) + 1){
      return false;
    }

    result = 0;

    for(uint8_t i = 0; i < width; ++i){
      result = (result << 8) | static_cast<uint64_t>(encoded[i+1]);
    }
    return true;
  };

  uint64_t raw = 0;

  switch(decoded.type){
    case BeetonPayload::Type::NONE:
      return encoded.size() == 1;

    case BeetonPayload::Type::BOOL:
      if(!readUnsigned(1, raw) || raw > 1){
        return false;
      }

      decoded.value = static_cast<int64_t>(raw);
      return true;

    case BeetonPayload::Type::UINT8:
      if(!readUnsigned(1, raw)){
        return false;
      }

      decoded.value = static_cast<int64_t>(raw);
      return true;

    case BeetonPayload::Type::UINT16:
        if(!readUnsigned(2, raw)) {
          return false;
        }

        decoded.value = static_cast<int64_t>(raw);
        return true;

      case BeetonPayload::Type::UINT32:
        if(!readUnsigned(4, raw)) {
          return false;
        }

        decoded.value = static_cast<int64_t>(raw);
        return true;

      case BeetonPayload::Type::INT8:
        if(!readUnsigned(1, raw)) {
          return false;
        }

        decoded.value =
            (raw & 0x80) ? static_cast<int64_t>(raw) - 0x100
                         : static_cast<int64_t>(raw);
        return true;

      case BeetonPayload::Type::INT16:
        if(!readUnsigned(2, raw)) {
          return false;
        }

        decoded.value =
            (raw & 0x8000) ? static_cast<int64_t>(raw) - 0x10000
                           : static_cast<int64_t>(raw);
        return true;

      case BeetonPayload::Type::INT32:
        if(!readUnsigned(4, raw)) {
          return false;
        }

        decoded.value =
            (raw & 0x80000000ULL)
                ? static_cast<int64_t>(raw) - 0x100000000LL
                : static_cast<int64_t>(raw);
        return true;

      case BeetonPayload::Type::BYTES:
        decoded.bytes.assign(encoded.begin() + 1, encoded.end());
        return true;

      default:
        decoded.type = BeetonPayload::Type::NONE;
        return false;
      }
}
