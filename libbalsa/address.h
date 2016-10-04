/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_ADDRESS_H__
#define __LIBBALSA_ADDRESS_H__

#include <gtk/gtk.h>
#include <gmime/gmime.h>

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
    LAST_NAME,
    NICK_NAME,
    ORGANIZATION,
    EMAIL_ADDRESS,
    NUM_FIELDS
};

/* General address structure to be used with address books.
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
     * VCard: parsed from N: field
     * LDAP/LDIF: cn, givenName, surName.
     */
    gchar *full_name;
    gchar *first_name;
    gchar *last_name;

    /* Organisation
     * VCard: ORG: field
     * ldif: o: attribute.
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
LibBalsaAddress *libbalsa_address_new_from_vcard(const gchar *str,
						 const gchar *charset);
gchar * libbalsa_address_extract_name(const gchar * string,
                                      gchar ** last_name,
                                      gchar ** first_name);

void libbalsa_address_set_copy(LibBalsaAddress *dest, LibBalsaAddress *src);
gchar *libbalsa_address_to_gchar(LibBalsaAddress * address, gint n);

const gchar *libbalsa_address_get_name_from_list(InternetAddressList
                                                 * address_list);
const gchar *libbalsa_address_get_mailbox_from_list(InternetAddressList *
                                                    address_list);
gint libbalsa_address_n_mailboxes_in_list(InternetAddressList *
                                          address_list);

/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_set_edit_entries() initializes the GtkEntry widgets
    in entries with values from address
*/
void libbalsa_address_set_edit_entries(const LibBalsaAddress * address,
                                       GtkWidget ** entries);
/** libbalsa_address_get_edit_widget() returns an widget adapted for a
    LibBalsaAddress edition, with initial values set if address is
    provided. The edit entries are set in entries array and enumerated
    with LibBalsaAddressField constants. The widget accepts drops of
    type TARGET_ADDRESS and TARGET_STRING.
*/

enum {
    LIBBALSA_ADDRESS_TRG_STRING,
    LIBBALSA_ADDRESS_TRG_ADDRESS
};

extern GtkTargetEntry libbalsa_address_target_list[2];

GtkWidget *libbalsa_address_get_edit_widget(const LibBalsaAddress *addr,
                                            GtkWidget **entries,
                                            GCallback changed_cb,
                                            gpointer changed_data);
LibBalsaAddress *libbalsa_address_new_from_edit_entries(GtkWidget **widget);
#endif				/* __LIBBALSA_ADDRESS_H__ */
