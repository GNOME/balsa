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

/*
 * LDAP address book
 */
#include "config.h"

#if ENABLE_LDAP

#include <gnome.h>
#include <lber.h>
#include <ldap.h>

#include "address-book.h"
#include "address-book-ldap.h"
#include "information.h"

/* FIXME: Configurable... */
#define LDAP_CACHE_TIMEOUT 300	/* Seconds */

static GtkObjectClass *parent_class = NULL;

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass *
				      klass);
static void libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab);
static void libbalsa_address_book_ldap_destroy(GtkObject * object);

static void libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab);

static gboolean
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab);
static void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab);

static void libbalsa_address_book_ldap_add_from_server(LibBalsaAddressBook *ab,
						       LDAPMessage * e);

static void libbalsa_address_book_ldap_save_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);
static void libbalsa_address_book_ldap_load_config(LibBalsaAddressBook *ab,
						   const gchar * prefix);

static GList *libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix, 
							 gchar ** new_prefix);

static gchar *create_name(gchar *, gchar *);

GtkType libbalsa_address_book_ldap_get_type(void)
{
    static GtkType address_book_ldap_type = 0;

    if (!address_book_ldap_type) {
	static const GtkTypeInfo address_book_ldap_info = {
	    "LibBalsaAddressBookLdap",
	    sizeof(LibBalsaAddressBookLdap),
	    sizeof(LibBalsaAddressBookLdapClass),
	    (GtkClassInitFunc) libbalsa_address_book_ldap_class_init,
	    (GtkObjectInitFunc) libbalsa_address_book_ldap_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_book_ldap_type =
	    gtk_type_unique(libbalsa_address_book_get_type(),
			    &address_book_ldap_info);
    }

    return address_book_ldap_type;
}

static void
libbalsa_address_book_ldap_class_init(LibBalsaAddressBookLdapClass * klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(LIBBALSA_TYPE_ADDRESS_BOOK);

    object_class = GTK_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->destroy = libbalsa_address_book_ldap_destroy;

    address_book_class->load = libbalsa_address_book_ldap_load;

    address_book_class->save_config =
	libbalsa_address_book_ldap_save_config;
    address_book_class->load_config =
	libbalsa_address_book_ldap_load_config;

    address_book_class->alias_complete = 
	libbalsa_address_book_vcard_alias_complete;
}

static void
libbalsa_address_book_ldap_init(LibBalsaAddressBookLdap * ab)
{
    ab->host = NULL;
    ab->base_dn = NULL;
    ab->directory = NULL;
}

static void
libbalsa_address_book_ldap_destroy(GtkObject * object)
{
    LibBalsaAddressBookLdap *addr_ldap;

    addr_ldap = LIBBALSA_ADDRESS_BOOK_LDAP(object);

    g_free(addr_ldap->host);
    addr_ldap->host = NULL;
    g_free(addr_ldap->base_dn);
    addr_ldap->base_dn = NULL;

    libbalsa_address_book_ldap_close_connection(addr_ldap);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

}

LibBalsaAddressBook *
libbalsa_address_book_ldap_new(const gchar * name, const gchar * host,
			       const gchar * base_dn)
{
    LibBalsaAddressBookLdap *ldap;
    LibBalsaAddressBook *ab;

    ldap = gtk_type_new(LIBBALSA_TYPE_ADDRESS_BOOK_LDAP);
    ab = LIBBALSA_ADDRESS_BOOK(ldap);

    ab->name = g_strdup(name);
    ldap->host = g_strdup(host);
    ldap->base_dn = g_strdup(base_dn);

    /* We open on demand... */
    ldap->directory = NULL;
    return ab;
}

/*
 * Close the ldap connection....
 */
static void
libbalsa_address_book_ldap_close_connection(LibBalsaAddressBookLdap * ab)
{
    if (ab->directory) {
	ldap_destroy_cache(ab->directory);
	ldap_unbind(ab->directory);
	ab->directory = NULL;
    }
}

/*
 * Opens the ldap connection, and binds to the server.
 * Also enables LDAP caching.
 */
static gboolean
libbalsa_address_book_ldap_open_connection(LibBalsaAddressBookLdap * ab)
{
    int result;

    g_return_val_if_fail(ab->host != NULL, FALSE);

    ab->directory = ldap_init(ab->host, LDAP_PORT);
    if (ab->directory == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to initialise LDAP server.\n"
			       "Check that the servername is valid."));
	perror("ldap_init");
	return FALSE;
    }

    result = ldap_simple_bind_s(ab->directory, NULL, NULL);

    if (result != LDAP_SUCCESS) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to bind to server: %s\n"
			       "Check that the servername is valid."),
			     ldap_err2string(result));
	ldap_unbind_s(ab->directory);
	return FALSE;
    }
    ldap_enable_cache(ab->directory, LDAP_CACHE_TIMEOUT, 0);
    return TRUE;
}


/*
 * ldap_load_addresses ()
 *
 * Load addresses in the LDAP server into a GList.
 *
 * Side effects:
 *   Spits out balsa_information() when it deems necessary.
 */
static void
libbalsa_address_book_ldap_load(LibBalsaAddressBook * ab)
{
    LibBalsaAddressBookLdap *ldap_ab;
    LDAPMessage *result, *e;
    int rc, num_entries = 0;

    g_list_foreach(ab->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(ab->address_list);
    ab->address_list = NULL;

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    /*
     * Connect to the server.
     */
    if (ldap_ab->directory == NULL) {
	if (!libbalsa_address_book_ldap_open_connection(ldap_ab))
	    return;
    }

    /*
     * Attempt to search for e-mail addresses.  It returns success
     * or failure, but not all the matches.
     */
    rc = ldap_search_s(ldap_ab->directory, ldap_ab->base_dn,
		       LDAP_SCOPE_SUBTREE, "(mail=*)", NULL, 0, &result);
    if (rc != LDAP_SUCCESS) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to do a search: %s"
			       "Check that the base name is valid."),
			     ldap_err2string(rc));
	return;
    }

    /*
     * Now loop over all the results, and spit out the output.
     */
    num_entries = 0;
    e = ldap_first_entry(ldap_ab->directory, result);
    while (e != NULL) {
	libbalsa_address_book_ldap_add_from_server(ab, e);

	e = ldap_next_entry(ldap_ab->directory, e);
    }
    ldap_msgfree(result);

}

/*
 * ldap_add_from_server ()
 *
 * Load addresses from the server.  It loads a single address in an
 * LDAPMessage.
 *
 */
static void
libbalsa_address_book_ldap_add_from_server(LibBalsaAddressBook * ab,
					   LDAPMessage * e)
{
    LibBalsaAddressBookLdap *ldap_ab;
    gchar *name = NULL, *email = NULL, *id = NULL;
    gchar *first = NULL, *last = NULL;
    LibBalsaAddress *address = NULL;
    char *a;
    char **vals;
    BerElement *ber = NULL;
    int i;

    ldap_ab = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    for (a = ldap_first_attribute(ldap_ab->directory, e, &ber);
	 a != NULL; a = ldap_next_attribute(ldap_ab->directory, e, ber)) {
	/*
	 * For each attribute, print the attribute name
	 * and values.
	 */
	if ((vals = ldap_get_values(ldap_ab->directory, e, a)) != NULL) {
	    for (i = 0; vals[i] != NULL; i++) {
		if ((g_strcasecmp(a, "sn") == 0) && (!last))
		    last = g_strdup(vals[i]);
		if ((g_strcasecmp(a, "cn") == 0) && (!id))
		    id = g_strdup(vals[i]);
		if ((g_strcasecmp(a, "givenname") == 0) && (!first))
		    first = g_strdup(vals[i]);
		if ((g_strcasecmp(a, "mail") == 0) && (!email))
		    email = g_strdup(vals[i]);
	    }
	    ldap_value_free(vals);
	}
    }
    /*
     * Record will have e-mail (searched)
     */
    name = create_name(first, last);

    address = libbalsa_address_new();

    address->first_name = first;
    address->last_name = last;
    address->address_list = g_list_append(address->address_list, email);

    if (id)
	address->id = id;
    else
	address->id = g_strdup(_("No-Id"));

    if (name)
	address->full_name = name;
    else if (id)
	address->full_name = g_strdup(id);
    else
	address->full_name = g_strdup(_("No-Name"));

    ab->address_list = g_list_append(ab->address_list, address);

    name = NULL;
    id = NULL;
    email = NULL;
    /*
     * Man page says: please free this when done.
     * If I do, I get segfault.
     * gdb session shows that ldap_unbind attempts to free
     * this later anyway (documentation for older version?)
     if (ber != NULL) ber_free (ber, 0);
     */
}

/*
 * create_name()
 *
 * Creates a full name from a given first name and surname.
 * 
 * Returns:
 *   gchar * a full name
 *   NULL on failure (both first and last names invalid.
 */
static gchar *
create_name(gchar * first, gchar * last)
{
    if ((first == NULL) && (last == NULL))
	return NULL;
    else if (first == NULL)
	return g_strdup(last);
    else if (last == NULL)
	return g_strdup(first);
    else
	return g_strdup_printf("%s %s", first, last);
}

static void
libbalsa_address_book_ldap_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookLdap *ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    gnome_config_set_string("Host", ldap->host);
    gnome_config_set_string("BaseDN", ldap->base_dn);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_ldap_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    LibBalsaAddressBookLdap *ldap;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDAP(ab));

    ldap = LIBBALSA_ADDRESS_BOOK_LDAP(ab);

    ldap->host = gnome_config_get_string("Host");
    ldap->base_dn = gnome_config_get_string("BaseDN");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList *libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix, 
							 gchar ** new_prefix)
{
    g_return_val_if_fail ( LIBBALSA_ADDRESS_BOOK_LDAP(ab), NULL);
    g_warning(_("Alias completion not supported for LDAP - Yet!\n"));
    return NULL;
}
#endif				/*LDAP_ENABLED */
