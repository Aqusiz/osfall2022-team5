#include <linux/rotation.h>
#include <linux/syscalls.h>

// 0 <= degree < 360, 0 < range < 180
// degree - range <= LOCK RANGE <= degree + range
long rotlock_read(int degree, int range);
long rotlock_write(int degree, int range);

long rotunlock_read(int degree, int range);
long rotunlock_write(int degree, int range);

// release holding locks, remove waiting locks
// when a thread that has holding or wating locks is terminating
void exit_rotlock();
void do_exit();
