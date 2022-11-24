#include <linux/list.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#define INIT_ROTATION(name) {\
    .degree = -1,\
    .read_wait_list = {\
        .degree = -1,\
        .range = -1,\
        .wait = 0,\
        .lock_list = LIST_HEAD_INIT(name.read_wait_list.lock_list),\
    },\
    .write_wait_list = {\
        .degree = -1,\
        .range = -1,\
        .wait = 0,\
        .lock_list = LIST_HEAD_INIT(name.write_wait_list.lock_list),\
    },\
    .read_lock_list = {\
        .degree = -1,\
        .range = -1,\
        .wait = 0,\
        .lock_list = LIST_HEAD_INIT(name.read_lock_list.lock_list),\
    },\
    .write_lock_list = {\
        .degree = -1,\
        .range = -1,\
        .wait = 0,\
        .lock_list = LIST_HEAD_INIT(name.write_lock_list.lock_list),\
    },\
    .lock = __MUTEX_INITIALIZER(name.lock),\
}

typedef struct
{
    int degree;
    int range;
    int wait;
    wait_queue_head_t q;
    struct list_head lock_list;
    struct task_struct *task_struct;
} rot_lock_list;

typedef struct
{
    int degree;
    rot_lock_list read_wait_list;
    rot_lock_list write_wait_list;
    rot_lock_list read_lock_list;
    rot_lock_list write_lock_list;
    struct mutex lock;
} global_rot_state;

extern long rotlock_read(int degree, int range);
extern long rotlock_write(int degree, int range);
extern long rotunlock_read(int degree, int range);
extern long rotunlock_write(int degree, int range);
// need to think about return type and parameters
extern void exit_rot_lock(struct task_struct * cur);