/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1999 Stuart Parmenter
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

#ifndef __LIBBALSA_CONTACT_H__
#define __LIBBALSA_CONTACT_H__

#include <glib.h>

#include "libbalsa.h"

enum
{
    CARD_NAME,
    FIRST_NAME,
    LAST_NAME,
    ORGANIZATION,
    EMAIL_ADDRESS,
    NUM_FIELDS
};

    /* possible error values obtained while trying to store a contact vCard */
enum
{
    LIBBALSA_CONTACT_CARD_STORED_SUCCESSFULLY,
    LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE,
    LIBBALSA_CONTACT_CARD_NAME_FIELD_EMPTY,
    LIBBALSA_CONTACT_CARD_NAME_EXISTS
};

struct _LibBalsaContact
{
    gchar *card_name;
    gchar *first_name;
    gchar *last_name;
    gchar *organization;
    gchar *email_address;
};

LibBalsaContact *libbalsa_contact_new(void);
void libbalsa_contact_free(LibBalsaContact *contact);
void libbalsa_contact_list_free(GList *contact_list);
gint libbalsa_contact_store(LibBalsaContact *contact, const gchar *fname);

#endif /* __LIBBALSA_CONTACT_H__ */
