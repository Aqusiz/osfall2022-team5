# OS project 3
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

tizen 실행 후 테스트)
./test

## 2. High level design and implementation

### Rotation Lock Implementation

### rotation.h
```c
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
```
rotation lock에 필요한 entity를 정의한다.
lock을 잡고 있는 process와 lock을 기다리는 process가 read/write 모두 따로 관리된다.
global_rot_state는 변수명 그대로 전역적인 관리는 위한 struct이고, rotation lock과 관련된 모든 정보는 결과적으로 이 안에 들어간다. mutex는 state에 접근할 때 syncronization을 보장하기 위해 사용한다.

### rotation.c
```c
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
```
위 5가지 system call의 implementation이 주된 내용이다.

```c
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
```
sleep 상태인 process를 적절히 wake시키는 함수이다.
sleep은 기본적으로 lock을 잡지 못한 reader에게 발생하도록 설계가 되어있고, 따라서 이 함수에서는 reader의 wait list를 순회하며 깨울 만한 process를  찾는다.
조건에 맞는 reader가 존재한다면, 해당하는 process를 깨운 뒤 wait list에서 lock list로 이동시킨다.
```c
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
```
terminate시 가진 lock을 모두 release하는 함수이다.
task_struct의 tgid값을 확인하여 해당 process와 관련이 있는 lock인지 확인 할 수 있다.
따라서 모든 lock과 wait list를 순회하며 tgid값이 같은 process를 제거한다.
그 후에는 위에서 정의한 wake를 이용해 process의 종료로 인해 lock이 acquire가능해진 경우 다른 process가 그것을 잡을 수 있도록 한다.
```c
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
```
새로운 degree를 설정해 준다. 
이후 변경된 degree에 의해 lock을 잡을 수 있게 되면 잡을 수 있도록 process를 wake한다.
```c
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
```
reader lock을 잡는 함수이다. 
lock을 못잡는 경우는 degree가 맞지 않는 경우, 이미 writer가 lock을 잡고 있는 경우, writer가 lock을 기다리는 경우가 있다. 이 경우에는 reader wait list에 자신을 추가한다. (reader / writer problem에서와 같이 starvation을 방지하기 위해 writer가 lock을 잡고자 하는 요구를 우선적으로 수용하도록 한 구조이다.)
그 외에는 lock을 잡을 수 있는 것이므로, reader lock list에 자신을 추가한다.
```c
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
```
writer lock을 잡는 함수이다.
writer가 lock을 못잡는 경우는 degree가 맞지 않는 경우, 이미 lock을 잡고 있는 writer가 있을 경우, 이미 lock을 기다리고 있는 reader/writer가 있는 경우이다. 이 경우에는 lock을 기다리는 리스트에 자신을 추가한다.
그 외에는 lock을 잡을 수 있으므로, writer lock list에 자신을 추가한다.
```c
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
```
read lock을 release하는 함수이다.
read lock list를 순회하며 tgid를 비교하여 자신이 잡은 lock을 range 내에서 제거한다.
lock을 release하였으므로 다른 process의 wake를 마지막에 시도한다.
```c
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
```
write lock을 release하는 함수이다. read의 것과 거의 동일한 구현이다.
## 3. Investigation of Rotation Lock

### trial.c
```c
void factorize (int num) {
    int first_flag = 1;
    int div = 2;
    while (num > 1) {
        while (num % div == 0) {
            num /= div;
            if (first_flag) {
                printf("%d", div);
                first_flag = 0;
            } else {
                printf(" * %d", div);
            }
        }
        div++;
    }
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        printf("No argument\n");
        return 1;
    }

    int id = atoi(argv[1]);

    FILE *fp;
    int ret;
    while(1) {
        ret = ROTLOCK_READ(90, 90);
        fp = fopen("data", "rt");

        if (fp == NULL) {
            printf("fopen error\n");
            return 1;
        }

        int num;
        fscanf(fp, "%d", &num);
        printf("trial-%d: %d ", id, num);
        factorize(num);
        printf("\n");

        fclose(fp);
        ROTUNLOCK_READ(90, 90);
    }

    return 0;
}
```
### selector.c
```c
int main(int argc, char *argv[]) {
    if (argc == 1) {
        printf("No argument.\n");
        return 1;
    }

    int num = atoi(argv[1]);

    FILE *fp;
    int ret;
    while(1) {
        ret = ROTLOCK_WRITE(90, 90);
        fp = fopen("data", "wt");

        if (fp == NULL) {
            printf("fopen error\n");
            return 1;
        }
        printf("selector: %d\n", num);
        fprintf(fp, "%d\n", num++);
        fclose(fp);
        ROTUNLOCK_WRITE(90, 90);
    }

    return 0;
}
```
rotation 범위를 90,90으로만 주었다.
process가 활발하게 동작하다가 잠시 멈추는 현상이 반복되므로 range에 따라 lock을 잡거나 못잡는다는 것을 확인 할 수 있었다.
자세한 것은 demo video에서 확인할 수 있다.

## 4. Lessons learned

- stravation 문제의 해결 방식을 직접 적용해 보려니 생각보다 고려해야 할 케이스가 너무 많았었다. 실제로 deadlock이나 starvation을 해결하는 것은 굉장히 어렵다고 느꼈다.
- rotation lock이라는 새로운 lock의 방식에 대해 알아볼 수 있었다.