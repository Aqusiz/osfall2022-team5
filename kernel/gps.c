#include <linux/gps.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/namei.h>

#define PRECISION 1000000 // Precision of fractional part
#define EARTH_R 6371000   // Earth radius

long set_gps_location(struct gps_location __user *loc);
long get_gps_location(const char __user *pathname, struct gps_location __user *loc);

struct gps_location init_location = {
    .lat_integer = 0,
    .lat_fractional = 0,
    .lng_integer = 0,
    .lng_fractional = 0,
    .accuracy = 0};

DEFINE_SPINLOCK(loc_lock);

void location_lock(void)
{
    spin_lock(&loc_lock);
}

void location_unlock(void)
{
    spin_unlock(&loc_lock);
}

struct gps_location get_init_location(void)
{
    struct gps_location ret_location;

    location_lock();
    ret_location = init_location;
    location_unlock();

    return ret_location;
}

struct my_float
{
    long long int integer;
    long long int fractional;
};

static const struct my_float MF_ZERO = {0, 0};
static const struct my_float MY_PI = {3, 141593};

struct my_float MFLOAT(long long int a, long long int b)
{
    struct my_float retval = {a, b};
    return retval;
}

struct my_float
_carry_float(struct my_float mf)
{
    struct my_float result;
    long long int carry = mf.fractional / PRECISION;

    result.integer = mf.integer + carry;
    result.fractional = mf.fractional % PRECISION;

    if (result.fractional < 0)
    {
        result.fractional += PRECISION;
        result.integer -= 1;
    }

    return result;
}

struct my_float int_to_float(long long int n)
{
    struct my_float result;
    result = MFLOAT(n / PRECISION, n % PRECISION);
    result = _carry_float(result);

    return result;
}

struct my_float add_float(struct my_float mf1, struct my_float mf2)
{
    struct my_float result;

    result.integer = mf1.integer + mf2.integer;
    result.fractional = mf1.fractional + mf2.fractional;

    result = _carry_float(result);

    return result;
}

struct my_float sub_float(struct my_float mf1, struct my_float mf2)
{
    struct my_float result;

    result.integer = mf1.integer - mf2.integer - 1;
    result.fractional = mf1.fractional - mf2.fractional + PRECISION;

    result = _carry_float(result);

    return result;
}

struct my_float avg_float(struct my_float mf1, struct my_float mf2)
{
    long long int _result;
    struct my_float result;

    _result = (mf1.integer * PRECISION + mf1.fractional + mf2.integer * PRECISION + mf2.fractional) / 2;
    result = int_to_float(_result);

    return result;
}

struct my_float mul_float(struct my_float mf1, struct my_float mf2)
{
    struct my_float result;

    result.integer = mf1.integer * mf2.integer;
    result.fractional = 0;

    result.integer += mf1.integer * mf2.fractional / PRECISION;
    result.fractional += mf1.integer * mf2.fractional % PRECISION;

    result.integer += mf1.fractional * mf2.integer / PRECISION;
    result.fractional += mf1.fractional * mf2.integer % PRECISION;

    result.fractional += mf1.fractional * mf2.fractional / PRECISION;

    result = _carry_float(result);

    return result;
}

struct my_float deg_to_rad(struct my_float mf)
{
    struct my_float result;
    long long int _result;

    result = mul_float(mf, MY_PI);

    _result = (result.integer * PRECISION + result.fractional) / 180;

    result = int_to_float(_result);

    return result;
}

struct my_float neg_float(struct my_float mf)
{
    return sub_float(MFLOAT(0, 0), mf);
}

// mf1 == mf2
int eq_float(struct my_float mf1, struct my_float mf2)
{
    if ((mf1.integer == mf2.integer) &&
        (mf1.fractional == mf2.fractional))
        return 1;
    return 0;
}

// mf1 < mf2
int lt_float(struct my_float mf1, struct my_float mf2)
{
    if (mf1.integer < mf2.integer)
        return 1;

    if (mf1.integer > mf2.integer)
        return 0;

    if (mf1.fractional < mf2.fractional)
        return 1;

    return 0;
}

// mf1 <= mf2
int lteq_float(struct my_float mf1, struct my_float mf2)
{
    if (lt_float(mf1, mf2) || eq_float(mf1, mf2))
        return 1;

    return 0;
}

struct my_float abs_float(struct my_float mf)
{
    if (lt_float(mf, MF_ZERO))
        return neg_float(mf);
    return mf;
}

int _sin_degree[90] = {0, 17452, 34899, 52336, 69756, 87156, 104528, 121869, 139173, 156434,
                       173648, 190809, 207912, 224951, 241922, 258819, 275637, 292372, 309017, 325568,
                       342020, 358368, 374607, 390731, 406737, 422618, 438371, 453990, 469472, 484810,
                       500000, 515038, 529919, 544639, 559193, 573576, 587785, 601815, 615661, 629320,
                       642788, 656059, 669131, 681998, 694658, 707107, 719340, 731354, 743145, 754710,
                       766044, 777146, 788011, 798636, 809017, 819152, 829038, 838671, 848048, 857167,
                       866025, 874620, 882948, 891007, 898794, 906308, 913545, 920505, 927184, 933580,
                       939693, 945519, 951057, 956305, 961262, 965926, 970296, 974370, 978148, 981627,
                       984808, 987688, 990268, 992546, 994522, 996195, 997564, 998630, 999391, 999848};

struct my_float sin_float(struct my_float deg)
{
    long long int left, right, result;

    // deg < 0 : negation
    if (lt_float(deg, MF_ZERO))
        return neg_float(sin_float(neg_float(deg)));

    // deg == 90
    if (eq_float(deg, MFLOAT(90, 0)))
        return MFLOAT(1, 0);

    // 90 < deg : sin(deg) = sin(180 - deg)
    if (lt_float(MFLOAT(90, 0), deg))
        return sin_float(sub_float(MFLOAT(180, 0), deg));

    // if deg is integer
    if (deg.fractional == 0)
        return MFLOAT(0, _sin_degree[deg.integer]);

    // interpolation
    left = _sin_degree[deg.integer];
    right = (deg.integer == 89) ? PRECISION : _sin_degree[deg.integer + 1];

    result = (left * (PRECISION - deg.fractional) + right * deg.fractional) / PRECISION;

    if (result >= PRECISION)
        result = PRECISION - 1;

    return MFLOAT(0, result);
}

struct my_float cos_float(struct my_float deg)
{
    // cos = sin(90 - deg)
    return sin_float(sub_float(MFLOAT(90, 0), deg));
}

struct my_float deg_arc_len(struct my_float deg, int radius)
{
    struct my_float rad = deg_to_rad(deg);
    return mul_float(rad, MFLOAT(radius, 0));
}

// return 1 only if current location & file location overlap, else 0
int check_access(struct gps_location *loc)
{
    struct gps_location curr_loc;
    struct my_float cur_lat, cur_lng, loc_lat, loc_lng, avg_lat, lat_diff, lng_diff, dx, dy, diagsq;
    long long int rot_radius, dist;

    location_lock();
    curr_loc = init_location;
    location_unlock();

    cur_lat = MFLOAT(curr_loc.lat_integer, curr_loc.lat_fractional);
    cur_lng = MFLOAT(curr_loc.lng_integer, curr_loc.lng_fractional);
    loc_lat = MFLOAT(loc->lat_integer, loc->lat_fractional);
    loc_lng = MFLOAT(loc->lng_integer, loc->lng_fractional);

    avg_lat = avg_float(cur_lat, loc_lat);
    lat_diff = abs_float(sub_float(cur_lat, loc_lat));
    lng_diff = abs_float(sub_float(cur_lng, loc_lng));

    // if 180 < lng_diff then lng_diff = 360 - lng_diff
    if (lt_float(MFLOAT(180, 0), lng_diff))
        lng_diff = sub_float(MFLOAT(360, 0), lng_diff);

    rot_radius = EARTH_R;
    rot_radius *= cos_float(avg_lat).fractional;
    rot_radius /= PRECISION;

    dist = curr_loc.accuracy + loc->accuracy;

    dx = deg_arc_len(lng_diff, rot_radius);
    dy = deg_arc_len(lat_diff, EARTH_R);

    diagsq = add_float(mul_float(dx, dx), mul_float(dy, dy));

    return lteq_float(diagsq, MFLOAT(dist * dist, 0));
}

short is_in_range(struct gps_location *loc)
{
    int ret;
    if (loc->lat_integer < -90 || loc->lat_integer > 90 ||
        loc->lng_integer < -180 || loc->lng_integer > 180 ||
        loc->lat_fractional < 0 || loc->lat_fractional > 999999 ||
        loc->lng_fractional < 0 || loc->lng_fractional > 999999 ||
        loc->accuracy < 0)
        ret = 0;
    else
        ret = 1;
    return ret;
}

long set_gps_location(struct gps_location __user *loc)
{
    struct gps_location *_loc;

    if (loc == NULL)
        return -EFAULT;

    _loc = (struct gps_location *)kmalloc(sizeof(struct gps_location), GFP_KERNEL);

    if (!_loc)
        return -EFAULT;

    if (copy_from_user(_loc, loc, sizeof(struct gps_location)) > 0)
    {
        kfree(_loc);
        return -EFAULT;
    }

    if (!is_in_range(_loc))
    {
        kfree(_loc);
        return -EINVAL;
    }

    if ((_loc->lat_integer == 90 && _loc->lat_fractional > 0) ||
        (_loc->lng_integer == 180 && _loc->lng_fractional > 0))
    {
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

long get_gps_location(const char __user *pathname, struct gps_location __user *loc)
{
    struct filename *tmp;
    struct gps_location *_loc;
    struct path path;
    struct inode *inode;
    int error = 0;

    if (pathname == NULL || loc == NULL)
        return -EFAULT;

    tmp = getname(pathname);
    if (IS_ERR(tmp))
    {
        return PTR_ERR(tmp);
    }

    error = kern_path(tmp->name, 0, &path);
    if (error)
    {
        return -EFAULT;
    }

    inode = path.dentry->d_inode;

    if (inode_permission(inode, MAY_READ | MAY_GET_LOCATION))
    {
        return -EACCES;
    }

    if (!inode->i_op->get_gps_location)
    {
        return -ENODEV;
    }

    _loc = (struct gps_location *)kmalloc(sizeof(struct gps_location), GFP_KERNEL);
    inode->i_op->get_gps_location(inode, _loc);

    error = copy_to_user(loc, _loc, sizeof(struct gps_location));
    kfree(_loc);

    if (error)
    {
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