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

#ifndef __LIBBALSA_ADDRESS_H__
#define __LIBBALSA_ADDRESS_H__

#include <gtk/gtkobject.h>

#define LIBBALSA_TYPE_ADDRESS				(libbalsa_address_get_type())
#define LIBBALSA_ADDRESS(obj)				(GTK_CHECK_CAST (obj, LIBBALSA_TYPE_ADDRESS, LibBalsaAddress))
#define LIBBALSA_ADDRESS_CLASS(klass)			(GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS, LibBalsaAddressClass))
#define LIBBALSA_IS_ADDRESS(obj)			(GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_ADDRESS))
#define LIBBALSA_IS_ADDRESS_CLASS(klass)		(GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS))

typedef struct _LibBalsaAddress LibBalsaAddress;
typedef struct _LibBalsaAddressClass LibBalsaAddressClass;

typedef enum _LibBalsaAddressField LibBalsaAddressField;

enum _LibBalsaAddressField {
    FULL_NAME,
    FIRST_NAME,
    LAST_NAME,
    ORGANIZATION,
    EMAIL_ADDRESS,
    NUM_FIELDS
};

struct _LibBalsaAddress {
    GtkObject parent;

    /*
     * ID
     * VCard FN: Field
     * An ldap feature..
     */
    gchar *id;

    /* First and last names
     * VCard: N: field
     * Full name is the bit in <> in an rfc822 address
     */
    gchar *full_name;
    gchar *first_name;
    gchar *last_name;

    /* Organisation
     * VCard: ORG: field
     */
    gchar *organization;

    /* Email addresses
     * A list of user@domain.
     */
    GList *address_list;
};

struct _LibBalsaAddressClass {
    GtkObjectClass parent_class;
};

GtkType libbalsa_address_get_type(void);

LibBalsaAddress *libbalsa_address_new(void);
LibBalsaAddress *libbalsa_address_new_from_string(gchar * address);
GList *libbalsa_address_new_list_from_string(gchar * address);

gchar *libbalsa_address_to_gchar(LibBalsaAddress * addr, gint n);

/* get pointer to descriptive name (full name if available, or e-mail) */
const gchar *libbalsa_address_get_name(const LibBalsaAddress * addr);

#endif				/* __LIBBALSA_ADDRESS_H__ */
