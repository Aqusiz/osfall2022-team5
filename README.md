# OS project 1
### Team 5
### 강휘현 김준오


## 1. How to build

커널 및 모듈 컴파일링)
커널 디렉토리(/osfall2022-team5) 내부에서 ./build-rpi3-arm64.sh

테스트 프로그램 컴파일링)
/osfall2022-team5/test 디렉토리에서 arm-linux-gnueabi-gcc -I../include test.c -o test

tizen 내부에 집어넣는 법)
sudo mount tizen-image/rootfs.img {mount directory(임의로 지정)}
ptree_mod.ko 파일과 test 실행파일을 {mount}/root 내부로 복사
sudo umount {mount}

tizen 실행) ./qemu.sh

tizen 실행 후 커널 모듈 삽입 및 테스트)
insmod ptree_mod.ko
./test
rmmod ptree_mod.ko

## 2. High level design and implementation

### Ptree implementation (ptree_mod.c)

```c
struct task_struct{
    ...
    // included in 'linux/sched.h'
}
struct prinfo
{
    int64_t state;          /* current state of process */
    pid_t pid;              /* process id */
    pid_t parent_pid;       /* process id of parent */
    pid_t first_child_pid;  /* pid of oldest child */
    pid_t next_sibling_pid; /* pid of younger sibling */
    int64_t uid;            /* user id of process owner */
    char comm[64];          /* name of program executed */
};
```
핵심이 되는 자료구조 task_struct와 prinfo이다.   
task_struct는 sibling, parent, children을 가지고 있는데, 빈 리스트는 dummy head로 표현된다.   
task_struct를 이용해 doubly linked list를 순회하고 정보를 prinfo에 담아 출력하는 구조이다.   
```c
int ptree(struct prinfo *buf, int *nr)
{
    struct task_struct *init = &init_task;
    // Error handling while get values from user space
    if (!buf || !nr) return -EINVAL;
    if (!access_ok(VERIFY_WRITE, nr, sizeof(int))) return -EFAULT;
    if (get_user(_nr, nr)) return -EFAULT;
    if (_nr < 1) return -EINVAL;

    p = (struct prinfo *) kmalloc(sizeof(struct prinfo)*_nr, GFP_KERNEL);

    read_lock(&tasklist_lock);
    dfs(init);
    read_unlock(&tasklist_lock);

    // Error handling while put values to user space
    if(cnt < _nr) _nr = cnt;
    if(put_user(_nr, nr)) return -EFAULT;
    if(copy_to_user(buf, p, sizeof(struct prinfo) * cnt)) return -EFAULT;
    kfree(p);
    return cnt;
}
```
module로서 작동하는 ptree이다.   
기본적인 예외처리를 한 후, prinfo를 담을 memory를 kernel에 kmalloc을 이용해 할당한다.   
그 후 lock을 건 다음, dfs방식으로 process tree를 순회하여 ptree를 출력한 뒤 unlock한다.   
init_task는 pid 0이기 때문에 0부터 순차적으로 출력될 것인데, 자세한 내용은 아래에서 설명한다.   
그 후 메모리를 user에게 다시 복사한 뒤, 할당한 메모리를 free한다.
```c
void dfs(struct task_struct *curr_task) {
    int i = 0;
    struct task_struct *iter;   // iterator for children list
    struct prinfo *curr_prinfo = &p[cnt];
    
    if (cnt >= _nr) return;
    cnt++;
    // Build prinfo with current task
    curr_prinfo->state = curr_task->state;
    curr_prinfo->pid = curr_task->pid;
    curr_prinfo->parent_pid = curr_task->parent->pid;
    curr_prinfo->uid = (int64_t)__kuid_val(task_uid(curr_task));
    for (i = 0; i < TASK_COMM_LEN && curr_task->comm[i] != '\0'; i++) {
        curr_prinfo->comm[i] = curr_task->comm[i];
    }
    curr_prinfo->comm[i] = '\0';
    // Check sibling list
    if (list_empty(&curr_task->sibling) 
    || list_is_last(&curr_task->sibling, &curr_task->parent->children)) {
        curr_prinfo->next_sibling_pid = 0;
    }
    else {
        temp_task = list_next_entry(curr_task, sibling);
        curr_prinfo->next_sibling_pid = temp_task->pid;
    }
    // Check children list and run recursive dfs
    if (list_empty(&curr_task->children)) curr_prinfo->first_child_pid = 0;
    else {
        temp_task = list_first_entry(&curr_task->children, struct task_struct, sibling);
        curr_prinfo->first_child_pid = temp_task->pid;
        list_for_each_entry(iter, &curr_task->children, sibling) {
            dfs(iter);
        }
    }
}
```
tree 순회를 하는 함수이다.
dfs를 재귀를 이용해 구현하였고, task struct를 순회하며 prinfo를 채워나간다.   
list_empty와 list_for_each_entry가 주요 로직인데, 각각 task_struct가 empty인지 확인해주고, 순회해준다.   
sibling을 먼저 탐색하여 같은 level의 process들을 먼저 확인하고, 그 후 child를 탐색한다. 그렇게 tree 구조로 완성해 나간다.   
```c
void *legacy_syscall = NULL;
static int ptree_mod_init(void)
{
    // Reserve legacy syscall & replace it to yourown
    legacy_syscall = compat_sys_call_table[398];
    compat_sys_call_table[398] = ptree;
    printk("module loaded\n");

    return 0;
}

static void ptree_mod_exit(void)
{
    // Revert syscall to legacy
    compat_sys_call_table[398] = legacy_syscall;
    printk("module exit\n");
}

module_init(ptree_mod_init);
module_exit(ptree_mod_exit);
```
kernel module로서 작성하였기 때문에 module을 집어넣고 삭제하는 과정 또한 필요하다.
insmod시 ptree_mod_init이 실행되어 기존 398번 ptree syscall이 legacy로 저장되고 새로 작성한 ptree로 교체된다.
rmmod시 398번 syscall을 lecagy에 저장해둔 기존 ptree syscall로 교체한다.

### Test code Implementation (test.c)

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

struct prinfo {
  int64_t state;            /* current state of process */
  pid_t   pid;              /* process id */
  pid_t   parent_pid;       /* process id of parent */
  pid_t   first_child_pid;  /* pid of oldest child */
  pid_t   next_sibling_pid; /* pid of younger sibling */
  int64_t uid;              /* user id of process owner */
  char    comm[64];         /* name of program executed */
};

int main(int argc, char **argv) {
    int cnt;
    int nr = 100;
    struct prinfo *p;
    p = (struct prinfo *) malloc(sizeof(struct prinfo) * nr);

    cnt = syscall(398, p, &nr);

    for (int i = 0; i < nr; i++) {
      printf("%s,%d,%lld,%d,%d,%d,%lld\n", p[i].comm, p[i].pid, p[i].state, p[i].parent_pid, p[i].first_child_pid, p[i].next_sibling_pid, p[i].uid);
    }
}
```
insmod를 실행한 뒤 main을 실행하야 한다.   
syscall을 이용해 prinfo와 cnt를 가져오고, 그것을 출력해 ptree 구조를 확인한다.   

## 3. Investigation of process tree
```
swapper/0,0,0,0,1,0,0
systemd,1,1,0,133,2,0
systemd-journal,133,1,1,0,170,0
systemd-udevd,170,1,1,0,173,0
dbus-daemon,173,1,1,0,199,81
dlog_logger,199,1,1,0,200,1901
amd,200,1,1,0,213,301
buxton2d,213,1,1,0,217,375
key-manager,217,1,1,0,227,444
systemd-logind,227,1,1,0,228,0
license-manager,228,1,1,0,235,402
cynara,235,1,1,0,238,401
resourced-headl,238,1,1,0,246,0
security-manage,246,1,1,266,252,0
security-manage,266,1,246,0,0,0
login,252,1,1,340,258,0
bash,340,1,252,407,0,0
test,407,0,340,0,0,0
alarm-server,258,1,1,0,260,301
oded,260,1,1,0,264,0
deviced,264,1,1,0,265,0
device-certific,265,1,1,0,277,402
net-config,277,1,1,0,288,551
murphyd,288,1,1,0,255,451
pulseaudio,255,1,1,0,303,122
focus_server,303,1,1,0,305,451
connmand,305,1,1,0,306,551
tlm,306,1,1,317,311,0
tlm-sessiond,317,1,306,348,0,0
bash,348,1,317,0,0,5001
storaged,311,1,1,0,315,0
mm-resource-man,315,1,1,0,337,451
muse-server,337,1,1,0,345,451
systemd,345,1,1,352,0,5001
(sd-pam),352,1,345,0,362,5001
sh,362,1,345,408,395,5001
sleep,408,0,362,0,0,5001
pkg_recovery_he,395,0,345,0,405,5001
(chmod),405,0,345,0,0,5001
kthreadd,2,1,0,3,0,0
kworker/0:0,3,1026,2,0,4,0
kworker/0:0H,4,1026,2,0,5,0
kworker/u8:0,5,1026,2,0,6,0
mm_percpu_wq,6,1026,2,0,7,0
ksoftirqd/0,7,1,2,0,8,0
rcu_preempt,8,0,2,0,9,0
rcu_sched,9,1026,2,0,10,0
rcu_bh,10,1026,2,0,11,0
migration/0,11,1,2,0,12,0
cpuhp/0,12,1,2,0,13,0
cpuhp/1,13,1,2,0,14,0
migration/1,14,1,2,0,15,0
ksoftirqd/1,15,1,2,0,16,0
kworker/1:0,16,1026,2,0,17,0
kworker/1:0H,17,1026,2,0,18,0
cpuhp/2,18,1,2,0,19,0
migration/2,19,1,2,0,20,0
ksoftirqd/2,20,1,2,0,21,0
kworker/2:0,21,1026,2,0,22,0
kworker/2:0H,22,1026,2,0,23,0
cpuhp/3,23,1,2,0,24,0
migration/3,24,1,2,0,25,0
ksoftirqd/3,25,1,2,0,26,0
kworker/3:0,26,1026,2,0,27,0
kworker/3:0H,27,1026,2,0,28,0
kdevtmpfs,28,1,2,0,29,0
netns,29,1026,2,0,30,0
kworker/1:1,30,1026,2,0,31,0
kworker/0:1,31,1026,2,0,32,0
kworker/3:1,32,1026,2,0,33,0
kworker/2:1,33,1026,2,0,34,0
khungtaskd,34,1,2,0,35,0
oom_reaper,35,1,2,0,36,0
writeback,36,1026,2,0,37,0
kcompactd0,37,1,2,0,38,0
crypto,38,1026,2,0,39,0
kblockd,39,1026,2,0,40,0
cfg80211,40,1026,2,0,41,0
watchdogd,41,1026,2,0,42,0
mptcp_wq,42,1026,2,0,43,0
rpciod,43,1026,2,0,44,0
xprtiod,44,1026,2,0,45,0
kauditd,45,1,2,0,46,0
kswapd0,46,1,2,0,47,0
nfsiod,47,1026,2,0,76,0
kthrotld,76,1026,2,0,77,0
kworker/0:1H,77,1026,2,0,78,0
kworker/1:1H,78,1026,2,0,79,0
iscsi_eh,79,1026,2,0,80,0
DWC Notificatio,80,1026,2,0,81,0
ipv6_addrconf,81,1026,2,0,82,0
krfcommd,82,1,2,0,96,0
jbd2/vda-8,96,1,2,0,97,0
ext4-rsv-conver,97,1026,2,0,98,0
kworker/2:1H,98,1026,2,0,102,0
jbd2/vdd-8,102,1,2,0,103,0
ext4-rsv-conver,103,1026,2,0,105,0
kworker/3:1H,105,1026,2,0,109,0
jbd2/vdb-8,109,1,2,0,110,0
ext4-rsv-conver,110,1026,2,0,119,0
kworker/u8:1,119,1026,2,0,129,0
kworker/3:2,129,1026,2,0,138,0
kworker/u8:2,138,1026,2,0,139,0
kworker/2:2,139,1026,2,0,145,0
kworker/0:2,145,1026,2,0,152,0
kworker/1:2,152,1026,2,0,404,0
systemd-cgroups,404,0,2,0,406,0
systemd-cgroups,406,0,2,0,0,0
```
process와 pid, state, parent-pid, first-child pid, first-sibling pid, uid가 순서대로 출력된다.   
dfs 순서로 출력되는지도 확인 가능한데,child가 있으면 그 다음 프로세스 pid는 child pid이고 child pid가 0이면 그 다음 프로세스 pid는 first sibling pid가 된다.
주요 process의 hierarchy는 다음과 같다.

```
swapper/0,0,0,0,1,0,0
systemd,1,1,0,133,2,0
systemd-journal,133,1,1,0,170,0
systemd-udevd,170,1,1,0,173,0
.....
systemd-logind,227,1,1,0,228,0
....
kthreadd,2,1,0,3,0,0
kworker/0:0,3,1026,2,0,4,0
kworker/0:0H,4,1026,2,0,5,0
kworker/u8:0,5,1026,2,0,6,0
....
kworker/1:0,16,1026,2,0,17,0
kworker/1:0H,17,1026,2,0,18,0
...
```

Swapper는 pid 0으로, 최상위 process이다.     
Systemd는 system service를 관리한다. 즉, systemd로 시작하는 process들의 부모 프로세스이다.   
Kthread는 kernel thread daemon으로, 모든 kthread가 포크되는 지점이다. 즉, kworker의 부모 프로세스이다.   


## 4. Lessons learned
kernel programming의 경우 디버깅의 flow를 정립하지 않는 이상 작업이 굉장히 번거롭다는 것을 깨달았다. 테스트 코드를 이용하며 이러한 부분의 해소법을 어느정도 익힌 듯 하다.
많은 헤더파일을 읽어보며 매크로와 포인터 연산에 대한 시각을 넓힐 수 있었다.
Doubly linked list에 대해 알아보고 이용하며 linked list라는 자료구조에 대해 보다 깊게 이해할 수 있었다.

