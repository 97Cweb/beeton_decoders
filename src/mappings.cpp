#include "Beeton.h"

#include <FS.h>
#include <SD.h>
// Load .csv mappings for things, actions, and local IDs
void Beeton::loadMappings(const char *thingsPath, const char *actionsPath, const char *definePath) {
  if(!SD.begin()) {
    logBeeton(BEETON_LOG_ERROR, "SD card mount failed!");
    return;
  }

  ensureFileExists(thingsPath);
  ensureFileExists(actionsPath);
  ensureFileExists(definePath);

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
  if(!file)
    return;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    line.toLowerCase();
    if(line.length() == 0 || line.startsWith("#"))
      continue;

    int comma = line.indexOf(',');
    if(comma > 0) {
      String name = line.substring(0, comma);
      uint16_t id = line.substring(comma + 1).toInt();
      nameToThing[name] = id;
      thingToName[id] = name;
    }
  }
  file.close();
}

void Beeton::loadActions(const char *path) {
  File file = SD.open(path);
  if(!file)
    return;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    line.toLowerCase();
    if(line.length() == 0 || line.startsWith("#"))
      continue;

    int first = line.indexOf(',');
    int second = line.indexOf(',', first + 1);
    if(first > 0 && second > first) {
      String thing = line.substring(0, first);
      String action = line.substring(first + 1, second);
      uint8_t id = line.substring(second + 1).toInt();
      actionNameToId[thing][action] = id;
      actionIdToName[thing][id] = action;
      logBeeton(BEETON_LOG_INFO, "Parsed action mapping: %s,%s -> %u", thing.c_str(),
                action.c_str(), static_cast<unsigned>(id));
    }
  }
  file.close();
}
/*
 * TODO: Strict CSV and numeric validation
 *
 * The mapping loaders currently use String::toInt(), which silently accepts
 * malformed values:
 *
 *   ""     -> 0
 *   "abc"  -> 0
 *   "12x"  -> 12
 *
 * Required fixes:
 *
 * 1. Trim whitespace around every field.
 *
 * 2. Reject missing, empty, or extra fields:
 *      all_things.csv:  thing_name,thing_number
 *      all_actions.csv: thing_name,action_name,action_number
 *      define_this.csv: thing_name,instance_id
 *
 * 3. Replace String::toInt() with a strict unsigned parser that:
 *      - requires the entire field to be numeric;
 *      - rejects signs, trailing characters, and overflow;
 *      - accepts zero when explicitly written as "0";
 *      - validates the destination range.
 *
 * 4. Validate numeric ranges:
 *      thing_number:  0..65535
 *      action_number: 0..255
 *      instance_id:   0..255
 *
 * 5. Report the filename, line number, field, and rejected value.
 *
 * 6. Decide whether one invalid row should:
 *      - be skipped while loading valid rows; or
 *      - fail the entire mapping file.
 *
 * 7. Detect duplicate mappings and report whether an existing entry is being
 *    replaced.
 *
 * 8. Ignore blank lines and define/document comment-line syntax if desired.
 */
void Beeton::loadDefines(const char *path) {
  File file = SD.open(path);
  if(!file)
    return;

  while(file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    line.toLowerCase();
    if(line.length() == 0 || line.startsWith("#"))
      continue;

    int comma = line.indexOf(',');
    if(comma > 0) {
      String thing = line.substring(0, comma);
      uint8_t id = line.substring(comma + 1).toInt();
      if(nameToThing.count(thing)) {
        localThings.push_back({nameToThing[thing], id});
      }
    }
  }
  file.close();
}
