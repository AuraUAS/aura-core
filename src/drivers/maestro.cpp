#include <fcntl.h>		// open()
#include <termios.h>		// tcgetattr() et. al.

#include "util/strutils.h"
#include "maestro.h"

bool maestro_t::open( const char *device_name ) {
    fd = ::open( device_name, O_RDWR | O_NOCTTY | O_NONBLOCK );
    // fd = ::open( device_name, O_RDWR | O_NOCTTY );
    if ( fd < 0 ) {
        fprintf( stderr, "open serial: unable to open %s - %s\n",
                 device_name, strerror(errno) );
	return false;
    }

    return true;
}

void maestro_t::init( pyPropertyNode *config ) {
    act_node = pyGetNode("/actuators", true);
    if ( config->hasChild("device") ) {
        string device = config->getString("device");
        if ( open(device.c_str()) ) {
            printf("maestro device opened: %s\n", device.c_str());
        } else {
            printf("unable to open maestro device: %s\n", device.c_str());
        }
    } else {
        printf("no maestro device specified\n");
    }
}

void maestro_t::write_channel(int ch, float norm, bool symmetrical) {
    // target value is 1/4 us, so center (1500) would have a value of
    // 6000 for a symmetrical channel
    int target = 1500.0 * 4;
    if ( symmetrical ) {
        // rudder, etc.
        target = (1500 + 500 * norm) * 4;
    } else {
        // throttle
        target = (1000 + 1000 * norm) * 4;
    }
    uint8_t command[] = {0x84, ch, target & 0x7F, target >> 7 & 0x7F};
    if ( ::write(fd, command, sizeof(command)) == -1) {
        perror("maestro error writing");
    }
}

void maestro_t::write() {
    write_channel(0, act_node.getDouble("throttle"), false);
    write_channel(1, act_node.getDouble("aileron"), true);
    write_channel(2, act_node.getDouble("elevator"), true);
    write_channel(3, act_node.getDouble("rudder"), true);
    write_channel(4, act_node.getDouble("flaps"), true);
    write_channel(5, act_node.getDouble("gear"), true);
}

void maestro_t::close() {
    ::close(fd);
}