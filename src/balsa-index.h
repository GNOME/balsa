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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_INDEX_H__
#define __BALSA_INDEX_H__

#include "libbalsa.h"
#include "filter.h"
#include "mailbox-node.h"

G_BEGIN_DECLS

#define BALSA_TYPE_INDEX (balsa_index_get_type ())

G_DECLARE_FINAL_TYPE(BalsaIndex, balsa_index, BALSA, INDEX, GtkTreeView)

    typedef enum { BALSA_INDEX_WIDE, BALSA_INDEX_NARROW }
        BalsaIndexWidthPreference;


/* tree model columns */
    enum {
        BNDX_MESSAGE_COLUMN,
        BNDX_INDEX_COLUMN,
        BNDX_STATUS_COLUMN,
        BNDX_ATTACH_COLUMN,
        BNDX_FROM_COLUMN,
        BNDX_SUBJECT_COLUMN,
        BNDX_DATE_COLUMN,
        BNDX_SIZE_COLUMN,
        BNDX_COLOR_COLUMN,
        BNDX_WEIGHT_COLUMN,
        BNDX_N_COLUMNS
    };

/* function prototypes */
    
    GtkWidget *balsa_index_new(void);


/* sets the mail stream; if it's a new stream, then it's 
 * contents is loaded into the index */
    void balsa_index_set_mailbox_node(BalsaIndex       * bindex,
                                      BalsaMailboxNode * mbnode);
    void balsa_index_load_mailbox_node(BalsaIndex * bindex);
    void balsa_index_set_width_preference(BalsaIndex *bindex,
                                          BalsaIndexWidthPreference pref);
    void balsa_index_scroll_on_open(BalsaIndex *index);
    void balsa_index_update_tree(BalsaIndex *bindex, gboolean expand);
    void balsa_index_set_thread_messages(BalsaIndex * bindex,
                                         gboolean thread_messages);
    void balsa_index_set_view_filter(BalsaIndex *bindex,
                                     int filter_no,
                                     const gchar *filter_string,
                                     LibBalsaCondition *filter);

/* move or copy a list of messages */
    void balsa_index_transfer(BalsaIndex * index, GArray * msgnos,
                              LibBalsaMailbox * to_mailbox, gboolean copy);

/* select up/down the index */
    void balsa_index_select_next(BalsaIndex *);
    gboolean balsa_index_select_next_unread(BalsaIndex * index);
    void balsa_index_select_next_flagged(BalsaIndex * bindex);
    void balsa_index_select_previous(BalsaIndex *);
    void balsa_index_set_next_msgno(BalsaIndex * bindex, guint msgno);
    guint balsa_index_get_next_msgno(BalsaIndex * bindex);

    void balsa_index_find(BalsaIndex * bindex,
			  LibBalsaMailboxSearchIter * search_iter,
                          gboolean previous, gboolean wrap);

/* balsa index page stuff */
    void balsa_message_reply(gpointer user_data);
    void balsa_message_replytoall(gpointer user_data);
    void balsa_message_replytogroup(gpointer user_data);
    void balsa_message_newtosender(gpointer user_data);

    void balsa_message_forward_attached(gpointer data);
    void balsa_message_forward_inline(gpointer data);
    void balsa_message_forward_quoted(gpointer data);
    void balsa_message_continue(gpointer data);

    void balsa_message_move_to_trash(gpointer user_data);

    void balsa_index_toggle_flag(BalsaIndex *index,
                                 LibBalsaMessageFlag flag);

    void balsa_index_reset(BalsaIndex * index);
    gint balsa_find_notebook_page_num(LibBalsaMailbox * mailbox);
    void balsa_index_set_column_widths(BalsaIndex * index);
    GList * balsa_index_selected_list(BalsaIndex * index);
    GArray * balsa_index_selected_msgnos_new(BalsaIndex * index);
    void balsa_index_selected_msgnos_free(BalsaIndex * index, GArray * msgnos);
    void balsa_index_move_subtree(BalsaIndex * index,
                                  GtkTreePath * root,
                                  GtkTreePath * new_parent);

    /* Updating index columns when preferences change */
    void balsa_index_refresh_date (BalsaIndex * index);
    void balsa_index_refresh_size (BalsaIndex * index);

    /* Expunge deleted messages. */
    void balsa_index_expunge(BalsaIndex * index);
 
    /* Message window */ 	 
    guint balsa_index_next_msgno(BalsaIndex * index, guint current_msgno);
    guint balsa_index_previous_msgno(BalsaIndex * index,
                                     guint current_msgno);

    /* Pipe messages */
    void balsa_index_pipe(BalsaIndex * index);

    /* Make sure messages are visible. */
    void balsa_index_ensure_visible(BalsaIndex * index);

    /* Select all without previewing any. */
    void balsa_index_select_all(BalsaIndex * bindex);

    /* Select thread containing current message. */
    void balsa_index_select_thread(BalsaIndex * bindex);

    /* Count of selected messages. */
    gint balsa_index_count_selected_messages(BalsaIndex * bindex);

/*
 * Getters
 */

BalsaMailboxNode * balsa_index_get_mailbox_node(BalsaIndex *bindex);
guint balsa_index_get_current_msgno(BalsaIndex *bindex);
gint balsa_index_get_filter_no(BalsaIndex *bindex);
gboolean balsa_index_get_next_message(BalsaIndex *bindex);
gboolean balsa_index_get_prev_message(BalsaIndex *bindex);
const gchar * balsa_index_get_filter_string(BalsaIndex *bindex);

/*
 * Convenience
 */

void balsa_index_set_last_use_time(BalsaIndex *bindex);
time_t balsa_index_get_last_use_time(BalsaIndex *bindex);
LibBalsaMailbox * balsa_index_get_mailbox(BalsaIndex *bindex);

#define BALSA_INDEX_VIEW_ON_OPEN "balsa-index-view-on-open"

G_END_DECLS

#endif				/* __BALSA_INDEX_H__ */
