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

#include <gtk/gtk.h>
#include "c-client.h"

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
    GtkBin bin;

    MAILSTREAM *stream;
    glong last_message;

    /* pixmap and icon for new messages */
    GdkPixmap *new_xpm;
    GdkBitmap *new_xpm_mask;

    /* progress bar to be updated while loading messages */
    GtkProgressBar *progress_bar;
  };

struct _BalsaIndexClass
  {
    GtkBinClass parent_class;

    void (*select_message) (BalsaIndex *bindex,
			    MAILSTREAM *stream,
			    glong mesgno);
  };



guint balsa_index_get_type (void);

GtkWidget *balsa_index_new ();


/* sets the mail stream; if it's a new stream, then it's 
 * contents is loaded into the index */
void balsa_index_set_stream (BalsaIndex * bindex,
			     MAILSTREAM * stream);

/* appends any new messages in the stream to the index, 
 * XXX: maybe this should be re-named balsa_index_ping?? */
void balsa_index_append_new_messages (BalsaIndex * bindex);


/* select up/down the index */
void balsa_index_select_next (BalsaIndex *);
void balsa_index_select_previous (BalsaIndex *);

void balsa_delete_message(BalsaIndex * bindex);

/* set the pointer to the progress bar that's used to show
 * progress loading images */
void balsa_index_set_progress_bar (BalsaIndex * bindex,
				   GtkProgressBar * progress_bar);
GtkProgressBar * balsa_index_get_progress_bar (BalsaIndex * bindex);



#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_INDEX_H__ */
