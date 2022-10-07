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