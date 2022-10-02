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

int nr_get;
int nr_put;

// traverses ptree by dfs.
int traverse_ptree(struct task_struct *task, struct prinfo *begin_addr)
{
    int process_cnt = 0;
}

int ptree(struct prinfo *buf, int *nr)
{
    struct prinfo *begin_addr;
    int process_cnt;
    int get_user_error;
    int put_user_error;
    int copy_to_user_error;
    nr_get = 0;
    nr_put = 0;

    // null check & address validation
    if (!buf || !nr)
    {
        return -EINVAL;
    }

    if (!access_ok(VERIFY_WRITE, nr, sizeof(int)))
    {
        return -EFAULT;
    }

    get_user_error = get_user(nr_get, nr); // error is not zero when error occured during get_user
    if (get_user_error)
    {
        return -EFAULT;
    }

    if (nr_get < 1)
    {
        return -EINVAL;
    }

    begin_addr = (struct prinfo *)kmalloc(nr_get * sizeof(struct prinfo), GFP_KERNEL);
    if (!begin_addr)
    {
        return -ENOMEM;
    }

    read_lock(&tasklist_lock);
    process_cnt = traverse_ptree(&init_task, begin_addr);
    read_unlock(&tasklist_lock);

    if (process_cnt < nr_put)
    {
        nr_put = process_cnt; // nr_put can be larger than actual number
    }
    put_user_error = put_user(nr_put, nr);
    if (put_user_error)
    {
        return -EFAULT;
    }

    // finally, copy to user memory
    copy_to_user_error = copy_to_user(buf, begin_addr, sizeof(struct prinfo) * nr_get);
    if (copy_to_user_error)
    {
        return -EFAULT;
    }

    kfree(begin_addr);
    return process_cnt;
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

module_init(hello_mod_init);
module_exit(hello_mod_exit);
