#include "Routing.h"

#include "Beeton.h"
#include "BeetonConfig.h"
#include "WString.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace {
    std::map<uint32_t,String> thingIdToIp;
    uint32_t makeThingIdKey(
            uint16_t thing,
            uint8_t id
            ){
        return (static_cast<uint32_t>(thing)<<8) | id;
    }

    void registerThingOwner(
            uint16_t thing,
            uint8_t id,
            const String &ip
            ){
        thingIdToIp[makeThingIdKey(thing, id)] = ip;
    }
    
    //main thing to edit, how do things get into the table?
    bool handleAnnouncement(const BeetonPacket &packet){
        if(packet.payload.size() %3 !=0){
            return true;
        }

        for(size_t i = 0; i<packet.payload.size();i+=3){
            const uint16_t thing = (static_cast<uint16_t>(packet.payload[i]) << 8) | packet.payload[i+1];

            const uint8_t id = packet.payload[i + 2];

            registerThingOwner(thing, id, packet.originIp);
        }
        return true;
    }
}

bool routingHandleLeaderPacket(const BeetonPacket &packet){
    switch(packet.action){
        case BEETON_LEADER_ACTION_ANNOUNCE:
            return handleAnnouncement(packet);
        default:
            return false;
    }
}

bool routingFindDestination(uint16_t thing, uint8_t id, String &outIp){
    const auto it = thingIdToIp.find(
            makeThingIdKey(thing, id)
            );
    if(it == thingIdToIp.end()){
        return false;
    }
    outIp = it->second;
    return true;
}

const std::map<uint32_t,String> & routingGetKnownDestinations(){
    return thingIdToIp;
}
