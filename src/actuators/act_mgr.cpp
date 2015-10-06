/**
 * \file: act_mgr.cpp
 *
 * Front end management interface for output actuators
 *
 * Copyright (C) 2009 - Curtis L. Olson curtolson@gmail.com
 *
 * $Id: act_mgr.cpp,v 1.3 2009/08/25 15:04:01 curt Exp $
 */

#include <math.h>
#include <string.h>
#include <stdio.h>

#include "comms/remote_link.h"
#include "comms/logging.h"
#include "include/globaldefs.h"
#include "init/globals.hxx"
#include "props/props.hxx"
#include "util/myprof.h"
#include "util/timing.h"

#include "act_fgfs.hxx"
#include "sensors/ardupilot.hxx"
#include "sensors/APM2.hxx"
#include "sensors/Goldy2.hxx"

#include "act_mgr.h"

//
// Global variables
//

// flight control output property nodes
static SGPropertyNode *output_aileron_node = NULL;
static SGPropertyNode *output_elevator_node = NULL;
static SGPropertyNode *output_elevator_damp_node = NULL;
static SGPropertyNode *output_throttle_node = NULL;
static SGPropertyNode *output_rudder_node = NULL;

// deprecated, moved downstream
// static SGPropertyNode *act_elevon_mix_node = NULL;

// actuator global limits (dynamically adjustable)
static SGPropertyNode *act_aileron_min = NULL;
static SGPropertyNode *act_aileron_max = NULL;
static SGPropertyNode *act_elevator_min = NULL;
static SGPropertyNode *act_elevator_max = NULL;
static SGPropertyNode *act_throttle_min = NULL;
static SGPropertyNode *act_throttle_max = NULL;
static SGPropertyNode *act_rudder_min = NULL;
static SGPropertyNode *act_rudder_max = NULL;

// actuator property nodes
static SGPropertyNode *act_timestamp_node = NULL;
static SGPropertyNode *act_aileron_node = NULL;
static SGPropertyNode *act_elevator_node = NULL;
static SGPropertyNode *act_throttle_node = NULL;
static SGPropertyNode *act_rudder_node = NULL;
static SGPropertyNode *act_channel5_node = NULL;
static SGPropertyNode *act_channel6_node = NULL;
static SGPropertyNode *act_channel7_node = NULL;
static SGPropertyNode *act_channel8_node = NULL;

// comm property nodes
static SGPropertyNode *act_console_skip = NULL;
static SGPropertyNode *act_logging_skip = NULL;

// throttle safety
static SGPropertyNode *throttle_safety_node = NULL;

// master autopilot switch
static SGPropertyNode *ap_master_switch_node = NULL;
static SGPropertyNode *fcs_mode_node = NULL;

static myprofile debug6a;
static myprofile debug6b;


void Actuator_init() {
    debug6a.set_name("debug6a act update and output");
    debug6b.set_name("debug6b act console logging");

    // bind properties
    output_aileron_node = fgGetNode("/controls/flight/aileron", true);
    output_elevator_node = fgGetNode("/controls/flight/elevator", true);
    output_elevator_damp_node = fgGetNode("/controls/flight/elevator-damp", true);
    output_throttle_node = fgGetNode("/controls/engine/throttle", true);
    output_rudder_node = fgGetNode("/controls/flight/rudder", true);

    // act_elevon_mix_node = fgGetNode("/config/actuators/elevon-mixing", true);

    act_aileron_min = fgGetNode("/config/actuators/limits/aileron-min", true);
    act_aileron_max = fgGetNode("/config/actuators/limits/aileron-max", true);
    act_elevator_min = fgGetNode("/config/actuators/limits/elevator-min", true);
    act_elevator_max = fgGetNode("/config/actuators/limits/elevator-max", true);
    act_throttle_min = fgGetNode("/config/actuators/limits/throttle-min", true);
    act_throttle_max = fgGetNode("/config/actuators/limits/throttle-max", true);
    act_rudder_min = fgGetNode("/config/actuators/limits/rudder-min", true);
    act_rudder_max = fgGetNode("/config/actuators/limits/rudder-max", true);

    act_aileron_min->setFloatValue(-1.0);
    act_aileron_max->setFloatValue( 1.0);
    act_elevator_min->setFloatValue(-1.0);
    act_elevator_max->setFloatValue( 1.0);
    act_throttle_min->setFloatValue( 0.0);
    act_throttle_max->setFloatValue( 1.0);
    act_rudder_min->setFloatValue(-1.0);
    act_rudder_max->setFloatValue( 1.0);

    act_timestamp_node = fgGetNode("/actuators/actuator/time-stamp", true);
    act_aileron_node = fgGetNode("/actuators/actuator/channel", 0, true);
    act_elevator_node = fgGetNode("/actuators/actuator/channel", 1, true);
    act_throttle_node = fgGetNode("/actuators/actuator/channel", 2, true);
    act_rudder_node = fgGetNode("/actuators/actuator/channel", 3, true);
    act_channel5_node = fgGetNode("/actuators/actuator/channel", 4, true);
    act_channel6_node = fgGetNode("/actuators/actuator/channel", 5, true);
    act_channel7_node = fgGetNode("/actuators/actuator/channel", 6, true);
    act_channel8_node = fgGetNode("/actuators/actuator/channel", 7, true);

    // initialize comm nodes
    act_console_skip = fgGetNode("/config/remote-link/actuator-skip", true);
    act_logging_skip = fgGetNode("/config/logging/actuator-skip", true);

    // throttle safety
    throttle_safety_node = fgGetNode("/actuators/throttle-safety", true);

    // master autopilot switch
    ap_master_switch_node = fgGetNode("/autopilot/master-switch", true);
    fcs_mode_node = fgGetNode("/config/fcs/mode", true);

    // default to ap on unless pilot inputs turn it off (so we can run
    // with no pilot inputs connected)
    ap_master_switch_node->setBoolValue( true );

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/actuators", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "actuator" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    printf("i = %d  name = %s module = %s\n",
	    	   i, name.c_str(), module.c_str());

	    if ( module == "null" ) {
		// do nothing
	    } else if ( module == "APM2" ) {
		APM2_act_init( section );
	    } else if ( module == "ardupilot" ) {
		ardupilot_init( section );
	    } else if ( module == "fgfs" ) {
		fgfs_act_init( section );
	    } else if ( module == "Goldy2" ) {
		goldy2_act_init( section );
	    } else {
		printf("Unknown actuator = '%s' in config file\n",
		       module.c_str());
	    }
	}
    }
}


static void set_actuator_values_ap() {
    float aileron = output_aileron_node->getFloatValue();
    if ( aileron < act_aileron_min->getFloatValue() ) {
	aileron = act_aileron_min->getFloatValue();
    }
    if ( aileron > act_aileron_max->getFloatValue() ) {
	aileron = act_aileron_max->getFloatValue();
    }
    act_aileron_node->setFloatValue( aileron );

    float elevator = output_elevator_node->getFloatValue()
	+ output_elevator_damp_node->getFloatValue();
    if ( elevator < act_elevator_min->getFloatValue() ) {
	elevator = act_elevator_min->getFloatValue();
    }
    if ( elevator > act_elevator_max->getFloatValue() ) {
	elevator = act_elevator_max->getFloatValue();
    }
    act_elevator_node->setFloatValue( elevator );

#if 0				// deprecate elevon mixing at this level
    if ( act_elevon_mix_node->getBoolValue() ) {
        // elevon mixing mode (i.e. flying wing)

        // aileron
	act_aileron_node
	    ->setFloatValue( aileron + elevator );

        // elevator
	act_elevator_node
	    ->setFloatValue( aileron - elevator );
    } else {
        // conventional airframe mode

        //aileron
	act_aileron_node->setFloatValue( aileron );

        //elevator
	act_elevator_node->setFloatValue( elevator );
    }
#endif
    
    // rudder
    float rudder = output_rudder_node->getFloatValue();
    if ( rudder < act_rudder_min->getFloatValue() ) {
	rudder = act_rudder_min->getFloatValue();
    }
    if ( rudder > act_rudder_max->getFloatValue() ) {
	rudder = act_rudder_max->getFloatValue();
    }
    act_rudder_node->setFloatValue( rudder );

    // CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!!
    // CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!!

    // Placing the engine throttle under autopilot control requires
    // EXTREME care!!!!

    // Propellers are constructed of sharp knife-like material.
    // Electric motors don't quit and give up if they encounter intial
    // resistance.  Severe injuries to hand or face or any other body
    // part in the vicinity of the motor or prop can occur at any
    // time.

    // Care must be taken during initial setup, and then from that
    // point on during all operational, testing, and ground handling
    // phases.  Extreme vigilance must always be maintianed at all
    // times (especially if the autopilot has control of the
    // throttle.)

    // I cannot stress this point enough!!!  One nanosecond of
    // distraction or loss of focus can result in severe lifelong
    // injury or death!  Do not take your fingers or face for granted.
    // Always maintain utmost caution and correct safety procedures to
    // ensure safe operation with a throttle enabled UAS:

    // 1. Never put your fingers or any other body part in the
    // vicinity or path of the propellor.

    // 2. When the prop is moving (i.e. power test on the ground)
    // always stay behind the prop arc.  If a blade shatters it will
    // shoot outwards and forwards and you never want to be in the
    // path of a flying knife.

    // 3. Always stay behind the aircraft.  If the engine
    // inadvertantly powers up or goes from idle to full throttle, the
    // aircraft could be propelled right at you.

    // Safety is ultimately the responsibility of the operator at the
    // field.  Never put yourself or helpers or spectators in a
    // position where a moment of stupidity will result in an injury.
    // Always make sure everyone is positioned so that if you do make
    // a mistake everyone is still protected and safe!

    // As an internal safety measure, the throttle will be completely
    // turned off (value of 0.0 on a 0.0 - 1.0 scale) when the
    // pressure altitude is < 100' AGL.

    // None of the built in safety measures are sufficient for a safe
    // system!  Pressure sensor readings can glitch, bugs can creep
    // into the code over time, anything can happen.  Be extremely
    // distrustful of the propellor and always make sure your body
    // parts are never in the path of the propellor or where the
    // propellor and aircraft could go if the engine came alive
    // unexpectedly.

    // throttle

    double throttle = output_throttle_node->getFloatValue();
    if ( throttle < act_throttle_min->getFloatValue() ) {
	throttle = act_throttle_min->getFloatValue();
    }
    if ( throttle > act_throttle_max->getFloatValue() ) {
	throttle = act_throttle_max->getFloatValue();
    }
    act_throttle_node->setFloatValue( throttle );

    static bool sas_throttle_override = false;

    if ( !sas_throttle_override ) {
	if ( strcmp(fcs_mode_node->getStringValue(), "sas") == 0 ) {
	    // in sas mode require a sequence of zero throttle, full
	    // throttle, and zero throttle again before throttle pass
	    // through can become active under 100' AGL

	    static int sas_throttle_state = 0;
	    if ( sas_throttle_state == 0 ) {
		if ( output_throttle_node->getFloatValue() < 0.05 ) {
		    // wait for zero throttle
		    sas_throttle_state = 1;
		}
	    } else if ( sas_throttle_state == 1 ) {
		if ( output_throttle_node->getFloatValue() > 0.95 ) {
		    // next wait for full throttle
		    sas_throttle_state = 2;
		}
	    } else if ( sas_throttle_state == 2 ) {
		if ( output_throttle_node->getFloatValue() < 0.05 ) {
		    // next wait for zero throttle again.  Throttle pass
		    // through is now live, even under 100' AGL
		    sas_throttle_state = 3;
		    sas_throttle_override = true;
		}
	    }
	}
    }

    // for any mode that is not sas (and then only if the safety
    // override sequence has been completed), override and disable
    // throttle output if within 100' of the ground (assuming ground
    // elevation is the pressure altitude we recorded with the system
    // started up.
    if ( ! sas_throttle_override ) {
	if ( throttle_safety_node->getBoolValue() ) {
	    act_throttle_node->setFloatValue( 0.0 );
	}
    }

    // printf("throttle = %.2f %d\n", throttle_out_node->getFloatValue(),
    //        servo_out.chn[2]);

    // CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!!
    // CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!! CAUTION!!!
}


static void set_actuator_values_pilot() {
    // The following lines would act as a manual pass-through at the
    // ugear level.  However, manaul pass-through is handled more
    // efficiently (less latency) directly on APM2.x hardware.
    //
    // act_aileron_node->setFloatValue( pilot_aileron_node->getFloatValue() );
    // act_elevator_node->setFloatValue( pilot_elevator_node->getFloatValue() );
    // act_throttle_node->setFloatValue( pilot_throttle_node->getFloatValue() );
    // act_rudder_node->setFloatValue( pilot_rudder_node->getFloatValue() );
}


bool Actuator_update() {
    debug6a.start();

    // time stamp for logging
    act_timestamp_node->setDoubleValue( get_Time() );
    if ( ap_master_switch_node->getBoolValue() ) {
	set_actuator_values_ap();
    } else {
	set_actuator_values_pilot();
    }

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/actuators", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "actuator" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    if ( module == "null" ) {
		// do nothing
	    } else if ( module == "APM2" ) {
		APM2_act_update();
	    } else if ( module == "ardupilot" ) {
		ardupilot_update();
	    } else if ( module == "fgfs" ) {
		fgfs_act_update();
	    } else if ( module == "Goldy2" ) {
		goldy2_act_update();
	    } else {
		printf("Unknown actuator = '%s' in config file\n",
		       module.c_str());
	    }
	}
    }

    debug6a.stop();

    debug6b.start();

    if ( remote_link_on || log_to_file ) {
	// actuators

	uint8_t buf[256];
	int size = packetizer->packetize_actuator( buf );

	if ( remote_link_on ) {
	    remote_link_actuator( buf, size, act_console_skip->getIntValue() );
	}

	if ( log_to_file ) {
	    log_actuator( buf, size, act_logging_skip->getIntValue() );
	}
    }

    debug6b.stop();

    return true;
}


void Actuators_close() {
    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/actuators", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "actuator" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    if ( module == "null" ) {
		// do nothing
	    } else if ( module == "APM2" ) {
		APM2_act_close();
	    } else if ( module == "ardupilot" ) {
		ardupilot_close();
	    } else if ( module == "fgfs" ) {
		fgfs_act_close();
	    } else if ( module == "Goldy2" ) {
		goldy2_act_close();
	    } else {
		printf("Unknown actuator = '%s' in config file\n",
		       module.c_str());
	    }
	}
    }
}
