#include "Beeton.h"

#include <FS.h>
#include <SD.h>
#include <cstdint>
// Load .csv mappings for things, actions, and local IDs
void Beeton::loadMappings(const char *thingsPath, const char *actionsPath, const char *definePath) {
  if(!SD.begin()) {
    logBeeton(BEETON_LOG_ERROR, "SD card mount failed!");
    return;
  }

  ensureFileExists(thingsPath);
  ensureFileExists(actionsPath);
  ensureFileExists(definePath);

  nameToThing.clear();
  thingToName.clear();
  actionNameToId.clear();
  actionIdToName.clear();
  localThings.clear();

  loadThings(thingsPath);
  loadActions(actionsPath);
  loadDefines(definePath);
}

void Beeton::ensureFileExists(const char *path) {
  String filePath = String(path);
  int slashIndex = filePath.lastIndexOf('/');

  // Step 1: Create folder if it doesn't exist
  if(slashIndex > 0) {
    String folder = filePath.substring(0, slashIndex);
    if(!SD.exists(folder)) {
      if(SD.mkdir(folder)) {
        logBeeton(BEETON_LOG_WARN, "Created folder: %s", folder.c_str());
      } else {
        logBeeton(BEETON_LOG_ERROR, "Failed to create folder: %s", folder.c_str());
      }
    }
  }

  // Step 2: Create file if it doesn't exist
  if(!SD.exists(path)) {
    File f = SD.open(path, FILE_WRITE);
    if(f) {
      logBeeton(BEETON_LOG_WARN, "Created blank file: %s", path);
      f.close();
    } else {
      logBeeton(BEETON_LOG_ERROR, "Failed to create file: %s", path);
    }
  }
}

void Beeton::loadThings(const char *path) {
  File file = SD.open(path);
  if(!file) {
    logBeeton(BEETON_LOG_ERROR, "Failed to open mapping file: %s", path);
    return;
  }

  size_t lineNumber = 0;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    lineNumber++;
    line.trim();

    if(line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    std::vector<String> fields = splitCsv(line);

    if(fields.size() != 2) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: expected 2 fields, received %zu", path, lineNumber,
                fields.size());
      continue;
    }

    for(String &field : fields) {
      field.trim();
    }

    fields[0].toLowerCase();

    if(fields[0].length() == 0) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: empty thing_name", path, lineNumber);
      continue;
    }

    uint32_t parsedThing = 0;

    if(!parseUnsignedField(fields[1], UINT16_MAX, parsedThing)) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: invalid thing_number  '%s'; expected 0..65535", path,
                lineNumber, fields[1].c_str());
      continue;
    }

    const uint16_t thing = static_cast<uint16_t>(parsedThing);

    auto nameIt = nameToThing.find(fields[0]);
    if(nameIt != nameToThing.end()) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: duplicate thing_name '%s'; already mapped to %u", path,
                lineNumber, fields[0].c_str(), static_cast<unsigned>(nameIt->second));
      continue;
    }

    auto idIt = thingToName.find(thing);
    if(idIt != thingToName.end()) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: duplicate thing number %u; already mapped to '%s'", path,
                lineNumber, static_cast<unsigned>(thing), idIt->second.c_str());
      continue;
    }

    nameToThing[fields[0]] = thing;
    thingToName[thing] = fields[0];
  }
  file.close();
}

void Beeton::loadActions(const char *path) {
  File file = SD.open(path);

  if(!file) {
    logBeeton(BEETON_LOG_ERROR, "Failed to open mapping file: %s", path);
    return;
  }

  size_t lineNumber = 0;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    lineNumber++;
    line.trim();

    if(line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    std::vector<String> fields = splitCsv(line);

    if(fields.size() != 3) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: expected 3 fields, received %zu", path, lineNumber,
                fields.size());
      continue;
    }

    for(String &field : fields) {
      field.trim();
    }

    fields[0].toLowerCase();
    fields[1].toLowerCase();

    if(fields[0].length() == 0) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: empty thing_name", path, lineNumber);
      continue;
    }

    if(fields[1].length() == 0) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: empty action_name", path, lineNumber);
      continue;
    }

    if(nameToThing.find(fields[0]) == nameToThing.end()) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: unknown thing_name '%s'", path, lineNumber,
                fields[0].c_str());
      continue;
    }

    uint32_t parsedAction = 0;

    if(!parseUnsignedField(fields[2], UINT8_MAX, parsedAction)) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: invalid action_number '%s'; expected 0..255", path,
                lineNumber, fields[2].c_str());
      continue;
    }

    const uint8_t action = static_cast<uint8_t>(parsedAction);

    auto thingByName = actionNameToId.find(fields[0]);
    if(thingByName != actionNameToId.end() &&
       thingByName->second.find(fields[1]) != thingByName->second.end()) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: duplicate action_name '%s' for thing '%s'", path,
                lineNumber, fields[1].c_str(), fields[0].c_str());
      continue;
    }

    auto thingById = actionIdToName.find(fields[0]);
    if(thingById != actionIdToName.end()) {
      auto actionIt = thingById->second.find(action);

      if(actionIt != thingById->second.end()) {
        logBeeton(BEETON_LOG_WARN,
                  "%s:%zu: duplicate action_number %u for thing '%s'; "
                  "already mapped to '%s'",
                  path, lineNumber, static_cast<unsigned>(action), fields[0].c_str(),
                  actionIt->second.c_str());
        continue;
      }
    }

    actionNameToId[fields[0]][fields[1]] = action;
    actionIdToName[fields[0]][action] = fields[1];

    logBeeton(BEETON_LOG_INFO, "Parsed action mapping: %s,%s -> %u", fields[0].c_str(),
              fields[1].c_str(), static_cast<unsigned>(action));
  }

  file.close();
}
/*
 * Mapping files use a simple comma-separated format without quoted fields.
 *
 * Blank lines and lines beginning with '#' are ignored. Invalid rows are
 * logged and skipped while valid rows continue loading. Names are stored
 * in lowercase, and whitespace around individual fields is ignored.
 */
void Beeton::loadDefines(const char *path) {
  File file = SD.open(path);

  if(!file) {
    logBeeton(BEETON_LOG_ERROR, "Failed to open mapping file: %s", path);
    return;
  }

  size_t lineNumber = 0;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    lineNumber++;
    line.trim();

    if(line.length() == 0 || line.startsWith("#")) {
      continue;
    }

    std::vector<String> fields = splitCsv(line);

    if(fields.size() != 2) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: expected 2 fields, received %zu", path, lineNumber,
                fields.size());
      continue;
    }

    for(String &field : fields) {
      field.trim();
    }

    fields[0].toLowerCase();

    if(fields[0].length() == 0) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: empty thing_name", path, lineNumber);
      continue;
    }

    auto thingIt = nameToThing.find(fields[0]);

    if(thingIt == nameToThing.end()) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: unknown thing_name '%s'", path, lineNumber,
                fields[0].c_str());
      continue;
    }

    uint32_t parsedInstance = 0;

    if(!parseUnsignedField(fields[1], UINT8_MAX, parsedInstance)) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: invalid instance_id '%s'; expected 0..255", path,
                lineNumber, fields[1].c_str());
      continue;
    }

    const uint8_t instance = static_cast<uint8_t>(parsedInstance);
    bool duplicate = false;

    for(const BeetonThing &existing : localThings) {
      if(existing.thing == thingIt->second && existing.id == instance) {
        duplicate = true;
        break;
      }
    }

    if(duplicate) {
      logBeeton(BEETON_LOG_WARN, "%s:%zu: duplicate local thing '%s' instance %u", path, lineNumber,
                fields[0].c_str(), static_cast<unsigned>(instance));
      continue;
    }

    localThings.push_back({thingIt->second, instance});
  }

  file.close();
}
