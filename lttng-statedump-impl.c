/*
 * lttng-statedump.c
 *
 * Linux Trace Toolkit Next Generation Kernel State Dump
 *
 * Copyright 2005 Jean-Hugues Deschenes <jean-hugues.deschenes@polymtl.ca>
 * Copyright 2006-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Changes:
 *	Eric Clement:                   Add listing of network IP interface
 *	2006, 2007 Mathieu Desnoyers	Fix kernel threads
 *	                                Various updates
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/cpu.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fdtable.h>
#include <linux/swap.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/cgroup.h>
#include <../kernel/cgroup/cgroup-internal.h>

#include <lttng-events.h>
#include <lttng-tracer.h>
#include <wrapper/cgroup.h>
#include <wrapper/irqdesc.h>
#include <wrapper/spinlock.h>
#include <wrapper/fdtable.h>
#include <wrapper/nsproxy.h>
#include <wrapper/irq.h>
#include <wrapper/tracepoint.h>
#include <wrapper/genhd.h>
#include <wrapper/file.h>
#include <wrapper/time.h>

#ifdef CONFIG_LTTNG_HAS_LIST_IRQ
#include <linux/irq.h>
#endif

/* Define the tracepoints, but do not build the probes */
#define CREATE_TRACE_POINTS
#define TRACE_INCLUDE_PATH instrumentation/events/lttng-module
#define TRACE_INCLUDE_FILE lttng-statedump
#define LTTNG_INSTRUMENTATION
#include <instrumentation/events/lttng-module/lttng-statedump.h>

DEFINE_TRACE(lttng_statedump_block_device);
DEFINE_TRACE(lttng_statedump_end);
DEFINE_TRACE(lttng_statedump_interrupt);
DEFINE_TRACE(lttng_statedump_file_descriptor);
DEFINE_TRACE(lttng_statedump_start);
DEFINE_TRACE(lttng_statedump_process_state);
DEFINE_TRACE(lttng_statedump_network_interface);

struct lttng_fd_ctx {
	char *page;
	struct lttng_session *session;
	struct task_struct *p;
	struct files_struct *files;
};

/*
 * Protected by the trace lock.
 */
static struct delayed_work cpu_work[NR_CPUS];
static DECLARE_WAIT_QUEUE_HEAD(statedump_wq);
static atomic_t kernel_threads_to_run;

enum lttng_thread_type {
	LTTNG_USER_THREAD = 0,
	LTTNG_KERNEL_THREAD = 1,
};

enum lttng_execution_mode {
	LTTNG_USER_MODE = 0,
	LTTNG_SYSCALL = 1,
	LTTNG_TRAP = 2,
	LTTNG_IRQ = 3,
	LTTNG_SOFTIRQ = 4,
	LTTNG_MODE_UNKNOWN = 5,
};

enum lttng_execution_submode {
	LTTNG_NONE = 0,
	LTTNG_UNKNOWN = 1,
};

enum lttng_process_status {
	LTTNG_UNNAMED = 0,
	LTTNG_WAIT_FORK = 1,
	LTTNG_WAIT_CPU = 2,
	LTTNG_EXIT = 3,
	LTTNG_ZOMBIE = 4,
	LTTNG_WAIT = 5,
	LTTNG_RUN = 6,
	LTTNG_DEAD = 7,
};

static
int lttng_enumerate_block_devices(struct lttng_session *session)
{
	struct class *ptr_block_class;
	struct device_type *ptr_disk_type;
	struct class_dev_iter iter;
	struct device *dev;

	ptr_block_class = wrapper_get_block_class();
	if (!ptr_block_class)
		return -ENOSYS;
	ptr_disk_type = wrapper_get_disk_type();
	if (!ptr_disk_type) {
		return -ENOSYS;
	}
	class_dev_iter_init(&iter, ptr_block_class, NULL, ptr_disk_type);
	while ((dev = class_dev_iter_next(&iter))) {
		struct disk_part_iter piter;
		struct gendisk *disk = dev_to_disk(dev);
		struct hd_struct *part;

		/*
		 * Don't show empty devices or things that have been
		 * suppressed
		 */
		if (get_capacity(disk) == 0 ||
		    (disk->flags & GENHD_FL_SUPPRESS_PARTITION_INFO))
			continue;

		disk_part_iter_init(&piter, disk, DISK_PITER_INCL_PART0);
		while ((part = disk_part_iter_next(&piter))) {
			char name_buf[BDEVNAME_SIZE];
			char *p;

			p = wrapper_disk_name(disk, part->partno, name_buf);
			if (!p) {
				disk_part_iter_exit(&piter);
				class_dev_iter_exit(&iter);
				return -ENOSYS;
			}
			trace_lttng_statedump_block_device(session,
					part_devt(part), name_buf);
		}
		disk_part_iter_exit(&piter);
	}
	class_dev_iter_exit(&iter);
	return 0;
}

#ifdef CONFIG_INET

static
void lttng_enumerate_device(struct lttng_session *session,
		struct net_device *dev)
{
	struct in_device *in_dev;
	struct in_ifaddr *ifa;

	if (dev->flags & IFF_UP) {
		in_dev = in_dev_get(dev);
		if (in_dev) {
			for (ifa = in_dev->ifa_list; ifa != NULL;
			     ifa = ifa->ifa_next) {
				trace_lttng_statedump_network_interface(
					session, dev, ifa);
			}
			in_dev_put(in_dev);
		}
	} else {
		trace_lttng_statedump_network_interface(
			session, dev, NULL);
	}
}

static
int lttng_enumerate_network_ip_interface(struct lttng_session *session)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, dev)
		lttng_enumerate_device(session, dev);
	read_unlock(&dev_base_lock);

	return 0;
}
#else /* CONFIG_INET */
static inline
int lttng_enumerate_network_ip_interface(struct lttng_session *session)
{
	return 0;
}
#endif /* CONFIG_INET */

static
int lttng_dump_one_fd(const void *p, struct file *file, unsigned int fd)
{
	const struct lttng_fd_ctx *ctx = p;
	const char *s = d_path(&file->f_path, ctx->page, PAGE_SIZE);
	unsigned int flags = file->f_flags;
	struct fdtable *fdt;

	/*
	 * We don't expose kernel internal flags, only userspace-visible
	 * flags.
	 */
	flags &= ~FMODE_NONOTIFY;
	fdt = files_fdtable(ctx->files);
	/*
	 * We need to check here again whether fd is within the fdt
	 * max_fds range, because we might be seeing a different
	 * files_fdtable() than iterate_fd(), assuming only RCU is
	 * protecting the read. In reality, iterate_fd() holds
	 * file_lock, which should ensure the fdt does not change while
	 * the lock is taken, but we are not aware whether this is
	 * guaranteed or not, so play safe.
	 */
	if (fd < fdt->max_fds && lttng_close_on_exec(fd, fdt))
		flags |= O_CLOEXEC;
	if (IS_ERR(s)) {
		struct dentry *dentry = file->f_path.dentry;

		/* Make sure we give at least some info */
		spin_lock(&dentry->d_lock);
		trace_lttng_statedump_file_descriptor(ctx->session, ctx->p, fd,
			dentry->d_name.name, flags, file->f_mode);
		spin_unlock(&dentry->d_lock);
		goto end;
	}
	trace_lttng_statedump_file_descriptor(ctx->session, ctx->p, fd, s,
		flags, file->f_mode);
end:
	return 0;
}

static
void lttng_enumerate_task_fd(struct lttng_session *session,
		struct task_struct *p, char *tmp)
{
	struct lttng_fd_ctx ctx = { .page = tmp, .session = session, .p = p };
	struct files_struct *files;

	task_lock(p);
	files = p->files;
	if (!files)
		goto end;
	ctx.files = files;
	lttng_iterate_fd(files, 0, lttng_dump_one_fd, &ctx);
end:
	task_unlock(p);
}

static
int lttng_enumerate_file_descriptors(struct lttng_session *session)
{
	struct task_struct *p;
	char *tmp;

	tmp = (char *) __get_free_page(GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* Enumerate active file descriptors */
	rcu_read_lock();
	for_each_process(p)
		lttng_enumerate_task_fd(session, p, tmp);
	rcu_read_unlock();
	free_page((unsigned long) tmp);
	return 0;
}

#if 0
/*
 * FIXME: we cannot take a mmap_sem while in a RCU read-side critical section
 * (scheduling in atomic). Normally, the tasklist lock protects this kind of
 * iteration, but it is not exported to modules.
 */
static
void lttng_enumerate_task_vm_maps(struct lttng_session *session,
		struct task_struct *p)
{
	struct mm_struct *mm;
	struct vm_area_struct *map;
	unsigned long ino;

	/* get_task_mm does a task_lock... */
	mm = get_task_mm(p);
	if (!mm)
		return;

	map = mm->mmap;
	if (map) {
		down_read(&mm->mmap_sem);
		while (map) {
			if (map->vm_file)
				ino = map->vm_file->lttng_f_dentry->d_inode->i_ino;
			else
				ino = 0;
			trace_lttng_statedump_vm_map(session, p, map, ino);
			map = map->vm_next;
		}
		up_read(&mm->mmap_sem);
	}
	mmput(mm);
}

static
int lttng_enumerate_vm_maps(struct lttng_session *session)
{
	struct task_struct *p;

	rcu_read_lock();
	for_each_process(p)
		lttng_enumerate_task_vm_maps(session, p);
	rcu_read_unlock();
	return 0;
}
#endif

#ifdef CONFIG_LTTNG_HAS_LIST_IRQ

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
#define irq_desc_get_chip(desc) get_irq_desc_chip(desc)
#endif

static
int lttng_list_interrupts(struct lttng_session *session)
{
	unsigned int irq;
	unsigned long flags = 0;
	struct irq_desc *desc;

#define irq_to_desc	wrapper_irq_to_desc
	/* needs irq_desc */
	for_each_irq_desc(irq, desc) {
		struct irqaction *action;
		const char *irq_chip_name =
			irq_desc_get_chip(desc)->name ? : "unnamed_irq_chip";

		local_irq_save(flags);
		wrapper_desc_spin_lock(&desc->lock);
		for (action = desc->action; action; action = action->next) {
			trace_lttng_statedump_interrupt(session,
				irq, irq_chip_name, action);
		}
		wrapper_desc_spin_unlock(&desc->lock);
		local_irq_restore(flags);
	}
	return 0;
#undef irq_to_desc
}
#else
static inline
int lttng_list_interrupts(struct lttng_session *session)
{
	return 0;
}
#endif

/*
 * Called with task lock held.
 */
static
void lttng_statedump_process_ns(struct lttng_session *session,
		struct task_struct *p,
		enum lttng_thread_type type,
		enum lttng_execution_mode mode,
		enum lttng_execution_submode submode,
		enum lttng_process_status status)
{
	struct nsproxy *proxy;
	struct pid_namespace *pid_ns;

	/*
	 * Back and forth on locking strategy within Linux upstream for nsproxy.
	 * See Linux upstream commit 728dba3a39c66b3d8ac889ddbe38b5b1c264aec3
	 * "namespaces: Use task_lock and not rcu to protect nsproxy"
	 * for details.
	 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0) || \
		LTTNG_UBUNTU_KERNEL_RANGE(3,13,11,36, 3,14,0,0) || \
		LTTNG_UBUNTU_KERNEL_RANGE(3,16,1,11, 3,17,0,0) || \
		LTTNG_RHEL_KERNEL_RANGE(3,10,0,229,13,0, 3,11,0,0,0,0))
	proxy = p->nsproxy;
#else
	rcu_read_lock();
	proxy = task_nsproxy(p);
#endif
	if (proxy) {
		pid_ns = lttng_get_proxy_pid_ns(proxy);
		do {
			trace_lttng_statedump_process_state(session,
				p, type, mode, submode, status, pid_ns);
			pid_ns = pid_ns->parent;
		} while (pid_ns);
	} else {
		trace_lttng_statedump_process_state(session,
			p, type, mode, submode, status, NULL);
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0) || \
		LTTNG_UBUNTU_KERNEL_RANGE(3,13,11,36, 3,14,0,0) || \
		LTTNG_UBUNTU_KERNEL_RANGE(3,16,1,11, 3,17,0,0) || \
		LTTNG_RHEL_KERNEL_RANGE(3,10,0,229,13,0, 3,11,0,0,0,0))
	/* (nothing) */
#else
	rcu_read_unlock();
#endif
}

/*
 * Called with task lock held.
 */
static
void lttng_statedump_process_cgroup(struct lttng_session *session,
		struct task_struct *p)
{
	struct css_set *css_set;
	struct cgroup_subsys_state *css;
	struct cgroup *cgrp;
	int i;

	rcu_read_lock();
	css_set = p->cgroups;

	for(i = 0; i < CGROUP_SUBSYS_COUNT; i++) {
		css = css_set->subsys[i];
		cgrp = css->cgroup;
		if (css)
			printk(KERN_INFO "LTTng cgroups: process_tid=%d; cgroup_hierarchy_id=%d; cgroup_id=%d",
					p->pid, cgrp->root->hierarchy_id, cgrp->id);	
	}

	rcu_read_unlock();
}

static
int lttng_enumerate_process_states(struct lttng_session *session)
{
	struct task_struct *g, *p;

	rcu_read_lock();
	for_each_process(g) {
		p = g;
		do {
			enum lttng_execution_mode mode =
				LTTNG_MODE_UNKNOWN;
			enum lttng_execution_submode submode =
				LTTNG_UNKNOWN;
			enum lttng_process_status status;
			enum lttng_thread_type type;

			task_lock(p);
			if (p->exit_state == EXIT_ZOMBIE)
				status = LTTNG_ZOMBIE;
			else if (p->exit_state == EXIT_DEAD)
				status = LTTNG_DEAD;
			else if (p->state == TASK_RUNNING) {
				/* Is this a forked child that has not run yet? */
				if (list_empty(&p->rt.run_list))
					status = LTTNG_WAIT_FORK;
				else
					/*
					 * All tasks are considered as wait_cpu;
					 * the viewer will sort out if the task
					 * was really running at this time.
					 */
					status = LTTNG_WAIT_CPU;
			} else if (p->state &
				(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) {
				/* Task is waiting for something to complete */
				status = LTTNG_WAIT;
			} else
				status = LTTNG_UNNAMED;
			submode = LTTNG_NONE;

			/*
			 * Verification of t->mm is to filter out kernel
			 * threads; Viewer will further filter out if a
			 * user-space thread was in syscall mode or not.
			 */
			if (p->mm)
				type = LTTNG_USER_THREAD;
			else
				type = LTTNG_KERNEL_THREAD;
			lttng_statedump_process_ns(session,
				p, type, mode, submode, status);
			lttng_statedump_process_cgroup(session, p);
			task_unlock(p);
		} while_each_thread(g, p);
	}
	rcu_read_unlock();

	return 0;
}

static
void lttng_dump_cgroup_file_param(struct lttng_session *session,
		struct cgroup *cgrp, struct cgroup_subsys_state *css,
		struct cftype *cft, struct seq_file *sf, struct kernfs_node *kn,
		struct kernfs_open_file *of) {
	if (cft->read_u64) {
		u64 param_val = cft->read_u64(css, cft);
		printk(KERN_INFO "param %s, value %llu", cft->name, param_val);
	}
	if (cft->read_s64) {
		s64 param_val = cft->read_s64(css, cft);
		printk(KERN_INFO "param %s, value %lld", cft->name, param_val);
	}
	if (cft->seq_show) {
		int seq_ret;
		void *old_kn_parent;
		void *old_kn_priv;

		printk(KERN_INFO "param %s follows", cft->name);

		of->kn = cgrp->kn;
		sf->private = of;

		/* HORRIBLE CODE FOLLOWS */
		old_kn_parent = cgrp->kn->parent;
		old_kn_priv = cgrp->kn->priv;
		cgrp->kn->parent = kn;
		kn->priv = cgrp;
		cgrp->kn->priv = cft;

		if (cft->seq_start) {
			void *v;
			loff_t pos;
			pos = 0;
			v = cft->seq_start(sf, &pos);
			while (v) {
				sf->from = 0;
				sf->count = 0;
				sf->pad_until = 0;
				sf->index = 0;
				sf->read_pos = 0;
				seq_ret = cft->seq_show(sf, v);
				if (seq_ret)
					printk(KERN_INFO "ERROR: param %s, ret %d", cft->name, seq_ret);
				else {
					printk(KERN_INFO "%s", sf->buf);
					printk(KERN_INFO "----------------");
				}
				v = cft->seq_next(sf, v, &pos);
			}
			cft->seq_stop(sf, v);
		} else {
			sf->from = 0;
			sf->count = 0;
			sf->pad_until = 0;
			sf->index = 0;
			sf->read_pos = 0;
			seq_ret = cft->seq_show(sf, NULL);
			if (seq_ret)
				printk(KERN_INFO "ERROR: param %s, ret %d", cft->name, seq_ret);
			else {
				printk(KERN_INFO "%s", sf->buf);
				printk(KERN_INFO "----------------");
			}
		}

		/* Let's clean up our mess */
		cgrp->kn->parent = old_kn_parent;
		cgrp->kn->priv = old_kn_priv;
	}
}

static
int lttng_enumerate_cgroups_states(struct lttng_session *session)
{
	struct cgroup_root *root;
	bool cgrp_dfl_visible;
	struct list_head* cgroup_roots_ptr;
	struct cgroup_subsys **cgroup_subsys;

	/* Fake seq_file variables */
	struct seq_file *fake_sf;
	struct kernfs_open_file *fake_of;
	struct kernfs_node *fake_kn;
	char *buf;
	int buf_size;

	/* Fake seq_file allocation and preparation */
	buf_size = 5000; /* Ok that's quite a lot */
	fake_sf = kmalloc(sizeof(struct seq_file), GFP_KERNEL);
	fake_of = kmalloc(sizeof(struct kernfs_open_file), GFP_KERNEL);
	fake_kn = kmalloc(sizeof(struct kernfs_node), GFP_KERNEL);
	buf = kmalloc(buf_size, GFP_KERNEL);
	fake_sf->buf = buf;
	fake_sf->size = buf_size;

	cgrp_dfl_visible = *wrapper_get_cgrp_dfl_visible();
	cgroup_roots_ptr = wrapper_get_cgroup_roots();
	cgroup_subsys = wrapper_get_cgroup_subsys();

	mutex_lock(&cgroup_mutex);
	printk(KERN_INFO "LTTng cgroups: Holding locks...\n");

	/* Iterate through the hierarchies */
	list_for_each_entry((root), cgroup_roots_ptr, root_list) {
		struct cgroup *cgrp, *d_cgrp;
		struct cgroup_subsys *ss;
		struct cgroup_subsys_state *css, *d_css;
		int ssid;
		struct cftype *cfts;
		struct cftype *cft;

		if (root == &cgrp_dfl_root && !cgrp_dfl_visible)
			continue;

		printk(KERN_INFO "Hierarchy ID %d:", root->hierarchy_id);
		cgrp = &(root->cgrp);
		if (strlen(root->name))
			printk(KERN_INFO "Hierarchy name=%s", root->name);

		for_each_subsys(ss, ssid) {
			/* Check that the subsystem ss is attached to the hierarchy */
			if (!(root->subsys_mask & (1 << ssid)))
				continue;
			if (strcmp(ss->name, "debug") == 0)
				continue;
			printk(KERN_INFO "subsystem names: %s, %s", ss->name, ss->legacy_name);				
			css = wrapper_cgroup_get_e_css(cgrp, ss);
			if (!css)
				continue;
			/* Iterate through descendant cgroup subsystems */	
			rcu_read_lock();
			wrapper_css_for_each_descendant_pre(d_css, css) {
				int ancestor_id;
				d_cgrp = d_css->cgroup;

				if (d_cgrp->id == 1)
					ancestor_id = 0;
				else
					ancestor_id = d_cgrp->ancestor_ids[d_cgrp->level - 1];

				/* Print cgroup info */
				printk(KERN_INFO "cgroup: hierarchy_id=%d; id=%d; ancestor_id=%d; name=%s",
							root->hierarchy_id, d_cgrp->id, ancestor_id, d_cgrp->kn->name);

				/* Print css info */
				if (wrapper_cgroup_on_dfl(d_cgrp)) {
					cfts = ss->dfl_cftypes;
				} else {
					cfts = ss->legacy_cftypes;
				}
				if (cfts) {
					for (cft = cfts; cft->name[0] != '\0'; cft++) {
						/* Skip cftype if flags indicate so */
						if ((cft->flags & __CFTYPE_ONLY_ON_DFL) && !wrapper_cgroup_on_dfl(d_cgrp))
							continue;
						if ((cft->flags & __CFTYPE_NOT_ON_DFL) && wrapper_cgroup_on_dfl(d_cgrp))
							continue;
						if ((cft->flags & CFTYPE_NOT_ON_ROOT) && !cgroup_parent(d_cgrp))
							continue;
						if ((cft->flags & CFTYPE_ONLY_ON_ROOT) && cgroup_parent(d_cgrp))
							continue;

						lttng_dump_cgroup_file_param(session, d_cgrp, d_css, cft, fake_sf,
									fake_kn, fake_of);
					}
				}
			}
			rcu_read_unlock();
		}
	}

	/* Cleaning */
	kfree(buf);
	kfree(fake_of);
	kfree(fake_kn);
	kfree(fake_sf);

	printk(KERN_INFO "LTTng cgroups: Releasing locks...\n");
	mutex_unlock(&cgroup_mutex);
	printk(KERN_INFO "LTTng cgroups: Locks released\n");

	return 0;
}

static
void lttng_statedump_work_func(struct work_struct *work)
{
	if (atomic_dec_and_test(&kernel_threads_to_run))
		/* If we are the last thread, wake up do_lttng_statedump */
		wake_up(&statedump_wq);
}

static
int do_lttng_statedump(struct lttng_session *session)
{
	int cpu, ret;

	trace_lttng_statedump_start(session);
	ret = lttng_enumerate_process_states(session);
	if (ret)
		return ret;
	ret = lttng_enumerate_file_descriptors(session);
	if (ret)
		return ret;
	/*
	 * FIXME
	 * ret = lttng_enumerate_vm_maps(session);
	 * if (ret)
	 * 	return ret;
	 */
	ret = lttng_list_interrupts(session);
	if (ret)
		return ret;
	ret = lttng_enumerate_network_ip_interface(session);
	if (ret)
		return ret;
	ret = lttng_enumerate_block_devices(session);
	switch (ret) {
	case 0:
		break;
	case -ENOSYS:
		printk(KERN_WARNING "LTTng: block device enumeration is not supported by kernel\n");
		break;
	default:
		return ret;
	}

	ret = lttng_enumerate_cgroups_states(session);
	if (ret)
		return ret;

	/* TODO lttng_dump_idt_table(session); */
	/* TODO lttng_dump_softirq_vec(session); */
	/* TODO lttng_list_modules(session); */
	/* TODO lttng_dump_swap_files(session); */

	/*
	 * Fire off a work queue on each CPU. Their sole purpose in life
	 * is to guarantee that each CPU has been in a state where is was in
	 * syscall mode (i.e. not in a trap, an IRQ or a soft IRQ).
	 */
	get_online_cpus();
	atomic_set(&kernel_threads_to_run, num_online_cpus());
	for_each_online_cpu(cpu) {
		INIT_DELAYED_WORK(&cpu_work[cpu], lttng_statedump_work_func);
		schedule_delayed_work_on(cpu, &cpu_work[cpu], 0);
	}
	/* Wait for all threads to run */
	__wait_event(statedump_wq, (atomic_read(&kernel_threads_to_run) == 0));
	put_online_cpus();
	/* Our work is done */
	trace_lttng_statedump_end(session);
	return 0;
}

/*
 * Called with session mutex held.
 */
int lttng_statedump_start(struct lttng_session *session)
{
	return do_lttng_statedump(session);
}
EXPORT_SYMBOL_GPL(lttng_statedump_start);

static
int __init lttng_statedump_init(void)
{
	/*
	 * Allow module to load even if the fixup cannot be done. This
	 * will allow seemless transition when the underlying issue fix
	 * is merged into the Linux kernel, and when tracepoint.c
	 * "tracepoint_module_notify" is turned into a static function.
	 */
	(void) wrapper_lttng_fixup_sig(THIS_MODULE);
	return 0;
}

module_init(lttng_statedump_init);

static
void __exit lttng_statedump_exit(void)
{
}

module_exit(lttng_statedump_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Jean-Hugues Deschenes");
MODULE_DESCRIPTION("Linux Trace Toolkit Next Generation Statedump");
MODULE_VERSION(__stringify(LTTNG_MODULES_MAJOR_VERSION) "."
	__stringify(LTTNG_MODULES_MINOR_VERSION) "."
	__stringify(LTTNG_MODULES_PATCHLEVEL_VERSION)
	LTTNG_MODULES_EXTRAVERSION);
