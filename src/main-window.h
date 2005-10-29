/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#include "toolbar-factory.h"

#define BALSA_TYPE_WINDOW		       (balsa_window_get_type ())
#define BALSA_WINDOW(obj)		       (GTK_CHECK_CAST (obj, BALSA_TYPE_WINDOW, BalsaWindow))
#define BALSA_WINDOW_CLASS(klass)	       (GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_WINDOW, BalsaWindowClass))
#define BALSA_IS_WINDOW(obj)		       (GTK_CHECK_TYPE (obj, BALSA_TYPE_WINDOW))
#define BALSA_IS_WINDOW_CLASS(klass)	       (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_WINDOW))

/* Type values for mailbox checking */
enum MailboxCheckType {
    TYPE_BACKGROUND,
    TYPE_CALLBACK
};

typedef struct _BalsaWindow BalsaWindow;
typedef struct _BalsaWindowClass BalsaWindowClass;

struct _BalsaWindow {
    GnomeApp window;

    GtkWidget *progress_bar;
    GtkWidget *mblist;
    GtkWidget *sos_entry;       /* SenderOrSubject filter entry */
    GtkWidget *notebook;
    GtkWidget *preview;		/* message is child */
    GtkWidget *hpaned;
    GtkWidget *vpaned;
    GtkWidget *current_index;
    GtkWidget *filter_choice;
    LibBalsaMessage *current_message;
};

struct _BalsaWindowClass {
    GnomeAppClass parent_class;

    void (*open_mbnode)  (BalsaWindow * window, BalsaMailboxNode * mbnode);
    void (*close_mbnode) (BalsaWindow * window, BalsaMailboxNode * mbnode);
    void (*identities_changed) (BalsaWindow * window);
};

/*
 * Constants for enable_empty_trash()
 */
typedef enum {
    TRASH_EMPTY, /* guaranteed to be empty */
    TRASH_FULL,  /* guaranteed to contain something */
    TRASH_CHECK  /* uncertain, better check */
} TrashState;


GtkType balsa_window_get_type(void);
GtkWidget *balsa_window_new(void);
GtkWidget *balsa_window_find_current_index(BalsaWindow * window);
void balsa_window_enable_mailbox_menus(BalsaWindow * window,
				       BalsaIndex * index);
void balsa_window_update_book_menus(BalsaWindow *window);
LibBalsaCondition* balsa_window_get_view_filter(BalsaWindow *window);
void balsa_window_refresh(BalsaWindow * window);
void balsa_window_open_mbnode(BalsaWindow * window, BalsaMailboxNode*mbnode);
void balsa_window_close_mbnode(BalsaWindow * window, BalsaMailboxNode*mbnode);
void balsa_identities_changed(BalsaWindow *bw);

void balsa_window_set_filter_label(BalsaWindow * window, gboolean to_field);
void balsa_window_update_tab(BalsaMailboxNode * mbnode);
void enable_empty_trash(BalsaWindow * window, TrashState status);
void balsa_window_enable_continue(BalsaWindow * window);
void balsa_change_window_layout(BalsaWindow *window);
gboolean mail_progress_notify_cb(void);
gboolean send_progress_notify_cb(void);
void check_new_messages_cb(GtkWidget *, gpointer data);
void check_new_messages_real(GtkWidget *, gpointer data, int type);
void check_new_messages_count(LibBalsaMailbox * mailbox, gboolean notify);
void empty_trash(BalsaWindow * window);
void update_view_menu(BalsaWindow * window);
BalsaToolbarModel *balsa_window_get_toolbar_model(void);
void balsa_window_select_all(GtkWindow * window);

/* functions to manipulate the progress bars of the window */
void balsa_window_increase_activity(BalsaWindow* window);
void balsa_window_decrease_activity(BalsaWindow* window);
gboolean balsa_window_setup_progress(BalsaWindow* window, gfloat upper_bound);
void balsa_window_clear_progress(BalsaWindow* window);
#ifdef BALSA_USE_THREADS
/* the increment model was not designed for threading and does not work. */
#define balsa_window_increment_progress(arg)
#else
void balsa_window_increment_progress(BalsaWindow* window);
#endif

#if defined(__FILE__) && defined(__LINE__)
# ifdef __FUNCTION__
#  define BALSA_DEBUG_MSG(message)  if (balsa_app.debug)  fprintf(stderr, "[%lu] %12s | %4d | %30s: %s\n", (unsigned long) time(NULL), __FILE__, __LINE__, __FUNCTION__, message)
#  define BALSA_DEBUG() if (balsa_app.debug) fprintf (stderr, "[%lu] %12s | %4d | %30s\n", (unsigned long) time(NULL), __FILE__, __LINE__, __FUNCTION__)
# else
#  define BALSA_DEBUG_MSG(message)  if (balsa_app.debug)  fprintf(stderr, "[%lu] %12s | %4d: %s\n", (unsigned long) time(NULL), __FILE__, __LINE__, message)
#  define BALSA_DEBUG() if (balsa_app.debug)  fprintf(stderr, "[%lu] %12s | %4d\n", (unsigned long) time(NULL), __FILE__, __LINE__)
# endif
#endif

#endif				/* __MAIN_WINDOW_H__ */
