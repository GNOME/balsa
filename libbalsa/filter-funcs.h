/* -*- C -*-
 * filter-funcs.h
 *
 * Various internal filter functions, not for general
 * use.
 */


#ifndef _FUNCS_H
#define _FUNCS_H

/*
 * Files including this one need to
 * #include "filter.h"
 * first.
 */

void filter_free (filter *,
		  gpointer);
GList *filter_clear_filters (GList *);
filter *filter_new (void);
gint filter_append_regex (filter *,
			  gchar *);
void filter_delete_regex (filter_regex *,
			  gpointer);
void filter_regcomp (filter_regex *,
		     gpointer);
gint filter_compile_regexs (filter *);

#endif /* _FUNCS_H */
