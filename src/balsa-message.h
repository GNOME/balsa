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

#ifndef __BALSA_MESSAGE_H__
#define __BALSA_MESSAGE_H__

#include <gnome.h>
#include "libbalsa.h"

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */


#define BALSA_TYPE_MESSAGE          (balsa_message_get_type ())
#define BALSA_MESSAGE(obj)          GTK_CHECK_CAST (obj, BALSA_TYPE_MESSAGE, BalsaMessage)
#define BALSA_MESSAGE_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MESSAGE, BalsaMessageClass)
#define BALSA_IS_MESSAGE(obj)       GTK_CHECK_TYPE (obj, BALSA_TYPE_MESSAGE)


typedef struct _BalsaMessage BalsaMessage;
typedef struct _BalsaMessageClass BalsaMessageClass;

typedef struct _BalsaPartInfo BalsaPartInfo;

struct _BalsaMessage {
	GtkViewport parent;

       /* The vbox widget */
       GtkWidget *vbox;
 
	/* Widget to hold headers */
	GtkWidget *header_text;
	ShownHeaders shown_headers;
	gboolean show_all_headers;

	/* Notebook to hold content + structure */
        GtkWidget *notebook;

	/* Widgets to hold content */
        GtkWidget *cont_viewport;
	GtkWidget *content;
	gboolean content_has_focus;

        /* Widget to hold structure tree */
        GtkWidget *treeview;
        gint info_count;
        GList *save_all_list;
        GtkWidget *save_all_popup;
    
	gboolean wrap_text;

	BalsaPartInfo *current_part;

	LibBalsaMessage *message;
};

struct _BalsaMessageClass {
	GtkViewportClass parent_class;

	void (*select_part) (BalsaMessage * message);
};

GtkType balsa_message_get_type(void);
GtkWidget *balsa_message_new(void);

gboolean balsa_message_set(BalsaMessage * bmessage,
			   LibBalsaMessage * message);

void balsa_message_next_part(BalsaMessage * bmessage);
void balsa_message_previous_part(BalsaMessage * bmessage);
void balsa_message_save_current_part(BalsaMessage * bmessage);

void balsa_message_set_displayed_headers(BalsaMessage * bmessage,
					     ShownHeaders sh);
void balsa_message_set_wrap(BalsaMessage * bmessage, gboolean wrap);

gboolean balsa_message_can_select(BalsaMessage * bmessage);
gboolean balsa_message_grab_focus(BalsaMessage * bmessage);

void reflow_string(gchar * str, gint mode, gint * cur_pos, int width);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* __BALSA_MESSAGE_H__ */
