/**
 * \file: airdata_mgr.cxx
 *
 * Front end management interface for reading air data.
 *
 * Copyright (C) 2009 - Curtis L. Olson curtolson@flightgear.org
 *
 */


#include "python/pyprops.hxx"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "comms/logging.h"
#include "comms/remote_link.h"
#include "include/globaldefs.h"
#include "init/globals.hxx"
#include "util/lowpass.hxx"
#include "util/myprof.h"

#include "APM2.hxx"
#include "Goldy2.hxx"
#include "imu_fgfs.hxx"

#include "airdata_mgr.hxx"

//
// Global variables
//

// initial values are the 'time factor'
static LowPassFilter pressure_alt_filt( 0.1 );
static LowPassFilter ground_alt_filt( 30.0 );
static LowPassFilter airspeed_filt( 0.5 );
static LowPassFilter Ps_filt_err( 300.0 );
static LowPassFilter climb_filt( 1.0 );

static float true_alt_m = 0.0;

// property nodes
static pyPropertyNode airdata_node;
static pyPropertyNode filter_node;
static pyPropertyNode pos_filter_node;
static pyPropertyNode pos_pressure_node;
static pyPropertyNode pos_combined_node;
static pyPropertyNode vel_node;
static pyPropertyNode remote_link_node;
static pyPropertyNode logging_node;
static pyPropertyNode task_node;

// static SGPropertyNode *airdata_timestamp_node = NULL;
// static SGPropertyNode *airdata_airspeed_node = NULL;
// static SGPropertyNode *airdata_pressure_node = NULL;

// // input property nodes
// static SGPropertyNode *filter_navigation_node = NULL;
// static SGPropertyNode *filter_alt_node = NULL;
// static SGPropertyNode *filter_ground_node = NULL;

// // output property nodes
// static SGPropertyNode *pressure_alt_node = NULL;
// static SGPropertyNode *pressure_alt_smoothed_node = NULL;
// static SGPropertyNode *airspeed_node = NULL;
// static SGPropertyNode *airspeed_smoothed_node = NULL;
// static SGPropertyNode *pressure_error_m_node = NULL;
// static SGPropertyNode *true_alt_m_node = NULL;
// static SGPropertyNode *true_alt_ft_node = NULL;
// static SGPropertyNode *true_agl_m_node = NULL;
// static SGPropertyNode *true_agl_ft_node = NULL;
// static SGPropertyNode *agl_alt_m_node = NULL;
// static SGPropertyNode *agl_alt_ft_node = NULL;
// static SGPropertyNode *vert_fps_node = NULL;
// static SGPropertyNode *ground_alt_press_m_node = NULL;
// static SGPropertyNode *true_oat_node = NULL;

// // comm property nodes
// static SGPropertyNode *airdata_console_skip = NULL;
// static SGPropertyNode *airdata_logging_skip = NULL;

// // mission nodes
// static SGPropertyNode *is_airborne_node = NULL;

static myprofile debug2b1;
static myprofile debug2b2;

// 1. ground altitude, 2. error between pressure altitude and gps altitude
static bool airdata_calibrated = false;
static bool alt_error_calibrated = false;
    
void AirData_init() {
    debug2b1.set_name("debug2b1 airdata update");
    debug2b2.set_name("debug2b2 airdata console link");

    airdata_node = pyGetNode("/sensors/airdata", true);
    filter_node = pyGetNode("/filters/filter", true);
    pos_filter_node = pyGetNode("/position/filter", true);
    pos_pressure_node = pyGetNode("/position/pressure", true);
    pos_combined_node = pyGetNode("/position/combined", true);
    vel_node = pyGetNode("/velocity", true);
    remote_link_node = pyGetNode("/config/remote_link", true);
    logging_node = pyGetNode("/config/logging", true);
    task_node = pyGetNode("/task", true);

    // // initialize air data property nodes
    // airdata_timestamp_node = pyGetNode("/sensors/airdata/time-stamp", true);
    // airdata_airspeed_node = pyGetNode("/sensors/airdata/airspeed-kt", true);
    // airdata_pressure_node = pyGetNode("/sensors/airdata/pressure-mbar", true);

    // // input property nodes
    // filter_navigation_node = pyGetNode("/filters/filter/navigation", true);
    // filter_alt_node = pyGetNode("/position/filter/altitude-m", true);
    // filter_ground_node = pyGetNode("/position/filter/altitude-ground-m", true);

    // // filtered/computed output property nodes
    // pressure_alt_node = pyGetNode("/position/pressure/altitude-m", true);
    // pressure_alt_smoothed_node = pyGetNode("/position/pressure/altitude-smoothed-m", true);
    // airspeed_node = pyGetNode("/velocity/airspeed-kt", true);
    // airspeed_smoothed_node = pyGetNode("/velocity/airspeed-smoothed-kt", true);

    // true_alt_m_node = pyGetNode("/position/combined/altitude-true-m",true);
    // true_alt_ft_node = pyGetNode("/position/combined/altitude-true-ft",true);
    // true_agl_m_node = pyGetNode("/position/combined/altitude-agl-m",true);
    // true_agl_ft_node = pyGetNode("/position/combined/altitude-agl-ft",true);
    // agl_alt_m_node = pyGetNode("/position/pressure/altitude-agl-m", true);
    // agl_alt_ft_node = pyGetNode("/position/pressure/altitude-agl-ft", true);

    // pressure_error_m_node
    // 	= pyGetNode("/position/pressure/pressure-error-m", true);
    // vert_fps_node
    // 	= pyGetNode("/velocity/pressure-vertical-speed-fps",true);
    // ground_alt_press_m_node
    //     = pyGetNode("/position/pressure/altitude-ground-m", true);
    // true_oat_node = pyGetNode("/position/pressure/outside-air-temp-degC", true);

    // // initialize comm nodes
    // airdata_console_skip = pyGetNode("/config/remote-link/airdata-skip", true);
    // airdata_logging_skip = pyGetNode("/config/logging/airdata-skip", true);

    // // initialize mission nodes
    // is_airborne_node = pyGetNode("/task/is-airborne", true);
 
    // traverse configured modules
    pyPropertyNode toplevel = pyGetNode("/config/sensors/airdata_group", true);
    for ( int i = 0; i < toplevel.getLen("airdata"); i++ ) {
	pyPropertyNode section = toplevel.getChild("airdata", i);
	string source = section.getString("source");
	bool enabled = section.getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	pyPropertyNode parent = pyGetNode("/sensors/", true);
	pyPropertyNode base = parent.getChild("airdata", i, true);
	printf("airdata: %d = %s\n", i, source.c_str());
	if ( source == "null" ) {
	    // do nothing
	} else if ( source == "APM2" ) {
	    APM2_airdata_init( &base );
	} else if ( source == "fgfs" ) {
	    fgfs_airdata_init( &base );
	} else if ( source == "Goldy2" ) {
	    goldy2_airdata_init( &base, &section );
	} else {
	    printf("Unknown air data source = '%s' in config file\n",
		   source.c_str());
	}
    }
}


static void update_pressure_helpers() {
    static float pressure_alt_filt_last = 0.0;
    static double last_time = 0.0;
    double cur_time = airdata_node.getDouble("timestamp");

    double dt = cur_time - last_time;
    if ( dt > 1.0 ) {
	dt = 1.0;		// keep dt smallish
    }

    if ( dt < 0.000001 ) {
	return; 		// do nothing if dt zero
    }

    //
    // 1. Compute altitude from airdata pressure sensor
    //

    // Forumula taken from:
    //   http://keisan.casio.com/exec/system/1224585971
    // or possibly:
    //   http://keisan.casio.com/has10/SpecExec.cgi?path=06000000%2eScience%2f02100100%2eEarth%20science%2f12000300%2eAltitude%20from%20atmospheric%20pressure%2fdefault%2exml&charset=utf-8
    //
    // h = (((P0/P)^(1/5.257) - 1) * (T+273.15)) / 0.0065
    // T = h*0.0065 / ((P0/P)^(1/5.257) - 1) - 273.15

    double P = airdata_node.getDouble("pressure_mbar"); // sensed pressure
    const double P0 = 1013.25;	// standard sea level pressure

    // The APM temp sensor is highly biased by board temp and cabin
    // interior temperature (not OAT), so it really makes more sense
    // to just pick a fixed value here so our relative altitude does
    // not drift as our airplane temp changes.  We have a later system
    // that estimates the error between gps altitude and pressure
    // altitude.
    const double T = 15.0;	// standard temp

    // Compute altitude on a standard day
    double tmp1 = pow((P0/P), 1.0/5.257) - 1.0;
    double alt_m = (tmp1 * (T + 273.15)) / 0.0065;
    pos_pressure_node.setDouble( "altitude_m", alt_m );

    //
    // 2. Filter/Smooth Altitude and airspeed to reduce noise
    //

    float Pt = airdata_node.getDouble("airspeed_kt");
    float Ps = alt_m; /* pressure_alt_node.getDouble(); */
    float filter_alt_m = pos_filter_node.getDouble("altitude_m");

    if ( !airdata_calibrated ) {
	airdata_calibrated = true;
	airspeed_filt.init( Pt );
	pressure_alt_filt.init( Ps );
	ground_alt_filt.init( Ps );
	climb_filt.init( 0.0 );
    }
    
    airspeed_filt.update( Pt, dt );
    pressure_alt_filt.update( Ps, dt );
    if ( ! task_node.getBool("is_airborne") ) {
	// ground reference altitude averaged current altitude over
	// first 30 seconds while on the ground
	ground_alt_filt.update( Ps, dt );
    }


    // publish values
    vel_node.setDouble( "airspeed_kt", Pt /* raw */ );
    vel_node.setDouble( "airspeed_smoothed_kt", airspeed_filt.get_value() );
    pos_pressure_node.setDouble( "altitude_smoothed_m", pressure_alt_filt.get_value() );
    pos_pressure_node.setDouble( "altitude_ground_m", ground_alt_filt.get_value() );

    //
    // 3. Compute a filtered error difference between gps altitude and
    //    pressure altitude.
    //

    if ( !alt_error_calibrated ) {
	if ( filter_node.getString("navigation") == "valid" ) {
	    alt_error_calibrated = true;
	    Ps_filt_err.init( filter_alt_m - Ps );
	}
    } else {
	Ps_filt_err.update( filter_alt_m - Ps, dt );

	// best guess at true altitude
	true_alt_m = pressure_alt_filt.get_value() + Ps_filt_err.get_value();
    }

    // true altitude estimate - filter ground average is our best
    // estimate of true agl if altitude has not changed recently.
    double true_agl_m = true_alt_m - filter_node.getDouble("altitude_ground_m");

    //
    // 4.0 Compute outside air temperature estimate based on 'true'
    // altitude fed back into the above formula.  Note if this seems
    // way off from reality then the math has been cross checked and
    // consider there may be a bias in the pressure sensor
    //

    // T = h*0.0065 / ((P0/P)^(1/5.257) - 1) - 273.15
    double T_est = ((true_alt_m * 0.0065) / tmp1) - 273.15;
    pos_pressure_node.setDouble( "outside_air_temp_degC", T_est );

    //
    // 5.0 Compute some other stuff
    // 

    // compute rate of climb based on pressure altitude change
    float climb = (pressure_alt_filt.get_value() - pressure_alt_filt_last) / dt;
    pressure_alt_filt_last = pressure_alt_filt.get_value();
    climb_filt.update( climb, dt );

    last_time = cur_time;

    // publish values to property tree
    pos_pressure_node.setDouble( "pressure_error_m", Ps_filt_err.get_value() );
    pos_combined_node.setDouble( "altitude_true_m", true_alt_m );
    pos_combined_node.setDouble( "altitude_true_ft",
				 true_alt_m * SG_METER_TO_FEET );
    pos_combined_node.setDouble( "altitude_agl_m", true_agl_m );
    pos_combined_node.setDouble( "altitude_agl_ft",
				 true_agl_m * SG_METER_TO_FEET );
    pos_pressure_node.setDouble( "altitude_agl_m",
				 pressure_alt_filt.get_value()
				 - ground_alt_filt.get_value() );
    pos_pressure_node.setDouble( "altitude_agl_ft",
				 (pressure_alt_filt.get_value()
				  - ground_alt_filt.get_value() )
				 * SG_METER_TO_FEET );
    vel_node.setDouble( "pressure_vertical_speed_fps",
			climb_filt.get_value() * SG_METER_TO_FEET );

    // printf("Ps = %.1f nav = %.1f bld = %.1f vsi = %.2f\n",
    //        pressure_alt_filt, navpacket.alt, true_alt_m, climb_filt.get_value());

#if 0
    // experimental section ... try to estimate thermal activity ...
    static SGPropertyNode *throttle = pyGetNode("/controls/engine/throttle", true);
    static double sum_x = 0.0;
    static double sum_y = 0.0;
    static double sum_x2 = 0.0;
    static double sum_y2 = 0.0;
    static double sum_xy = 0.0;

    double x = throttle.getDouble();
    double y = climb_filt.get_value() * SG_METER_TO_FEET * 60.0; // fpm
    double n = 6000.0;		// 100hz * 60 sec
    double nfact = (n-1.0)/n;
    sum_x = sum_x*nfact + x;
    sum_y = sum_y*nfact + y;
    //printf("x=%.2f y=%.0f sum_x=%.1f sum_y=%.0f\n", x, y, sum_x, sum_y);
    sum_x2 = sum_x2*nfact + x*x;
    sum_y2 = sum_y2*nfact + y*y;
    sum_xy = sum_xy*nfact + x*y;

    double a1 = (n*sum_xy - sum_x*sum_y) / (n*sum_x2 - sum_x*sum_x);
    double a0 = (sum_y - a1*sum_x) / n;
    printf("y = %.2f + %.2f * x\n", a0, a1);
#endif

}


bool AirData_update() {
    debug2b1.start();

    air_prof.start();

    bool fresh_data = false;

    // traverse configured modules
    pyPropertyNode toplevel = pyGetNode("/config/sensors/airdata_group", true);
    for ( int i = 0; i < toplevel.getLen("airdata"); i++ ) {
	pyPropertyNode section = toplevel.getChild("airdata", i);
	string source = section.getString("source");
	bool enabled = section.getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	string basename = "/sensors/";
	if ( source == "null" ) {
	    // do nothing
	} else if ( source == "APM2" ) {
	    fresh_data = APM2_airdata_update();
	} else if ( source == "fgfs" ) {
	    fresh_data = fgfs_airdata_update();
	} else if ( source == "Goldy2" ) {
	    fresh_data = goldy2_airdata_update();
	} else {
	    printf("Unknown air data source = '%s' in config file\n",
		   source.c_str());
	}
    }

    debug2b1.stop();
    debug2b2.start();

    if ( fresh_data ) {
	update_pressure_helpers();

	if ( remote_link_on || log_to_file ) {
	    uint8_t buf[256];
	    int size = packetizer->packetize_airdata( buf );

	    if ( remote_link_on ) {
		// printf("sending filter packet\n");
		remote_link_airdata( buf, size,
				     remote_link_node.getLong("airdata_skip") );
	    }

	    if ( log_to_file ) {
		log_airdata( buf, size, logging_node.getLong("airdata_skip") );
	    }
	}
    }

    debug2b2.stop();

    air_prof.stop();

    return fresh_data;
}


void AirData_recalibrate() {
    // traverse configured modules
    pyPropertyNode toplevel = pyGetNode("/config/sensors/airdata_group", true);
    for ( int i = 0; i < toplevel.getLen("airdata"); i++ ) {
	pyPropertyNode section = toplevel.getChild("airdata", i);
	string source = section.getString("source");
	bool enabled = section.getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	if ( source == "null" ) {
	    // do nothing
	} else if ( source == "APM2" ) {
	    APM2_airdata_zero_airspeed();
	} else if ( source == "fgfs" ) {
	    // do nothing
	} else if ( source == "Goldy2" ) {
	    // do nothing
	} else {
	    printf("Unknown air data source = '%s' in config file\n",
		   source.c_str());
	}
    }
    // mark these as requiring calibrate so they will be reinited
    // starting with current values
    airdata_calibrated = false;
    alt_error_calibrated = false;
}


void AirData_close() {
    // traverse configured modules
    pyPropertyNode toplevel = pyGetNode("/config/sensors/airdata_group", true);
    for ( int i = 0; i < toplevel.getLen("airdata"); i++ ) {
	pyPropertyNode section = toplevel.getChild("airdata", i);
	string source = section.getString("source");
	bool enabled = section.getBool("enable");
	if ( !enabled ) {
	    continue;
	}
	if ( source == "null" ) {
	    // do nothing
	} else if ( source == "APM2" ) {
	    APM2_airdata_close();
	} else if ( source == "fgfs" ) {
	    // nop
	} else if ( source == "Goldy2" ) {
	    goldy2_airdata_close();
	} else {
	    printf("Unknown air data source = '%s' in config file\n",
		   source.c_str());
	}
    }
}
