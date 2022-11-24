#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ROTLOCK_READ(degree, range) syscall(399, degree, range);
#define ROTUNLOCK_READ(degree, range) syscall(401, degree, range);

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