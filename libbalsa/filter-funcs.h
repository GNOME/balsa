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

void filter_free (filter * fil,
		  gpointer throwaway);
GList *filter_clear_filters (GList * filter_list);
filter *filter_new ();
gint filter_append_regex (filter * fil,
			  gchar * reg);

#endif /* _FUNCS_H */
