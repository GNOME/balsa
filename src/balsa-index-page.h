/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Jay Painter and Stuart Parmenter
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
#ifndef __INDEX_CHILD_H__
#define __INDEX_CHILD_H__

#include "main-window.h"
#include "libbalsa.h"
#include "balsa-index.h"
#include "sendmsg-window.h"

GtkType balsa_index_page_get_type(void);

#define BALSA_TYPE_INDEX_PAGE		       (balsa_index_page_get_type ())
#define BALSA_INDEX_PAGE(obj)		       (GTK_CHECK_CAST (obj, BALSA_TYPE_INDEX_PAGE, BalsaIndexPage))
#define BALSA_INDEX_PAGE_CLASS(klass)	       (GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_INDEX_PAGE, BalsaIndexPageClass))
#define BALSA_IS_INDEX_PAGE(obj)	       (GTK_CHECK_TYPE (obj, BALSA_TYPE_INDEX_PAGE))
#define BALSA_IS_INDEX_PAGE_CLASS(klass)       (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_INDEX_PAGE))

typedef struct _BalsaIndexPage BalsaIndexPage;
typedef struct _BalsaIndexPageClass BalsaIndexPageClass;

struct _BalsaIndexPage
{
  GtkObject object;

  LibBalsaMailbox *mailbox;
  GTimeVal last_use;
  GtkWidget *window; /* "real" BalsaWindow parent */
  GtkWidget *sw;
  GtkWidget *index;
};

struct _BalsaIndexPageClass
{
  GtkObjectClass parent_class;
};

GtkType balsa_index_page_get_type(void);
GtkObject *balsa_index_page_new(BalsaWindow *window);
gboolean balsa_index_page_load_mailbox(BalsaIndexPage *page, LibBalsaMailbox *mailbox);
void balsa_index_page_close_and_destroy( GtkObject *obj );

void balsa_message_new (GtkWidget * widget);
void balsa_message_reply (GtkWidget * widget, gpointer index);
void balsa_message_replytoall (GtkWidget * widget, gpointer index);
void balsa_message_forward (GtkWidget * widget, gpointer index);
void balsa_message_continue (GtkWidget * widget, gpointer index);
void balsa_message_next (GtkWidget * widget, gpointer index);
void balsa_message_next_unread (GtkWidget* widget, gpointer index);
void balsa_message_previous (GtkWidget * widget, gpointer index);
void balsa_message_delete (GtkWidget * widget, gpointer index);
void balsa_message_undelete (GtkWidget * widget, gpointer index);
void balsa_message_toggle_flagged (GtkWidget * widget, gpointer index);
void balsa_message_store_address (GtkWidget * widget, gpointer index);

void balsa_index_page_reset(BalsaIndexPage *page);
gint balsa_find_notebook_page_num(LibBalsaMailbox *mailbox);
BalsaIndexPage* balsa_find_notebook_page(LibBalsaMailbox *mailbox);
void balsa_index_update_message (BalsaIndexPage *index_page);

#endif /* __INDEX_CHILD_H__ */
