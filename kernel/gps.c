#include <linux/gps.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/namei.h>

long set_gps_location(struct gps_location __user *loc);
long get_gps_location(const char __user *pathname, struct gps_location __user *loc);

struct gps_location init_location = INIT_GPS_LOCATION(init_location);
EXPORT_SYMBOL(init_location);

DEFINE_SPINLOCK(loc_lock);

void location_lock(void) {
    spin_lock(&loc_lock);
}

void location_unlock(void) {
    spin_unlock(&loc_lock);
}

int check_access(struct gps_location *loc) {
    // TODO : calculate distance and check access
    return 1;
}

short is_in_range(struct gps_location *loc) {
    int ret;
    if (loc->lat_integer < -90 || loc->lat_integer > 90 ||
        loc->lng_integer < -180 || loc->lng_integer > 180 ||
        loc->lat_fractional < 0 || loc->lat_fractional > 999999 ||
        loc->lng_fractional < 0 || loc->lng_fractional > 999999 ||
        loc->accuracy < 0) ret = 0;
    else ret = 1;
    return ret;
}

long set_gps_location(struct gps_location __user *loc) {
    struct gps_location *_loc;

    if (loc == NULL) return -EFAULT;

    _loc = (struct gps_location *)kmalloc(sizeof(struct gps_location), GFP_KERNEL);

    if (!_loc) return -EFAULT;

    if(copy_from_user(_loc, loc, sizeof(struct gps_location)) > 0) {
        kfree(_loc);
        return -EFAULT;
    }

    if (!is_in_range(_loc)) {
        kfree(_loc);
        return -EINVAL;
    }

    if ( (_loc->lat_integer == 90 && _loc->lat_fractional > 0) ||
         (_loc->lng_integer == 180 && _loc->lng_fractional > 0)) {
        kfree(_loc);
        return -EINVAL;
    }

    location_lock();
    init_location.lat_integer = _loc->lat_integer;
    init_location.lat_fractional = _loc->lat_fractional;
    init_location.lng_integer = _loc->lng_integer;
    init_location.lng_fractional = _loc->lng_fractional;
    init_location.accuracy = _loc->accuracy;
    location_unlock();

    kfree(_loc);
    return 0;
}

long get_gps_location(const char __user *pathname, struct gps_location __user * loc) {
    struct filename *tmp;
    struct gps_location *_loc;
    struct path path;
    struct inode *inode;
    int error = 0;

    if (pathname == NULL || loc == NULL) return -EFAULT;

    tmp = getname(pathname);
	if (IS_ERR(tmp)) {
		return PTR_ERR(tmp);
    }

    error = kern_path(tmp->name, 0, &path);
    if (error) {
        return -EFAULT;
    }

    inode = path.dentry->d_inode;

    if (inode_permission(inode, MAY_READ | MAY_GET_LOCATION)) {
        return -EACCES;
    }

    if (!inode->i_op->get_gps_location) {
        return -ENODEV;
    }

    _loc = (struct gps_location *)kmalloc(sizeof(struct gps_location), GFP_KERNEL);
    inode->i_op->get_gps_location(inode, _loc);
    
    error = copy_to_user(loc, _loc, sizeof(struct gps_location));
    kfree(_loc);

    if (error) {
        return -EFAULT;
    }

    return 0;
}

SYSCALL_DEFINE1(set_gps_location, struct gps_location __user *, loc)
{
    return set_gps_location(loc);
}

SYSCALL_DEFINE2(get_gps_location, const char __user *, pathname, struct gps_location __user *, loc)
{
    return get_gps_location(pathname, loc);
}