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

#ifndef __BALSA_MESSAGE_H__
#define __BALSA_MESSAGE_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"
#include "balsa-app.h"

G_BEGIN_DECLS

#define BALSA_TYPE_MESSAGE (balsa_message_get_type ())

G_DECLARE_FINAL_TYPE(BalsaMessage, balsa_message, BALSA, MESSAGE, GtkBox)

typedef struct _BalsaPartInfo BalsaPartInfo;

typedef struct _BalsaMimeWidget BalsaMimeWidget;

typedef enum {
    BALSA_MESSAGE_FOCUS_STATE_NO,
    BALSA_MESSAGE_FOCUS_STATE_YES,
    BALSA_MESSAGE_FOCUS_STATE_HOLD
} BalsaMessageFocusState;

GtkWidget *balsa_message_new(void);

gboolean balsa_message_set(BalsaMessage * bmessage,
			   LibBalsaMailbox * mailbox, guint msgno);
void balsa_message_recheck_crypto(BalsaMessage *balsa_message);

void balsa_message_next_part(BalsaMessage * bmessage);
gboolean balsa_message_has_next_part(BalsaMessage * bmessage);
void balsa_message_previous_part(BalsaMessage * bmessage);
gboolean balsa_message_has_previous_part(BalsaMessage * bmessage);
void balsa_message_save_current_part(BalsaMessage * bmessage);
void balsa_message_copy_part(GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      data);

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

void balsa_message_perform_crypto(LibBalsaMessage * message,
				  LibBalsaChkCryptoMode chk_mode,
				  gboolean no_mp_signed,
				  guint max_ref);

void balsa_message_find_in_message (BalsaMessage * bm);

/*
 * Getters
 */
gboolean balsa_message_get_wrap_text(BalsaMessage *bm);
BalsaMessageFocusState balsa_message_get_focus_state(BalsaMessage *bm);
GtkScrolledWindow * balsa_message_get_scroll(BalsaMessage *bm);
BalsaMimeWidget * balsa_message_get_bm_widget(BalsaMessage *bm);
LibBalsaMessage * balsa_message_get_message(BalsaMessage *bm);
ShownHeaders balsa_message_get_shown_headers(BalsaMessage *bm);
GtkWidget * balsa_message_get_face_box(BalsaMessage *bm);
GtkWidget * balsa_message_get_tree_view(BalsaMessage *bm);

/*
 * Setters
 */
void balsa_message_set_focus_state(BalsaMessage *bm,
                                   BalsaMessageFocusState focus_state);
void balsa_message_set_face_box(BalsaMessage *bm, GtkWidget *face_box);

G_END_DECLS

#endif				/* __BALSA_MESSAGE_H__ */
