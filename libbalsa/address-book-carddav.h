/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others, See the file AUTHORS for a list.
 *
 * CardDAV address book support has been written by Copyright (C) Albrecht Dreß <mailto:albrecht.dress@posteo.de> 2023.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/*
 * Note: This implementation has the following limitations:
 * * Only secure CardDAV (i.e. https) servers are supported.  Connecting to servers running http only is not possible.
 * * The server must support basic authentication.  Using providers requiring OAuth2 (e.g. GMail) is currently unsupported.
 * * The implementation supports reading existing contacts and adding new ones, but not deleting or modifying existing contacts.
 */

#ifndef LIBBALSA_ADDRESS_BOOK_CARDDAV_H__
#define LIBBALSA_ADDRESS_BOOK_CARDDAV_H__

#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV		(libbalsa_address_book_carddav_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaAddressBookCarddav,
					 libbalsa_address_book_carddav,
					 LIBBALSA,
					 ADDRESS_BOOK_CARDDAV,
					 LibBalsaAddressBook);


/** @brief Create a CardDAV address book
 *
 * @param[in] name address book name
 * @param[in] base_dom_url domain or URL of the CardDAV service
 * @param[in] user user name, required
 * @param[in] password password
 * @param[in] full_uri full @c https URL of the address book, required
 * @param[in] carddav_name WebDAV (RFC 4918) @em displayname of the address book
 * @param[in] refresh_minutes time in minutes after which the internal CardDAV cache is refreshed if necessary
 * @param[in] force_mget force using @em addressbook-multiget for broken CardDAV servers
 * @return a new CardDAV address book instance
 */
LibBalsaAddressBook *libbalsa_address_book_carddav_new(const gchar *name,
													   const gchar *base_dom_url,
													   const gchar *user,
													   const gchar *password,
													   const gchar *full_uri,
													   const gchar *carddav_name,
													   guint        refresh_minutes,
													   gboolean     force_mget);

/* getters */
const gchar *libbalsa_address_book_carddav_get_base_dom_url(LibBalsaAddressBookCarddav *ab);
const gchar *libbalsa_address_book_carddav_get_user(LibBalsaAddressBookCarddav *ab);
const gchar *libbalsa_address_book_carddav_get_password(LibBalsaAddressBookCarddav *ab);
const gchar *libbalsa_address_book_carddav_get_full_url(LibBalsaAddressBookCarddav *ab);
const gchar *libbalsa_address_book_carddav_get_carddav_name(LibBalsaAddressBookCarddav *ab);
guint libbalsa_address_book_carddav_get_refresh(LibBalsaAddressBookCarddav *ab);
gboolean libbalsa_address_book_carddav_get_force_mget(LibBalsaAddressBookCarddav *ab);

/* setters */
void libbalsa_address_book_carddav_set_base_dom_url(LibBalsaAddressBookCarddav *ab,
													const gchar                *base_dom_url);
void libbalsa_address_book_carddav_set_user(LibBalsaAddressBookCarddav *ab,
											const gchar                *user);
void libbalsa_address_book_carddav_set_password(LibBalsaAddressBookCarddav *ab,
												const gchar                *password);
void libbalsa_address_book_carddav_set_full_url(LibBalsaAddressBookCarddav *ab,
												const gchar                *full_url);
void libbalsa_address_book_carddav_set_carddav_name(LibBalsaAddressBookCarddav *ab,
													const gchar                *carddav_name);
void libbalsa_address_book_carddav_set_refresh(LibBalsaAddressBookCarddav *ab,
											   guint                       refresh_minutes);
void libbalsa_address_book_carddav_set_force_mget(LibBalsaAddressBookCarddav *ab,
												  gboolean                    force_mget);

#endif		/* LIBBALSA_ADDRESS_BOOK_CARDDAV_H__ */
