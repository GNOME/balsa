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


#ifndef __LIBBALSA_ADDRESS_VIEW_H__
#define __LIBBALSA_ADDRESS_VIEW_H__

#include <gtk/gtk.h>
#include <gmime/gmime.h>

G_BEGIN_DECLS
#define LIBBALSA_TYPE_ADDRESS_VIEW (libbalsa_address_view_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaAddressView,
                     libbalsa_address_view,
                     LIBBALSA,
                     ADDRESS_VIEW,
                     GtkTreeView)

LibBalsaAddressView *libbalsa_address_view_new(const gchar * const *types,
                                               guint n_types,
                                               GList * address_book_list,
                                               gboolean fallback);
void libbalsa_address_view_set_domain(LibBalsaAddressView * address_view,
                                      const gchar * domain);
void libbalsa_address_view_set_from_string(LibBalsaAddressView *
                                           address_view,
                                           const gchar * address_type,
                                           const gchar * addresses);
void libbalsa_address_view_add_from_string(LibBalsaAddressView *
                                           address_view,
                                           const gchar * address_type,
                                           const gchar * addresses);
void libbalsa_address_view_add_to_row(LibBalsaAddressView * address_view,
                                      GtkTreeRowReference * row_ref,
                                      const gchar * addresses);
void libbalsa_address_view_set_from_list(LibBalsaAddressView *
                                         address_view,
                                         const gchar * address_type,
                                         InternetAddressList * list);

gint libbalsa_address_view_n_addresses(LibBalsaAddressView * address_view);
InternetAddressList *libbalsa_address_view_get_list(LibBalsaAddressView *
                                                    address_view,
                                                    const gchar *
                                                    address_type);
void libbalsa_address_view_set_crypt_mode(LibBalsaAddressView *address_view,
                                          guint                mode);

void libbalsa_address_view_set_book_icon(const char * book_icon);
void libbalsa_address_view_set_close_icon(const char * close_icon);
void libbalsa_address_view_set_drop_down_icon(const char * drop_down_icon);
void libbalsa_address_view_set_key_icons(const char *yes_icon,
                                         const char *no_icon);

G_END_DECLS
#endif                          /* __LIBBALSA_ADDRESS_VIEW_H__ */
