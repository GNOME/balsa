/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#ifndef __BALSA_INDEX_H__
#define __BALSA_INDEX_H__

#include <gnome.h>
#include "libbalsa.h"
#include "filter.h"
#include "mailbox-node.h"

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

    GtkType balsa_index_get_type(void);

#define BALSA_TYPE_INDEX          (balsa_index_get_type ())
#define BALSA_INDEX(obj)          (GTK_CHECK_CAST (obj, BALSA_TYPE_INDEX, BalsaIndex))
#define BALSA_INDEX_CLASS(klass)  (GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_INDEX, BalsaIndexClass))
#define BALSA_IS_INDEX(obj)       (GTK_CHECK_TYPE (obj, BALSA_TYPE_INDEX))
#define BALSA_IS_INDEX_CLASS(klass) (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_INDEX))


    typedef struct _BalsaIndex BalsaIndex;
    typedef struct _BalsaIndexClass BalsaIndexClass;

    struct _BalsaIndex {
        GtkTreeView tree_view;
        
        GtkWidget* window;       

        /* the popup menu and some items we need to refer to */
        GtkWidget *popup_menu;
        GtkWidget *delete_item;
        GtkWidget *undelete_item;
        GtkWidget *move_to_trash_item;
        GtkWidget *toggle_item;
        GtkWidget *move_to_item;

        BalsaMailboxNode* mailbox_node;
        guint current_msgno;
        LibBalsaMessage* current_message;
	gboolean current_message_is_deleted;
        gboolean prev_message;
        gboolean next_message;
        int    filter_no;
        gchar *filter_string; /* Quick view filter string, if any */

        /* signal handler ids */
        gulong row_expanded_id;
        gulong row_collapsed_id;

	LibBalsaMailboxSearchIter *search_iter;
    };

    struct _BalsaIndexClass {
	GtkTreeViewClass parent_class;

        void (*index_changed) (BalsaIndex* bindex);
    };

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
    gboolean balsa_index_load_mailbox_node(BalsaIndex * bindex,
                                           BalsaMailboxNode * mbnode,
					   GError **err);
    void balsa_index_scroll_on_open(BalsaIndex *index);
    void balsa_index_update_tree(BalsaIndex *bindex, gboolean expand);
    void balsa_index_set_threading_type(BalsaIndex * bindex, int thtype);
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
    void balsa_index_select(BalsaIndex * index, LibBalsaMessage * message);

    void balsa_index_find(BalsaIndex * bindex,
			  LibBalsaMailboxSearchIter * search_iter,
                          gboolean previous, gboolean wrap);

/* balsa index page stuff */
    void balsa_message_reply(GtkWidget * widget, gpointer user_data);
    void balsa_message_replytoall(GtkWidget * widget, gpointer user_data);
    void balsa_message_replytogroup(GtkWidget * widget, gpointer user_data);

    void balsa_message_forward_attached(GtkWidget * widget, gpointer data);
    void balsa_message_forward_inline(GtkWidget * widget, gpointer data);
    void balsa_message_forward_quoted(GtkWidget * widget, gpointer data);
    void balsa_message_forward_default(GtkWidget * widget, gpointer data);
    void balsa_message_continue(GtkWidget * widget, gpointer data);

    void balsa_message_move_to_trash(GtkWidget * widget, gpointer user_data);

    void balsa_index_toggle_flag(BalsaIndex *index,
                                 LibBalsaMessageFlag flag);

    void balsa_index_reset(BalsaIndex * index);
    gint balsa_find_notebook_page_num(LibBalsaMailbox * mailbox);
    void balsa_index_set_column_widths(BalsaIndex * index);
    GList * balsa_index_selected_list(BalsaIndex * index);
    GArray * balsa_index_selected_msgnos(BalsaIndex * index);
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

#define BALSA_INDEX_VIEW_ON_OPEN "balsa-index-view-on-open"

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_INDEX_H__ */
