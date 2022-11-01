#include "sched.h"

static void enqueue_task_wrr(struct rq *rq, struct task_struct *p, int flag);
static void dequeue_task_wrr(struct rq *rq, struct task_struct *p, int flag);
static void yield_task_wrr(struct rq *rq);
static bool yield_to_task_wrr(struct rq *rq, struct task_struct *p, int preempt);
static void check_preempt_curr_wrr(struct rq *rq, struct task_struct *p, int flag);
static struct task_struct * pick_next_task_wrr(struct rq *rq, struct task_struct *p, struct rq_flags *rf);
static void put_prev_task_wrr(struct rq *rq, struct task_struct *p);
#ifdef CONFIG_SMP
static int select_task_rq_wrr(struct task_struct *p, int task_cpu, int sd_flag, int flags);
static void migrate_task_rq_wrr(struct task_struct *p);
static void rq_online_wrr(struct rq *rq);
static void rq_offline_wrr(struct rq *rq);
static void task_dead_wrr(struct task_struct *p);
static void set_cpus_allowed_common(struct task_struct *p, const struct cpumask *newmask);
#endif
static void set_curr_task_wrr(struct rq *rq);
static void task_tick_wrr(struct rq *rq, task_struct *p, int queued);
static void task_fork_wrr(struct task_struct *p);
static unsigned int get_rr_interval_wrr(struct rq *rq, struct task_struct *task);
static void prio_changed_wrr(struct rq *this_rq, struct task_struct *task, int oldprio);
static void switched_to_wrr(struct rq *this_rq, struct task_struct *task);
static void switched_from_wrr(struct rq *this_rq, struct task_struct *task);
static void update_curr_wrr(struct rq *rq);

const struct sched_class wrr_sched_class = {
	.next			= &fair_sched_class,
	.enqueue_task		= enqueue_task_wrr,
	.dequeue_task		= dequeue_task_wrr,
	.yield_task		= yield_task_wrr,
//	.yield_to_task	= yield_to_task_wrr,

	.check_preempt_curr	= check_preempt_curr_wrr,

	.pick_next_task		= pick_next_task_wrr,
	.put_prev_task		= put_prev_task_wrr,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_wrr,
	.migrate_task_rq	= migrate_task_rq_wrr,

	.rq_online		= rq_online_wrr,
	.rq_offline		= rq_offline_wrr,

	.task_dead		= task_dead_wrr,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.set_curr_task          = set_curr_task_wrr,
	.task_tick		= task_tick_wrr,
	.task_fork		= task_fork_wrr,

	.get_rr_interval	= get_rr_interval_wrr,

	.prio_changed		= prio_changed_wrr,
	.switched_to		= switched_to_wrr,
	.switched_from		= switched_from_wrr,

	.update_curr		= update_curr_wrr,
};