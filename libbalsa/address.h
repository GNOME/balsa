/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#ifndef __LIBBALSA_ADDRESS_H__
#define __LIBBALSA_ADDRESS_H__

#include <gtk/gtk.h>

#define LIBBALSA_TYPE_ADDRESS				(libbalsa_address_get_type())
#define LIBBALSA_ADDRESS(obj)				(G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_ADDRESS, LibBalsaAddress))
#define LIBBALSA_ADDRESS_CLASS(klass)			(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS, LibBalsaAddressClass))
#define LIBBALSA_IS_ADDRESS(obj)			(G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_ADDRESS))
#define LIBBALSA_IS_ADDRESS_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS))

typedef struct _LibBalsaAddress LibBalsaAddress;
typedef struct _LibBalsaAddressClass LibBalsaAddressClass;

typedef enum _LibBalsaAddressField LibBalsaAddressField;

enum _LibBalsaAddressField {
    FULL_NAME,
    FIRST_NAME,
#if !defined(ENABLE_TOUCH_UI)
    MIDDLE_NAME,
#endif
    LAST_NAME,
    NICK_NAME,
    ORGANIZATION,
    EMAIL_ADDRESS,
    NUM_FIELDS
};

/* General address structure. it should possible subclass more compact
   rfc2822_mailbox, or something.
*/
struct _LibBalsaAddress {
    GObject parent;

    /*
     * ID
     * VCard FN: Field
     * LDAP/LDIF: xmozillanickname
     */
    gchar *nick_name;

    /* First and last names
     * VCard: N: field
     * LDAP/LDIF: cn
     * Full name is the bit in <> in an rfc822 address
     */
    gchar *full_name;
    gchar *first_name;
    gchar *middle_name;
    gchar *last_name;

    /* Organisation
     * VCard: ORG: field
     */
    gchar *organization;

    /* Email addresses
     * A list of mailboxes, ie. user@domain.
     */
    GList *address_list;
};

struct _LibBalsaAddressClass {
    GObjectClass parent_class;
};

GType libbalsa_address_get_type(void);
 
LibBalsaAddress *libbalsa_address_new(void);
LibBalsaAddress *libbalsa_address_new_from_string(const gchar * address);
void libbalsa_address_set_copy(LibBalsaAddress *dest, LibBalsaAddress *src);
gchar *libbalsa_address_to_gchar(LibBalsaAddress * address, gint n);
GList *libbalsa_address_new_list_from_string(const gchar * address);

/* get pointer to descriptive name (full name if available, or e-mail) */
const gchar *libbalsa_address_get_name(const LibBalsaAddress * addr);

/* libbalsa_address_get_mailbox and libbalsa_address_get_phrase
   are used to create the ESMTP envelope of the message. Note that
   they have different semantics than libbalsa_addres_to_gchar() 
   when the address is a group. get_mailbox() must return all addresses
   while address_to_gchar() may return empty list.
*/
/* XXX - added by Brian Stafford <brian@stafford.uklinux.net> */
const gchar *libbalsa_address_get_mailbox(LibBalsaAddress * address, gint n);
#if ENABLE_ESMTP
/* XXX - added by Brian Stafford <brian@stafford.uklinux.net> */
const gchar *libbalsa_address_get_phrase(LibBalsaAddress * address);
#endif

/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_get_edit_widget() returns an widget adapted
    for a LibBalsaAddress edition, with initial values set if address
    is provided. The edit entries are set in entries array 
    and enumerated with LibBalsaAddressField constants
*/
GtkWidget *libbalsa_address_get_edit_widget(LibBalsaAddress *addr,
                                            GtkWidget **entries,
                                            GCallback changed_cb,
                                            gpointer changed_data);
LibBalsaAddress *libbalsa_address_new_from_edit_entries(GtkWidget **widget);
#endif				/* __LIBBALSA_ADDRESS_H__ */
