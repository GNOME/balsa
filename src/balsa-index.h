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
#include "mailbox-node.h"

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

    GtkType balsa_index_get_type(void);

#define BALSA_TYPE_INDEX          (balsa_index_get_type ())
#define BALSA_INDEX(obj)          (GTK_CHECK_CAST (obj, balsa_index_get_type (), BalsaIndex))
#define BALSA_INDEX_CLASS(klass)  (GTK_CHECK_CLASS_CAST (klass, balsa_index_get_type (), BalsaIndexClass))
#define BALSA_IS_INDEX(obj)       (GTK_CHECK_TYPE (obj, balsa_index_get_type ()))
#define BALSA_IS_INDEX_CLASS(klass) (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_INDEX))


    typedef struct _BalsaIndex BalsaIndex;
    typedef struct _BalsaIndexClass BalsaIndexClass;

    struct _BalsaIndex {
        GtkScrolledWindow sw;    
        
        GtkCTree* ctree;
        GtkWidget* window;       

        BalsaMailboxNode* mailbox_node;
        LibBalsaMessage* first_new_message;

        int threading_type;
        GTimeVal last_use;

	gchar *date_string;
	gboolean line_length;
    };

    struct _BalsaIndexClass {
	GtkScrolledWindowClass parent_class;

	void (*select_message) (BalsaIndex * bindex,
				LibBalsaMessage * message);
	void (*unselect_message) (BalsaIndex * bindex,
				  LibBalsaMessage * message);
        void (*unselect_all_messages) (BalsaIndex* bindex);
    };


/* function prototypes */
    
    GtkWidget *balsa_index_new(void);


/* sets the mail stream; if it's a new stream, then it's 
 * contents is loaded into the index */
    gboolean balsa_index_load_mailbox_node(BalsaIndex * bindex,
                                           BalsaMailboxNode * mbnode);
    void balsa_index_refresh(BalsaIndex * bindex);
    void balsa_index_update_tree(BalsaIndex *bindex, gboolean expand);
    void balsa_index_set_threading_type(BalsaIndex * bindex, int thtype);
    void balsa_index_set_sort_order(BalsaIndex * bindex, int column, 
				    GtkSortType order);
    void balsa_index_set_first_new_message(BalsaIndex * bindex);

/* adds a new message */
    void balsa_index_add(BalsaIndex * bindex, LibBalsaMessage * message);
    void balsa_index_redraw_current(BalsaIndex *);

/* move or copy a list of messages */
    void balsa_index_transfer(GList * messages,
                              LibBalsaMailbox * from_mailbox,
                              LibBalsaMailbox * to_mailbox,
                              BalsaIndex * from_bindex, gboolean copy);

/* select up/down the index */
    void balsa_index_select_next(BalsaIndex *);
    void balsa_index_select_next_unread(BalsaIndex * bindex);
    void balsa_index_select_next_flagged(BalsaIndex * bindex);
    void balsa_index_select_previous(BalsaIndex *);
    void balsa_index_select_row(BalsaIndex * bindex, gint row);

#ifdef BALSA_SHOW_ALL
    void balsa_index_find(BalsaIndex * bindex,gint op,GSList * conditions,gboolean previous);
#endif /* BALSA_SHOW_ALL */

/* retrieve the selection */
    void balsa_index_get_selected_rows(BalsaIndex * bindex,
				       GtkCTreeNode *** rows,
				       guint * nb_rows);


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
    void balsa_message_delete(GtkWidget * widget, gpointer user_data);
    void balsa_message_undelete(GtkWidget * widget, gpointer user_data);

    void balsa_message_toggle_flagged(GtkWidget * widget, gpointer user_data);
    void balsa_message_toggle_new(GtkWidget * widget, gpointer user_data);

    void balsa_index_reset(BalsaIndex * index);
    gint balsa_find_notebook_page_num(LibBalsaMailbox * mailbox);
    void balsa_index_update_message(BalsaIndex * index);

    /* Threading Stuff */
    void balsa_index_threading(BalsaIndex* bindex);

    /* Updating index columns when preferences change */
    void balsa_index_refresh_date (BalsaIndex *);
    void balsa_index_refresh_size (BalsaIndex *);

    /* Change the display of all indexes when balsa_app.hide_deleted is
     * changed */
    void balsa_index_hide_deleted(gboolean hide);
    void balsa_index_sync_backend(LibBalsaMailbox * mailbox);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_INDEX_H__ */
