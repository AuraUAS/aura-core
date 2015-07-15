// control.cpp - high level control/autopilot interface
//
// Written by Curtis Olson, started January 2006.
//
// Copyright (C) 2006  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//

#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "comms/display.h"
#include "comms/logging.h"
#include "comms/packetizer.hxx"
#include "comms/remote_link.h"
#include "include/globaldefs.h"
#include "init/globals.hxx"

#include "include/util.h"
#include "xmlauto.hxx"

#include "control.h"


//
// global variables
//

// the "FlightGear" autopilot
static FGXMLAutopilot ap;


// autopilot control properties
static SGPropertyNode *ap_master_switch_node = NULL;
static SGPropertyNode *fcs_mode_node = NULL;

static SGPropertyNode *roll_lock_node = NULL;
static SGPropertyNode *yaw_lock_node = NULL;
static SGPropertyNode *altitude_lock_node = NULL;
static SGPropertyNode *speed_lock_node = NULL;
static SGPropertyNode *pitch_lock_node = NULL;
static SGPropertyNode *pointing_lock_node = NULL;

static SGPropertyNode *lookat_mode_node = NULL;
static SGPropertyNode *ned_n_node = NULL;
static SGPropertyNode *ned_e_node = NULL;
static SGPropertyNode *ned_d_node = NULL;

static SGPropertyNode *roll_deg_node = NULL;
static SGPropertyNode *pitch_deg_node = NULL;
static SGPropertyNode *target_roll_deg_node = NULL;
static SGPropertyNode *target_pitch_base_deg_node = NULL;

// console/logging property nodes
static SGPropertyNode *ap_console_skip = NULL;
static SGPropertyNode *ap_logging_skip = NULL;

// home
static SGPropertyNode *home_lon_node = NULL;
static SGPropertyNode *home_lat_node = NULL;
static SGPropertyNode *home_alt_node = NULL;

// task
static SGPropertyNode *task_name_node = NULL;


static void bind_properties() {
    ap_master_switch_node = fgGetNode("/autopilot/master-switch", true);
    fcs_mode_node = fgGetNode("/config/fcs/mode", true);

    roll_lock_node = fgGetNode("/autopilot/locks/roll", true);
    yaw_lock_node = fgGetNode("/autopilot/locks/yaw", true);
    altitude_lock_node = fgGetNode("/autopilot/locks/altitude", true);
    speed_lock_node = fgGetNode("/autopilot/locks/speed", true);
    pitch_lock_node = fgGetNode("/autopilot/locks/pitch", true);
    pointing_lock_node = fgGetNode("/autopilot/locks/pointing", true);

    lookat_mode_node = fgGetNode("/pointing/lookat-mode", true);
    ned_n_node = fgGetNode("/pointing/vector/north", true);
    ned_e_node = fgGetNode("/pointing/vector/east", true);
    ned_d_node = fgGetNode("/pointing/vector/down", true);

    roll_deg_node = fgGetNode("/orientation/roll-deg", true);
    pitch_deg_node = fgGetNode("/orientation/pitch-deg", true);
    target_roll_deg_node
	= fgGetNode("/autopilot/settings/target-roll-deg", true);
    target_pitch_base_deg_node
	= fgGetNode("/autopilot/settings/target-pitch-base-deg", true);

    ap_console_skip = fgGetNode("/config/remote-link/autopilot-skip", true);
    ap_logging_skip = fgGetNode("/config/logging/autopilot-skip", true);

    home_lon_node = fgGetNode("/task/home/longitude-deg", true );
    home_lat_node = fgGetNode("/task/home/latitude-deg", true );
    home_alt_node = fgGetNode("/task/home/altitude-ft", true );

    task_name_node = fgGetNode("/task/current-task-id", true );
}


void control_init() {
    // initialize the autopilot class and build the structures from the
    // configuration file values

    bind_properties();

    // initialize and build the autopilot controller from the property
    // tree config (/config/fcs/autopilot)
    ap.init();

    if ( display_on ) {
	printf("Autopilot initialized\n");
    }
}


void control_reinit() {
    // reread autopilot configuration from the property tree and reset
    // all stages (i.e. real time gain tuning)

    ap.reinit();
}


void control_update(double dt)
{
    // FIXME: there's probably a better place than this, but we need
    // to update the pattern routes every frame (even if the route
    // task is not active) and so the code to do this is going here
    // for now.
    route_mgr->reposition_if_necessary();

    // log auto/manual mode changes
    static bool last_ap_mode = false;
    if ( ap_master_switch_node->getBoolValue() != last_ap_mode ) {
	if ( event_log_on ) {
	    string ap_master_str;
	    if ( ap_master_switch_node->getBoolValue() ) {
		ap_master_str = "autopilot";
	    } else {
		ap_master_str = "manual flight";
	    }
	    event_log( "Master control switch:", ap_master_str.c_str() );
	}
	last_ap_mode = ap_master_switch_node->getBoolValue();
    }
    
    static string last_fcs_mode = "";
    string fcs_mode = fcs_mode_node->getStringValue();
    if ( ap_master_switch_node->getBoolValue() ) {
	if ( last_fcs_mode != fcs_mode ) {
	    if ( event_log_on ) {
		event_log( "control mode changed to:", fcs_mode.c_str() );
	    }

	    // turn on pointing (universally for now)
	    pointing_lock_node->setStringValue( "on" );
	    lookat_mode_node->setStringValue( "ned-vector" );
	    ned_n_node->setFloatValue( 0.0 );
	    ned_e_node->setFloatValue( 0.0 );
	    ned_d_node->setFloatValue( 1.0 );

	    if ( fcs_mode == "inactive" ) {
		// unset all locks for "inactive"
		roll_lock_node->setStringValue( "" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "" );
	    } else if ( fcs_mode == "basic" ) {
		// set lock modes for "basic" inner loops only
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "autocoord" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "elevator" );
	    } else if ( fcs_mode == "roll" ) {
		// set lock modes for roll only
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "" );
	    } else if ( fcs_mode == "roll+pitch" ) {
		// set lock modes for roll and pitch
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "elevator" );
	    } else if ( fcs_mode == "basic+alt+speed" ) {
		// set lock modes for "basic" + alt hold
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "autocoord" );
		altitude_lock_node->setStringValue( "throttle" );
		speed_lock_node->setStringValue( "pitch" );
		pitch_lock_node->setStringValue( "elevator" );
	    } else if ( fcs_mode == "cas" ) {
		// set lock modes for "cas"
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "elevator" );
		pointing_lock_node->setStringValue( "on" );

		float target_roll_deg = roll_deg_node->getFloatValue();
		if ( target_roll_deg > 45.0 ) { target_roll_deg = 45.0; }
		if ( target_roll_deg < -45.0 ) { target_roll_deg = -45.0; }
		target_roll_deg_node->setFloatValue( target_roll_deg );

		float target_pitch_base_deg = pitch_deg_node->getFloatValue();
		if ( target_pitch_base_deg > 15.0 ) {
		    target_pitch_base_deg = 15.0;
		}
		if ( target_pitch_base_deg < -15.0 ) {
		    target_pitch_base_deg = -15.0;
		}
		target_pitch_base_deg_node->setFloatValue( target_pitch_base_deg );
	    }
	}
	last_fcs_mode = fcs_mode;
    } else {
	if ( fcs_mode != "" ) {
	    // autopilot is just de-activated, clear lock modes
	    roll_lock_node->setStringValue( "" );
	    yaw_lock_node->setStringValue( "" );
	    altitude_lock_node->setStringValue( "" );
	    speed_lock_node->setStringValue( "" );
	    pitch_lock_node->setStringValue( "" );
	    pointing_lock_node->setStringValue( "" );
	}
	last_fcs_mode = "";
    }

    // update the autopilot stages (even in manual flight mode.)  This
    // keeps the differential metric up to date, tracks manual inputs,
    // and keeps more continuity in the flight when the mode is
    // switched to autopilot.
    ap.update( dt );

    // FIXME !!!
    // I want a departure route, an approach route, and mission route,
    // and circle hold point (all indicated on the ground station map.)
    // FIXME !!!
    
    if ( remote_link_on || log_to_file ) {
	// send one waypoint per message, then home location (with
	// index = 65535)

	static int wp_index = 0;
	int index = 0;
	SGWayPoint wp;
	int route_size = 0;

	string task_name = task_name_node->getStringValue();
	if ( task_name == "route" ) {
	    if ( route_mgr != NULL ) {
		route_size = route_mgr->size();
		if ( route_size > 0 && wp_index < route_size ) {
		    wp = route_mgr->get_waypoint( wp_index );
		    index = wp_index;
		}
	    }
	} else if ( task_name == "circle-coord" ) {
	    if ( circle_mgr != NULL ) {
		wp = circle_mgr->get_center();
		route_size = 1;
		index = wp_index;
	    }
	}

	// special case send home as a route waypoint with id = 65535
	if ( wp_index == route_size ) {
	    wp = SGWayPoint( home_lon_node->getDoubleValue(),
			     home_lat_node->getDoubleValue(),
			     home_alt_node->getDoubleValue() );
	    index = 65535;
	}

	uint8_t buf[256];
	int pkt_size = packetizer->packetize_ap( buf, route_size, &wp, index );
	
	if ( remote_link_on ) {
	    bool result = remote_link_ap( buf, pkt_size,
					  ap_console_skip->getIntValue() );
	    if ( result ) {
		wp_index++;
		if ( wp_index > route_size ) {
		    wp_index = 0;
		}
	    }
	}

	if ( log_to_file ) {
	    log_ap( buf, pkt_size, ap_logging_skip->getIntValue() );
	}
    }
}


void control_close() {
  // nothing to see here, move along ...
}
