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
}
filter_match_type;

typedef enum
{
    FILTER_MATCHES,
    FILTER_NOMATCH,
    FILTER_ALWAYS,
} filter_when_type;

typedef enum
{
    FILTER_NOTHING = 0,
    FILTER_COPY,
    FILTER_MOVE,
    FILTER_PRINT,
    FILTER_RUN,
    FILTER_TRASH
} filter_action_type;
        
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

    /* The match type fields */
    union _match
      {
        gchar string[1024];        /* for FILTER_SIMPLE */
        gchar command[1024];        /* for FILTER_EXEC */
      }
    match;
    guint match_fields;                /* for FILTER_SIMPLE filters */

    /* The notification fields */
    gchar sound[PATH_MAX];
    gchar popup_text[256];

    /* The action */
    filter_action_type action;
    gchar action_string[PATH_MAX];

    /* other options I haven't thought of yet */

    /* the regex list */
    GList *regex;
  }
filter;

/*
 * Exported filter functions
 */
GList *filter_init (gchar * filter_file);
gint filter_load (GList * filter_list,
                  gchar * filter_file);
gint filter_save (GList * filter_list,
                  gchar * filter_file);
gint filter_run_all (GList * filter_list,
                     Message * message);
gint filter_run_group (GList * filter_list,
                       Message * message, gint group);
gint filter_run_nth (GList * filter_list,
                     Message * message, gint n);
gint filter_run_single (filter * filt,
                        Message * message);
void filter_free (filter * fil,
                  gpointer throwaway);
/*
 * Dialog calls
 */
void filter_edit_dialog (GList * filter_list);
void filter_run_dialog (GList * filter_list,
                        guint mode);

/*
 * Error calls
 */
gchar *filter_strerror (gint filter_errnum);
void filter_perror (const gchar * s);

#endif /* _FILTER_H */
