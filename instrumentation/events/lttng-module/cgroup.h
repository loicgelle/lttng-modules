#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup

#if !defined(LTTNG_TRACE_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define LTTNG_TRACE_CGROUP_H

#include <probes/lttng-tracepoint-event.h>
#include <linux/cgroup.h>

LTTNG_TRACEPOINT_EVENT_CLASS(cgroup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root),

	TP_FIELDS(
		ctf_integer(    int,    root, root->hierarchy_id)
		ctf_integer(    int,    ss_mask, root->subsys_mask)
		ctf_string(     name,   root->name)
	)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup_root, cgroup_setup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup_root, cgroup_destroy_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup_root, cgroup_remount,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)

)

LTTNG_TRACEPOINT_EVENT_CLASS_CODE(cgroup,

	TP_PROTO(struct cgroup *cgrp),

	TP_ARGS(cgrp),

	TP_locvar(
		char path_buf [255];
		unsigned int path_length;
	),

	TP_code_pre(
		if (cgrp->kn)
		{
			cgroup_path(cgrp, tp_locvar->path_buf, 255);
			tp_locvar->path_length = cgroup_path(cgrp, NULL, 0) + 1;
		}
		else {
			strcpy(tp_locvar->path_buf, "(null)");
			tp_locvar->path_length = strlen("(null)");
		}	
	),

	TP_FIELDS(
		ctf_integer(int,        root,   cgrp->root->hierarchy_id)
		ctf_integer(int,        id,     cgrp->id)
		ctf_integer(int,        level,  cgrp->level)	
		ctf_sequence_text(char, path,   tp_locvar->path_buf, size_t, tp_locvar->path_length)
	),
    
	TP_code_post()

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup, cgroup_mkdir,

	TP_PROTO(struct cgroup *cgroup),

	TP_ARGS(cgroup)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup, cgroup_rmdir,

	TP_PROTO(struct cgroup *cgroup),

	TP_ARGS(cgroup)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup, cgroup_release,

	TP_PROTO(struct cgroup *cgroup),

	TP_ARGS(cgroup)

) 

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup, cgroup_rename,

	TP_PROTO(struct cgroup *cgroup),

	TP_ARGS(cgroup)

)

LTTNG_TRACEPOINT_EVENT_CLASS_CODE(cgroup_migrate,

	TP_PROTO(struct cgroup *dst_cgrp, struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, task, threadgroup),

	TP_locvar(
		char dst_path_buf [255];
		unsigned int dst_path_length;
	),

	TP_code_pre(
		if (dst_cgrp->kn)
		{
			cgroup_path(dst_cgrp, tp_locvar-> dst_path_buf, 255);
			tp_locvar-> dst_path_length = cgroup_path(dst_cgrp, NULL, 0) + 1 ;
		}
		else {
			strcpy(tp_locvar->dst_path_buf, "(null)");
			tp_locvar->dst_path_length = strlen("(null)");
		}
			
	),

	TP_FIELDS(
		ctf_integer(int, dst_root,  dst_cgrp->root->hierarchy_id)
		ctf_integer(int, dst_id,    dst_cgrp->id)
		ctf_integer(int, dst_level, dst_cgrp->level)

		ctf_sequence_text(char, dst_path, tp_locvar->dst_path_buf, size_t, tp_locvar->dst_path_length)
	
		ctf_integer(int, pid, task->pid)
		ctf_string(comm, task->comm)
	),

	TP_code_post()

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup_migrate, cgroup_attach_task,

	TP_PROTO(struct cgroup *dst_cgrp, struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, task, threadgroup)

)

LTTNG_TRACEPOINT_EVENT_INSTANCE(cgroup_migrate, cgroup_transfer_tasks,

	TP_PROTO(struct cgroup *dst_cgrp, struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, task, threadgroup)

)

LTTNG_TRACEPOINT_EVENT(cgroup_file_write_fail,

	TP_PROTO(struct cgroup *cgrp,
		struct cftype *cft,
		int ret),

	TP_ARGS(cgrp, cft, ret),

	TP_FIELDS(
		ctf_integer(int, root, cgrp->root->hierarchy_id)
		ctf_integer(int, id, cgrp->id)
		ctf_string(ss_name, cft->ss->name)
		ctf_string(name, cft->name)
		ctf_integer(int, ret, ret)
	)

)

LTTNG_TRACEPOINT_EVENT(cgroup_file_write_u64,

	TP_PROTO(struct cgroup *cgrp,
		struct cftype *cft,
		u64 val,
		int ret),

	TP_ARGS(cgrp, cft, val, ret),

	TP_FIELDS(
		ctf_integer(int, root, cgrp->root->hierarchy_id)
		ctf_integer(int, id, cgrp->id)
		ctf_string(ss_name, cft->ss->name)
		ctf_string(name, cft->name)
		ctf_integer(u64, val, val)
		ctf_integer(int, ret, ret)
	)

)

LTTNG_TRACEPOINT_EVENT(cgroup_file_write_s64,

	TP_PROTO(struct cgroup *cgrp,
		struct cftype *cft,
		s64 val,
		int ret),

	TP_ARGS(cgrp, cft, val, ret),

	TP_FIELDS(
		ctf_integer(int, root, cgrp->root->hierarchy_id)
		ctf_integer(int, id, cgrp->id)
		ctf_string(ss_name, cft->ss->name)
		ctf_string(name, cft->name)
		ctf_integer(s64, val, val)
		ctf_integer(int, ret, ret)
	)

)

LTTNG_TRACEPOINT_EVENT(cgroup_file_write_str,

	TP_PROTO(struct cgroup *cgrp,
		struct cftype *cft,
		char *buf,
		int ret),

	TP_ARGS(cgrp, cft, buf, ret),

	TP_FIELDS(
		ctf_integer(int, root, cgrp->root->hierarchy_id)
		ctf_integer(int, id, cgrp->id)
		ctf_string(ss_name, cft->ss->name)
		ctf_string(name, cft->name)
		ctf_string(str, buf)
		ctf_integer(int, ret, ret)
	)

)

#endif /* if !defined(LTTNG_TRACE_CGROUP_H) || defined(TRACE_HEADER_CGROUP_READ) */

/* This part must be outside protection */
#include <probes/define_trace.h>
