#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

// not to be modified
MODULE_LICENSE("GPL v2");

// For utilizing kernel symbols
extern void *compat_sys_call_table[];
extern rwlock_t tasklist_lock;

// not to be modified
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

int _nr = 0, cnt = 0;
struct prinfo *p;
struct task_struct *temp_task;

void dfs(struct task_struct *curr_task) {
    if (cnt >= _nr) return;
    int i = 0;
    struct task_struct *iter;
    struct prinfo *curr_prinfo = &p[cnt];
    cnt++;

    curr_prinfo->state = curr_task->state;
    curr_prinfo->pid = curr_task->pid;
    curr_prinfo->parent_pid = curr_task->parent->pid;
    curr_prinfo->uid = (int64_t)__kuid_val(task_uid(curr_task));
    for (i = 0; i < TASK_COMM_LEN && curr_task->comm[i] != '\0'; i++) {
        curr_prinfo->comm[i] = curr_task->comm[i];
    }
    curr_prinfo->comm[i] = '\0';

    if (list_empty(&curr_task->sibling) 
    || list_is_last(&curr_task->sibling, &curr_task->parent->children)) curr_prinfo->next_sibling_pid = 0;
    else {
        temp_task = list_next_entry(curr_task, sibling);
        curr_prinfo->next_sibling_pid = temp_task->pid;
    }

    if (list_empty(&curr_task->children)) curr_prinfo->first_child_pid = 0;
    else {
        temp_task = list_first_entry(&curr_task->children, struct task_struct, sibling);
        curr_prinfo->first_child_pid = temp_task->pid;
        list_for_each_entry(iter, &curr_task->children, sibling) {
            dfs(iter);
        }
    }
}

int ptree(struct prinfo *buf, int *nr)
{
    struct task_struct *init = &init_task;
    if (!buf || !nr) return -EINVAL;
    if (!access_ok(VERIFY_WRITE, nr, sizeof(int))) return -EFAULT;
    if (get_user(_nr, nr)) return -EFAULT;
    if (_nr < 1) return -EINVAL;

    p = (struct prinfo *) kmalloc(sizeof(struct prinfo)*_nr, GFP_KERNEL);

    read_lock(&tasklist_lock);
    dfs(init);
    read_unlock(&tasklist_lock);

    copy_to_user(buf, p, sizeof(struct prinfo) * cnt);
    kfree(p);
    return cnt;
}

void *legacy_syscall = NULL;
static int ptree_mod_init(void)
{
    // Reserve legacy syscall & replace it to yourown
    // We neet to check whether this way of implementation is right.
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