/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
/*
 * filter-funcs.h
 *
 * Various internal filter functions, not for general
 * use.
 */


#ifndef __FILTER_FUNCS_H__
#define __FILTER_FUNCS_H__

#include "filter.h"
#include <gtk/gtk.h>

/* Conditions definitions */

/*  match flags */
#define CONDITION_EMPTY         0       /* for initialization */
#define CONDITION_MATCH_TO      1<<0	/* match in the To: field */
#define CONDITION_MATCH_FROM    1<<1	/* match in the From: field */
#define CONDITION_MATCH_SUBJECT 1<<2	/* match in the Subject field */
#define CONDITION_MATCH_CC      1<<3	/* match in the cc: field */
#define CONDITION_MATCH_US_HEAD 1<<4    /* match in a user header */
#define CONDITION_MATCH_BODY    1<<7	/* match in the body */

/* match_fields macros */
#define CONDITION_SETMATCH(x, y) \
          ((((LibBalsaCondition*)(x))->match.string.fields) |= (y))
#define CONDITION_CLRMATCH(x, y) \
          ((((LibBalsaCondition*)(x))->match.string.fields) &= ~(y))
#define CONDITION_CHKMATCH(x, y) \
          ((((LibBalsaCondition*)(x))->match.string.fields) & (y))

/* Filter defintions */
/* filter flags */
#define FILTER_EMPTY         0	/* for clearing bitfields */

#define FILTER_VALID         1<<1	/* ready to filter (eg regex strings 
					   have been compiled with regcomp(), with no errors...) */					
#define FILTER_COMPILED      1<<2	/* the filter needs to be compiled (ie there are uncompiled regex) */

/* flag operation macros */
#define FILTER_SETFLAG(x, y) ((((LibBalsaFilter*)(x))->flags) |= (y))
#define FILTER_CLRFLAG(x, y) ((((LibBalsaFilter*)(x))->flags) &= ~(y))
#define FILTER_CHKFLAG(x, y) ((((LibBalsaFilter*)(x))->flags) & (y))

/* Conditions */

LibBalsaCondition* libbalsa_condition_new(void);

void libbalsa_conditions_free(GSList * conditions);

LibBalsaConditionRegex* libbalsa_condition_regex_new(void);
void libbalsa_condition_regex_free(LibBalsaConditionRegex *, gpointer);
void regexs_free(GSList *);
void libbalsa_condition_compile_regexs(LibBalsaCondition* cond);
gboolean libbalsa_condition_compare(LibBalsaCondition *c1,
                                    LibBalsaCondition *c2);

/* Filters */
/* Free a filter
 * free_condition is a gint into a gpointer : if <>0 the function frees filter conditions also
 */
LibBalsaFilter *libbalsa_filter_new(void);
void libbalsa_filter_free(LibBalsaFilter *, gpointer free_condition);
void libbalsa_filter_clear_filters(GSList *,gint free_conditions);
void libbalsa_filter_append_condition(LibBalsaFilter*, LibBalsaCondition *);
void libbalsa_filter_prepend_condition(LibBalsaFilter*, LibBalsaCondition*,
                                       ConditionMatchType op);
void libbalsa_filter_delete_regex(LibBalsaFilter*,LibBalsaCondition*,
                                  LibBalsaConditionRegex *, gpointer);
gboolean libbalsa_filter_compile_regexs(LibBalsaFilter *);

gboolean libbalsa_filter_export_sieve(LibBalsaFilter* fil, gchar* filename);

/* GtkTreeView helper */
GtkTreeView *libbalsa_filter_list_new(gboolean with_data,
                                      const gchar * title,
                                      GtkSelectionMode mode,
                                      GCallback selection_changed_cb,
                                      gboolean sorted);
#endif				/* __FILTER_FUNCS_H__ */
