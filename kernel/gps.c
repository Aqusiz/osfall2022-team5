#include <linux/gps.h>

long sys_set_gps_location(struct gps_location __user *loc);
long sys_get_gps_location(const char __user *pathname, struct gps_location __user *loc);

SYSCALL_DEFINE1(set_gps_location, struct gps_location __user *, loc)
{
}

SYSCALL_DEFINE2(get_gps_location, const char __user *, pathname, struct gps_location __user *, loc)
{
}