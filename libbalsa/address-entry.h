/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#ifndef __LIBBALSA_ADDRESS_ENTRY_H__
#define __LIBBALSA_ADDRESS_ENTRY_H__

#include "address.h"

#define FOCUS_LOST 0
#define FOCUS_TAINTED 1
#define FOCUS_CACHED 2

/*************************************************************************
 *                                                                       *
 * Structures that the widget uses extensively.                          *
 *                                                                       *
 *************************************************************************/

/*
 * A structure that describes an e-mail entry.  That is, it contains
 * the user input, and the alias expansian of ONE e-mail entry.
 *
 * user  is the user's input.  This is a character string.  If this
 *       contains an '@', '%' or '!', we won't expand.
 *       @ for smtp, and ! for uucp.  There are no leading spaces,
 *       but there can be trailing spaces if the user pressed 'space'.
 *
 * match is the current match to the user's input.  If this is NULL,
 *       there is no match.
 *
 * cursor is the last known cursor position in the input.  It is
 *        offset from the first character in the input, and may
 *        not exceed the length of the input.
 *        -1 is cursor not set.
 *
 * tabs is the number of tab keys pressed.  In essence, this is the
 *      nth match of the user input.
 */
typedef struct {
    gchar *user;
    gchar *match;
    LibBalsaAddress *address;
    unsigned cursor; /* position in terms of bytes, not UTF-8 characters */
    gint tabs;
} emailData;


/*************************************************************************
 *                                                                       *
 * A subclass of gtkentry to allow alias completion.                     *
 *                                                                       *
 *************************************************************************/

#define LIBBALSA_TYPE_ADDRESS_ENTRY		(libbalsa_address_entry_get_type())
#define LIBBALSA_ADDRESS_ENTRY(obj)		(GTK_CHECK_CAST (obj, LIBBALSA_TYPE_ADDRESS_ENTRY, LibBalsaAddressEntry))
#define LIBBALSA_ADDRESS_ENTRY_CLASS(klass)	(GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_ENTRY, LibBalsaAddressEntryClass))
#define LIBBALSA_IS_ADDRESS_ENTRY(obj)		(GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_ADDRESS_ENTRY))
#define LIBBALSA_IS_ADDRESS_ENTRY_CLASS(klass)	(GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_ENTRY))

/* LibBalsaAddressEntry is typedef'd here, but the structure is declared
 * in address-entry.c to keep it opaque */
typedef struct _LibBalsaAddressEntry LibBalsaAddressEntry;
typedef struct _LibBalsaAddressEntryClass LibBalsaAddressEntryClass;


struct _LibBalsaAddressEntryClass {
    GtkEntryClass parent_class;
};


/*************************************************************************
 *                                                                       *
 * Functions that outside programs are allowed to call to act on the     *
 * widget.                                                               *
 *                                                                       *
 * LibBalsaAddressEntry subclasses GtkEntry, so any gtk_entry_* or       *
 * gtk_editable_* method may also be used, by casting accordingly.       *
 *                                                                       *
 *************************************************************************/

GtkType libbalsa_address_entry_get_type(void);
GtkWidget *libbalsa_address_entry_new(void);
void libbalsa_address_entry_set_find_match(LibBalsaAddressEntry *, void *);
void libbalsa_address_entry_set_domain(LibBalsaAddressEntry *, void *);
void libbalsa_address_entry_clear_to_send(LibBalsaAddressEntry *
                                          address_entry);
gboolean libbalsa_address_entry_matching(LibBalsaAddressEntry *
                                         address_entry);

GList *libbalsa_address_entry_get_list(LibBalsaAddressEntry *address_entry);
#endif
