/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
/*
 * filter.h
 * 
 * Header for libfilter, the mail filtering porting of balsa
 *
 * Author:  Joel Becker
 */

#ifndef _FILTER_H
#define _FILTER_H

#include "config.h"
#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  include <sys/types.h>
#  include <regex.h>
#endif
#include <glib.h>

#include "libbalsa.h"

/* filter match types */
typedef enum {
    FILTER_NONE,
    FILTER_SIMPLE,
    FILTER_REGEX,
    FILTER_EXEC
} filter_match_type;

typedef enum {
    FILTER_MATCHES,
    FILTER_NOMATCH,
    FILTER_ALWAYS,
} filter_when_type;

typedef enum {
    FILTER_NOTHING,
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
typedef struct _filter {
    gint group;
    gchar *name;
    filter_match_type type;
    filter_when_type match_when;
    guint flags;

    /* The match type fields */
    union _match {
	gchar string[1024];	/* for FILTER_SIMPLE */
	gchar command[1024];	/* for FILTER_EXEC */
    } match;
    guint match_fields;		/* for FILTER_SIMPLE filters */

    /* The notification fields */
    gchar sound[PATH_MAX];
    gchar popup_text[256];

    /* The action */
    filter_action_type action;
    gchar action_string[PATH_MAX];

    /* other options I haven't thought of yet */

    /* the regex list */
    GList *regex;
} filter;

/*
 * Exported filter functions
 */
GList *filter_init(gchar * filter_file);
gint filter_load(GList * filter_list, gchar * filter_file);
gint filter_save(GList * filter_list, gchar * filter_file);
gint filter_run_all(GList * filter_list, LibBalsaMessage * message);
gint filter_run_group(GList * filter_list,
		      LibBalsaMessage * message, gint group);
gint filter_run_nth(GList * filter_list,
		    LibBalsaMessage * message, gint n);
gint filter_run_single(filter * filt, LibBalsaMessage * message);
void filter_free(filter * fil, gpointer throwaway);
/*
 * Dialog calls
 */
void filter_edit_dialog(GList * filter_list);
void filter_run_dialog(GList * filter_list, guint mode);

/*
 * Error calls
 */
gchar *filter_strerror(gint filter_errnum);
void filter_perror(const gchar * s);

#endif				/* _FILTER_H */
