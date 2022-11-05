# OS project 2
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

### WRR implementation (sched.c, wrr.c ,,,)
### sched.h

```c
struct sched_wrr_entity {
	struct list_head run_list;
	unsigned int weight;
	unsigned int time_slice;
	unsigned short on_rq;
};

```
rt,cfs에서와 같이 scheduling에 필요한 entity를 정의한다.
wrr이 이용할 값은 weight, time_slice이고 rq에 들어있는지 확인하기 위해 on_rq를 추가하여 boolean처럼 이용한다.

```c
// Add wrr_rq
struct wrr_rq{
	struct wrr_array active;
	unsigned int wrr_nr_running;
	unsigned int weight_sum;
	int wrr_queued;
	struct rq *rq;
	unsigned int load_balancing_dc;

	raw_spinlock_t wrr_runtime_lock;
	struct list_head pending_tasks;
}

```
wrr의 rq를 정의한다. 마찬가지로 rt, cfs와 비슷한 구조를 가지는데 group scheduling은 고려하지 않으므로 boosted, task_group과 같은 변수는 제외한다.
active를 통해 wrr 스케줄러에 들어있는 entity를 확인할 수 있고, 나머지 변수들을 통해 weighted sum과 rq에 있는 태스크 개수를 알 수 있다.
load_balancing_dc는 load balancing을 위한 down counter로, 0이 되면 초기화됨과 동시에 load balancing이 trigger된다.

### wrr.c
전반적인 함수 구조는 rt.c와 cfs.c를 참고하였다. 다만 group scheduling을 고려하지 않아 그중 일부만 구현하였다.
함수를 구현한 뒤 sched_class struct에 함수를 할당하여 wrr scheduler class를 구현하였다.
```c
static void enqueue_wrr_entity(struct rq *rq, struct sched_wrr_entity *wrr_se, unsigned int flags)
{
	struct wrr_rq *wrr_rq = &rq->wrr;
	struct wrr_array *array = &wrr_rq->active;
	struct list_head *queue = &array->queue;

	// Actually add a task to rq
	list_add_tail(&wrr_se->run_list, queue);
	// Update entity and rq weight sum
	wrr_se->on_rq = 1;
	wrr_rq->wrr_nr_running += 1;
	wrr_rq->weight_sum += wrr_se->weight;
	add_nr_running(rq, 1);
}
static void enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;

	enqueue_wrr_entity(rq, wrr_se, flags);
}
```
entity를 rq의 끝에 추가한 뒤, rq의 running entity개수와 weighted sum을 업데이트 한다.
또한 entity의 on_rq값을 1로 만들고, nr에 1을 더한다.
```c
static void __dequeue_wrr_entity(struct rq *rq, struct sched_wrr_entity *wrr_se, unsigned int flags)
{
	struct wrr_rq *wrr_rq = &rq->wrr;

	// Pop a task from rq
	list_del_init(&wrr_se->run_list);
	// Update entity and rq weight sum
	wrr_se->on_rq = 0;
	wrr_rq->wrr_nr_running -= 1;
	wrr_rq->weight_sum -= wrr_se->weight;
	sub_nr_running(rq, 1);
}

static void dequeue_wrr_entity(struct rq *rq, struct sched_wrr_entity *wrr_se, unsigned int flags)
{
	if (wrr_se->on_rq)
		__dequeue_wrr_entity(rq, wrr_se, flags);
}
static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;

	update_curr_wrr(rq);
	dequeue_wrr_entity(rq, wrr_se, flags);
}
```
enqueue를 반대로 시행한다. entity의 포인터를 제거한 뒤, running entity개수와 weightsum개수를 빼준다.
그다음 entity의 on_rq값을 0으로 하고, nr에서 1을 뺀다.
```c
static void requeue_wrr_entity(struct wrr_rq *wrr_rq, struct sched_wrr_entity *wrr_se)
{

	if (wrr_se->on_rq)
	{
		struct wrr_array *array = &wrr_rq->active;
		struct list_head *queue = &array->queue;
		// Actually move a task back to tail
		list_move_tail(&wrr_se->run_list, queue);
	}
}

static void requeue_task_wrr(struct rq *rq, struct task_struct *p)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;
	struct wrr_rq *wrr_rq;

	wrr_rq = &rq->wrr;
	requeue_wrr_entity(wrr_rq, wrr_se);
}

// Called when a task yields cpu
static void yield_task_wrr(struct rq *rq)
{
	requeue_task_wrr(rq, rq->curr);
}

```
yield시 requeue 과정을 거친다. 단순히 active 리스트에 있던 entity를 맨 뒤로 옮긴다.
```c
static struct sched_wrr_entity *pick_next_wrr_entity(struct rq *rq,
													 struct wrr_rq *wrr_rq)
{
	struct wrr_array *array = &wrr_rq->active;
	struct sched_wrr_entity *next = NULL;
	struct list_head *queue;

	queue = &array->queue;

	// Check empty first
	if (list_empty_careful(queue->next))
	{
		return NULL;
	}

	// Simply find next and return
	next = list_entry(queue->next, struct sched_wrr_entity, run_list);
	return next;
}

static struct task_struct *_pick_next_task_wrr(struct rq *rq)
{
	struct sched_wrr_entity *wrr_se;
	struct task_struct *p;
	struct wrr_rq *wrr_rq = &rq->wrr;

	wrr_se = pick_next_wrr_entity(rq, wrr_rq);

	// Check nullity first
	if (wrr_se == NULL)
	{
		return NULL;
	}

	p = container_of(wrr_se, struct task_struct, wrr);
	p->se.exec_start = rq_clock_task(rq);

	return p;
}

static struct task_struct *pick_next_task_wrr(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	struct task_struct *p;

	if (prev->sched_class == &wrr_sched_class)
		update_curr_wrr(rq);

	put_prev_task(rq, prev);

	p = _pick_next_task_wrr(rq);

	return p;
}
```
pick_next는 다음 task를 가져온다. wrr_rq의 리스트 맨 앞의 태스크를 가져온다.
```c
static int select_task_rq_wrr(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	int cpui;
	int target_cpu = cpu;
	int target_weight_sum = INT_MAX;

	rcu_read_lock();

	for_each_online_cpu(cpui)
	{
		struct rq *rq = cpu_rq(cpui);
		struct wrr_rq *wrr_rq = &rq->wrr;

		if (cpui == WRR_CPU_EMPTY)
			continue;

		if (cpumask_test_cpu(cpui, &p->cpus_allowed))
		{
			// find the rq having smallest weight sum
			if (wrr_rq->weight_sum < target_weight_sum)
			{
				target_weight_sum = wrr_rq->weight_sum;
				target_cpu = cpui;
			}
		}
	}
	rcu_read_unlock();

	return target_cpu;
}
```
select_task는 weighted sum이 가장 작은 cpu를 찾는 과정이다. 이 때 cpu를 순회하므로 lock을 걸어야 한다.
순회하며 가장 작은 cpu를 반환하고,예외 케이스를 처리한다.
```c
// called every tick to check time slice
static void task_tick_wrr(struct rq *rq, struct task_struct *p, int queued)
{
	struct wrr_rq *wrr_rq = &rq->wrr;
	struct wrr_array *array = &wrr_rq->active;
	struct list_head *queue = &array->queue;

	update_curr_wrr(rq);

	// decrease timeslice every tick
	if (--p->wrr.time_slice)
		return;

	p->wrr.time_slice = calc_wrr_timeslice(p->wrr.weight);

	if (queue->prev != queue->next)
	{
		requeue_task_wrr(rq, p);
		resched_curr(rq);
		return;
	}
}
```
매 tick마다 호출된다. 시간의 흐름을 반영해야 하므로 time slice를 낮추어 down count한다.
이 때 time slice값이 0이 되면 time slice를 초기화시키고 requeue한다.
```c
static void update_curr_wrr(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;

	if (curr->sched_class != &wrr_sched_class)
		return;

	// calculate time delta
	delta_exec = rq_clock_task(rq) - curr->se.exec_start;
	// optimization : unlikely means most of case are true
	if (unlikely((s64)delta_exec <= 0))
		return;

	cpufreq_update_util(rq, SCHED_CPUFREQ_RT);

	schedstat_set(curr->se.statistics.exec_max,
				  max(curr->se.statistics.exec_max, delta_exec));

	// update
	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = rq_clock_task(rq);
	cpuacct_charge(curr, delta_exec);
}
```
현재 task의 runtime statistics를 업데이트한다. 실행 시간의 변화량을 구하고, 현재 task의 runtime에 더해준다.
중간에 변화량이 0 이하인 부분이 있는데, 이 부분은 대부분 true이기 때문에 unlikely로 최적화한다.

### Load Balancer Implementation

```c
static void __trigger_load_balance_wrr()
{
	int cpui, min_cpui = -1, max_cpui = -1;
	int min_weight_sum = INT_MAX, max_weight_sum = -1;
	struct rq *min_rq, *max_rq;
	struct wrr_rq *min_wrr_rq, *max_wrr_rq;
	struct list_head *head;
	struct sched_wrr_entity *wrr_se;
	int migrate_weight = -1;
	struct sched_wrr_entity *migrate_se = NULL;
	unsigned long flags;

	// lock, find max and min weighted cpu idx, and unlock
	rcu_read_lock();
	for_each_online_cpu(cpui) {
		struct rq *curr_rq = cpu_rq(cpui);
		struct wrr_rq *curr_wrr_rq = &curr_rq->wrr;
		int curr_weight_sum = curr_wrr_rq->weight_sum;

		if (cpui == WRR_CPU_EMPTY) continue;

		if (curr_weight_sum > max_weight_sum) {
			max_weight_sum = curr_weight_sum;
			max_cpui = cpui;
		}

		if (curr_weight_sum < min_weight_sum) {
			min_weight_sum = curr_weight_sum;
			min_cpui = cpui;
		}
	}
	rcu_read_unlock();
	// init max_rq, min_rq
	if (max_cpui == min_cpui) return;
	max_rq = cpu_rq(max_cpui);
	min_rq = cpu_rq(min_cpui);

	local_irq_save(flags);
	double_rq_lock(max_rq, min_rq);

	max_wrr_rq = &max_rq->wrr;
	min_wrr_rq = &min_rq->wrr;
	head = &max_wrr_rq->active.queue;
	list_for_each_entry(wrr_se, head, run_list){
		int weight = wrr_se->weight;
		struct task_struct *p = container_of(wrr_se, struct task_struct, wrr);
		// check eligiblity
		if (max_rq->curr == p) continue;
		if (!cpumask_test_cpu(min_cpui, &p->cpus_allowed)) continue;
		if (max_weight_sum - weight <= min_weight_sum + weight) continue;

		if (migrate_weight < weight) {
			migrate_weight = weight;
			migrate_se = wrr_se;
		}
	}

	if (migrate_se != NULL) {
		struct task_struct *p = container_of(migrate_se, struct task_struct, wrr);
		p->on_rq = TASK_ON_RQ_MIGRATING;
		deactivate_task(max_rq, p, DEQUEUE_NOCLOCK);
		set_task_cpu(p, min_cpui);
		activate_task(min_rq, p, ENQUEUE_NOCLOCK);
		p->on_rq = TASK_ON_RQ_QUEUED;
	}

	double_rq_unlock(max_rq, min_rq);
	local_irq_restore(flags);
	return;
}
```
Help documentation의 내용을 그대로 구현한다.
cpu를 순회해야 하므로 먼저 rcu lock을 잡고, cpu를 순회하며 최소, 최대 weight_sum을 갖는 cpu를 각각 찾는다.
그 다음 두 cpu의 run queue에 lock을 잡은 뒤 rq내에서 weight_sum을 역전시키지 않는 최대 weight task를 찾는다.
그 다음 해당 task를 migrate시킨다. 

### System Call Implementation

```c
long sched_setweight(pid_t pid, int weight)
{
	struct rq *rq;
	struct task_struct *p;
	struct rq_flags rf;
	int prev_weight;
	int euid = current_euid().val;

	if (pid < 0) return -EINVAL;
	if (weight < 1 || weight > 20) return -EINVAL;

	if((p = find_process_by_pid(pid)) == NULL) return -EINVAL;
	
	task_rq_lock(p, &rf);
	rq = task_rq(p);

	if (p->policy != SCHED_WRR) {
		task_rq_unlock(rq, p, &rf);
		return -EPERM;
	}
	if (p->wrr.weight < weight && euid != 0) {
		task_rq_unlock(rq, p, &rf);
		return -EPERM;
	}
	if (p->wrr.weight >= weight && euid != 00 && !check_same_owner(p)) {
		task_rq_unlock(rq, p, &rf);
		return -EPERM;
	}

	prev_weight = p->wrr.weight;
	if (p->wrr.on_rq) {
		rq->wrr.weight_sum += weight - prev_weight;
	}
	p->wrr.time_slice = weight*WRR_TIMESLICE;
	p->wrr.weight = weight;

	task_rq_unlock(rq, p, &rf);
	return 0;
}

long sched_getweight(pid_t pid)
{
	struct task_struct *p;

	if (pid < 0) return -EINVAL;

	p = find_process_by_pid(pid);

	if (p == NULL) return -EINVAL;
	if (p->policy != SCHED_WRR) return -EINVAL;

	return p->wrr.weight;
}
```
sched_setweight는 process의 weight를 설정한다.
set이 일어나기 전에 user가 process의 owner인지를 확인하고, 예외 케이스를 확인한다.
process가 rq에 있는 상태라면 weight_sum이 바뀌므로 같이 업데이트를 해준다. 또한 time_slice도 바뀌어야 하므로 같이 업데이트 해준다.

scehd_getweight는 예외 케이스를 제외하면 정상적으로 weight를 반환한다.

## 3. Investigation of Weighted Round Robin Scheduler
```c
int main(int argc, char *argv[])
{
    pid_t pid = getpid();
    srand(time(NULL));
    // generate weight between 1~20
    int random_weight = rand() % 20 + 1;
    long long big_prime = (long long)10000000000000007;

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

    printf("weight : %d, time : %lf \n", random_weight, (double)(end - start)/1000);
}
```

위는 random weight를 통해 큰 수를 prime factorization하는 프로그램이다. 이를 여러번 돌려가며 소요되는 시간을 관찰해 보았다.
```shell
10000000000000007's prime factorization is : 53^1 x113^1 x277^1 x1117^1 x5396507^1 x1
weight : 4, time : 187050.225000 


10000000000000007's prime factorization is : 53^1 x113^1 x277^1 x1117^1 x5396507^1 x1
weight : 8, time : 86037.420000 

10000000000000007's prime factorization is : 53^1 x113^1 x277^1 x1117^1 x5396507^1 x1
weight : 9, time : 81563.953000 


10000000000000007's prime factorization is : 53^1 x113^1 x277^1 x1117^1 x5396507^1 x1
weight : 17, time : 59726.909000 


10000000000000007's prime factorization is : 53^1 x113^1 x277^1 x1117^1 x5396507^1 x1
weight : 20, time : 59439.712000 
```
이론적으로는 weight와 소요 시간은 반비례해야한다.
weight가 커지면 시간이 줄어드는 경향성은 보이나, 명확한 비례관계를 관찰하기는 어렵다. 
그 이유를 추측해보면 다음과 같다.
1. 너무 많은 프로세스가 동시에 돌아가고 있다. 그리고 모든 프로세스가 wrr인 것은 아니기 때문에 예측대로 되지 않을 수 있다. 
2. 같은 프로그램을 여러번 실행하다 보니 caching에 의해 시간이 예측할 수 없게 변한 것일 수 있다.
3. load balancing은 lock을 해야하는데, 이러한 처리들이 비효율적이기 때문에 소요시간에 영향을 줄 수 있다.

## 4. Lessons learned

- 이전에는 system call을 모듈화하여 과정이 비교적 간단했는데, 이번에는 system call을 직접 등록하게 되었다. 그 과정에서 system call table을 포함한 linux의 낮은 레벨의 코드를 많이 읽어보아야 했는데, 이 과정에서 system call이 어떻게 등록되고 호출되는지 이해할 수 있었다.
- 다른 scheduler의 코드들을 읽어보며 이론상의 내용과 실제 구현을 비교해볼 수 있었다. 
- C에서의 클래스 구현과 이용을 배울 수 있었다.