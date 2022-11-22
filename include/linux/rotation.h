extern long rotlock_read(int degree, int range);
extern long rotlock_write(int degree, int range);
extern long rotunlock_read(int degree, int range);
extern long rotunlock_write(int degree, int range);
// need to think about return type and parameters
extern void do_exit();