//
// globals.cxx - global references
//
// Written by Curtis Olson, curtolson <at> gmail <dot> com.
// Started Fall 2009.
// This code is released into the public domain.
// 

#include "globals.hxx"


pyModuleEventLog *events = NULL;
pyModulePacker *packer = NULL;
UGPacketizer *packetizer = NULL;
UGTelnet *telnet = NULL;
AuraCircleMgr *circle_mgr = NULL;
FGRouteMgr *route_mgr = NULL;
pyModuleBase *mission_mgr = NULL;


bool AuraCoreInit() {
    // create instances
    events = new pyModuleEventLog;
    packer = new pyModulePacker;
    packetizer = new UGPacketizer;
    circle_mgr = new AuraCircleMgr;
    route_mgr = new FGRouteMgr;
    mission_mgr = new pyModuleBase;

    // import and init the python modules
    events->init("comms.events");
    packer->init("comms.packer");
    mission_mgr->init("mission.mission_mgr");
    
    return true;
}
