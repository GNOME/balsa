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

#ifndef __CONTACT_H__
#define __CONTACT_H__

#include <glib.h>

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
    CONTACT_CARD_STORED_SUCCESSFULLY,
    CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE,
    CONTACT_CARD_NAME_FIELD_EMPTY,
    CONTACT_CARD_NAME_EXISTS
};

struct _Contact
{
    gchar *card_name;
    gchar *first_name;
    gchar *last_name;
    gchar *organization;
    gchar *email_address;
};

Contact *contact_new(void);
void contact_free(Contact *contact);
void contact_list_free(GList *contact_list);
gint contact_store(Contact *contact, const gchar *fname);

#endif /* __CONTACT_H__ */
