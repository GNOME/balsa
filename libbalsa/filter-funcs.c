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

    regfree(filter_reg->compiled);

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
void filter_free(filter *fil,
		 gpointer throwaway)
{
    if (! fil)
	return;

    if (fil->name)
	g_free(fil->name);

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
		   (GFunc)filter_free,
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

    if (! newfil)
    {
	filter_errno = FILTER_ENOMEM;
	return(NULL);
    }

    newfil->type = FILTER_NONE;
    newfil->flags = FILTER_EMPTY;
    newfil->match_fields = FILTER_EMPTY;
    newfil->match.string[0] = '\0';
#ifdef HAVE_LIBESD
    newfil->sound[0] = '\0';
#endif
    newfil->popup_text[0] = '\0';
    newfil->regex = NULL;

    return(newfil);
} /* end filter_new() */


/*
 * filter_append_regex()
 *
 * Adds a regex to the current filter.
 * Convenience wrapper around g_malloc()
 * and g_list_append()
 *
 * Arguments:
 *    filter *fil - the filter to add the regex to
 *    gchar *regex - the regular expression
 *
 * Returns:
 *    gint - 0 for success, negative on error
 */
gint filter_append_regex(filter *fil,
			 gchar *reg)
{
    filter_regex *temp;

    temp = (filter_regex*)g_malloc(sizeof(filter_regex));
    if (! temp)
    {
	filter_errno = FILTER_ENOMEM;
	return(-FILTER_ENOMEM);
    }

    temp->string = g_strdup(reg);
    temp->compiled = NULL;

    fil->regex = g_list_append(fil->regex,
			       temp);

    FILTER_SETFLAG(fil, FILTER_MODIFIED);

    return(0);
} /* end filter_append_regex() */


/*
 * filter_regcomp()
 *
 * Compiles a regex for a filter
 *
 * Arguments:
 *    filter_regex *fre - the filter_regex struct to compile
 */
void filter_regcomp(filter_regex *fre,
		    gpointer throwaway)
{
    gint rc;

    rc = regcomp(fre->compiled,
		 fre->string,
		 FILTER_REGCOMP);

    if (rc != 0)
    {
	gchar errorstring[256];

	regerror(rc,
		 fre->compiled,
		 errorstring,
		 256);

	filter_errno = FILTER_EREGSYN;
    }
    
} /* end filter_regcomp() */


/*
 * filter_compile_regexs
 *
 * Compiles all the regexs a filter has
 *
 * Arguments:
 *    filter *fil - the filter to compile
 *
 * Returns:
 *    gint - 0 for success, negative otherwise.
 */
gint filter_compile_regexs(filter *fil)
{
    /*
     * Clear filter_errno, because it is
     * the only way we know if a compile failed
     */
    filter_errno = FILTER_NOERR;

    /* ASSERT: we were only called if there were filters */
    g_list_foreach(fil->regex,
		   (GFunc)filter_regcomp,
		   NULL);
    if (filter_errno != 0)
    {
	gchar errorstring[1024];
	g_snprintf(errorstring,
		   1024,
		   "Unable to compile filter %s",
		   fil->name);
	filter_perror(errorstring);
	FILTER_CLRFLAG(fil, FILTER_ENABLED);
    }

    return(0);
}
