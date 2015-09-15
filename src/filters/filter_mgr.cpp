/**
 * \file: filter_mgr.cpp
 *
 * Front end management interface for executing the available filter codes.
 *
 * Copyright (C) 2009 - Curtis L. Olson curtolson <at> gmail <dot> com
 *
 * $Id: adns_mgr.cpp,v 1.6 2009/05/15 17:04:56 curt Exp $
 */


#include <stdio.h>

//#include "include/ugear_config.h"

#include "comms/remote_link.h"
#include "comms/logging.h"
#include "filters/curt/adns_curt.hxx"
#include "filters/umngnss_euler/umngnss_euler.h"
#include "filters/umngnss_quat/umngnss_quat.h"
#include "include/globaldefs.h"
#include "init/globals.hxx"
#include "props/props.hxx"
#include "util/myprof.h"

#include "filter_mgr.h"


//
// Global variables
//

static double last_imu_time = 0.0;

// imu property nodes
static SGPropertyNode *imu_timestamp_node = NULL;
static SGPropertyNode *imu_p_node = NULL;
static SGPropertyNode *imu_q_node = NULL;
static SGPropertyNode *imu_r_node = NULL;
static SGPropertyNode *imu_ax_node = NULL;
static SGPropertyNode *imu_ay_node = NULL;
static SGPropertyNode *imu_az_node = NULL;
static SGPropertyNode *imu_hx_node = NULL;
static SGPropertyNode *imu_hy_node = NULL;
static SGPropertyNode *imu_hz_node = NULL;

// filter property nodes
static SGPropertyNode *filter_timestamp_node = NULL;
static SGPropertyNode *filter_theta_node = NULL;
static SGPropertyNode *filter_phi_node = NULL;
static SGPropertyNode *filter_psi_node = NULL;
static SGPropertyNode *filter_lat_node = NULL;
static SGPropertyNode *filter_lon_node = NULL;
static SGPropertyNode *filter_alt_m_node = NULL;
static SGPropertyNode *filter_alt_ft_node = NULL;
static SGPropertyNode *filter_vn_node = NULL;
static SGPropertyNode *filter_ve_node = NULL;
static SGPropertyNode *filter_vd_node = NULL;
static SGPropertyNode *filter_status_node = NULL;

static SGPropertyNode *filter_phi_dot_node = NULL;
static SGPropertyNode *filter_the_dot_node = NULL;
static SGPropertyNode *filter_psi_dot_node = NULL;

static SGPropertyNode *filter_track_node = NULL;
static SGPropertyNode *filter_vel_node = NULL;
static SGPropertyNode *filter_vert_speed_fps_node = NULL;
static SGPropertyNode *filter_ground_alt_m_node = NULL;
static SGPropertyNode *filter_alt_agl_m_node = NULL;
static SGPropertyNode *filter_alt_agl_ft_node = NULL;

// air data property nodes (wind estimation)
static SGPropertyNode *airdata_airspeed_node = NULL;
static SGPropertyNode *est_wind_speed_kt = NULL;
static SGPropertyNode *est_wind_dir_deg = NULL;
static SGPropertyNode *est_wind_east_mps = NULL;
static SGPropertyNode *est_wind_north_mps = NULL;
static SGPropertyNode *est_pitot_scale_factor = NULL;
static SGPropertyNode *true_airspeed_kt = NULL;
static SGPropertyNode *true_heading_deg = NULL;
static SGPropertyNode *true_air_east_mps = NULL;
static SGPropertyNode *true_air_north_mps = NULL;

// official altitude outputs
static SGPropertyNode *official_alt_m_node = NULL;
static SGPropertyNode *official_alt_ft_node = NULL;
static SGPropertyNode *official_agl_m_node = NULL;
static SGPropertyNode *official_agl_ft_node = NULL;
static SGPropertyNode *official_ground_m_node = NULL;

// comm property nodes
static SGPropertyNode *filter_console_skip = NULL;
static SGPropertyNode *filter_logging_skip = NULL;


void Filter_init() {
    // initialize imu property nodes
    imu_timestamp_node = fgGetNode("/sensors/imu/time-stamp", true);
    imu_p_node = fgGetNode("/sensors/imu/p-rad_sec", true);
    imu_q_node = fgGetNode("/sensors/imu/q-rad_sec", true);
    imu_r_node = fgGetNode("/sensors/imu/r-rad_sec", true);
    imu_ax_node = fgGetNode("/sensors/imu/ax-mps_sec", true);
    imu_ay_node = fgGetNode("/sensors/imu/ay-mps_sec", true);
    imu_az_node = fgGetNode("/sensors/imu/az-mps_sec", true);
    imu_hx_node = fgGetNode("/sensors/imu/hx", true);
    imu_hy_node = fgGetNode("/sensors/imu/hy", true);
    imu_hz_node = fgGetNode("/sensors/imu/hz", true);

    // airdata airspeed (unfiltered)
    airdata_airspeed_node = fgGetNode("/sensors/airdata/airspeed-kt", true);
    est_wind_speed_kt = fgGetNode("/filters/wind-est/wind-speed-kt", true);
    est_wind_dir_deg = fgGetNode("/filters/wind-est/wind-dir-deg", true);
    est_wind_east_mps = fgGetNode("/filters/wind-est/wind-east-mps", true);
    est_wind_north_mps = fgGetNode("/filters/wind-est/wind-north-mps", true);
    true_airspeed_kt = fgGetNode("/filters/wind-est/true-airspeed-kt", true);
    true_heading_deg = fgGetNode("/filters/wind-est/true-heading-deg", true);
    true_air_east_mps = fgGetNode("/filters/wind-est/true-airspeed-east-mps", true);
    true_air_north_mps = fgGetNode("/filters/wind-est/true-airspeed-north-mps", true);
    est_pitot_scale_factor = fgGetNode("/filters/wind-est/pitot-scale-factor", true);
    est_pitot_scale_factor->setFloatValue( 1.0 ); // initialize to 1.0

    // initialize comm nodes
    filter_console_skip = fgGetNode("/config/remote-link/filter-skip", true);
    filter_logging_skip = fgGetNode("/config/logging/filter-skip", true);

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/filters", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "filter" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    string basename = "/filters/";
	    basename += section->getDisplayName();
	    printf("i = %d  name = %s module = %s %s\n",
	    	   i, name.c_str(), module.c_str(), basename.c_str());
	    
	    if ( module == "curt" ) {
		curt_adns_init( basename );
	    } else if ( module == "umn-euler" ) {
		umngnss_euler_init( basename, section );
	    } else if ( module == "umn-quat" ) {
		umngnss_quat_init( basename, section );
	    } else {
		printf("Unknown filter = '%s' in config file\n",
		       module.c_str());
	    }
	}
    }

    // initialize output property nodes (after module initialization
    // so we know that the reference properties will exist
    filter_timestamp_node = fgGetNode("/filters/time-stamp", true);
    filter_theta_node = fgGetNode("/orientation/pitch-deg", true);
    filter_phi_node = fgGetNode("/orientation/roll-deg", true);
    filter_psi_node = fgGetNode("/orientation/heading-deg", true);
    filter_lat_node = fgGetNode("/position/latitude-deg", true);
    filter_lon_node = fgGetNode("/position/longitude-deg", true);
    filter_alt_m_node = fgGetNode("/position/filter/altitude-m", true);
    filter_alt_ft_node = fgGetNode("/position/filter/altitude-ft", true);
    filter_vn_node = fgGetNode("/velocity/vn-ms", true);
    filter_ve_node = fgGetNode("/velocity/ve-ms", true);
    filter_vd_node = fgGetNode("/velocity/vd-ms", true);
    filter_status_node = fgGetNode("/health/navigation", true);

    filter_phi_dot_node = fgGetNode("/orientation/phi-dot-rad_sec", true);
    filter_the_dot_node = fgGetNode("/orientation/the-dot-rad_sec", true);
    filter_psi_dot_node = fgGetNode("/orientation/psi-dot-rad_sec", true);

    filter_track_node = fgGetNode("/orientation/groundtrack-deg", true);
    filter_vel_node = fgGetNode("/velocity/groundspeed-ms", true);
    filter_vert_speed_fps_node
	= fgGetNode("/velocity/vertical-speed-fps", true);
    filter_ground_alt_m_node
	= fgGetNode("/position/filter/altitude-ground-m", true);
    filter_alt_agl_m_node
	= fgGetNode("/position/filter/altitude-agl-m", true);
    filter_alt_agl_ft_node
	= fgGetNode("/position/filter/altitude-agl-ft", true);

    if ( toplevel->nChildren() > 0 ) {
	filter_timestamp_node->alias("/filters/filter[0]/time-stamp");
	filter_theta_node->alias("/filters/filter[0]/pitch-deg");
	filter_phi_node->alias("/filters/filter[0]/roll-deg");
	filter_psi_node->alias("/filters/filter[0]/heading-deg");
	filter_lat_node->alias("/filters/filter[0]/latitude-deg");
	filter_lon_node->alias("/filters/filter[0]/longitude-deg");
	filter_alt_m_node->alias("/filters/filter[0]/altitude-m");
	filter_vn_node->alias("/filters/filter[0]/vn-ms");
	filter_ve_node->alias("/filters/filter[0]/ve-ms");
	filter_vd_node->alias("/filters/filter[0]/vd-ms");
	filter_status_node->alias("/filters/filter[0]/navigation");

	filter_alt_ft_node->alias("/filters/filter[0]/altitude-ft");
	filter_track_node->alias("/filters/filter[0]/groundtrack-deg");
	filter_vel_node->alias("/filters/filter[0]/groundspeed-ms");
	filter_vert_speed_fps_node->alias("/filters/filter[0]/vertical-speed-fps");
    }

    // initialize altitude output nodes
    official_alt_m_node = fgGetNode("/position/altitude-m", true);
    official_alt_ft_node = fgGetNode("/position/altitude-ft", true);
    official_agl_m_node = fgGetNode("/position/altitude-agl-m", true);
    official_agl_ft_node = fgGetNode("/position/altitude-agl-ft", true);
    official_ground_m_node = fgGetNode("/position/altitude-ground-m", true);

    // select official source (currently AGL is pressure based,
    // absolute ground alt is based on average gps/filter value at
    // startup, and MSL altitude is based on pressure altitude -
    // pressure error (pressure error computed as average difference
    // between gps altitude and pressure altitude over time)):
    //
    // 1. /position/pressure
    // 2. /position/filter
    // 3. /position/combined
    official_alt_m_node->alias("/position/combined/altitude-true-m");
    official_alt_ft_node->alias("/position/combined/altitude-true-ft");
    official_agl_m_node->alias("/position/pressure/altitude-agl-m");
    official_agl_ft_node->alias("/position/pressure/altitude-agl-ft");
    official_ground_m_node->alias("/position/filter/altitude-ground-m");    
}


static void update_euler_rates() {
    double phi = filter_phi_node->getDoubleValue() * SGD_DEGREES_TO_RADIANS;
    double the = filter_theta_node->getDoubleValue() * SGD_DEGREES_TO_RADIANS;
    /*double psi = filter_psi_node->getDoubleValue() * SGD_DEGREES_TO_RADIANS;*/

    // direct computation of euler rates given body rates and estimated
    // attitude (based on googled references):
    // http://www.princeton.edu/~stengel/MAE331Lecture9.pdf
    // http://www.mathworks.com/help/aeroblks/customvariablemass6dofeulerangles.html

    double p = imu_p_node->getDoubleValue();
    double q = imu_q_node->getDoubleValue();
    double r = imu_r_node->getDoubleValue();

    if ( SGD_PI_2 - fabs(the) > 0.00001 ) {
	double phi_dot = p + q * sin(phi) * tan(the) + r * cos(phi) * tan(the);
	double the_dot = q * cos(phi) - r * sin(phi);
	double psi_dot = q * sin(phi) / cos(the) + r * cos(phi) / cos(the);
	filter_phi_dot_node->setDoubleValue(phi_dot);
	filter_the_dot_node->setDoubleValue(the_dot);
	filter_psi_dot_node->setDoubleValue(psi_dot);
	/* printf("dt=%.3f q=%.3f q(ned)=%.3f phi(dot)=%.3f\n",
	   dt,imu_q_node->getDoubleValue(), dq/dt, phi_dot);  */
	/* printf("%.3f %.3f %.3f %.3f\n",
	   cur_time,imu_q_node->getDoubleValue(), dq/dt, the_dot); */
   }
}


static void update_ground() {
    static double last_time = 0.0;

    double cur_time = filter_timestamp_node->getDoubleValue();
    static double start_time = cur_time;
    double elapsed_time = cur_time - start_time;

    double dt = cur_time - last_time;
    if ( dt > 1.0 ) {
	dt = 1.0;		// keep dt smallish
    }

    // determine ground reference altitude.  Average filter altitude
    // over first 30 seconds the filter becomes active.
    static float ground_alt_filter = filter_alt_m_node->getFloatValue();

    if ( elapsed_time >= dt && elapsed_time >= 0.001 && elapsed_time <= 30.0 ) {
	ground_alt_filter
	    = ((elapsed_time - dt) * ground_alt_filter
	       + dt * filter_alt_m_node->getFloatValue())
	    / elapsed_time;
	filter_ground_alt_m_node->setDoubleValue( ground_alt_filter );
    }

    float agl_m = filter_alt_m_node->getFloatValue() - ground_alt_filter;
    filter_alt_agl_m_node->setDoubleValue( agl_m );
    filter_alt_agl_ft_node->setDoubleValue( agl_m * SG_METER_TO_FEET );

    last_time = cur_time;
}


// onboard wind estimate (requires airspeed, true heading, and ground
// velocity fector)
static void update_wind() {
    // Estimate wind direction and speed based on ground track speed
    // versus aircraft heading and indicated airspeed.
    static double pitot_scale_filt = 1.0;

    double airspeed_kt = airdata_airspeed_node->getDoubleValue();
    if ( airspeed_kt < 15.0 ) {
	// indicated airspeed < 15 kts (hopefully) indicating we are
	// not flying and thus the assumptions the following code is
	// based on do not yet apply so we should exit now.  We are
	// assuming that we won't see > 15 kts sitting still on the
	// ground and that our stall speed is above 15 kts.  Is there
	// a more reliable way to determine if we are "flying"
	// vs. "not flying"?

	return;
    }

    double psi = SGD_PI_2
	- filter_psi_node->getDoubleValue() * SG_DEGREES_TO_RADIANS;
    double ue = cos(psi) * (airspeed_kt * pitot_scale_filt * SG_KT_TO_MPS);
    double un = sin(psi) * (airspeed_kt * pitot_scale_filt * SG_KT_TO_MPS);
    double we = ue - filter_ve_node->getDoubleValue();
    double wn = un - filter_vn_node->getDoubleValue();

    static double filt_we = 0.0, filt_wn = 0.0;
    filt_we = 0.9998 * filt_we + 0.0002 * we;
    filt_wn = 0.9998 * filt_wn + 0.0002 * wn;

    double wind_deg = 90 - atan2( filt_wn, filt_we ) * SGD_RADIANS_TO_DEGREES;
    if ( wind_deg < 0 ) { wind_deg += 360.0; }
    double wind_speed_kt = sqrt( filt_we*filt_we + filt_wn*filt_wn ) * SG_MPS_TO_KT;

    est_wind_speed_kt->setDoubleValue( wind_speed_kt );
    est_wind_dir_deg->setDoubleValue( wind_deg );
    est_wind_east_mps->setDoubleValue( filt_we );
    est_wind_north_mps->setDoubleValue( filt_wn );

    // estimate pitot tube bias
    double true_e = filt_we + filter_ve_node->getDoubleValue();
    double true_n = filt_wn + filter_vn_node->getDoubleValue();

    double true_deg = 90 - atan2( true_n, true_e ) * SGD_RADIANS_TO_DEGREES;
    if ( true_deg < 0 ) { true_deg += 360.0; }
    double true_speed_kt = sqrt( true_e*true_e + true_n*true_n ) * SG_MPS_TO_KT;

    true_airspeed_kt->setDoubleValue( true_speed_kt );
    true_heading_deg->setDoubleValue( true_deg );
    true_air_east_mps->setDoubleValue( true_e );
    true_air_north_mps->setDoubleValue( true_n );

    double pitot_scale = 1.0;
    if ( airspeed_kt > 1.0 ) {
	pitot_scale = true_speed_kt / airspeed_kt;
	// don't let the scale factor exceed some reasonable limits
	if ( pitot_scale < 0.75 ) { pitot_scale = 0.75;	}
	if ( pitot_scale > 1.25 ) { pitot_scale = 1.25; }
    }

    pitot_scale_filt = 0.9995 * pitot_scale_filt + 0.0005 * pitot_scale;
    est_pitot_scale_factor->setDoubleValue( pitot_scale_filt );

    // if ( display_on ) {
    //   printf("true: %.2f kt  %.1f deg (scale = %.4f)\n", true_speed_kt, true_deg, pitot_scale_filt);
    // }
}


bool Filter_update() {
    filter_prof.start();

    double imu_time = imu_timestamp_node->getDoubleValue();
    double imu_dt = imu_time - last_imu_time;
    bool fresh_filter_data = false;

    // sanity check (i.e. if system clock was changed by another process)
    if ( imu_dt > 1.0 ) { imu_dt = 0.01; }
    if ( imu_dt < 0.0 ) { imu_dt = 0.01; }

    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/filters", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "filter" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    if ( module == "null" ) {
		// do nothing
	    } else if ( module == "curt" ) {
		fresh_filter_data = curt_adns_update( imu_dt );
	    } else if ( module == "umn-euler" ) {
		fresh_filter_data = umngnss_euler_update();
	    } else if ( module == "umn-quat" ) {
		fresh_filter_data = umngnss_quat_update();
	    }
	}
    }

    filter_prof.stop();

    if ( fresh_filter_data ) {
	update_euler_rates();
	update_ground();
	update_wind();
    }
	     
    if ( remote_link_on || log_to_file ) {
	uint8_t buf[256];
	int size = packetizer->packetize_filter( buf );

	if ( remote_link_on ) {
	    // printf("sending filter packet\n");
	    remote_link_filter( buf, size,
				filter_console_skip->getIntValue() );
	}

	if ( log_to_file ) {
	    log_filter( buf, size, filter_logging_skip->getIntValue() );
	}
    }

    last_imu_time = imu_time;

#if 0
    static SGPropertyNode *tp = fgGetNode("/sensors/imu/pitch-truth-deg", true);
    static SGPropertyNode *ep = fgGetNode("/orientation/pitch-deg", true);
    printf("pitch error: %.2f (true = %.2f est = %.2f)\n", ep->getDoubleValue() - tp->getDoubleValue(), tp->getDoubleValue(), ep->getDoubleValue());
#endif

    return fresh_filter_data;
}


void Filter_close() {
    // traverse configured modules
    SGPropertyNode *toplevel = fgGetNode("/config/filters", true);
    for ( int i = 0; i < toplevel->nChildren(); ++i ) {
	SGPropertyNode *section = toplevel->getChild(i);
	string name = section->getName();
	if ( name == "filter" ) {
	    string module = section->getChild("module", 0, true)->getStringValue();
	    bool enabled = section->getChild("enable", 0, true)->getBoolValue();
	    if ( !enabled ) {
		continue;
	    }
	    if ( module == "null" ) {
		// do nothing
	    } else if ( module == "umn-euler" ) {
		umngnss_euler_close();
	    } else if ( module == "umn-quat" ) {
		umngnss_quat_close();
	    }
	}
    }
}
