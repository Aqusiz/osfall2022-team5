# OS project 4
### Team 5
### 강휘현 김준오


## 1. How to build

커널 및 모듈 컴파일링)   
커널 디렉토리(/osfall2022-team5) 내부에서 ./build-rpi3-arm64.sh

테스트 프로그램 컴파일링)   
/osfall2022-team5/test 디렉토리에서 arm-linux-gnueabi-gcc -I../include test.c -o test -lm

tizen 내부에 집어넣는 법)   
sudo mount tizen-image/rootfs.img {mount directory(임의로 지정)}   
ptree_mod.ko 파일과 test 실행파일을 {mount}/root 내부로 복사   
sudo umount {mount}

tizen 실행) ./qemu.sh   
test 프로그램 컴파일) make (make clean for cleaning)   
file system mount) mount -o loop -t ext2 /root/proj4.fs /root/proj4   
 file location 확인) ./file_loc {file}   
현재 gps update) ./gpsupdate {gps_location args}

## 2. High level design and implementation

### Geo-tagged file system Implementation

### gps.h
```c
struct gps_location
{
    int lat_integer;
    int lat_fractional;
    int lng_integer;
    int lng_fractional;
    int accuracy;
};
```
기본 가이드대로 gps_location을 나타내는 entity를 정의한다.   
floating point연산을 사용할 수 없으므로 integer와 fractional을 사용한다는 점에 주목해야 했다. 

### gps.c
system call set_gps_location, get_gps_location에 대한 정의와 permission check를 위한 distance 계산 logic이 주된 내용이다.
```c
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
```
현재 gps location을 설정한다.   
유효하지 않은 input들과 위도,경도를 validation 한 뒤 간단하게 location을 설정한다.   
이 때 location은 shared state이므로 lock을 이용해 업데이트 해야한다.
```c
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
```
file이 가진 gps location을 확인한다.   
path로부터 inode의 정보를 가져오고, inode에 미리 정의해둔 gps location 정보를 가져온다.    
이를 위해 inode.c에 gps location 관련 정보를 추가하고, inode operation을 file.c에 정의해 두었다.
```c
// file.c
int ext2_set_gps_location(struct inode *inode) {
	struct ext2_inode_info *ei = EXT2_I(inode);
	struct gps_location *loc = &init_location;
	location_lock();
	// lat, lng to U32
	ei->i_lat_integer = (__u32)(loc->lat_integer + 90);
	ei->i_lng_integer = (__u32)(loc->lng_integer + 180);
	ei->i_lat_fractional = (__u32)loc->lat_fractional;
	ei->i_lng_fractional = (__u32)loc->lng_fractional;
	ei->i_accuracy = (__u32)loc->accuracy;
	location_unlock();
	return 0;
}

int ext2_get_gps_location(struct inode *inode, struct gps_location *loc) {
	struct ext2_inode_info *ei = EXT2_I(inode);
	// U32 to lat, lng
	loc->lat_integer = ((int)ei->i_lat_integer - 90);
	loc->lng_integer = ((int)ei->i_lng_integer - 180);
	loc->lat_fractional = (int)ei->i_lat_fractional;
	loc->lng_fractional = (int)ei->i_lng_fractional;
	loc->accuracy = (int)ei->i_accuracy;
	return 0;
}

int ext2_permission(struct inode *inode, int mask) {
	struct gps_location loc;
	ext2_get_gps_location(inode, &loc);

	// check MAY_GET_LOCATION
	if (!(mask & MAY_GET_LOCATION) && !check_access(&loc)) {
		return -EACCES;
	}

	return generic_permission(inode, mask);
}
...
    .set_gps_location = ext2_set_gps_location,
    .get_gps_location = ext2_get_gps_location,
    .permission = ext2_permission,
...
// inode.c
// for gps location
	raw_inode->i_lat_integer = cpu_to_le32(ei->i_lat_integer);
	raw_inode->i_lat_fractional = cpu_to_le32(ei->i_lat_fractional);
	raw_inode->i_lng_integer = cpu_to_le32(ei->i_lng_integer);
	raw_inode->i_lng_fractional = cpu_to_le32(ei->i_lng_fractional);
	raw_inode->i_accuracy = cpu_to_le32(ei->i_accuracy);
...
    ei->i_lat_integer = le32_to_cpu(raw_inode->i_lat_integer);
	ei->i_lng_integer = le32_to_cpu(raw_inode->i_lng_integer);
	ei->i_lat_fractional = le32_to_cpu(raw_inode->i_lat_fractional);
	ei->i_lng_fractional = le32_to_cpu(raw_inode->i_lng_fractional);
	ei->i_accuracy = le32_to_cpu(raw_inode->i_accuracy);
```
### calculating distance
floating point 연산을 이용할 수 없었기에 미리 정의한 자료구조에 맞게 그러한 연산을 자체적으로 구현해야 했다.    
그러기 위해 my_float라는 integer, fractional part로 구현된 자료구조를 정의하였고, 그와 관련된 연산 또한 구현하였다.   
또한 구 위의 두 점을 구하기 위해서는 삼각함수의 값이 필요했는데, sin 0~90의 값을 미리 알고 있으면 나머지 범위의 값과 cos값을 적절한 연산으로 구현할 수 있다는 점을 이용해 sin value table을 array로 구현하여 이용하였다.
```c
#define PRECISION 1000000 // Precision of fractional part
#define EARTH_R 6371000   // Earth radius
struct my_float
{
    long long int integer;
    long long int fractional;
};
static const struct my_float MF_ZERO = {0, 0};
static const struct my_float MY_PI = {3, 141593};
int _sin_degree[90] = {0, 17452, 34899, 52336, ...
```
앞서 설명한 float구조와 sin값의 table이다.   
구에서의 거리를 구하기 위한 상수들 또한 정의하여 사용했다.   
sin값은 integer array로 되어있지만 [0,1]의 값을 갖는다는 점을 이용해 fractional의 값을 가지도록 하였다.
```c
struct my_float _carry_float(struct my_float mf); // fractional이 precision보다 커지면 carry로 integer를 1 증가시킨다.

struct my_float int_to_float(long long int n); // (integer,float) 형태로 concat된 int를 myfloat로 변환한다.

struct my_float add_float(struct my_float mf1, struct my_float mf2); 

struct my_float sub_float(struct my_float mf1, struct my_float mf2);

struct my_float avg_float(struct my_float mf1, struct my_float mf2); // (mf1+mf2)/2

struct my_float mul_float(struct my_float mf1, struct my_float mf2); // mf1 * mf2

struct my_float deg_to_rad(struct my_float mf); //  degree를 radian으로 변환한다.

struct my_float neg_float(struct my_float mf); // negation

int eq_float(struct my_float mf1, struct my_float mf2); // mf1 == mf2

int lt_float(struct my_float mf1, struct my_float mf2); // mf1 < mf2

int lteq_float(struct my_float mf1, struct my_float mf2); // mf1 <= mf2

struct my_float abs_float(struct my_float mf); // |mf|

struct my_float sin_float(struct my_float deg); // sin값을 구한다.
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

struct my_float cos_float(struct my_float deg) // cos값을 sin(90-deg)를 통해 구한다

struct my_float deg_arc_len(struct my_float deg, int radius) // degree를 radian으로 변환해 distance를 구한다.
```
위와 같은 연산들을 구현하였고, 구현 과정은 복잡하지 않아 생략하였다.   
삼각함수의 계산에서 sin값을 구하는 과정이 핵심이었는데, 미리 만들어둔 sin table과 더불에 interpolation을 이용하여 임의의 float값에 대한 sin값을 계산한다.
```c
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
    loc_lat = MFLOAT(loc.lat_integer, loc.lat_fractional);
    loc_lng = MFLOAT(loc.lng_integer, loc.lng_fractional);

    avg_lat = avg_float(cur_lat, loc_lat);
    lat_diff = abs_float(sub_float(cur_lat, loc_lat));
    lng_diff = abs_float(sub_float(cur_lng, loc_lng));

    // if 180 < lng_diff then lng_diff = 360 - lng_diff
    if (lt_float(MFLOAT(180, 0), lng_diff))
        lng_diff = sub_float(MFLOAT(360, 0), lng_diff);

    rot_radius = EARTH_R;
    rot_radius *= cos_float(avg_lat).fractional;
    rot_radius /= PRECISION;

    dist = curr_loc.accuracy + loc.accuracy;

    dx = deg_arc_len(lng_diff, rot_radius);
    dy = deg_arc_len(lat_diff, EARTH_R);

    diagsq = add_float(mul_float(dx, dx), mul_float(dy, dy));

    return lteq_float(diagsq, MFLOAT(dist * dist, 0));
}

```
이제 위에서 구현한 float 연산을 통하여 distance를 계산할 수 있다.   
현재 location을 원점으로 하고 accuracy를 반지름으로 하는 원과, file에 해당하는 원이 만나면 permission을 갖는 것으로 간주한다.   
그것을 위해 두 원점간의 distance를 계산하고, 계산한 distance가 반지름의 합보다 작거나 같은지를 확인하여 반환한다.
## error factor?
상술한 방법으로 구한 distance는 실제와 어느정도 오차를 가지게 되는데, 그 이유를 분석해 보면 다음과 같다.   
- precision : 현재의 precision은 1000000으로 설정되어있는데, 이는 소수점 6자리까지만 계산한다는 것이다. 그렇다면 그 아래 자리수들은 자연스럽게 error가 되고, 이 error들은 계산과정에서 누적된다.
- earth : 지구는 완벽한 구형이 아니기에 완벽한 구 형태로 가정하고 계산한 위의 식은 오차가 있다.

그런데 precision으로 인한 오차는 어느정도 받아들일 만하다고 결론지을 수 있는데. 위도와 경도의 precision을 고려해 보면 된다.    
위도와 경도 또한 소수점 6자리까지의 precision을 갖는데, 위도나 경도의 0.000001 차이는 0.000001m보단 클 것이다. precision으로 인한 distance error가 발생하더라도 comparison의 결과에 영향을 끼칠 가능성은 매우 낮을 것이다.

## 3. Investigation of Geo-tagged file system
```shell
root:-> ./file_loc ./proj4/foo.txt
./proj4/foo.txt location
lat     long    acc(m)
0.000000    0.000000    0.000000
google maps: https: //www.google.com/maps/place/0.000000 ,0.000000
root:-> ./file_loc ./proj4/bar.txt
./proj4/bar.txt location
lat     long    acc(m)
50.000005    50.000003    2
google maps: https: //www.google.com/maps/place/50.000005 ,50.000003
foot:~> ./file_loc ./proj4/test3.txt
-/proj4/test3.txt Location
lat     long    acc(m)
89.000099    170.000099    3
google maps: https: //www.google.com/maps/place/89.000099 ,170.000099
root:-> ./gpsupdate 0 0 0 0 0
set new GPS Location
root:-> cd proj4
root:~/proj4> cat foo. txt
root:~/proj4> cat bar.txt
cat: bar.txt: Permission denied
root:~/proj4> cat test3.txt
cat: test3.txt: Permission denied
```
location이 다른 file 3개를 생성한 뒤 file_loc과 gpsupdate를 통해 permission을 확인해본 결과이다.   
우선 location이 잘 설정된 것이 확인된다.   
그 후 (0,0)으로 gps를 업데이트 한 뒤, 정확히 그 위치의 Location을 가진 foo.txt에 접근이 가능해진 것이 확인된다. 나머지 파일들에 대해서는 permission denied가 확인된다.

## 4. Lessons learned
- 파일 시스템의 구현을 이론 시간에 배운 내용과 비교해가며 더 깊이 이해해볼 수 있었다.
- floating point 연산의 구현이 까다로웠는데, 새로운 structure와 sin table의 도입 등의 경험을 통해 문제 해결 능력을 기를 수 있었던 것 같다.
- 지금까지의 OS 과제를 통해 이제는 비교적 능숙하게 system call을 구현해낼 수 있게 된 것 같다.