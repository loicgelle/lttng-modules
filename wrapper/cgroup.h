#ifndef _LTTNG_WRAPPER_CGROUP_H
#define _LTTNG_WRAPPER_CGROUP_H

/*
 * wrapper/cgroup.h
 *
 * wrapper cgroup functions and data structures. Using
 * KALLSYMS to get its address when available, else we need to have a
 * kernel that exports this function to GPL modules.
 *
 * Loic Gelle
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

#include <linux/cgroup.h>

#ifdef CONFIG_KALLSYMS_ALL

#include <linux/kallsyms.h>
#include <wrapper/kallsyms.h>

#define wrapper_css_for_each_descendant_pre(pos, css)				\
	for ((pos) = wrapper_css_next_descendant_pre(NULL, (css)); (pos);	\
	     (pos) = wrapper_css_next_descendant_pre((pos), (css)))

static inline
struct cgroup_subsys_state* wrapper_cgroup_get_e_css(struct cgroup *cgroup,
							struct cgroup_subsys *ss)
{
	struct cgroup_subsys_state *(*cgroup_get_e_css_sym)(struct cgroup *cgroup,
								struct cgroup_subsys *ss);

	cgroup_get_e_css_sym = (void *) kallsyms_lookup_funcptr("cgroup_get_e_css");
	if (cgroup_get_e_css_sym) {
		return cgroup_get_e_css_sym(cgroup, ss);
	} else {
		printk_once(KERN_WARNING "LTTng: cgroup_get_e_css symbol lookup failed.\n");
		return NULL;
	}
}

static inline
bool wrapper_cgroup_on_dfl(struct cgroup *cgroup)
{
	bool (*cgroup_on_dfl_sym)(struct cgroup *cgroup);

	cgroup_on_dfl_sym = (void *) kallsyms_lookup_funcptr("cgroup_on_dfl");
	if (cgroup_on_dfl_sym) {
		return cgroup_on_dfl_sym(cgroup);
	} else {
		printk_once(KERN_WARNING "LTTng: cgroup_on_dfl symbol lookup failed.\n");
		return false;
	}
}

static inline
struct cgroup_subsys_state* wrapper_css_next_descendant_pre
							(struct cgroup_subsys_state *pos,
						    struct cgroup_subsys_state *css)
{
	struct cgroup_subsys_state *(*css_next_descendant_pre_sym)
								(struct cgroup_subsys_state *pos,
						    	struct cgroup_subsys_state *css);

	css_next_descendant_pre_sym = (void *)
								kallsyms_lookup_funcptr("css_next_descendant_pre");
	if (css_next_descendant_pre_sym) {
		return css_next_descendant_pre_sym(pos, css);
	} else {
		printk_once(KERN_WARNING "LTTng: css_next_descendant_pre symbol lookup failed.\n");
		return NULL;
	}
}

/* Helper function, (too) recently exposed in include/linux/cgroup.h */
static inline struct cgroup *cgroup_parent(struct cgroup *cgrp)
{
	struct cgroup_subsys_state *parent_css = cgrp->self.parent;

	if (parent_css)
		return container_of(parent_css, struct cgroup, self);
	return NULL;
}

static inline
bool* wrapper_get_cgrp_dfl_visible(void)
{
	return (bool*) kallsyms_lookup_dataptr("cgrp_dfl_visible");
}

static inline
struct list_head* wrapper_get_cgroup_roots(void)
{
	return (struct list_head*) kallsyms_lookup_dataptr("cgroup_roots");
}

static inline
struct cgroup_subsys** wrapper_get_cgroup_subsys(void)
{
	return (struct cgroup_subsys**) kallsyms_lookup_dataptr("cgroup_subsys");
}

#endif

#endif /* _LTTNG_WRAPPER_CGROUP_H */
