#include <linux/kernel.h>
#include <linux/rotation.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/semaphore.h>

int rotation = 0;

int readcnt = 0, writecnt = 0;

// 0 <= degree < 360, 0 < range < 180
// degree - range <= LOCK RANGE <= degree + range
long rotlock_read(int degree, int range);
long rotlock_write(int degree, int range);

long rotunlock_read(int degree, int range);
long rotunlock_write(int degree, int range);

// release holding locks, remove waiting locks
// when a thread that has holding or wating locks is terminating
// need to think about return type and parameters
void exit_rotlock(struct task_struct *tsk)
{
    return;
}

// sets the current device rotation in the kernel
// syscall number 398
SYSCALL_DEFINE1(set_rotation, int, degree)
{
    if (degree < 0 || degree >= 360)
        return -EINVAL;
    
    rotation = degree;
    // debug print
    printk("rotation: %d\n", rotation);

    return rotation;
}
