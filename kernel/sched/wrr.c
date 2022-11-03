#include "sched.h"

void init_wrr_rq(struct wrr_rq *wrr_rq);
static void enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flag);
static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flag);
static void yield_task_wrr(struct rq *rq);
static void check_preempt_curr_wrr(struct rq *rq, struct task_struct *p, int flag);
static struct task_struct *pick_next_task_wrr(struct rq *rq, struct task_struct *p, struct rq_flags *rf);
static void put_prev_task_wrr(struct rq *rq, struct task_struct *p);
#ifdef CONFIG_SMP
static int select_task_rq_wrr(struct task_struct *p, int task_cpu, int sd_flag, int flags);
#endif
static void set_curr_task_wrr(struct rq *rq);
static void task_tick_wrr(struct rq *rq, struct task_struct *p, int queued);
static void task_fork_wrr(struct task_struct *p);
static void prio_changed_wrr(struct rq *this_rq, struct task_struct *task, int oldprio);
static void switched_to_wrr(struct rq *this_rq, struct task_struct *task);

static void update_curr_wrr(struct rq *rq);

void trigger_load_balance_wrr(struct rq *rq);
static void __trigger_load_balance_wrr(void);

void init_wrr_rq(struct wrr_rq *wrr_rq)
{
	struct wrr_array *array;

	array = &wrr_rq->active;
	INIT_LIST_HEAD(&array->queue);

	wrr_rq->wrr_nr_running = 0;
	wrr_rq->weight_sum = 0;
	wrr_rq->load_balancing_dc = WRR_LOAD_BALANCE_PERIOD;
	raw_spin_lock_init(&wrr_rq->wrr_runtime_lock);
}

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

/*
 * Adding/removing a task to/from a rq
 */
static void enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;

	enqueue_wrr_entity(rq, wrr_se, flags);
}

static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	struct sched_wrr_entity *wrr_se = &p->wrr;

	update_curr_wrr(rq);
	dequeue_wrr_entity(rq, wrr_se, flags);
}

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

static void check_preempt_curr_wrr(struct rq *rq, struct task_struct *p, int flags)
{
	// no preemption. just resched
	resched_curr(rq);
	return;
}

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

static void put_prev_task_wrr(struct rq *rq, struct task_struct *p)
{
	update_curr_wrr(rq);
}

static int select_task_rq_wrr(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	int cpui;
	int target_cpu = cpu;
	int target_weight_sum = INT_MAX;

	rcu_read_lock();

	// https://stackoverflow.com/questions/24437724/diff-between-various-cpu-masks-linux-kernel
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

static void set_curr_task_wrr(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	p->se.exec_start = rq_clock_task(rq);
}

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

	p->wrr.time_slice = p->wrr.weight * WRR_TIMESLICE;

	if (queue->prev != queue->next)
	{
		requeue_task_wrr(rq, p);
		resched_curr(rq);
		return;
	}
}

static void task_fork_wrr(struct task_struct *p)
{
	// printk(KERN_INFO "fork %d\n", p->pid);
	return;
}

static void prio_changed_wrr(struct rq *rq, struct task_struct *p, int oldprio)
{
	// nothing to do in wrr
	return;
}

static void switched_to_wrr(struct rq *rq, struct task_struct *p)
{
	if (task_on_rq_queued(p) && rq->curr != p)
	{
		resched_curr(rq);
	}
}

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

	/* Kick cpufreq (see the comment in kernel/sched/sched.h). */
	cpufreq_update_util(rq, SCHED_CPUFREQ_RT);

	schedstat_set(curr->se.statistics.exec_max,
				  max(curr->se.statistics.exec_max, delta_exec));

	// update
	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = rq_clock_task(rq);
	cpuacct_charge(curr, delta_exec);
}

void trigger_load_balance_wrr(struct rq *rq)
{
	struct wrr_rq *wrr_rq = &rq->wrr;
	int cpu = smp_processor_id();

	if(--wrr_rq->load_balancing_dc) return;
	
	wrr_rq->load_balancing_dc = WRR_LOAD_BALANCE_PERIOD;

	if (cpu == cpumask_first(cpu_online_mask))
		__trigger_load_balance_wrr();
	
}

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

const struct sched_class wrr_sched_class = {
	.next = &fair_sched_class,
	.enqueue_task = enqueue_task_wrr,
	.dequeue_task = dequeue_task_wrr,
	.yield_task = yield_task_wrr,

	.check_preempt_curr = check_preempt_curr_wrr,

	.pick_next_task = pick_next_task_wrr,
	.put_prev_task = put_prev_task_wrr,

#ifdef CONFIG_SMP
	.select_task_rq = select_task_rq_wrr,
	.set_cpus_allowed = set_cpus_allowed_common,
#endif

	.set_curr_task = set_curr_task_wrr,
	.task_tick = task_tick_wrr,
	.task_fork = task_fork_wrr,

	.prio_changed = prio_changed_wrr,
	.switched_to = switched_to_wrr,

	.update_curr = update_curr_wrr,
};