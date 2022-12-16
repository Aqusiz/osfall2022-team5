#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/gps.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[])
{
    struct gps_location gps;

    if (argc != 2)
    {
        printf("invalid args\n");
        return -1;
    }
    if (syscall(399, argv[1], &gps) != 0)
    {
        printf("Couldn't get GPS: %s\n", strerror(errno));
        return -1;
    }

    printf("%s location\n"
           "lat\t\tlong\t\tacc(m)\n"
           "%d.%06d\t\t%d.%06d\t\t%d\n",
           argv[1],
           gps.lat_integer, gps.lat_fractional,
           gps.lng_integer, gps.lng_fractional,
           gps.accuracy);

    printf("google maps: https://www.google.com/maps/place/%d.%06d,%d.%06d\n",
           gps.lat_integer, gps.lat_fractional,
           gps.lng_integer, gps.lng_fractional);

    return 0;
}