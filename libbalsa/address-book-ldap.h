/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

/*
 * The LDAP address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_LDAP_H__
#define __LIBBALSA_ADDRESS_BOOK_LDAP_H__

#include <lber.h>
#include <ldap.h>

#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_LDAP		(libbalsa_address_book_ldap_get_type())
#define LIBBALSA_ADDRESS_BOOK_LDAP(obj)		(G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_ADDRESS_BOOK_LDAP, LibBalsaAddressBookLdap))
#define LIBBALSA_ADDRESS_BOOK_LDAP_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK_LDAP, LibBalsaAddressBookLdapClass))
#define LIBBALSA_IS_ADDRESS_BOOK_LDAP(obj)		(G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_ADDRESS_BOOK_LDAP))
#define LIBBALSA_IS_ADDRESS_BOOK_LDAP_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK_LDAP))

typedef struct _LibBalsaAddressBookLdap LibBalsaAddressBookLdap;
typedef struct _LibBalsaAddressBookLdapClass LibBalsaAddressBookLdapClass;

struct _LibBalsaAddressBookLdap {
    LibBalsaAddressBook parent;

    gchar *host;
    gchar *base_dn;
    gchar *bind_dn;
    gchar *priv_book_dn; /* location of user-writeable entries */
    gchar *passwd;
    gboolean enable_tls;

    LDAP *directory;
};

struct _LibBalsaAddressBookLdapClass {
    LibBalsaAddressBookClass parent_class;
};

GType libbalsa_address_book_ldap_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_ldap_new(const gchar *name,
						    const gchar *host,
						    const gchar *base_dn,
						    const gchar *bind_dn,
						    const gchar *passwd,
                                                    const gchar *priv_book_dn,
                                                    gboolean enable_tls);
void libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap *ab);


#endif				/* __LIBBALSA_ADDRESS_BOOK_LDAP_H__ */
