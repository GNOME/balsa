/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"
#include "balsa-app.h"

#if GTK_CHECK_VERSION(3, 10, 0)
#define BALSA_USE_GTK_STACK
#endif                          /* GTK_CHECK_VERSION(3, 10, 0) */

G_BEGIN_DECLS


#define BALSA_TYPE_MESSAGE          (balsa_message_get_type ())
#define BALSA_MESSAGE(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_MESSAGE, BalsaMessage)
#define BALSA_MESSAGE_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, BALSA_TYPE_MESSAGE, BalsaMessageClass)
#define BALSA_IS_MESSAGE(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, BALSA_TYPE_MESSAGE)


typedef struct _BalsaMessage BalsaMessage;
typedef struct _BalsaMessageClass BalsaMessageClass;

typedef struct _BalsaPartInfo BalsaPartInfo;

typedef struct _BalsaMimeWidget BalsaMimeWidget;

typedef enum {
    BALSA_MESSAGE_FOCUS_STATE_NO,
    BALSA_MESSAGE_FOCUS_STATE_YES,
    BALSA_MESSAGE_FOCUS_STATE_HOLD
} BalsaMessageFocusState;

struct _BalsaMessage {
#ifndef BALSA_USE_GTK_STACK
	GtkNotebook parent;
#else /* BALSA_USE_GTK_STACK */
        GtkBox parent;

        GtkWidget *stack;
        GtkWidget *switcher;
#endif /* BALSA_USE_GTK_STACK */

        /* Top-level MIME widget */
        BalsaMimeWidget *bm_widget;

	/* header-related information */
	ShownHeaders shown_headers;

	/* Widgets to hold content */
	GtkWidget *scroll;

        /* Widget to hold structure tree */
        GtkWidget *treeview;
        gint info_count;
        GList *save_all_list;
        GtkWidget *save_all_popup;
    
	gboolean wrap_text;

        BalsaPartInfo *current_part;
        GtkWidget *parts_popup;
        gboolean force_inline;

	LibBalsaMessage *message;

        BalsaMessageFocusState focus_state;

        /* Find-in-message stuff */
        GtkWidget  *find_bar;
        GtkWidget  *find_entry;
        GtkWidget  *find_next;
        GtkWidget  *find_prev;
        GtkWidget  *find_sep;
        GtkWidget  *find_label;
        GtkTextIter find_iter;
        gboolean    find_forward;

        /* Tab position for headers */
        gint tab_position;

        /* Widget to hold Faces */
        GtkWidget *face_box;

#ifdef HAVE_HTML_WIDGET
        gpointer html_find_info;
#endif				/* HAVE_HTML_WIDGET */
};

struct _BalsaMessageClass {
	GtkNotebookClass parent_class;

	void (*select_part) (BalsaMessage * message);
};

GType balsa_message_get_type(void);
GtkWidget *balsa_message_new(void);

gboolean balsa_message_set(BalsaMessage * bmessage,
			   LibBalsaMailbox * mailbox, guint msgno);

void balsa_message_next_part(BalsaMessage * bmessage);
gboolean balsa_message_has_next_part(BalsaMessage * bmessage);
void balsa_message_previous_part(BalsaMessage * bmessage);
gboolean balsa_message_has_previous_part(BalsaMessage * bmessage);
void balsa_message_save_current_part(BalsaMessage * bmessage);
void balsa_message_copy_part(const gchar *url, LibBalsaMessageBody *part);

void balsa_message_set_displayed_headers(BalsaMessage * bmessage,
					     ShownHeaders sh);
void balsa_message_set_wrap(BalsaMessage * bmessage, gboolean wrap);

gboolean balsa_message_can_select(BalsaMessage * bmessage);
gboolean balsa_message_grab_focus(BalsaMessage * bmessage);
gchar * balsa_message_sender_to_gchar(InternetAddressList * list, gint which);
GtkWidget *balsa_message_current_part_widget(BalsaMessage * bmessage);
GtkWindow *balsa_get_parent_window(GtkWidget *widget);

#ifdef HAVE_HTML_WIDGET
#define BALSA_MESSAGE_ZOOM_KEY "balsa-message-zoom"
gboolean balsa_message_can_zoom(BalsaMessage * bm);
void balsa_message_zoom(BalsaMessage * bm, gint in_out);
#endif				/* HAVE_HTML_WIDGET */

#ifdef HAVE_GPGME
void balsa_message_perform_crypto(LibBalsaMessage * message,
				  LibBalsaChkCryptoMode chk_mode,
				  gboolean no_mp_signed,
				  guint max_ref);
#endif

void balsa_message_find_in_message (BalsaMessage * bm);

G_END_DECLS

#endif				/* __BALSA_MESSAGE_H__ */
