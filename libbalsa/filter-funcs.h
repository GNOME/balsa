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

void filter_delete_filter(filter *fil,
			  gpointer throwaway);
GList *filter_clear_filters(GList *filter_list);
filter *filter_new();

#endif /* _FUNCS_H */
