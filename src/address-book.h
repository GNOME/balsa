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

#ifndef __ADDRESS_BOOK_H__
#define __ADDRESS_BOOK_H__

#define BALSA_TYPE_ADDRESS_BOOK            (balsa_address_book_get_type ())
#define BALSA_ADDRESS_BOOK(obj)            (GTK_CHECK_CAST ((obj), BALSA_TYPE_ADDRESS_BOOK, BalsaAddressBook))
#define BALSA_ADDRESS_BOOK_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), BALSA_TYPE_ADDRESS_BOOK, BalsaAddressBookClass))
#define BALSA_IS_ADDRESS_BOOK(obj)         (GTK_CHECK_TYPE ((obj), BALSA_TYPE_ADDRESS_BOOK))
#define BALSA_IS_ADDRESS_BOOK_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), BALSA_TYPE_ADDRESS_BOOK))


typedef struct _BalsaAddressBook BalsaAddressBook;
typedef struct _BalsaAddressBookClass BalsaAddressBookClass;

struct _BalsaAddressBook 
{
    GnomeDialog parent;

    /* Are we composing? */
    gboolean composing;

    /* The current address book */
    LibBalsaAddressBook *current_address_book;

    /* The address list */
    GtkWidget *address_clist;

    /* The send to list */
    GtkWidget *recipient_clist;

    /* Radio buttons for dist list mode */
    GtkWidget *single_address_mode_radio;
    GtkWidget *dist_address_mode_radio;
    guint toggle_handler_id;

    /* Stuff to hide when not in compose mode */
    GtkWidget *send_to_box;
    GtkWidget *arrow_box;
};

struct _BalsaAddressBookClass
{
    GnomeDialogClass parent_class;
};

GtkType balsa_address_book_get_type(void);
GtkWidget *balsa_address_book_new(gboolean composing);

gchar *balsa_address_book_get_recipients(BalsaAddressBook *ab);


#endif				/* __ADDRESS_BOOK_H__ */











