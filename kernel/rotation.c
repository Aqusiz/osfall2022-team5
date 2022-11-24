#include <linux/kernel.h>
#include <linux/rotation.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>

/*
 * sets the current device rotation in the kernel.
 * syscall number 398 (you may want to check this number!)
 */
long set_rotation(int degree); /* 0 <= degree < 360 */
/*
 * Take a read/or write lock using the given rotation range
 * returning 0 on success, -1 on failure.
 * system call numbers 399 and 400
 */
long rotlock_read(int degree, int range);  /* 0 <= degree < 360 , 0 < range < 180 */
long rotlock_write(int degree, int range); /* degree - range <= LOCK RANGE <= degree + range */

/*
 * Release a read/or write lock using the given rotation range
 * returning 0 on success, -1 on failure.
 * system call numbers 401 and 402
 */
long rotunlock_read(int degree, int range);  /* 0 <= degree < 360 , 0 < range < 180 */
long rotunlock_write(int degree, int range); /* degree - range <= LOCK RANGE <= degree + range */

global_rot_state init_rotation = INIT_global_rot_state(init_rotation);
EXPORT_SYMBOL(init_rotation);

// iterate waiting list to wake up one in range
int wake_up_wait_list(global_rot_state *rot) {
    rot_lock_list* lock_entity;
    rot_lock_list* tmp; 
    short is_reader_on = 0;
    int unlocked = 0;

    // writer hodling a lock -> do nothing
    list_for_each_entry(lock_entity, &rot->write_lock_list.lock_list, lock_list) {
        if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
            return 0;
        }
    }

    // still other readers can hold that lock.
    list_for_each_entry(lock_entity, &rot->read_lock_list.lock_list, lock_list) {
        if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
            is_reader_on = 1;
        }
    }

    // reader is not holding a lock -> release a writer lock
    if(!is_reader_on) {
        list_for_each_entry_safe(lock_entity, tmp, &rot->write_wait_list.lock_list, lock_list) {
            if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
                unlocked += 1;
                lock_entity->wait = 1;
                wake_up_interruptible(&lock_entity->q);
                list_del(&lock_entity->lock_list);
                list_add_tail(&lock_entity->lock_list, &rot->write_lock_list.lock_list);
                return unlocked;
            }
        }
    }

    list_for_each_entry_safe(lock_entity, tmp, &rot->read_wait_list.lock_list, lock_list) {
        if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
            unlocked += 1;
            lock_entity->wait = 1;
            wake_up_interruptible(&lock_entity->q);
            list_del(&lock_entity->lock_list);
            list_add_tail(&lock_entity->lock_list, &rot->read_lock_list.lock_list);
        }
    }

    return unlocked;
}

// release holding locks, remove waiting locks
// when a thread that has holding or wating locks is terminating
void exit_rot_lock(struct task_struct * cur) {
    global_rot_state *rot = &init_rotation;
    rot_lock_list* lock_entity;
    rot_lock_list* tmp;

    mutex_lock(&rot->lock);

    list_for_each_entry_safe(lock_entity, tmp, &rot->write_lock_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
        }
    }

    list_for_each_entry_safe(lock_entity, tmp, &rot->read_lock_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
        }
    }

    list_for_each_entry_safe(lock_entity, tmp, &rot->write_wait_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
        }
    }

    list_for_each_entry_safe(lock_entity, tmp, &rot->read_wait_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
        }
    }

    wake_up_wait_list(rot);
    mutex_unlock(&rot->lock);
    return;
}

long set_rotation(int degree) { /* 0 <= degree < 360 */
    global_rot_state *rot = &init_rotation;
    int unlocked = 0;

    mutex_lock(&rot->lock);
    // update global degree
    rot->degree = degree;
    unlocked = wake_up_wait_list(rot);
    mutex_unlock(&rot->lock);

    return unlocked;
}

long rotlock_read(int degree, int range) {  /* 0 <= degree < 360 , 0 < range < 180 */
    global_rot_state *rot = &init_rotation;
    rot_lock_list* lock_entity;
    rot_lock_list* new_lock_entity;

    // invalid degree or range
    if (!(0 <= degree && degree < 360) || !(0 < range && range < 180)) {
        return -1;
    }

    new_lock_entity = (rot_lock_list *)kmalloc(sizeof(rot_lock_list), GFP_KERNEL);
    new_lock_entity->degree = degree;
    new_lock_entity->range = range;
    new_lock_entity->wait = 0;
    new_lock_entity->task_struct = current;
    init_waitq_head(&new_lock_entity->q);
    INIT_LIST_HEAD(&new_lock_entity->lock_list);

    mutex_lock(&rot->lock);

    // not in lock range -> wait
    if (!(abs(rot->degree-degree) <= range || 360-abs(rot->degree-degree) <= range)) {
        list_add_tail(&new_lock_entity->lock_list, &rot->read_wait_list.lock_list);
        mutex_unlock(&rot->lock);
        wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
        return 0;
    }

    // existing waiting writer overlapped -> wait
    list_for_each_entry(lock_entity, &rot->write_wait_list.lock_list, lock_list) {
        if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
            list_add_tail(&new_lock_entity->lock_list, &rot->read_wait_list.lock_list);
            mutex_unlock(&rot->lock);
            wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
            return 0;
        }
    }

    // existing hodling writer overlapped -> wait
    list_for_each_entry(lock_entity, &rot->write_lock_list.lock_list, lock_list) {
        if (abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range 
            || 360-abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range) {
            list_add_tail(&new_lock_entity->lock_list, &rot->read_wait_list.lock_list);
            mutex_unlock(&rot->lock);
            wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
            return 0;            
        }
    }

    list_add_tail(&new_lock_entity->lock_list, &rot->read_lock_list.lock_list);
    mutex_unlock(&rot->lock);
    return 0;
}

long rotlock_write(int degree, int range) { /* degree - range <= LOCK RANGE <= degree + range */
    global_rot_state *rot = &init_rotation;
    rot_lock_list* lock_entity;
    rot_lock_list* new_lock_entity;

    // invalid degree or range
    if (!(0 <= degree && degree < 360) || !(0 < range && range < 180)) {
        return -1;
    }

    new_lock_entity = (rot_lock_list *)kmalloc(sizeof(rot_lock_list), GFP_KERNEL);
    new_lock_entity->degree = degree;
    new_lock_entity->range = range;
    new_lock_entity->wait = 0;
    new_lock_entity->task_struct = current;
    init_waitq_head(&new_lock_entity->q);
    INIT_LIST_HEAD(&new_lock_entity->lock_list);

    mutex_lock(&rot->lock);

    // not in range -> wait
    if (!(abs(rot->degree-degree) <= range || 360-abs(rot->degree-degree) <= range)) {
        list_add_tail(&new_lock_entity->lock_list, &rot->write_wait_list.lock_list);
        mutex_unlock(&rot->lock);
        wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
        return 0;
    }

    // waiting writer exists -> wait
    list_for_each_entry(lock_entity, &rot->write_wait_list.lock_list, lock_list) {
        if (abs(rot->degree-lock_entity->degree) <= lock_entity->range || 360-abs(rot->degree-lock_entity->degree) <= lock_entity->range) {
            list_add_tail(&new_lock_entity->lock_list, &rot->write_wait_list.lock_list);
            mutex_unlock(&rot->lock);
            wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
            return 0;
        }
    }

    // holding writer exists -> wait
    list_for_each_entry(lock_entity, &rot->write_lock_list.lock_list, lock_list) {
        if (abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range 
            || 360-abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range) {
            list_add_tail(&new_lock_entity->lock_list, &rot->write_wait_list.lock_list);
            mutex_unlock(&rot->lock);
            wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
            return 0;            
        }
    }

    // holding reader exists -> wait
    list_for_each_entry(lock_entity, &rot->read_lock_list.lock_list, lock_list) {
        if (abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range 
            || 360-abs(lock_entity->degree-new_lock_entity->degree) <= lock_entity->range + new_lock_entity->range) {
            list_add_tail(&new_lock_entity->lock_list, &rot->write_wait_list.lock_list);
            mutex_unlock(&rot->lock);
            wait_event_interruptible(new_lock_entity->q, (new_lock_entity->wait == 1));
            return 0;            
        }
    }

    list_add_tail(&new_lock_entity->lock_list, &rot->write_lock_list.lock_list);
    mutex_unlock(&rot->lock);
    return 0;
}

long rotunlock_read(int degree, int range) {  /* 0 <= degree < 360 , 0 < range < 180 */
    global_rot_state *rot = &init_rotation;
    rot_lock_list* lock_entity;
    rot_lock_list* tmp;
    struct task_struct *cur;

    // invalid degree or range
    if (!(0 <= degree && degree < 360) || !(0 < range && range < 180)) {
        return -1;
    }

    cur = current;
    mutex_lock(&rot->lock);

    list_for_each_entry_safe(lock_entity, tmp, &rot->read_lock_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid &&\
            lock_entity->degree == degree && lock_entity->range == range) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
            wake_up_wait_list(rot);
            mutex_unlock(&rot->lock);
            return 0;
        }
    }

    mutex_unlock(&rot->lock);
    // -1 : nothing to unlock
    return -1;
}

long rotunlock_write(int degree, int range) { /* degree - range <= LOCK RANGE <= degree + range */
    global_rot_state *rot = &init_rotation;
    rot_lock_list* lock_entity;
    rot_lock_list* tmp;
    struct task_struct *cur;

    // invalid degree or range
    if (!(0 <= degree && degree < 360) || !(0 < range && range < 180)) {
        return -1;
    }

    cur = current;
    mutex_lock(&rot->lock);

    list_for_each_entry_safe(lock_entity, tmp, &rot->write_lock_list.lock_list, lock_list) {
        if (lock_entity->task_struct->tgid == cur->tgid &&\
            lock_entity->degree == degree && lock_entity->range == range) {
            list_del(&lock_entity->lock_list);
            kfree(lock_entity);
            wake_up_wait_list(rot);
            mutex_unlock(&rot->lock);
            return 0;
        }
    }

    mutex_unlock(&rot->lock);
    // -1 : nothing to unlock
    return -1;
}

SYSCALL_DEFINE1(set_rotation, int, degree)
{
    return set_rotation(degree);
}

SYSCALL_DEFINE2(rotlock_read, int, degree, int, range)
{
    return rotlock_read(degree, range);
}

SYSCALL_DEFINE2(rotlock_write, int, degree, int, range)
{
    return rotlock_write(degree, range);
}

SYSCALL_DEFINE2(rotunlock_read, int, degree, int, range)
{
    return rotunlock_read(degree, range);
}

SYSCALL_DEFINE2(rotunlock_write, int, degree, int, range)
{
    return rotunlock_write(degree, range);
}