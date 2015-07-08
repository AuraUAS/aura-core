//
// FILE: APM2.cxx
// DESCRIPTION: interact with APM2 with "sensor head" firmware
//

#include <errno.h>		// errno
#include <fcntl.h>		// open()
#include <stdio.h>		// printf() et. al.
#include <termios.h>		// tcgetattr() et. al.
#include <unistd.h>		// tcgetattr() et. al.
#include <string.h>		// memset(), strerror()

#include "comms/display.h"
#include "comms/logging.h"
#include "init/globals.hxx"
#include "props/props.hxx"
#include "sensors/calibrate.hxx"
#include "util/timing.h"

#include "APM2.hxx"

#define START_OF_MSG0 147
#define START_OF_MSG1 224

#define ACK_PACKET_ID 20

#define PWM_RATE_PACKET_ID 21
#define BAUD_PACKET_ID 22
#define FLIGHT_COMMAND_PACKET_ID 23
#define ACT_GAIN_PACKET_ID 24
#define MIX_MODE_PACKET_ID 25
#define SAS_MODE_PACKET_ID 26
#define SERIAL_NUMBER_PACKET_ID 27
#define WRITE_EEPROM_PACKET_ID 28

#define PILOT_PACKET_ID 50
#define IMU_PACKET_ID 51
#define GPS_PACKET_ID 52
#define BARO_PACKET_ID 53
#define ANALOG_PACKET_ID 54

#define ACT_COMMAND_PACKET_ID 60

#define NUM_PILOT_INPUTS 8
#define NUM_ACTUATORS 8
#define NUM_IMU_SENSORS 7
#define NUM_ANALOG_INPUTS 6

#define PWM_CENTER 1520
#define PWM_HALF_RANGE 413
#define PWM_RANGE (PWM_HALF_RANGE * 2)
#define PWM_MIN (PWM_CENTER - PWM_HALF_RANGE)
#define PWM_MAX (PWM_CENTER + PWM_HALF_RANGE)

// Actuator gain (reversing) commands, format is cmd(byte) ch(byte) gain(float)
#define ACT_GAIN_DEFAULTS 0
#define ACT_GAIN_SET 1

// Mix mode commands (format is cmd(byte), gain 1 (float), gain 2 (float)
#define MIX_DEFAULTS 0
#define MIX_AUTOCOORDINATE 1
#define MIX_THROTTLE_TRIM 2
#define MIX_FLAP_TRIM 3
#define MIX_ELEVONS 4
#define MIX_FLAPERONS 5
#define MIX_VTAIL 6
#define MIX_DIFF_THRUST 7

// SAS mode commands (format is cmd(byte), gain)
#define SAS_DEFAULTS 0
#define SAS_ROLLAXIS 1
#define SAS_PITCHAXIS 2
#define SAS_YAWAXIS 3
#define SAS_CH7_TUNE 10

// APM2 interface and config property nodes
static SGPropertyNode *configroot = NULL;
static SGPropertyNode *APM2_device_node = NULL;
static SGPropertyNode *APM2_baud_node = NULL;
static SGPropertyNode *APM2_volt_ratio_node = NULL;
static SGPropertyNode *APM2_battery_cells_node = NULL;
static SGPropertyNode *APM2_amp_offset_node = NULL;
static SGPropertyNode *APM2_amp_ratio_node = NULL;
static SGPropertyNode *APM2_analog_nodes[NUM_ANALOG_INPUTS];
static SGPropertyNode *APM2_pitot_calibrate_node = NULL;
static SGPropertyNode *APM2_extern_volt_node = NULL;
static SGPropertyNode *APM2_extern_cell_volt_node = NULL;
static SGPropertyNode *APM2_extern_amp_node = NULL;
static SGPropertyNode *APM2_extern_amp_sum_node = NULL;
static SGPropertyNode *APM2_board_vcc_node = NULL;
static SGPropertyNode *APM2_pilot_packet_count_node = NULL;
static SGPropertyNode *APM2_imu_packet_count_node = NULL;
static SGPropertyNode *APM2_gps_packet_count_node = NULL;
static SGPropertyNode *APM2_baro_packet_count_node = NULL;
static SGPropertyNode *APM2_analog_packet_count_node = NULL;

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
static SGPropertyNode *imu_temp_node = NULL;
//static SGPropertyNode *imu_p_bias_node = NULL;
//static SGPropertyNode *imu_q_bias_node = NULL;
//static SGPropertyNode *imu_r_bias_node = NULL;
static SGPropertyNode *imu_ax_bias_node = NULL;
static SGPropertyNode *imu_ay_bias_node = NULL;
static SGPropertyNode *imu_az_bias_node = NULL;

// gps property nodes
static SGPropertyNode *gps_timestamp_node = NULL;
static SGPropertyNode *gps_day_secs_node = NULL;
static SGPropertyNode *gps_date_node = NULL;
static SGPropertyNode *gps_lat_node = NULL;
static SGPropertyNode *gps_lon_node = NULL;
static SGPropertyNode *gps_alt_node = NULL;
static SGPropertyNode *gps_ve_node = NULL;
static SGPropertyNode *gps_vn_node = NULL;
static SGPropertyNode *gps_vd_node = NULL;
static SGPropertyNode *gps_unix_sec_node = NULL;
static SGPropertyNode *gps_satellites_node = NULL;
static SGPropertyNode *gps_status_node = NULL;

// pilot input property nodes
static SGPropertyNode *pilot_timestamp_node = NULL;
static SGPropertyNode *pilot_aileron_node = NULL;
static SGPropertyNode *pilot_elevator_node = NULL;
static SGPropertyNode *pilot_throttle_node = NULL;
static SGPropertyNode *pilot_rudder_node = NULL;
static SGPropertyNode *pilot_channel5_node = NULL;
static SGPropertyNode *pilot_channel6_node = NULL;
static SGPropertyNode *pilot_channel7_node = NULL;
static SGPropertyNode *pilot_channel8_node = NULL;
static SGPropertyNode *pilot_manual_node = NULL;
static SGPropertyNode *pilot_status_node = NULL;

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
static SGPropertyNode *act_status_node = NULL;

// air data nodes
static SGPropertyNode *airdata_timestamp_node = NULL;
static SGPropertyNode *airdata_pressure_node = NULL;
static SGPropertyNode *airdata_temperature_node = NULL;
static SGPropertyNode *airdata_climb_rate_mps_node = NULL;
static SGPropertyNode *airdata_climb_rate_fps_node = NULL;
static SGPropertyNode *airdata_airspeed_mps_node = NULL;
static SGPropertyNode *airdata_airspeed_kt_node = NULL;

static bool master_opened = false;
static bool imu_inited = false;
static bool gps_inited = false;
static bool airdata_inited = false;
static bool pilot_input_inited = false;
static bool actuator_inited = false;

static int fd = -1;
static string device_name = "/dev/ttyS0";
static int baud = 230400;
static int act_pwm_rate_hz = 50;
static float volt_div_ratio = 100; // a nonsense value
static int battery_cells = 4;
static float extern_amp_offset = 0.0;
static float extern_amp_ratio = 0.1; // a nonsense value
static float extern_amp_sum = 0.0;
static float pitot_calibrate = 1.0;
static bool reverse_imu_mount = false;

static SGPropertyNode *act_config = NULL;
static int last_ack_id = 0;
static int last_ack_subid = 0;
//static bool ack_baud_rate = false;

static uint16_t act_rates[NUM_ACTUATORS] = { 50, 50, 50, 50, 50, 50, 50, 50 };

static double pilot_in_timestamp = 0.0;
static uint16_t pilot_input[NUM_PILOT_INPUTS]; // internal stash

static double imu_timestamp = 0.0;
static int16_t imu_sensors[NUM_IMU_SENSORS];

struct gps_sensors_t {
    double timestamp;
    uint32_t time;
    uint32_t date;
    int32_t latitude;
    int32_t longitude;
    int32_t altitude;
    uint16_t ground_speed;
    uint16_t ground_course;
    // int32_t speed_3d;
    int16_t hdop;
    uint8_t num_sats;
    uint8_t status;
} gps_sensors;

struct air_data_t {
    double timestamp;
    float pressure;
    float temp;
    float climb_rate;
    float airspeed;
} airdata;

static float analog[NUM_ANALOG_INPUTS];     // internal stash

static bool airspeed_inited = false;
static double airspeed_zero_start_time = 0.0;

//static UGCalibrate p_cal;
//static UGCalibrate q_cal;
//static UGCalibrate r_cal;
static UGCalibrate ax_cal;
static UGCalibrate ay_cal;
static UGCalibrate az_cal;

static uint32_t pilot_packet_counter = 0;
static uint32_t imu_packet_counter = 0;
static uint32_t gps_packet_counter = 0;
static uint32_t baro_packet_counter = 0;
static uint32_t analog_packet_counter = 0;


// (Deprecated) initialize input property nodes
static void bind_input( SGPropertyNode *config ) {
    configroot = config;
}


static void APM2_cksum( uint8_t hdr1, uint8_t hdr2, uint8_t *buf, uint8_t size, uint8_t *cksum0, uint8_t *cksum1 )
{
    uint8_t c0 = 0;
    uint8_t c1 = 0;

    c0 += hdr1;
    c1 += c0;

    c0 += hdr2;
    c1 += c0;

    for ( uint8_t i = 0; i < size; i++ ) {
        c0 += (uint8_t)buf[i];
        c1 += c0;
    }

    *cksum0 = c0;
    *cksum1 = c1;
}


#if 0
bool APM2_request_baud( uint32_t baud ) {
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 4;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = BAUD_PACKET_ID;
    // packet length (1 byte)
    buf[1] = size;
    len = write( fd, buf, 2 );

    // actuator data
    *(uint32_t *)buf = baud;
  
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( BAUD_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}
#endif

static bool APM2_act_write_eeprom() {
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = WRITE_EEPROM_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 0;
    len = write( fd, buf, 2 );

    // check sum (2 bytes)
    APM2_cksum( WRITE_EEPROM_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


static bool APM2_act_set_serial_number( uint16_t serial_number ) {
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = SERIAL_NUMBER_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 2;
    len = write( fd, buf, 2 );

    // actuator data
    uint8_t hi = serial_number / 256;
    uint8_t lo = serial_number - (hi * 256);
    buf[size++] = lo;
    buf[size++] = hi;
  
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( SERIAL_NUMBER_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


static bool APM2_act_set_pwm_rates( uint16_t rates[NUM_ACTUATORS] ) {
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = PWM_RATE_PACKET_ID;
    // packet length (1 byte)
    buf[1] = NUM_ACTUATORS * 2;
    len = write( fd, buf, 2 );

    // actuator data
    for ( int i = 0; i < NUM_ACTUATORS; i++ ) {
	uint16_t val = rates[i];
	uint8_t hi = val / 256;
	uint8_t lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;
    }
  
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( PWM_RATE_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


static bool APM2_act_gain_mode( int channel, float gain)
{
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = ACT_GAIN_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 3;
    len = write( fd, buf, 2 );

    buf[size++] = (uint8_t)channel;

    uint16_t val;
    uint8_t hi, lo;
    
    // gain
    val = 32767 + gain * 10000;
    hi = val / 256;
    lo = val - (hi * 256);
    buf[size++] = lo;
    buf[size++] = hi;
    
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( ACT_GAIN_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


static bool APM2_act_mix_mode( int mode_id, bool enable,
			       float gain1, float gain2)
{
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = MIX_MODE_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 6;
    len = write( fd, buf, 2 );

    buf[size++] = mode_id;
    buf[size++] = enable;

    uint16_t val;
    uint8_t hi, lo;
    
    // gain1
    val = 32767 + gain1 * 10000;
    hi = val / 256;
    lo = val - (hi * 256);
    buf[size++] = lo;
    buf[size++] = hi;
    
    // gain2
    val = 32767 + gain2 * 10000;
    hi = val / 256;
    lo = val - (hi * 256);
    buf[size++] = lo;
    buf[size++] = hi;
    
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( MIX_MODE_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


static bool APM2_act_sas_mode( int mode_id, bool enable, float gain)
{
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = SAS_MODE_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 4;
    len = write( fd, buf, 2 );

    buf[size++] = mode_id;
    buf[size++] = enable;

    uint16_t val;
    uint8_t hi, lo;
    
    // gain
    val = 32767 + gain * 10000;
    hi = val / 256;
    lo = val - (hi * 256);
    buf[size++] = lo;
    buf[size++] = hi;
    
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( SAS_MODE_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


// initialize imu output property nodes 
static void bind_imu_output( string rootname ) {
    if ( imu_inited ) {
	return;
    }

    SGPropertyNode *outputroot = fgGetNode( rootname.c_str(), true );

    imu_timestamp_node = outputroot->getChild("time-stamp", 0, true);
    imu_p_node = outputroot->getChild("p-rad_sec", 0, true);
    imu_q_node = outputroot->getChild("q-rad_sec", 0, true);
    imu_r_node = outputroot->getChild("r-rad_sec", 0, true);
    imu_ax_node = outputroot->getChild("ax-mps_sec", 0, true);
    imu_ay_node = outputroot->getChild("ay-mps_sec", 0, true);
    imu_az_node = outputroot->getChild("az-mps_sec", 0, true);
    imu_hx_node = outputroot->getChild("hx", 0, true);
    imu_hy_node = outputroot->getChild("hy", 0, true);
    imu_hz_node = outputroot->getChild("hz", 0, true);
    imu_temp_node = outputroot->getChild("temp_C", 0, true);
    //imu_p_bias_node = outputroot->getChild("p-bias", 0, true);
    //imu_q_bias_node = outputroot->getChild("q-bias", 0, true);
    //imu_r_bias_node = outputroot->getChild("r-bias", 0, true);
    imu_ax_bias_node = outputroot->getChild("ax-bias", 0, true);
    imu_ay_bias_node = outputroot->getChild("ay-bias", 0, true);
    imu_az_bias_node = outputroot->getChild("az-bias", 0, true);

    imu_inited = true;
}


// initialize gps output property nodes 
static void bind_gps_output( string rootname ) {
    if ( gps_inited ) {
	return;
    }

    SGPropertyNode *outputroot = fgGetNode( rootname.c_str(), true );
    gps_timestamp_node = outputroot->getChild("time-stamp", 0, true);
    gps_day_secs_node = outputroot->getChild("day-seconds", 0, true);
    gps_date_node = outputroot->getChild("date", 0, true);
    gps_lat_node = outputroot->getChild("latitude-deg", 0, true);
    gps_lon_node = outputroot->getChild("longitude-deg", 0, true);
    gps_alt_node = outputroot->getChild("altitude-m", 0, true);
    gps_ve_node = outputroot->getChild("ve-ms", 0, true);
    gps_vn_node = outputroot->getChild("vn-ms", 0, true);
    gps_vd_node = outputroot->getChild("vd-ms", 0, true);
    gps_satellites_node = outputroot->getChild("satellites", 0, true);
    gps_status_node = outputroot->getChild("status", 0, true);
    gps_unix_sec_node = outputroot->getChild("unix-time-sec", 0, true);

    gps_inited = true;
}


// initialize actuator property nodes 
static void bind_act_nodes() {
    if ( actuator_inited ) {
	return;
    }

    act_timestamp_node = fgGetNode("/actuators/actuator/time-stamp", true);
    act_aileron_node = fgGetNode("/actuators/actuator/channel", 0, true);
    act_elevator_node = fgGetNode("/actuators/actuator/channel", 1, true);
    act_throttle_node = fgGetNode("/actuators/actuator/channel", 2, true);
    act_rudder_node = fgGetNode("/actuators/actuator/channel", 3, true);
    act_channel5_node = fgGetNode("/actuators/actuator/channel", 4, true);
    act_channel6_node = fgGetNode("/actuators/actuator/channel", 5, true);
    act_channel7_node = fgGetNode("/actuators/actuator/channel", 6, true);
    act_channel8_node = fgGetNode("/actuators/actuator/channel", 7, true);
    act_status_node = fgGetNode("/actuators/actuator/status", true);

    actuator_inited = true;
}

// initialize airdata output property nodes 
static void bind_airdata_output( string rootname ) {
    if ( airdata_inited ) {
	return;
    }

    SGPropertyNode *outputroot = fgGetNode( rootname.c_str(), true );

    airdata_timestamp_node = outputroot->getChild("time-stamp", 0, true);
    airdata_pressure_node = outputroot->getChild("pressure-mbar", 0, true);
    airdata_temperature_node = outputroot->getChild("temp-degC", 0, true);
    airdata_climb_rate_mps_node
	= outputroot->getChild("vertical-speed-mps", 0, true);
    airdata_climb_rate_fps_node
	= outputroot->getChild("vertical-speed-fps", 0, true);
    airdata_airspeed_mps_node = outputroot->getChild("airspeed-mps", 0, true);
    airdata_airspeed_kt_node = outputroot->getChild("airspeed-kt", 0, true);

    airdata_inited = true;
}


// initialize airdata output property nodes 
static void bind_pilot_controls( string rootname ) {
    if ( pilot_input_inited ) {
	return;
    }

    pilot_timestamp_node = fgGetNode("/sensors/pilot/time-stamp", true);
    pilot_aileron_node = fgGetNode("/sensors/pilot/aileron", true);
    pilot_elevator_node = fgGetNode("/sensors/pilot/elevator", true);
    pilot_throttle_node = fgGetNode("/sensors/pilot/throttle", true);
    pilot_rudder_node = fgGetNode("/sensors/pilot/rudder", true);
    pilot_channel5_node = fgGetNode("/sensors/pilot/channel", 4, true);
    pilot_channel6_node = fgGetNode("/sensors/pilot/channel", 5, true);
    pilot_channel7_node = fgGetNode("/sensors/pilot/channel", 6, true);
    pilot_channel8_node = fgGetNode("/sensors/pilot/channel", 7, true);
    pilot_manual_node = fgGetNode("/sensors/pilot/manual", true);
    pilot_status_node = fgGetNode("/sensors/pilot/status", true);

    pilot_input_inited = true;
}


// send our configured init strings to configure gpsd the way we prefer
static bool APM2_open_device( int baud_bits ) {
    if ( display_on ) {
	printf("APM2 Sensor Head on %s @ %d(code) baud\n", device_name.c_str(),
	       baud_bits);
    }

    fd = open( device_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK );
    if ( fd < 0 ) {
        fprintf( stderr, "open serial: unable to open %s - %s\n",
                 device_name.c_str(), strerror(errno) );
	return false;
    }

    struct termios config;	// Old Serial Port Settings

    memset(&config, 0, sizeof(config));

    // Save Current Serial Port Settings
    // tcgetattr(fd,&oldTio); 

    // Configure New Serial Port Settings
    config.c_cflag     = baud_bits | // bps rate
                         CS8	 | // 8n1
                         CLOCAL	 | // local connection, no modem
                         CREAD;	   // enable receiving chars
    config.c_iflag     = IGNPAR;   // ignore parity bits
    config.c_oflag     = 0;
    config.c_lflag     = 0;
    config.c_cc[VTIME] = 0;
    config.c_cc[VMIN]  = 1;	   // block 'read' from returning until at
                                   // least 1 character is received

    // Flush Serial Port I/O buffer
    tcflush(fd, TCIOFLUSH);

    // Set New Serial Port Settings
    int ret = tcsetattr( fd, TCSANOW, &config );
    if ( ret > 0 ) {
        fprintf( stderr, "error configuring device: %s - %s\n",
                 device_name.c_str(), strerror(errno) );
	return false;
    }

    // Enable non-blocking IO (one more time for good measure)
    fcntl(fd, F_SETFL, O_NONBLOCK);

    return true;
}


// send our configured init strings to configure gpsd the way we prefer
static bool APM2_open() {
    if ( master_opened ) {
	return true;
    }

    APM2_device_node = fgGetNode("/config/sensors/APM2/device");
    if ( APM2_device_node != NULL ) {
	device_name = APM2_device_node->getStringValue();
    }
    APM2_baud_node = fgGetNode("/config/sensors/APM2/baud");
    if ( APM2_baud_node != NULL ) {
       baud = APM2_baud_node->getIntValue();
    }
    APM2_volt_ratio_node = fgGetNode("/config/sensors/APM2/volt-divider-ratio");
    if ( APM2_volt_ratio_node != NULL ) {
	volt_div_ratio = APM2_volt_ratio_node->getFloatValue();
    }
    APM2_battery_cells_node = fgGetNode("/config/sensors/APM2/battery-cells");
    if ( APM2_battery_cells_node != NULL ) {
	battery_cells = APM2_battery_cells_node->getFloatValue();
    }
    if ( battery_cells < 1 ) { battery_cells = 1; }
    APM2_amp_offset_node = fgGetNode("/config/sensors/APM2/external-amp-offset");
    if ( APM2_amp_offset_node != NULL ) {
	extern_amp_offset = APM2_amp_offset_node->getFloatValue();
    }
    APM2_amp_ratio_node = fgGetNode("/config/sensors/APM2/external-amp-ratio");
    if ( APM2_amp_ratio_node != NULL ) {
	extern_amp_ratio = APM2_amp_ratio_node->getFloatValue();
    }

    for ( int i = 0; i < NUM_ANALOG_INPUTS; i++ ) {
	APM2_analog_nodes[i]
	    = fgGetNode("/sensors/APM2/raw-analog/channel", i, true);
    }
    APM2_pitot_calibrate_node = fgGetNode("/config/sensors/APM2/pitot-calibrate-factor");
    if ( APM2_pitot_calibrate_node != NULL ) {
	pitot_calibrate = APM2_pitot_calibrate_node->getFloatValue();
    }
    APM2_extern_volt_node = fgGetNode("/sensors/APM2/extern-volt", true);
    APM2_extern_cell_volt_node = fgGetNode("/sensors/APM2/extern-cell-volt", true);
    APM2_extern_amp_node = fgGetNode("/sensors/APM2/extern-amps", true);
    APM2_extern_amp_sum_node = fgGetNode("/sensors/APM2/extern-current-mah", true);
    APM2_board_vcc_node = fgGetNode("/sensors/APM2/board-vcc", true);
    APM2_pilot_packet_count_node
	= fgGetNode("/sensors/APM2/pilot-packet-count", true);
    APM2_imu_packet_count_node
	= fgGetNode("/sensors/APM2/imu-packet-count", true);
    APM2_gps_packet_count_node
	= fgGetNode("/sensors/APM2/gps-packet-count", true);
    APM2_baro_packet_count_node
	= fgGetNode("/sensors/APM2/baro-packet-count", true);
    APM2_analog_packet_count_node
	= fgGetNode("/sensors/APM2/analog-packet-count", true);

    int baud_bits = B115200;
    if ( baud == 115200 ) {
	baud_bits = B115200;
    } else if ( baud == 230400 ) {
	baud_bits = B230400;
    } else if ( baud == 500000 ) {
	baud_bits = B500000;
     } else {
	printf("unsupported baud rate = %d\n", baud);
    }

    if ( ! APM2_open_device( baud_bits ) ) {
	printf("device open failed ...");
	return false;
    }

    sleep(1);
    
    master_opened = true;

    return true;
}


#if 0
bool APM2_init( SGPropertyNode *config ) {
    printf("APM2_init()\n");

    bind_input( config );

    bool result = APM2_open();

    return result;
}
#endif


bool APM2_imu_init( string rootname, SGPropertyNode *config ) {
    if ( ! APM2_open() ) {
	return false;
    }

    bind_imu_output( rootname );

    SGPropertyNode *rev = config->getChild("reverse-imu-mount");
    if ( rev != NULL ) {
	if ( rev->getBoolValue() ) {
	    reverse_imu_mount = true;
	}
    }
    
    SGPropertyNode *cal = config->getChild("calibration");
    if ( cal != NULL ) {
	double min_temp = 27.0;
	double max_temp = 27.0;
	SGPropertyNode *min_node = cal->getChild("min-temp-C");
	if ( min_node != NULL ) {
	    min_temp = min_node->getFloatValue();
	}
	SGPropertyNode *max_node = cal->getChild("max-temp-C");
	if ( max_node != NULL ) {
	    max_temp = max_node->getFloatValue();
	}
	
	//p_cal.init( cal->getChild("p"), min_temp, max_temp );
	//q_cal.init( cal->getChild("q"), min_temp, max_temp );
	//r_cal.init( cal->getChild("r"), min_temp, max_temp );
	ax_cal.init( cal->getChild("ax"), min_temp, max_temp );
	ay_cal.init( cal->getChild("ay"), min_temp, max_temp );
	az_cal.init( cal->getChild("az"), min_temp, max_temp );

	// save the imu calibration parameters with the data file so that
	// later the original raw sensor values can be derived.
	if ( log_to_file ) {
	    log_imu_calibration( cal );
	}
    }
    
    return true;
}


bool APM2_gps_init( string rootname, SGPropertyNode *config ) {
    if ( ! APM2_open() ) {
	return false;
    }

    bind_gps_output( rootname );

    return true;
}


bool APM2_airdata_init( string rootname ) {
    if ( ! APM2_open() ) {
	return false;
    }

    bind_airdata_output( rootname );

    return true;
}


bool APM2_pilot_init( string rootname ) {
    if ( ! APM2_open() ) {
	return false;
    }

    bind_pilot_controls( rootname );

    return true;
}


bool APM2_act_init( SGPropertyNode *config ) {
    if ( ! APM2_open() ) {
	return false;
    }

    act_config = config;
    bind_act_nodes();

    return true;
}


#if 0
// swap big/little endian bytes
static void my_swap( uint8_t *buf, int index, int count )
{
    int i;
    uint8_t tmp;
    for ( i = 0; i < count / 2; ++i ) {
        tmp = buf[index+i];
        buf[index+i] = buf[index+count-i-1];
        buf[index+count-i-1] = tmp;
    }
}
#endif


// convert a pwm pulse length to a normalize [-1 to 1] or [0 to 1] range
static float normalize_pulse( int pulse, bool symmetrical ) {
    float result = 0.0;

    if ( symmetrical ) {
	// i.e. aileron, rudder, elevator
	result = (pulse - PWM_CENTER) / (float)PWM_HALF_RANGE;
	if ( result < -1.0 ) { result = -1.0; }
	if ( result > 1.0 ) { result = 1.0; }
    } else {
	// i.e. throttle
	result = (pulse - PWM_MIN) / (float)PWM_RANGE;
	if ( result < 0.0 ) { result = 0.0; }
	if ( result > 1.0 ) { result = 1.0; }
    }

    return result;
}

static bool APM2_parse( uint8_t pkt_id, uint8_t pkt_len,
			uint8_t *payload )
{
    bool new_data = false;
    static float extern_volt_filt = 0.0;
    static float extern_amp_filt = 0.0;

    if ( pkt_id == ACK_PACKET_ID ) {
	if ( display_on ) {
	    printf("Received ACK = %d %d\n", payload[0], payload[1]);
	}
	if ( pkt_len == 2 ) {
	    last_ack_id = payload[0];
	    last_ack_subid = payload[1];
	} else {
	    printf("APM2: packet size mismatch in ACK\n");
	}
    } else if ( pkt_id == PILOT_PACKET_ID ) {
	if ( pkt_len == NUM_PILOT_INPUTS * 2 ) {
	    uint8_t lo, hi;

	    pilot_in_timestamp = get_Time();

	    for ( int i = 0; i < NUM_PILOT_INPUTS; i++ ) {
		lo = payload[0 + 2*i]; hi = payload[1 + 2*i];
		pilot_input[i] = hi*256 + lo;
	    }

#if 0
	    if ( display_on ) {
		printf("%5.2f %5.2f %4.2f %5.2f %d\n",
		       pilot_aileron_node->getDoubleValue(),
		       pilot_elevator_node->getDoubleValue(),
		       pilot_throttle_node->getDoubleValue(),
		       pilot_rudder_node->getDoubleValue(),
		       pilot_manual_node->getIntValue());
	    }
#endif

	    pilot_packet_counter++;
	    APM2_pilot_packet_count_node->setIntValue( pilot_packet_counter );

	    new_data = true;
	} else {
	    if ( display_on ) {
		printf("APM2: packet size mismatch in pilot input\n");
	    }
	}
    } else if ( pkt_id == IMU_PACKET_ID ) {
	if ( pkt_len == NUM_IMU_SENSORS * 2 ) {
	    uint8_t lo, hi;

	    imu_timestamp = get_Time();

	    for ( int i = 0; i < NUM_IMU_SENSORS; i++ ) {
		lo = payload[0 + 2*i]; hi = payload[1 + 2*i];
		imu_sensors[i] = hi*256 + lo;
	    }

#if 0
	    if ( display_on ) {
		for ( int i = 0; i < NUM_IMU_SENSORS; i++ ) {
		    printf("%d ", imu_sensors[i]);
		}
		printf("\n");
	    }
#endif
		      
	    imu_packet_counter++;
	    APM2_imu_packet_count_node->setIntValue( imu_packet_counter );

	    new_data = true;
	} else {
	    if ( display_on ) {
		printf("APM2: packet size mismatch in imu input\n");
	    }
	}
    } else if ( pkt_id == GPS_PACKET_ID ) {
	if ( pkt_len == 28 ) {
	    gps_sensors.timestamp = get_Time();
	    gps_sensors.time = *(uint32_t *)payload; payload += 4;
	    gps_sensors.date = *(uint32_t *)payload; payload += 4;
	    gps_sensors.latitude = *(int32_t *)payload; payload += 4;
	    gps_sensors.longitude = *(int32_t *)payload; payload += 4;
	    gps_sensors.altitude = *(int32_t *)payload; payload += 4;
	    gps_sensors.ground_speed = *(uint16_t *)payload; payload += 2;
	    gps_sensors.ground_course = *(uint16_t *)payload; payload += 2;
	    // gps_sensors.speed_3d = *(int32_t *)payload; payload += 4;
	    gps_sensors.hdop = *(int16_t *)payload; payload += 2;
	    gps_sensors.num_sats = *(uint8_t *)payload; payload += 1;
	    gps_sensors.status = *(uint8_t *)payload; payload += 1;

#if 0
	    if ( display_on ) {
		for ( int i = 0; i < NUM_IMU_SENSORS; i++ ) {
		    printf("%d ", imu_sensors[i]);
		}
		printf("\n");
	    }
#endif
		      
	    gps_packet_counter++;
	    APM2_gps_packet_count_node->setIntValue( gps_packet_counter );

	    new_data = true;
	} else {
	    if ( display_on ) {
		printf("APM2: packet size mismatch in gps input\n");
	    }
	}
    } else if ( pkt_id == BARO_PACKET_ID ) {
	if ( pkt_len == 12 ) {
	    airdata.timestamp = get_Time();
	    airdata.pressure = *(float *)payload; payload += 4;
	    airdata.temp = *(float *)payload; payload += 4;
	    airdata.climb_rate = *(float *)payload; payload += 4;

#if 0
	    if ( display_on ) {
		printf("baro %.3f %.1f %.1f %.1f\n", airdata.timestamp,
			airdata.pressure, airdata.temp, airdata.climb_rate);
	    }
#endif
		      
	    baro_packet_counter++;
	    APM2_baro_packet_count_node->setIntValue( baro_packet_counter );

	    new_data = true;
	} else {
	    if ( display_on ) {
		printf("APM2: packet size mismatch in barometer input\n");
	    }
	}
    } else if ( pkt_id == ANALOG_PACKET_ID ) {
	if ( pkt_len == 2 * NUM_ANALOG_INPUTS ) {
	    uint8_t lo, hi;
	    for ( int i = 0; i < NUM_ANALOG_INPUTS; i++ ) {
		lo = payload[0 + 2*i]; hi = payload[1 + 2*i];
		float val = (float)(hi*256 + lo);
		if ( i != 5 ) {
		    // tranmitted value is left shifted 6 bits (*64)
		    analog[i] = val / 64.0;
		} else {
		    // special case APM2 specific sensor values, write to
		    // property tree here
		    analog[i] = val / 1000.0;
		}
		APM2_analog_nodes[i]->setFloatValue( analog[i] );
	    }

	    // fill in property values that don't belong to some other
	    // sub system right now.
	    double analog_timestamp = get_Time();
	    static double last_analog_timestamp = analog_timestamp;
	    double dt = analog_timestamp - last_analog_timestamp;
	    last_analog_timestamp = analog_timestamp;

	    static float filter_vcc = analog[5];
	    filter_vcc = 0.9999 * filter_vcc + 0.0001 * analog[5];
	    APM2_board_vcc_node->setDoubleValue( filter_vcc );

	    float extern_volts = analog[1] * (filter_vcc/1024.0) * volt_div_ratio;
	    extern_volt_filt = 0.995 * extern_volt_filt + 0.005 * extern_volts;
	    float cell_volt = extern_volt_filt / (float)battery_cells;
	    float extern_amps = ((analog[2] * (filter_vcc/1024.0)) - extern_amp_offset) * extern_amp_ratio;
	    extern_amp_filt = 0.99 * extern_amp_filt + 0.01 * extern_amps;
	    /*printf("a[2]=%.1f vcc=%.2f ratio=%.2f amps=%.2f\n",
		analog[2], filter_vcc, extern_amp_ratio, extern_amps); */
	    extern_amp_sum += extern_amps * dt * 0.277777778; // 0.2777... is 1000/3600 (conversion to milli-amp hours)

	    APM2_extern_volt_node->setFloatValue( extern_volt_filt );
	    APM2_extern_cell_volt_node->setFloatValue( cell_volt );
	    APM2_extern_amp_node->setFloatValue( extern_amp_filt );
	    APM2_extern_amp_sum_node->setFloatValue( extern_amp_sum );

#if 0
	    if ( display_on ) {
		for ( int i = 0; i < NUM_ANALOG_INPUTS; i++ ) {
		    printf("%.2f ", (float)analog[i] / 64.0);
		}
		printf("\n");
	    }
#endif
		      
	    analog_packet_counter++;
	    APM2_analog_packet_count_node->setIntValue( analog_packet_counter );

	    new_data = true;
	} else {
	    if ( display_on ) {
		printf("APM2: packet size mismatch in analog input\n");
	    }
	}
    }

    return new_data;
}


#if 0
static void APM2_read_tmp() {
    int len;
    uint8_t input[16];
    len = read( fd, input, 1 );
    while ( len > 0 ) {
	printf("%c", input[0]);
	len = read( fd, input, 1 );
    }
}
#endif


static int APM2_read() {
    static int state = 0;
    static int pkt_id = 0;
    static int pkt_len = 0;
    static int counter = 0;
    static uint8_t cksum_A = 0, cksum_B = 0, cksum_lo = 0, cksum_hi = 0;
    int len;
    uint8_t input[500];
    static uint8_t payload[500];

    // if ( display_on ) {
    //   printf("read APM2, entry state = %d\n", state);
    // }

    bool new_data = false;

    if ( state == 0 ) {
	counter = 0;
	cksum_A = cksum_B = 0;
	len = read( fd, input, 1 );
	while ( len > 0 && input[0] != START_OF_MSG0 ) {
	    // fprintf( stderr, "state0: len = %d val = %2X (%c)\n", len, input[0] , input[0]);
	    len = read( fd, input, 1 );
	}
	if ( len > 0 && input[0] == START_OF_MSG0 ) {
	    // fprintf( stderr, "read START_OF_MSG0\n");
	    state++;
	}
    }
    if ( state == 1 ) {
	len = read( fd, input, 1 );
	if ( len > 0 ) {
	    if ( input[0] == START_OF_MSG1 ) {
		// fprintf( stderr, "read START_OF_MSG1\n");
		state++;
	    } else if ( input[0] == START_OF_MSG0 ) {
		// fprintf( stderr, "read START_OF_MSG0\n");
	    } else {
		state = 0;
	    }
	}
    }
    if ( state == 2 ) {
	len = read( fd, input, 1 );
	if ( len > 0 ) {
	    pkt_id = input[0];
	    cksum_A += input[0];
	    cksum_B += cksum_A;
	    // fprintf( stderr, "pkt_id = %d\n", pkt_id );
	    state++;
	}
    }
    if ( state == 3 ) {
	len = read( fd, input, 1 );
	if ( len > 0 ) {
	    pkt_len = input[0];
	    if ( pkt_len < 256 ) {
		// fprintf( stderr, "pkt_len = %d\n", pkt_len );
		cksum_A += input[0];
		cksum_B += cksum_A;
		state++;
	    } else {
		state = 0;
	    }
	}
    }
    if ( state == 4 ) {
	len = read( fd, input, 1 );
	while ( len > 0 ) {
	    payload[counter++] = input[0];
	    // fprintf( stderr, "%02X ", input[0] );
	    cksum_A += input[0];
	    cksum_B += cksum_A;
	    if ( counter >= pkt_len ) {
		break;
	    }
	    len = read( fd, input, 1 );
	}

	if ( counter >= pkt_len ) {
	    state++;
	    // fprintf( stderr, "\n" );
	}
    }
    if ( state == 5 ) {
	len = read( fd, input, 1 );
	if ( len > 0 ) {
	    cksum_lo = input[0];
	    state++;
	}
    }
    if ( state == 6 ) {
	len = read( fd, input, 1 );
	if ( len > 0 ) {
	    cksum_hi = input[0];
	    if ( cksum_A == cksum_lo && cksum_B == cksum_hi ) {
		// printf( "checksum passes (%d)!\n", pkt_id );
		new_data = APM2_parse( pkt_id, pkt_len, payload );
	    } else {
		if ( display_on ) {
		    // printf("checksum failed %d %d (computed) != %d %d (message)\n",
		    //	   cksum_A, cksum_B, cksum_lo, cksum_hi );
		}
	    }
	    // this is the end of a record, reset state to 0 to start
	    // looking for next record
	    state = 0;
	}
    }

    if ( new_data ) {
	return pkt_id;
    } else {
	return 0;
    }
}


// send a full configuration to APM2 and return true only when all
// parameters are acknowledged.
static bool APM2_send_config() {
    if ( display_on ) {
	printf("APM2_send_config()\n");
    }

    double start_time = 0.0;
    double timeout = 0.5;

    SGPropertyNode *APM2_serial_number_node
	= fgGetNode("/config/sensors/APM2/setup-serial-number");
    if ( APM2_serial_number_node != NULL ) {
	uint16_t serial_number = APM2_serial_number_node->getIntValue();
	start_time = get_Time();    
	APM2_act_set_serial_number( serial_number );
	last_ack_id = 0;
	while ( (last_ack_id != SERIAL_NUMBER_PACKET_ID) ) {
	    APM2_read();
	    if ( get_Time() > start_time + timeout ) {
		if ( display_on ) {
		    printf("Timeout waiting for set serial_number ack...\n");
		}
		return false;
	    }
	}
    }

    SGPropertyNode *pwm_rates = fgGetNode("/config/actuators/actuator/pwm-rates");
    if ( pwm_rates != NULL ) {
	for ( int i = 0; i < NUM_ACTUATORS; i++ ) {
	    act_rates[i] = 0; /* no change from default */
	}
	for ( int i = 0; i < pwm_rates->nChildren(); ++i ) {
	    SGPropertyNode *channel_node = pwm_rates->getChild(i);
	    int ch = channel_node->getIndex();
	    uint16_t rate_hz = channel_node->getIntValue();
	    act_rates[ch] = rate_hz;
	}
	start_time = get_Time();    
	APM2_act_set_pwm_rates( act_rates );
	last_ack_id = 0;
	while ( (last_ack_id != PWM_RATE_PACKET_ID) ) {
	    APM2_read();
	    if ( get_Time() > start_time + timeout ) {
		if ( display_on ) {
		    printf("Timeout waiting for pwm_rate ack...\n");
		}
		return false;
	    }
	}
    }

    SGPropertyNode *gains = fgGetNode("/config/actuators/actuator/gains");
    if ( gains != NULL ) {
	for ( int i = 0; i < gains->nChildren(); ++i ) {
	    SGPropertyNode *channel_node = gains->getChild(i);
	    int ch = channel_node->getIndex();
	    float gain = channel_node->getFloatValue();
	    if ( display_on ) {
		printf("gain: %d %.2f\n", ch, gain);
	    }
	    start_time = get_Time();    
	    APM2_act_gain_mode( ch, gain );
	    last_ack_id = 0;
	    last_ack_subid = 0;
	    while ( (last_ack_id != ACT_GAIN_PACKET_ID)
		    || (last_ack_subid != ch) )
	    {
		APM2_read();
		if ( get_Time() > start_time + timeout ) {
		    printf("Timeout waiting for gain %d ACK\n", ch);
		    return false;
		}
	    }
	}
    }

    SGPropertyNode *mixing = fgGetNode("/config/actuators/actuator/mixing");
    if ( mixing != NULL ) {
	for ( int i = 0; i < mixing->nChildren(); ++i ) {
	    string mode = "";
	    int mode_id = 0;
	    bool enable = false;
	    float gain1 = 0.0;
	    float gain2 = 0.0;
	    SGPropertyNode *mix_node = mixing->getChild(i);
	    SGPropertyNode *mode_node = mix_node->getChild("mode");
	    SGPropertyNode *enable_node = mix_node->getChild("enable");
	    SGPropertyNode *gain1_node = mix_node->getChild("gain1");
	    SGPropertyNode *gain2_node = mix_node->getChild("gain2");
	    if ( mode_node != NULL ) {
		mode = mode_node->getStringValue();
		if ( mode == "auto-coordination" ) {
		    mode_id = MIX_AUTOCOORDINATE;
		} else if ( mode == "throttle-trim" ) {
		    mode_id = MIX_THROTTLE_TRIM;
		} else if ( mode == "flap-trim" ) {
		    mode_id = MIX_FLAP_TRIM;
		} else if ( mode == "elevon" ) {
		    mode_id = MIX_ELEVONS;
		} else if ( mode == "flaperon" ) {
		    mode_id = MIX_FLAPERONS;
		} else if ( mode == "vtail" ) {
		    mode_id = MIX_VTAIL;
		} else if ( mode == "diff-thrust" ) {
		    mode_id = MIX_DIFF_THRUST;
		}
	    }
	    if ( enable_node != NULL ) {
		enable = enable_node->getBoolValue();
	    }
	    if ( gain1_node != NULL ) {
		gain1 = gain1_node->getFloatValue();
	    }
	    if ( gain2_node != NULL ) {
		gain2 = gain2_node->getFloatValue();
	    }
	    if ( display_on ) {
		printf("mix: %s %d %.2f %.2f\n", mode.c_str(), enable,
		       gain1, gain2);
	    }
	    start_time = get_Time();    
	    APM2_act_mix_mode( mode_id, enable, gain1, gain2);
	    last_ack_id = 0;
	    last_ack_subid = 0;
	    while ( (last_ack_id != MIX_MODE_PACKET_ID)
		    || (last_ack_subid != mode_id) )
	     {
		APM2_read();
		if ( get_Time() > start_time + timeout ) {
		    printf("Timeout waiting for %s ACK\n", mode.c_str());
		    return false;
		}
	    }
	}
    }

    SGPropertyNode *sas = fgGetNode("/config/actuators/actuator/sas");
    if ( sas != NULL ) {
	for ( int i = 0; i < sas->nChildren(); ++i ) {
	    string mode = "";
	    int mode_id = 0;
	    bool enable = false;
	    float gain = 0.0;
	    SGPropertyNode *section_node = sas->getChild(i);
	    string section_name = section_node->getName();
	    if ( section_name == "axis" ) {
		SGPropertyNode *mode_node = section_node->getChild("mode");
		SGPropertyNode *enable_node = section_node->getChild("enable");
		SGPropertyNode *gain_node = section_node->getChild("gain");
		if ( mode_node != NULL ) {
		    mode = mode_node->getStringValue();
		    if ( mode == "roll" ) {
			mode_id = SAS_ROLLAXIS;
		    } else if ( mode == "pitch" ) {
			mode_id = SAS_PITCHAXIS;
		    } else if ( mode == "yaw" ) {
			mode_id = SAS_YAWAXIS;
		    }
		}
		if ( enable_node != NULL ) {
		    enable = enable_node->getBoolValue();
		}
		if ( gain_node != NULL ) {
		    gain = gain_node->getFloatValue();
		}
	    } else if ( section_name == "pilot-tune" ) {
		SGPropertyNode *enable_node = section_node->getChild("enable");
		mode_id = SAS_CH7_TUNE;
		mode = "ch7-tune";
		if ( enable_node != NULL ) {
		    enable = enable_node->getBoolValue();
		}
		gain = 0.0; // not used
	    }
	    if ( display_on ) {
		printf("sas: %s %d %.2f\n", mode.c_str(), enable, gain);
	    }
	    start_time = get_Time();    
	    APM2_act_sas_mode( mode_id, enable, gain );
	    last_ack_id = 0;
	    last_ack_subid = 0;
	    while ( (last_ack_id != SAS_MODE_PACKET_ID)
		    || (last_ack_subid != mode_id) )
	     {
		APM2_read();
		if ( get_Time() > start_time + timeout ) {
		    printf("Timeout waiting for %s ACK\n", mode.c_str());
		    return false;
		}
	    }
	}
    }

    start_time = get_Time();    
    APM2_act_write_eeprom();
    last_ack_id = 0;
    while ( (last_ack_id != WRITE_EEPROM_PACKET_ID) ) {
	APM2_read();
	if ( get_Time() > start_time + timeout ) {
	    if ( display_on ) {
		printf("Timeout waiting for write EEPROM ack...\n");
	    }
	    return false;
	}
    }

    return true;
}


// generate a pwm pulse length from a normalized [-1 to 1] or [0 to 1] range
static int gen_pulse( double val, bool symmetrical ) {
    int pulse = 0;

    if ( symmetrical ) {
	// i.e. aileron, rudder, elevator
	if ( val < -1.5 ) { val = -1.5; }
	if ( val > 1.5 ) { val = 1.5; }
	pulse = PWM_CENTER + (int)(PWM_HALF_RANGE * val);
    } else {
	// i.e. throttle
	if ( val < 0.0 ) { val = 0.0; }
	if ( val > 1.0 ) { val = 1.0; }
	pulse = PWM_MIN + (int)(PWM_RANGE * val);
    }

    return pulse;
}


static bool APM2_act_write() {
    uint8_t buf[256];
    uint8_t cksum0, cksum1;
    uint8_t size = 0;
    int len;

    // start of message sync bytes
    buf[0] = START_OF_MSG0; buf[1] = START_OF_MSG1, buf[2] = 0;
    len = write( fd, buf, 2 );

    // packet id (1 byte)
    buf[0] = FLIGHT_COMMAND_PACKET_ID;
    // packet length (1 byte)
    buf[1] = 2 * NUM_ACTUATORS;
    len = write( fd, buf, 2 );

#if 0
    // generate some test data
    static double t = 0.0;
    t += 0.02;
    double dummy = sin(t);
    act_aileron_node->setFloatValue(dummy);
    act_elevator_node->setFloatValue(dummy);
    act_throttle_node->setFloatValue((dummy/2)+0.5);
    act_rudder_node->setFloatValue(dummy);
    act_channel5_node->setFloatValue(dummy);
    act_channel6_node->setFloatValue(dummy);
    act_channel7_node->setFloatValue(dummy);
    act_channel8_node->setFloatValue(dummy);
#endif

    // actuator data
    if ( NUM_ACTUATORS == 8 ) {
	int val;
	uint8_t hi, lo;

	val = gen_pulse( act_aileron_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_elevator_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_throttle_node->getFloatValue(), false );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_rudder_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_channel5_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_channel6_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_channel7_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;

	val = gen_pulse( act_channel8_node->getFloatValue(), true );
	hi = val / 256;
	lo = val - (hi * 256);
	buf[size++] = lo;
	buf[size++] = hi;
    }
  
    // write packet
    len = write( fd, buf, size );
  
    // check sum (2 bytes)
    APM2_cksum( FLIGHT_COMMAND_PACKET_ID, size, buf, size, &cksum0, &cksum1 );
    buf[0] = cksum0; buf[1] = cksum1; buf[2] = 0;
    len = write( fd, buf, 2 );

    return true;
}


bool APM2_update() {
    // read any pending APM2 data (and parse any completed messages)
    while ( APM2_read() > 0 );
    // APM2_read_tmp();

    return true;
}


bool APM2_imu_update() {
    static double last_imu_timestamp = -1000.0;
    
    APM2_update();

    if ( imu_inited ) {
	const double gyro_scale = 0.0174532 / 16.4;
	const double accel_scale = 9.81 / 4096.0;
	const double temp_scale = 0.02;

	double p_raw = (double)imu_sensors[0] * gyro_scale;
	double q_raw = (double)imu_sensors[1] * gyro_scale;
	double r_raw = (double)imu_sensors[2] * gyro_scale;
	double ax_raw = (double)imu_sensors[3] * accel_scale;
	double ay_raw = (double)imu_sensors[4] * accel_scale;
	double az_raw = (double)imu_sensors[5] * accel_scale;
	double temp_C = (double)imu_sensors[6] * temp_scale;

	if ( reverse_imu_mount ) {
	    // reverse roll/pitch gyros, and x/y accelerometers.
	    p_raw = -p_raw;
	    q_raw = -q_raw;
	    ax_raw = -ax_raw;
	    ay_raw = -ay_raw;
	}

	if ( imu_timestamp > last_imu_timestamp + 5.0 ) {
	    //imu_p_bias_node->setFloatValue( p_cal.eval_bias( temp_C ) );
	    //imu_q_bias_node->setFloatValue( q_cal.eval_bias( temp_C ) );
	    //imu_r_bias_node->setFloatValue( r_cal.eval_bias( temp_C ) );
	    imu_ax_bias_node->setFloatValue( ax_cal.eval_bias( temp_C ) );
	    imu_ay_bias_node->setFloatValue( ay_cal.eval_bias( temp_C ) );
	    imu_az_bias_node->setFloatValue( az_cal.eval_bias( temp_C ) );
	    last_imu_timestamp = imu_timestamp;
	}

	imu_p_node->setDoubleValue( p_raw );
	imu_q_node->setDoubleValue( q_raw );
	imu_r_node->setDoubleValue( r_raw );
	imu_ax_node->setDoubleValue( ax_cal.calibrate(ax_raw, temp_C) );
	imu_ay_node->setDoubleValue( ay_cal.calibrate(ay_raw, temp_C) );
	imu_az_node->setDoubleValue( az_cal.calibrate(az_raw, temp_C) );

	imu_timestamp_node->setDoubleValue( imu_timestamp );
	imu_temp_node->setDoubleValue( imu_sensors[6] * temp_scale );
    }

    return true;
}


// This function works ONLY with the UBLOX date format (the ublox reports
// weeks since the GPS epoch.)
static double ublox_date_time_to_unix_sec( int week, float gtime ) {
    double julianDate = (week * 7.0) + 
	(0.001 * gtime) / 86400.0 +  // 86400 = seconds in 1 day
	2444244.5; // 2444244.5 Julian date of GPS epoch (Jan 5 1980 at midnight)
    julianDate = julianDate - 2440587.5; // Subtract Julian Date of Unix Epoch (Jan 1 1970)

    double unixSecs = julianDate * 86400.0;

    // hardcoded handling of leap seconds
    unixSecs -= 16.0;

    // printf("unix time = %.0f\n", unixSecs);

    return unixSecs;
}

// This function works ONLY with the MTK16 date format (the ublox reports
// weeks since the GPS epoch.)
static double MTK16_date_time_to_unix_sec( int gdate, float gtime ) {
    gtime /= 1000.0;
    int hour = (int)(gtime / 3600); gtime -= hour * 3600;
    int min = (int)(gtime / 60); gtime -= min * 60;
    int isec = (int)gtime; gtime -= isec;
    float fsec = gtime;

    int day = gdate / 10000; gdate -= day * 10000;
    int mon = gdate / 100; gdate -= mon * 100;
    int year = gdate;

    // printf("%02d:%02d:%02d + %.3f  %02d / %02d / %02d\n", hour, min,
    //        isec, fsec, day, mon,
    //        year );

    struct tm t;
    t.tm_sec = isec;
    t.tm_min = min;
    t.tm_hour = hour;
    t.tm_mday = day;
    t.tm_mon = mon - 1;
    t.tm_year = year + 100;
    t.tm_gmtoff = 0;

    // force timezone to GMT/UTC so mktime() does the proper conversion
    tzname[0] = tzname[1] = (char *)"GMT";
    timezone = 0;
    daylight = 0;
    setenv("TZ", "UTC", 1);
    
    // printf("%d\n", mktime(&t));
    // printf("tzname[0]=%s, tzname[1]=%s, timezone=%d, daylight=%d\n",
    //        tzname[0], tzname[1], timezone, daylight);

    double result = (double)mktime(&t);
    result += fsec;

    // printf("unix time = %.0f\n", result);

    return result;
}


bool APM2_gps_update() {
    static double last_timestamp = 0.0;
    static double last_alt_m = -9999.9;

    APM2_update();

    if ( !gps_inited ) {
	return false;
    }

    double dt = gps_sensors.timestamp - last_timestamp;
    if ( dt < 0.001 ) {
	return false;
    }

    gps_timestamp_node->setDoubleValue(gps_sensors.timestamp);
    gps_day_secs_node->setDoubleValue(gps_sensors.time / 1000.0);
    gps_date_node->setDoubleValue(gps_sensors.date);
    gps_lat_node->setDoubleValue(gps_sensors.latitude / 10000000.0);
    gps_lon_node->setDoubleValue(gps_sensors.longitude / 10000000.0);
    double alt_m = gps_sensors.altitude / 100.0;
    gps_alt_node->setDoubleValue( alt_m );

    // compute horizontal speed components
    double speed_mps = gps_sensors.ground_speed * 0.01;
    double angle_rad = (90.0 - gps_sensors.ground_course*0.01)
	* SGD_DEGREES_TO_RADIANS;
    gps_vn_node->setDoubleValue( sin(angle_rad) * speed_mps );
    gps_ve_node->setDoubleValue( cos(angle_rad) * speed_mps );

    // compute vertical speed
    double vspeed_mps = 0.0;
    double da = 0.0;
    if ( last_alt_m > -1000.0 ) {
	da = alt_m - last_alt_m;
    }
    // dt should be safely non zero for a divide or we wouldn't be here
    vspeed_mps = da / dt;
    gps_vd_node->setDoubleValue( -vspeed_mps );
    last_alt_m = alt_m;

    //gps_vd_node = outputroot->getChild("vd-ms", 0, true);
    gps_satellites_node->setIntValue(gps_sensors.num_sats);
    gps_status_node->setIntValue( gps_sensors.status );
    double unix_secs = ublox_date_time_to_unix_sec( gps_sensors.date,
					            gps_sensors.time );
    gps_unix_sec_node->setDoubleValue( unix_secs );

    last_timestamp = gps_sensors.timestamp;

    return true;
}


bool APM2_airdata_update() {
    APM2_update();

    bool fresh_data = false;
    static double last_time = 0.0;
    static double analog0_sum = 0.0;
    static int analog0_count = 0;
    static float analog0_offset = 0.0;
    static float analog0_filter = 0.0;

    if ( airdata_inited ) {
	double cur_time = airdata.timestamp;

	if ( cur_time <= last_time ) {
	    return false;
	}

	if ( ! airspeed_inited ) {
	    if ( airspeed_zero_start_time > 0 ) {
		analog0_sum += analog[0];
		analog0_count++;
		analog0_offset = analog0_sum / analog0_count;
	    } else {
		airspeed_zero_start_time = get_Time();
		analog0_sum = 0.0;
		analog0_count = 0;
		analog0_filter = analog[0];
	    }
	    if ( cur_time > airspeed_zero_start_time + 10.0 ) {
		//printf("analog0_offset = %.2f\n", analog0_offset);
		airspeed_inited = true;
	    }
	}

	airdata_timestamp_node->setDoubleValue( cur_time );

	// basic pressure to airspeed formula: v = sqrt((2/p) * q)
	// where v = velocity, q = dynamic pressure (pitot tube sensor
	// value), and p = air density.

	// if p is specified in kg/m^3 (value = 1.225) and if q is
	// specified in Pa (N/m^2) where 1 psi == 6900 Pa, then the
	// velocity will be in meters per second.

	// The MPXV5004DP has a full scale span of 3.9V, Maximum
	// pressure reading is 0.57psi (4000Pa)

	// Example (APM2): With a 10bit ADC (APM2) we record a value
	// of 230 (0-1024) at zero velocity.  The sensor saturates at
	// a value of about 1017 (4000psi).  Thus:

	// Pa = (ADC - 230) * 5.083
	// Airspeed(mps) = sqrt( (2/1.225) * Pa )

	// This yields a theoretical maximum speed sensor reading of
	// about 81mps (156 kts)

	// hard coded (probably should use constants from the config file,
	// or zero itself out on init.)
	analog0_filter = 0.95 * analog0_filter + 0.05 * analog[0];
	//printf("analog0 = %.2f (analog[0] = %.2f)\n", analog0_filter, analog[0]);
	float Pa = (analog0_filter - analog0_offset) * 5.083;
	if ( Pa < 0.0 ) { Pa = 0.0; } // avoid sqrt(neg_number) situation
	float airspeed_mps = sqrt( 2*Pa / 1.225 ) * pitot_calibrate;
	float airspeed_kt = airspeed_mps * SG_MPS_TO_KT;
	airdata_airspeed_mps_node->setDoubleValue( airspeed_mps );
	airdata_airspeed_kt_node->setDoubleValue( airspeed_kt );

	// publish sensor values
	airdata_pressure_node->setDoubleValue( airdata.pressure / 100.0 );
	airdata_temperature_node->setDoubleValue( airdata.temp / 10.0 );
	airdata_climb_rate_mps_node->setDoubleValue( airdata.climb_rate );
	airdata_climb_rate_fps_node->setDoubleValue( airdata.climb_rate * SG_METER_TO_FEET );

	fresh_data = true;

	last_time = cur_time;
    }

    return fresh_data;
}


bool APM2_pilot_update() {
    APM2_update();

    if ( !pilot_input_inited ) {
	return false;
    }

    float val;

    pilot_timestamp_node->setDoubleValue( pilot_in_timestamp );

    val = normalize_pulse( pilot_input[0], true );
    pilot_aileron_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[1], true );
    pilot_elevator_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[2], false );
    pilot_throttle_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[3], true );
    pilot_rudder_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[4], true );
    pilot_channel5_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[5], true );
    pilot_channel6_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[6], true );
    pilot_channel7_node->setDoubleValue( val );

    val = normalize_pulse( pilot_input[7], true );
    pilot_channel8_node->setDoubleValue( val );

    pilot_manual_node->setIntValue( pilot_channel8_node->getDoubleValue() > 0 );

    return true;
}


bool APM2_act_update() {
    static bool actuator_configured = false;
    
    if ( !actuator_inited ) {
	return false;
    }

    if ( !actuator_configured ) {
	actuator_configured = APM2_send_config();
    }
    
    // send actuator commands to APM2 servo subsystem
    APM2_act_write();

    return true;
}


void APM2_close() {
    close(fd);

    master_opened = false;
}


void APM2_imu_close() {
    APM2_close();
}


void APM2_gps_close() {
    APM2_close();
}


void APM2_airdata_close() {
    APM2_close();
}


void APM2_pilot_close() {
    APM2_close();
}


void APM2_act_close() {
    APM2_close();
}
