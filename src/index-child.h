/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#include "mailbox.h"

#define INDEX_CHILD(obj)          GTK_CHECK_CAST (obj, index_child_get_type (), IndexChild)
#define INDEX_CHILD_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, index_child_get_type (), IndexChildClass)
#define IS_INDEX_CHILD(obj)       GTK_CHECK_TYPE (obj, index_child_get_type ())


typedef struct _IndexChild IndexChild;
typedef struct _IndexChildClass IndexChildClass;

struct _IndexChild
  {
    GnomeMDIChild mdi_child;

    Mailbox *mailbox;

    guint watcher_id;
    GtkWidget *window;
    GtkWidget *index;
    GtkAccelGroup *accel;
  };

struct _IndexChildClass
  {
    GnomeMDIChildClass parent_class;
  };

IndexChild *index_child_new (Mailbox *);
void index_child_changed(GnomeMDI *, GnomeMDIChild *);
IndexChild *index_child_get_active(GnomeMDI *);

#endif /* __INDEX_CHILD_H__ */
