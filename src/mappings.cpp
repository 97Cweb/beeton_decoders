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
                logBeeton(BEETON_LOG_WARN, "Created folder: %s\n", folder.c_str());
            } else {
                logBeeton(BEETON_LOG_ERROR, "Failed to create folder: %s\n", folder.c_str());
            }
        }
    }

    // Step 2: Create file if it doesn't exist
    if(!SD.exists(path)) {
        File f = SD.open(path, FILE_WRITE);
        if(f) {
            logBeeton(BEETON_LOG_WARN, "Created blank file: %s\n", path);
            f.close();
        } else {
            logBeeton(BEETON_LOG_ERROR, "Failed to create file: %s\n", path);
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
            logBeeton(BEETON_LOG_INFO, "Parsed action mapping: %s,%s -> %d\n", thing.c_str(),
                      action.c_str(), id);
        }
    }
    file.close();
}

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
