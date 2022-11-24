#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ROTLOCK_WRITE(degree, range) syscall(400, degree, range)
#define ROTUNLOCK_WRITE(degree, range) syscall(402, degree, range)

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

        fprintf(fp, "%d\n", num++);
        fclose(fp);
        ROTUNLOCK_WRITE(90, 90);
    }

    return 0;
}