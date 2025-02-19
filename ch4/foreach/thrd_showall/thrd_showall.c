/*
 * ch4/foreach/thrd_showall/thrd_showall.c
 ***************************************************************
 * This program is part of the source code released for the book
 *  "Linux Kernel Development Cookbook"
 *  (c) Author: Kaiwan N Billimoria
 *  Publisher:  Packt
 *  GitHub repository:
 *  https://github.com/PacktPublishing/Linux-Kernel-Development-Cookbook
 *
 * From: Ch 4 : Kernel and MM Internals - Essentials
 ****************************************************************
 * Brief Description:
 * This kernel module iterates over the task structures of all *threads*
 * currently alive on the box, printing out some details.
 * We use the do_each_thread() { ... } while_each_thread() macros to do
 * so here.
 *
 * For details, please refer the book, Ch 4.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>     /* current() */
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
#include <linux/sched/signal.h>
#endif

#define OURMODNAME   "thrd_showall"

MODULE_AUTHOR("Kaiwan N Billimoria");
MODULE_DESCRIPTION("LKDC book:ch4/foreach/thrd_showall:"
		   " demo to display all threads by iterating over the task list");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("0.1");

/* Display just this CPU's idle thread, i.e., the pid 0 task,
 * the (terribly named) 'swapper/n'; n = 0, 1, 2,...
 * Again, init_task is always the task structure of the first CPU's
 * idle thread, i.e., we're referencing swapper/0.
 */
static inline void disp_idle_thread(void)
{
	struct task_struct *t = &init_task;

	/* We know that the swapper is a kernel thread */
	pr_info("%6d %6d   0x%016lx  0x%016lx [%16s]\n",
		t->pid, t->pid, (unsigned long)t,
		(unsigned long)t->stack, t->comm);
}

static int showthrds(void)
{
	struct task_struct *g, *t;	/* 'g' : process ptr; 't': thread ptr */
	int nr_thrds = 1, total = 1;    /* total init to 1 for the idle thread */
#define BUFMAX		256
#define TMPMAX		128
	char buf[BUFMAX], tmp[TMPMAX];
	const char hdr[] =
"--------------------------------------------------------------------------------\n"
"    TGID   PID         current        stack-start      Thread Name   MT? # thrds\n"
"--------------------------------------------------------------------------------\n";

	pr_info("%s", hdr);
#if 0
	/* the tasklist_lock reader-writer spinlock for the task list 'should'
	 * be used here, but, it's not exported, hence unavailable to our
	 * kernel module */
	read_lock(&tasklist_lock);
#endif

	disp_idle_thread();

	do_each_thread(g, t) {     /* 'g' : process ptr; 't': thread ptr */
		task_lock(t);

		snprintf(buf, BUFMAX-1, "%6d %6d ", g->tgid, t->pid);

		/* task_struct addr and kernel-mode stack addr */
		snprintf(tmp, TMPMAX-1, "  0x%016lx", (unsigned long)t);
		strncat(buf, tmp, TMPMAX);
		snprintf(tmp, TMPMAX-1, "  0x%016lx", (unsigned long)t->stack);
		strncat(buf, tmp, TMPMAX);

		if (!g->mm) {	// kernel thread
		/* One might question why we don't use the get_task_comm() to
		 * obtain the task's name here; the short reason: it causes a
		 * deadlock! We shall explore this (and how to avoid it) in
		 * some detail in the chapter on Synchronization. For now, we
		 * just do it the simple way ...
		 */
			snprintf(tmp, TMPMAX-1, " [%16s]", t->comm);
		} else {
			snprintf(tmp, TMPMAX-1, "  %16s ", t->comm);
		}
		strncat(buf, tmp, TMPMAX);

		/* Is this the "main" thread of a multithreaded process?
		 * We check by seeing if (a) it's a userspace thread,
		 * (b) it's TGID == it's PID, and (c), there are >1 threads in
		 * the process.
		 * If so, display the number of threads in the overall process
		 * to the right..
		 */
		nr_thrds = get_nr_threads(g);
		if (g->mm && (g->tgid == t->pid) && (nr_thrds > 1)) {
			snprintf(tmp, TMPMAX-1, " %3d", nr_thrds);
			strncat(buf, tmp, TMPMAX);
		}

		snprintf(tmp, 2, "\n");
		strncat(buf, tmp, 2);
		pr_info("%s", buf);

		total++;
		memset(buf, 0, sizeof(buf));
		memset(tmp, 0, sizeof(tmp));
		task_unlock(t);
	} while_each_thread(g, t);
#if 0
	/* <same as above, reg the reader-writer spinlock for the task list> */
	read_unlock(&tasklist_lock);
#endif

	return total;
}

static int __init thrd_showall_init(void)
{
	int total;

	pr_debug("%s: inserted\n", OURMODNAME);
	total = showthrds();
	pr_info("%s: total # of threads on the system: %d\n",
		OURMODNAME, total);

	return 0;		/* success */
}

static void __exit thrd_showall_exit(void)
{
	pr_debug("%s: removed\n", OURMODNAME);
}

module_init(thrd_showall_init);
module_exit(thrd_showall_exit);
