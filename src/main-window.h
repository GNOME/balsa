/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __MAIN_WINDOW_H__
#define __MAIN_WINDOW_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <gio/gio.h>

#include "mailbox-node.h"
#include "toolbar-factory.h"

#define BALSA_TYPE_WINDOW balsa_window_get_type()

G_DECLARE_DERIVABLE_TYPE(BalsaWindow,
                         balsa_window,
                         BALSA,
                         WINDOW,
                         GtkApplicationWindow)

typedef enum {
    BALSA_PROGRESS_NONE = 0,
    BALSA_PROGRESS_ACTIVITY,
    BALSA_PROGRESS_INCREMENT
} BalsaWindowProgress;


struct _BalsaWindowClass {
    GtkApplicationWindowClass parent_class;

    void (*open_mbnode)  (BalsaWindow * window,
                          BalsaMailboxNode * mbnode,
                          gboolean set_current);
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

enum {
    FILTER_SENDER  = 0,
    FILTER_RECIPIENT = 1
};

GtkWidget *balsa_window_new(GtkApplication *application);
GtkWidget *balsa_window_find_current_index(BalsaWindow * window);
void balsa_window_update_book_menus(BalsaWindow *window);
void balsa_window_refresh(BalsaWindow * window);
void balsa_window_open_mbnode(BalsaWindow * window,
                              BalsaMailboxNode*mbnode,
                              gboolean set_current);
void balsa_window_close_mbnode(BalsaWindow * window, BalsaMailboxNode*mbnode);
void balsa_identities_changed(BalsaWindow *bw);

void balsa_window_update_tab(BalsaMailboxNode * mbnode);
void enable_empty_trash(BalsaWindow * window, TrashState status);
void balsa_window_enable_continue(BalsaWindow * window);
void balsa_change_window_layout(BalsaWindow *window);
gboolean mail_progress_notify_cb(GIOChannel * source,
                                 GIOCondition condition,
                                 BalsaWindow ** window);
gboolean send_progress_notify_cb(GIOChannel * source,
                                 GIOCondition condition,
                                 BalsaWindow ** window);
void check_new_messages_real(BalsaWindow * window, gboolean background_check);
void check_new_messages_count(LibBalsaMailbox * mailbox, gboolean notify);
void empty_trash(BalsaWindow * window);
void update_view_menu(BalsaWindow * window);
BalsaToolbarModel *balsa_window_get_toolbar_model(void);
void balsa_window_add_action_entries(GActionMap * action_map);
void balsa_window_select_all(GtkWindow * window);
gboolean balsa_window_next_unread(BalsaWindow * window);

/* functions to manipulate the progress bars of the window */
gboolean balsa_window_setup_progress(BalsaWindow * window,
                                     const gchar * text);
void balsa_window_clear_progress(BalsaWindow* window);
void balsa_window_progress_bar_set_fraction(BalsaWindow * window,
                                            gdouble       fraction);
void balsa_window_increase_activity(BalsaWindow * window,
                                    const gchar * message);
void balsa_window_decrease_activity(BalsaWindow * window,
                                    const gchar * message);
void balsa_window_set_statusbar(BalsaWindow     * window,
                                LibBalsaMailbox * mailbox);

/*
 * Getter
 */

GtkStatusbar * balsa_window_get_statusbar(BalsaWindow * window);

#endif				/* __MAIN_WINDOW_H__ */
