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

#ifndef __AB_WINDOW_H__
#define __AB_WINDOW_H__

#include <gtk/gtk.h>
#include <libbalsa.h>

#define BALSA_TYPE_AB_WINDOW            (balsa_ab_window_get_type ())
#define BALSA_AB_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), BALSA_TYPE_AB_WINDOW, BalsaAbWindow))
#define BALSA_AB_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), BALSA_TYPE_AB_WINDOW, BalsaAbWindowClass))
#define BALSA_IS_AB_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BALSA_TYPE_AB_WINDOW))
#define BALSA_IS_AB_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), BALSA_TYPE_AB_WINDOW))


typedef struct _BalsaAbWindow BalsaAbWindow;
typedef struct _BalsaAbWindowClass BalsaAbWindowClass;

struct _BalsaAbWindow 
{
    GtkDialog parent;

    /* Are we composing? */
    gboolean composing;

    /* The current address book */
    LibBalsaAddressBook *current_address_book;

    /* the filter entry */
    GtkWidget *filter_entry;

    /* The address list */
    GtkWidget *address_list;

    /* The send to list */
    GtkWidget *recipient_list;

    /* Radio buttons for dist list mode */
    GtkWidget *single_address_mode_radio;
    GtkWidget *dist_address_mode_radio;
    guint      toggle_handler_id;

    /* Stuff to hide when not in compose mode */
    GtkWidget *send_to_label;
    GtkWidget *send_to_list;
    GtkWidget *arrow_box;

    /* The address book list */
    GtkWidget *combo_box;
};

GType balsa_ab_window_get_type(void);
GtkWidget *balsa_ab_window_new(gboolean composing, GtkWindow* parent);

gchar *balsa_ab_window_get_recipients(BalsaAbWindow *ab);


#endif				/* __ADDRESS_BOOK_H__ */
