#include <linux/module.h>
#include <linux/syscalls.h>

MODULE_LICENSE("GPL v2");

// For utilizing kernel symbols
extern void* compat_sys_call_table[];
extern rwlock_t tasklist_lock;

void* legacy_syscall = NULL;
static int hello_mod_init(void) {
  // Reserve legacy syscall & replace it to yourown
  printk("module loaded\n");

  return 0;
}

static void hello_mod_exit(void) {
  // Revert syscall to legacy
  printk("module exit\n");
}

module_init(hello_mod_init);
module_exit(hello_mod_exit);
