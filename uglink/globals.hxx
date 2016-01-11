#ifndef _UGLINK_GLOBALS_HXX
#define _UGLINK_GLOBALS_HXX

#include <stdint.h>

#include "python/pyprops.hxx"


struct gps {
    double timestamp;
    double lat, lon, alt;
    double ve, vn, vd;
    double gps_time;
    int satellites;
    int status;
};

struct imu {
    double timestamp;
    float p, q, r;		/* angular velocities    */
    float ax, ay, az;		/* acceleration          */
    float hx, hy, hz;             /* magnetic field     	 */
    float temp;
    int status;
};

struct airdata {
    double timestamp;
    float pressure;		// mbar
    float temperature;		// deg C
    float airspeed;		// knots
    float altitude;		// meters
    float altitude_true;	// meters (corrected by filtered difference with gps)
    float climb_fpm;		// feet per minute
    float acceleration;		// knots per second
    float wind_dir;             // degrees (0-360)
    float wind_speed;           // knots
    float pitot_scale;          // multiplication factor
    int status;
};

struct filter {
    double timestamp;
    double phi, theta, psi;
    double lat, lon, alt;
    double ve, vn, vd;
    int status;
    int command_seq;
};

struct actuator {
    double timestamp;
    float ail;
    float ele;
    float thr;
    float rud;
    float ch5;
    float ch6;
    float ch7;
    float ch8;
    int status;
};

struct pilot {
    double timestamp;
    float ail;
    float ele;
    float thr;
    float rud;
    float ch5;
    float ch6;
    float ch7;
    float ch8;
    int status;
};

struct apstatus {
    double timestamp;
    float target_heading_deg;
    float target_roll_deg;
    float target_altitude_msl_ft;
    float target_climb_fps;
    float target_pitch_deg;
    float target_theta_dot;
    float target_speed_kt;
    int target_wp;
    double wp_lon;
    double wp_lat;
    int wp_index;
    int route_size;
};

struct health {
    double timestamp;
    float load_avg;		/* system "1 minute" load average */
    float avionics_vcc;		/* input vcc */
    float extern_volts;
    float extern_cell_volts;
    float extern_amps;
    float extern_mah;
};

// starts small, but this will likely expand as new payload packages
// are developed and tighting integrated into the system.
struct payload {
    double timestamp;
    int trigger_num;
};


#endif // _UGLINK_GLOBALS_HXX
