/* -*- C -*-
 * filter.h
 * 
 * Header for libfilter, the mail filtering porting of balsa
 *
 * Author:  Joel Becker
 */

#ifndef _FILTER_H
#define _FILTER_H

#include <sys/types.h>
#include <regex.h>
#include <glib.h>
#include "mailbox.h"


/* filter match types */
typedef enum
{
    FILTER_NONE = 0,
    FILTER_SIMPLE,
    FILTER_REGEX,
    FILTER_EXEC
} filter_match_type;

typedef enum
{
    FILTER_MATCHES,
    FILTER_NOMATCH,
    FILTER_ALWAYS,
} filter_when_type;
	

/* filter_run_dialog() modes */
#define FILTER_RUN_SINGLE    0
#define FILTER_RUN_MULTIPLE  1

/*
 * filter error codes
 * (not an enum cause they *have* to match filter_errlist
 */
#define FILTER_NOERR         0
#define FILTER_ENOFILE       1
#define FILTER_ENOREAD       2
#define FILTER_EFILESYN      3
#define FILTER_ENOMSG        4
#define FILTER_ENOMEM        5
#define FILTER_EREGSYN       6


/*
 * Filter errors set the variable filter_errno (like errno)
 */
gint filter_errno;

/* filters */
typedef struct _filter
{
    gint group;
    gchar *name;
    filter_match_type type;
    filter_when_type match_when;
    guint flags;

    /*
     * This should perhaps be a union, especially to 
     * share the string areas.
     * Also, I am tempted to provide limits for match_string
     * and exec_command, sort of like this:
     * union
     * {
     *     gchar match_string[1024];
     *     gchar exec_command[1024];
     * } _filter_strings;
     */
    guint match_fields; /* for FILTER_SIMPLE filters */
    gchar *match_string; /* for FILTER_SIMPLE filters */

    gchar *exec_command; /* for FILTER_EXEC filters */

    /* other options I haven't thought of yet */

    GList *regex;
} filter;

/*
 * Exported filter functions
 */
GList *filter_init(gchar *filter_file);
gint filter_load(GList *filter_list,
		 gchar *filter_file);
gint filter_save(GList *filter_list,
		 gchar *filter_file);
gint filter_run_all(GList *filter_list,
		    Message *message);
gint filter_run_group(GList *filter_list,
		      Message *message, gint group);
gint filter_run_nth(GList *filter_list,
		    Message *message, gint n);
gint filter_run_single(filter *filt,
		       Message *message);

/*
 * Dialog calls
 */
void filter_edit_dialog(GList *filter_list);
void filter_run_dialog(GList *filter_list,
		       guint mode);

/*
 * Error calls
 */
gchar *filter_strerror(gint filter_errnum);
void filter_perror(const gchar *s);

#endif /* _FILTER_H */
