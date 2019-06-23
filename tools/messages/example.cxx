#include <stdio.h>

#include "messages.h"

int main() {
    // create a test message
    message_simple_test_t st;
    st.dummy = 1234;
    uint8_t *msg = st.pack();
    printf("packed length = %d\n", (int)st.len);
    message_simple_test_t st_recv;
    st_recv.unpack(msg);
    printf("result = %d\n", st_recv.dummy);
    printf("\n");

    // create a gps message
    message_gps_v4_t gps;
    gps.latitude_deg = 43.241;
    gps.longitude_deg = -93.520;
    gps.altitude_m = 278.5;
    gps.vn_ms = 1.5;
    gps.ve_ms = -2.7;
    gps.vd_ms = -0.02;
    gps.satellites = 9;

    // pack it
    msg = gps.pack();
    printf("msg id = %d, packed length = %d\n", gps.id, gps.len);

    // pretend the serialized message got sent somewhere and now we
    // received it and deserialized it on the other side
    message_gps_v4_t gps_recv;
    gps_recv.unpack(msg);

    // let's see what we got
    printf("unpack lat: %f\n", gps_recv.latitude_deg);
    printf("unpack lon: %f\n", gps_recv.longitude_deg);
    printf("unpack alt: %f\n", gps_recv.altitude_m);
    printf("unpack vn: %f\n", gps_recv.vn_ms);
    printf("unpack ve: %f\n", gps_recv.ve_ms);
    printf("unpack vd: %f\n", gps_recv.vd_ms);
    printf("unpack vd: %d\n", gps_recv.satellites);
}