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

  Mailbox *mailbox;
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
void balsa_index_page_load_mailbox(BalsaIndexPage *page, Mailbox *mailbox);
void balsa_index_page_close_and_destroy( BalsaIndexPage *page );

#endif /* __INDEX_CHILD_H__ */
