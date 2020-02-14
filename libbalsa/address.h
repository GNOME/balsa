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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_ADDRESS_H__
#define __LIBBALSA_ADDRESS_H__

#include <gmime/gmime.h>
#include <gtk/gtk.h>

#define LIBBALSA_TYPE_ADDRESS (libbalsa_address_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaAddress, libbalsa_address, LIBBALSA, ADDRESS, GObject)

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

LibBalsaAddress *libbalsa_address_new(void);
LibBalsaAddress *libbalsa_address_new_from_vcard(const gchar *str,
						 const gchar *charset);
gchar * libbalsa_address_extract_name(const gchar * string,
                                      gchar ** last_name,
                                      gchar ** first_name);

void libbalsa_address_set_copy(LibBalsaAddress *dest, LibBalsaAddress *src);
gchar *libbalsa_address_to_gchar(LibBalsaAddress * address, gint n);

const gchar *libbalsa_address_get_name_from_list(InternetAddressList
                                                 * addr_list);
const gchar *libbalsa_address_get_mailbox_from_list(InternetAddressList *
                                                    addr_list);
gint libbalsa_address_n_mailboxes_in_list(InternetAddressList *
                                          addr_list);

/* =================================================================== */
/*                                UI PART                              */
/* =================================================================== */

/** libbalsa_address_set_edit_entries() initializes the GtkEntry widgets
    in entries with values from address
*/
void libbalsa_address_set_edit_entries(LibBalsaAddress * address,
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

GtkWidget *libbalsa_address_get_edit_widget(LibBalsaAddress *addr,
                                            GtkWidget **entries,
                                            GCallback changed_cb,
                                            gpointer changed_data);
LibBalsaAddress *libbalsa_address_new_from_edit_entries(GtkWidget **widget);

/*
 * Comparison func
 */
gint libbalsa_address_compare(LibBalsaAddress *a,
                              LibBalsaAddress *b);

/*
 * Getters
 */

const gchar * libbalsa_address_get_full_name   (LibBalsaAddress * address);
const gchar * libbalsa_address_get_first_name  (LibBalsaAddress * address);
const gchar * libbalsa_address_get_last_name   (LibBalsaAddress * address);
const gchar * libbalsa_address_get_nick_name   (LibBalsaAddress * address);
const gchar * libbalsa_address_get_organization(LibBalsaAddress * address);
const gchar * libbalsa_address_get_addr        (LibBalsaAddress * address);
guint         libbalsa_address_get_n_addrs     (LibBalsaAddress * address);
const gchar * libbalsa_address_get_nth_addr    (LibBalsaAddress * address, guint n);

/*
 * Setters
 */

void libbalsa_address_set_full_name   (LibBalsaAddress * address,
                                       const gchar     * full_name);
void libbalsa_address_set_first_name  (LibBalsaAddress * address,
                                       const gchar     * first_name);
void libbalsa_address_set_last_name   (LibBalsaAddress * address,
                                       const gchar     * last_name);
void libbalsa_address_set_nick_name   (LibBalsaAddress * address,
                                       const gchar     * nick_name);
void libbalsa_address_set_organization(LibBalsaAddress * address,
                                       const gchar     * organization);
void libbalsa_address_append_addr     (LibBalsaAddress * address,
                                       const gchar     * addr);

#endif				/* __LIBBALSA_ADDRESS_H__ */
