/*
 * For kernel module status and information
 *
 * Add module_name_list and l_module_name_list.
 *   Keith Owens <kaos@ocs.com.au> November 1999.
 * Björn Ekwall <bj0rn@blox.se> February 1999.
 *
 * This file is part of the Linux modutils.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _KERNEL_H
#define _KERNEL_H

#define K_SYMBOLS 1 /* Want info about symbols */
#define K_INFO 2 /* Want extended module info */
#define K_REFS 4 /* Want info about references */

struct module_stat {
	char *name;
	unsigned long addr;
	unsigned long modstruct; /* COMPAT_2_0! *//* depends on architecture? */
	unsigned long size;
	unsigned long flags;
	long usecount;
	size_t nsyms;
	struct module_symbol *syms;
	size_t nrefs;
	struct module_stat **refs;
	unsigned long status;
};

extern struct module_stat *module_stat;
extern size_t n_module_stat;
extern char *module_name_list;
extern size_t l_module_name_list;
extern struct module_symbol *ksyms;
extern size_t nksyms;
extern int k_new_syscalls;

int get_kernel_info(int type);

#endif /* _KERNEL_H */
