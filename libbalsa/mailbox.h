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

#ifndef __LIBBALSA_MAILBOX_H__
#define __LIBBALSA_MAILBOX_H__

#include <glib.h>
#include <gtk/gtk.h>

#include "libbalsa.h"

#define LIBBALSA_TYPE_MAILBOX			(libbalsa_mailbox_get_type())
#define LIBBALSA_MAILBOX(obj)			(GTK_CHECK_CAST ((obj), LIBBALSA_TYPE_MAILBOX, LibBalsaMailbox))
#define LIBBALSA_MAILBOX_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX, LibBalsaMailboxClass))
#define LIBBALSA_IS_MAILBOX(obj)		(GTK_CHECK_TYPE ((obj), LIBBALSA_TYPE_MAILBOX))
#define LIBBALSA_IS_MAILBOX_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX))

/*
 * enums
 */
typedef enum {
    MAILBOX_SORT_DATE = 1,
    MAILBOX_SORT_SIZE = 2,
    MAILBOX_SORT_SUBJECT = 3,
    MAILBOX_SORT_FROM = 4,
    MAILBOX_SORT_ORDER = 5,
    MAILBOX_SORT_THREADS = 6,
    MAILBOX_SORT_RECEIVED = 7,
    MAILBOX_SORT_TO = 8,
    MAILBOX_SORT_SCORE = 9,
    MAILBOX_SORT_ALIAS = 10,
    MAILBOX_SORT_ADDRESS = 11,
    MAILBOX_SORT_MASK = 0xf,
    MAILBOX_SORT_REVERSE = (1 << 4),
    MAILBOX_SORT_LAST = (1 << 5)
} LibBalsaMailboxSort;

/*
 * structures
 */
typedef struct _LibBalsaMailboxClass LibBalsaMailboxClass;
struct _CONTEXT;

struct _LibBalsaMailbox {
    GtkObject object;

    gchar *config_prefix;	/* unique string identifying mailbox in the config file */
    gchar *name;
    /* context refers libmutt internal data.           * 
     * AVOID ACCESSING ITS CONTENT SINCE THE STRUCTURE *
     * DEFINITION MAY CHANGE WITHOUT WARNING.          */
    struct _CONTEXT* context;
    guint open_ref;

    gboolean lock;
    gboolean is_directory;
    gboolean readonly;

    glong messages;
    glong new_messages;
    GList *message_list;

    /* info fields */
    gboolean has_unread_messages;
    glong unread_messages;	/* number of unread messages in the mailbox */
    glong total_messages;	/* total number of messages in the mailbox  */

    /* Mailing list contained in this mailbox. Or NULL */
    LibBalsaAddress *mailing_list_address;
};

struct _LibBalsaMailboxClass {
    GtkObjectClass parent_class;

    /* Signals */
    void (*open_mailbox) (LibBalsaMailbox * mailbox, gboolean append);
    void (*close_mailbox) (LibBalsaMailbox * mailbox);

    void (*message_new) (LibBalsaMailbox * mailbox,
			 LibBalsaMessage * message);
    void (*messages_new) (LibBalsaMailbox * mailbox,
			  GList * messages);
    void (*message_delete) (LibBalsaMailbox * mailbox,
			    LibBalsaMessage * message);
    void (*messages_delete) (LibBalsaMailbox * mailbox,
			     GList * messages);
    void (*message_append) (LibBalsaMailbox * mailbox,
			    LibBalsaMessage * message);
    void (*message_status_changed) (LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message);
    void (*set_unread_messages_flag) (LibBalsaMailbox * mailbox,
				      gboolean flag);

    /* Virtual Functions */
    FILE *(*get_message_stream) (LibBalsaMailbox * mailbox,
				 LibBalsaMessage * message);
    void (*check) (LibBalsaMailbox * mailbox);
    void (*save_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    void (*load_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
};

GtkType libbalsa_mailbox_get_type(void);

LibBalsaMailbox *libbalsa_mailbox_new_from_config(const gchar * prefix);

/* 
 * open and close a mailbox 
 */
/* XXX these need to return a value if they failed */
void libbalsa_mailbox_open(LibBalsaMailbox * mailbox, gboolean append);
void libbalsa_mailbox_close(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_load_messages(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_free_messages(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean has_unread);

FILE *libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
					  LibBalsaMessage * message);

/*
 * sorting mailbox
 */
void libbalsa_mailbox_sort(LibBalsaMailbox * mailbox,
			   LibBalsaMailboxSort sort);

gint libbalsa_mailbox_commit_changes(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_check(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix);
void libbalsa_mailbox_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix);

/*
 * misc mailbox releated functions
 */
GtkType libbalsa_mailbox_type_from_path(const gchar * filename);

#endif				/* __LIBBALSA_MAILBOX_H__ */
