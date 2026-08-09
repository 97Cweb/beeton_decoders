#include "Beeton.h"
// Lookup functions for name ↔ ID mappings
String Beeton::getThingName(uint16_t thing) {
  auto it = thingToName.find(thing);

  if(it == thingToName.end()) {
    return "unknown";
  }

  return it->second;
}

String Beeton::getActionName(const String &thingName, uint8_t actionId) {
  auto thingIt = actionIdToName.find(thingName);

  if(thingIt == actionIdToName.end()) {
    return "unknown";
  }

  auto actionIt = thingIt->second.find(actionId);

  if(actionIt == thingIt->second.end()) {
    return "unknown";
  }

  return actionIt->second;
}

bool Beeton::getThingId(const String &name, uint16_t &outThing) {
  auto it = nameToThing.find(name);

  if(it == nameToThing.end()) {
    return false;
  }

  outThing = it->second;
  return true;
}

bool Beeton::getActionId(const String &thingName, const String &actionName, uint8_t &outAction) {
  auto thingIt = actionNameToId.find(thingName);

  if(thingIt == actionNameToId.end()) {
    return false;
  }

  auto actionIt = thingIt->second.find(actionName);

  if(actionIt == thingIt->second.end()) {
    return false;
  }

  outAction = actionIt->second;
  return true;
}

bool Beeton::thingExists(uint16_t thing) { return thingToName.find(thing) != thingToName.end(); }

bool Beeton::actionExists(const String &thingName, uint8_t actionId) {
  auto thingIt = actionIdToName.find(thingName);

  if(thingIt == actionIdToName.end()) {
    return false;
  }

  return thingIt->second.find(actionId) != thingIt->second.end();
}
