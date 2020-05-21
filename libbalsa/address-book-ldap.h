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

/*
 * The LDAP address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_LDAP_H__
#define __LIBBALSA_ADDRESS_BOOK_LDAP_H__

#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_LDAP (libbalsa_address_book_ldap_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaAddressBookLdap, libbalsa_address_book_ldap,
        LIBBALSA, ADDRESS_BOOK_LDAP, LibBalsaAddressBook)

LibBalsaAddressBook *libbalsa_address_book_ldap_new(const gchar *name,
						    const gchar *host,
						    const gchar *base_dn,
						    const gchar *bind_dn,
						    const gchar *passwd,
                                                    const gchar *priv_book_dn,
                                                    gboolean enable_tls);
void libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap *ab_ldap);

/*
 * Getters
 */
const gchar * libbalsa_address_book_ldap_get_host   (LibBalsaAddressBookLdap * ab_ldap);
const gchar * libbalsa_address_book_ldap_get_passwd (LibBalsaAddressBookLdap * ab_ldap);
const gchar * libbalsa_address_book_ldap_get_base_dn(LibBalsaAddressBookLdap * ab_ldap);
const gchar * libbalsa_address_book_ldap_get_bind_dn(LibBalsaAddressBookLdap * ab_ldap);
const gchar * libbalsa_address_book_ldap_get_book_dn(LibBalsaAddressBookLdap * ab_ldap);
gboolean      libbalsa_address_book_ldap_get_enable_tls(LibBalsaAddressBookLdap *
                                                        ab_ldap);

/*
 * Setters
 */
void libbalsa_address_book_ldap_set_host      (LibBalsaAddressBookLdap * ab_ldap,
                                               const gchar             * host);
void libbalsa_address_book_ldap_set_passwd    (LibBalsaAddressBookLdap * ab_ldap,
                                               const gchar             * passwd);
void libbalsa_address_book_ldap_set_base_dn   (LibBalsaAddressBookLdap * ab_ldap,
                                               const gchar             * base_dn);
void libbalsa_address_book_ldap_set_bind_dn   (LibBalsaAddressBookLdap * ab_ldap,
                                               const gchar             * bind_dn);
void libbalsa_address_book_ldap_set_book_dn   (LibBalsaAddressBookLdap * ab_ldap,
                                               const gchar             * book_dn);
void libbalsa_address_book_ldap_set_enable_tls(LibBalsaAddressBookLdap * ab_ldap,
                                               gboolean                  enable_tls);

#endif				/* __LIBBALSA_ADDRESS_BOOK_LDAP_H__ */
