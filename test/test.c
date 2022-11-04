#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sched.h>
#include <uapi/linux/sched.h>
#include <time.h>
#include <sys/syscall.h>

void prime_factor(int n)
{
    int cur = n;
    int bound = (int)sqrt(n);
    int *cnt = (int *)calloc(bound + 1, sizeof(int));
    short found = 0;
    int i = 2;
    while (i <= bound)
    {
        if (cur % i == 0)
        {
            cnt[i]++;
            cur /= i;
        }
        else
        {
            i++;
        }
    }
    printf("%d's prime factorization is : ", n);
    for (int j = 2; j <= bound; j++)
    {
        if (cnt[j])
            printf("%d^%d x", j, cnt[j]);
    }
    printf("%d",cur);
    printf("\n");
}
int main(int argc, char *argv[])
{
    pid_t pid = getpid();
    srand(time(NULL));
    // generate weight between 1~20
    int random_weight = rand() % 20 + 1;
    int big_prime = 1000000007;

    const struct sched_param params = {0};

    int res = sched_setscheduler(pid, SCHED_WRR, &params);
    res = sched_getscheduler(pid);
    if (res < 0)
        return 0;
    if (res = syscall(398, pid, random_weight) < 0)
        printf("weight setting failed");
    printf("check weight. random_weight : %d, syscall get_weight : %d \n", random_weight, (int)syscall(399, pid));

    clock_t start = clock();
    prime_factor(big_prime);
    clock_t end = clock();

    printf("weight : %d, time : %lf \n", random_weight, (double)end - start);
}