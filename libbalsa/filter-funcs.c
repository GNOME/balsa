/* -*- C -*-
 * filter-funcs.c
 *
 * Private functions for filters.
 */

#include "config.h"

#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"


/*
 * filter_init()
 *
 * Initializes the filter system.
 * This runs filter_load if a filename is provided.  This
 * should only be run at startup.
 * As of right now, it doesn't do anything else, but
 * I suspect it might in the future.
 *
 * Arguments:
 *    gchar *filter_file - the filter configuration file
 *
 * Returns:
 *    The new filter list on success, NULL for error.
 *    Sets filter_errno on error.
 */
GList *filter_init(gchar *filter_file)
{
    GList *list;
    gint rc;

    rc = filter_load(list, filter_file);
    
    if (rc < 0)
	filter_perror("Error in filter initialization");

    return(list);
} /* end filter_init() */


/*
 * filter_delete_regex()
 *
 * Frees the memory for a filter_regex.
 *
 * Arguments:
 *    filter_regex *filter_reg - the filter_regex structure to clear
 *    gpointer throwaway - unused
 */
void filter_delete_regex(filter_regex *filter_reg,
			 gpointer throwaway)
{
    if (! filter_reg)
	return;

    if (filter_reg->string)
	g_free(filter_reg->string);

    if (filter_reg->compiled)
	g_free(filter_reg->compiled);

    return;
} /* end filter_delete_regex() */


/*
 * filter_delete_filter()
 *
 * Frees the memory for a filter
 *
 * Arguments:
 *    filter *fil - the filter to delete
 *    gpointer throwaway - unused
 */
void filter_delete_filter(filter *fil,
			  gpointer throwaway)
{
    if (! fil)
	return;

    if (fil->match_string)
	g_free(fil->match_string);

    if (fil->exec_command)
	g_free(fil->exec_command);

    if (fil->regex)
    {
	g_list_foreach(fil->regex,
		       (GFunc)filter_delete_regex,
		       NULL);
	g_list_free(fil->regex);
    }

    g_free(fil);
} /* end filter_delete_filter() */


/*
 * filter_clear_filters
 *
 * Clears the entire filter list
 *
 * Arguments:
 *    GList *filter_list - the filter list to clear
 *
 * Returns:
 *    GList *NULL - a null value (we cleard it, eh?)
 */
GList *filter_clear_filters(GList *filter_list)
{
    if (! filter_list)
	return(NULL);

    g_list_foreach(filter_list,
		   (GFunc)filter_delete_filter,
		   NULL);
    g_list_free(filter_list);

    return(NULL);
} /* end filter_clear_filters() */


/*
 * filter_new()
 *
 * Allocates a new filter, zeros it, and returns a pointer
 * (convienience wrapper around g_malloc())
 *
 * Returns:
 *    filter* - pointer to the new filter
 */
filter *filter_new()
{
    filter *newfil;

    newfil = (filter*)g_malloc(sizeof(filter));

    newfil->type = FILTER_NONE;
    newfil->flags = FILTER_EMPTY;
    newfil->match_fields = FILTER_EMPTY;
    newfil->match_string = NULL;
    newfil->exec_command = NULL;
    newfil->regex = NULL;

    return(newfil);
} /* end filter_new() */
