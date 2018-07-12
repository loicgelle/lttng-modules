/*
 * lttng-context-mnt-ns.c
 *
 * LTTng mount namespace context.
 *
 * Copyright (C) 2009-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *               2018 Michael Jeanson <mjeanson@efficios.com>
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
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <lttng-events.h>
#include <wrapper/ringbuffer/frontend_types.h>
#include <wrapper/vmalloc.h>
#include <wrapper/namespace.h>
#include <lttng-tracer.h>

#if !defined(LTTNG_MNT_NS_MISSING_HEADER) && \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))

#include <../fs/mount.h>

static
size_t mnt_ns_get_size(size_t offset)
{
	size_t size = 0;

	size += lib_ring_buffer_align(offset, lttng_alignof(unsigned int));
	size += sizeof(unsigned int);
	return size;
}

static
void mnt_ns_record(struct lttng_ctx_field *field,
		 struct lib_ring_buffer_ctx *ctx,
		 struct lttng_channel *chan)
{
	unsigned int mnt_ns_inum;

	/*
	 * nsproxy can be NULL when scheduled out of exit.
	 */
	if (!current->nsproxy)
		mnt_ns_inum = 0;
	else
		mnt_ns_inum = current->nsproxy->mnt_ns->lttng_ns_inum;
	lib_ring_buffer_align_ctx(ctx, lttng_alignof(mnt_ns_inum));
	chan->ops->event_write(ctx, &mnt_ns_inum, sizeof(mnt_ns_inum));
}

static
void mnt_ns_get_value(struct lttng_ctx_field *field,
		struct lttng_probe_ctx *lttng_probe_ctx,
		union lttng_ctx_value *value)
{
	unsigned int mnt_ns_inum;

	/*
	 * nsproxy can be NULL when scheduled out of exit.
	 */
	if (!current->nsproxy)
		mnt_ns_inum = 0;
	else
		mnt_ns_inum = current->nsproxy->mnt_ns->lttng_ns_inum;
	value->s64 = mnt_ns_inum;
}

int lttng_add_mnt_ns_to_ctx(struct lttng_ctx **ctx)
{
	struct lttng_ctx_field *field;

	field = lttng_append_context(ctx);
	if (!field)
		return -ENOMEM;
	if (lttng_find_context(*ctx, "mnt_ns")) {
		lttng_remove_context_field(ctx, field);
		return -EEXIST;
	}
	field->event_field.name = "mnt_ns";
	field->event_field.type.atype = atype_integer;
	field->event_field.type.u.basic.integer.size = sizeof(unsigned int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.alignment = lttng_alignof(unsigned int) * CHAR_BIT;
	field->event_field.type.u.basic.integer.signedness = lttng_is_signed_type(unsigned int);
	field->event_field.type.u.basic.integer.reverse_byte_order = 0;
	field->event_field.type.u.basic.integer.base = 10;
	field->event_field.type.u.basic.integer.encoding = lttng_encode_none;
	field->get_size = mnt_ns_get_size;
	field->record = mnt_ns_record;
	field->get_value = mnt_ns_get_value;
	lttng_context_update(*ctx);
	wrapper_vmalloc_sync_all();
	return 0;
}
EXPORT_SYMBOL_GPL(lttng_add_mnt_ns_to_ctx);

#endif
