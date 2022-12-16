#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/gps.h>

int main(int argc, char **argv)
{

    if (argc < 6)
    {
        printf("invalid args\n", argv[0]);
        return 1;
    }

    struct gps_location loc;

    loc.lat_integer = atoi(argv[1]);
    loc.lng_integer = atoi(argv[3]);
    loc.lat_fractional = atoi(argv[2]);
    loc.lng_fractional = atoi(argv[4]);
    loc.accuracy = atoi(argv[5]);

    int ret = syscall(398, &loc);

    if (ret < 0)
    {
        printf("setting gps failed\n");
        return 1;
    }

    printf("set new GPS location\n");
    return 0;
}