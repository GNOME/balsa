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

#ifndef __BALSA_INDEX_H__
#define __BALSA_INDEX_H__

#include <gnome.h>
#include "mailbox.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define BALSA_INDEX(obj)          GTK_CHECK_CAST (obj, balsa_index_get_type (), BalsaIndex)
#define BALSA_INDEX_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, balsa_index_get_type (), BalsaIndexClass)
#define BALSA_IS_INDEX(obj)       GTK_CHECK_TYPE (obj, balsa_index_get_type ())


  typedef struct _BalsaIndex BalsaIndex;
  typedef struct _BalsaIndexClass BalsaIndexClass;

  struct _BalsaIndex
    {
      GtkCList clist;

      Mailbox *mailbox;
      guint watcher_id;
      guint first_new_message;
    };

  struct _BalsaIndexClass
    {
      GtkCListClass parent_class;

      void (*select_message) (BalsaIndex * bindex, Message * message);
    };


  guint balsa_index_get_type (void);
  GtkWidget *balsa_index_new (void);


/* sets the mail stream; if it's a new stream, then it's 
 * contents is loaded into the index */
  void balsa_index_set_mailbox (BalsaIndex * bindex, Mailbox * mailbox);


/* adds a new message */
  void balsa_index_add (BalsaIndex * bindex, Message * message);
  void balsa_index_update_flag (BalsaIndex * bindex, Message * message);


/* select up/down the index */
  void balsa_index_select_next (BalsaIndex *);
  void balsa_index_select_previous (BalsaIndex *);


#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_INDEX_H__ */
