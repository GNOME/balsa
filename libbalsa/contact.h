/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

#ifndef __LIBBALSA_CONTACT_H__
#define __LIBBALSA_CONTACT_H__

#include <gtk/gtkobject.h>

#define LIBBALSA_TYPE_CONTACT			(libbalsa_contact_get_type())
#define LIBBALSA_CONTACT(obj)			(GTK_CHECK_CAST (obj, LIBBALSA_TYPE_CONTACT, LibBalsaContact))
#define LIBBALSA_CONTACT_CLASS(klass)		(GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_CONTACT, LibBalsaContactClass))
#define LIBBALSA_IS_CONTACT(obj)			(GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_CONTACT))
#define LIBBALSA_IS_CONTACT_CLASS(klass)		(GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_CONTACT))

typedef struct _LibBalsaContact LibBalsaContact;
typedef struct _LibBalsaContactClass LibBalsaContactClass;

typedef enum _LibBalsaContactField LibBalsaContactField;
typedef enum _LibBalsaContactError LibBalsaContactError;

enum _LibBalsaContactField
{
	CARD_NAME,
	FIRST_NAME,
	LAST_NAME,
	ORGANIZATION,
	EMAIL_ADDRESS,
	NUM_FIELDS
};

/* possible error values obtained while trying to store a contact vCard */
enum _LibBalsaContactError
{
	LIBBALSA_CONTACT_CARD_STORED_SUCCESSFULLY,
	LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE,
	LIBBALSA_CONTACT_CARD_NAME_FIELD_EMPTY,
	LIBBALSA_CONTACT_CARD_NAME_EXISTS
};

struct _LibBalsaContact
{
	GtkObject parent;

	gchar *card_name;
	gchar *first_name;
	gchar *last_name;
	gchar *organization;
	gchar *email_address;
};

struct _LibBalsaContactClass
{
	GtkObjectClass parent_class;
};

GtkType libbalsa_contact_get_type(void);

LibBalsaContact *libbalsa_contact_new(void);
void libbalsa_contact_list_free(GList *contact_list);
gint libbalsa_contact_store(LibBalsaContact *contact, const gchar *fname);

#endif /* __LIBBALSA_CONTACT_H__ */

