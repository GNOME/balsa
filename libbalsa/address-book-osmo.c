/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * Osmo address book support has been written by Copyright (C) 2016
 * Albrecht Dreß <albrecht.dress@arcor.de>.
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
 * Osmo address book
 *
 * Add remarks here...
 */
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if defined(HAVE_OSMO)
#include "address-book-osmo.h"

#include <glib/gi18n.h>
#include "rfc6350.h"

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "address-book"


/* for the time being, osmo svn rev. 1099 accepts only reading via DBus, not writing new or modified records */
#undef OSMO_CAN_WRITE


#define LOOKUP_MIN_LEN					2U


static void libbalsa_address_book_osmo_finalize(GObject *object);
static LibBalsaABErr libbalsa_address_book_osmo_load(LibBalsaAddressBook 		 *ab,
                                                     const gchar				 *filter,
                                                     LibBalsaAddressBookLoadFunc callback,
                                                     gpointer					 closure);
static GList *libbalsa_address_book_osmo_alias_complete(LibBalsaAddressBook *ab,
                                                        const gchar			*prefix);
static GList *osmo_read_addresses(LibBalsaAddressBookOsmo *ab_osmo,
                                     const gchar             *filter,
								  GError				  **error);


struct _LibBalsaAddressBookOsmo {
	LibBalsaAddressBook parent;

	GDBusProxy *proxy;
};

G_DEFINE_TYPE(LibBalsaAddressBookOsmo, libbalsa_address_book_osmo, LIBBALSA_TYPE_ADDRESS_BOOK);


static void
libbalsa_address_book_osmo_class_init(LibBalsaAddressBookOsmoClass *klass)
{
	LibBalsaAddressBookClass *address_book_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

	object_class->finalize = libbalsa_address_book_osmo_finalize;

	address_book_class->load = libbalsa_address_book_osmo_load;
#if defined(OSMO_CAN_WRITE)
	address_book_class->add_address = libbalsa_address_book_osmo_add_address;
	address_book_class->remove_address = libbalsa_address_book_osmo_remove_address;
	address_book_class->modify_address = libbalsa_address_book_osmo_modify_address;
#endif

	address_book_class->alias_complete = libbalsa_address_book_osmo_alias_complete;
}


static void
libbalsa_address_book_osmo_init(LibBalsaAddressBookOsmo *ab_osmo)
{
	libbalsa_address_book_set_is_expensive(LIBBALSA_ADDRESS_BOOK(ab_osmo), FALSE);
}


static void
libbalsa_address_book_osmo_finalize(GObject *object)
{
	LibBalsaAddressBookOsmo *ab_osmo;

	ab_osmo = LIBBALSA_ADDRESS_BOOK_OSMO(object);
	if (ab_osmo->proxy != NULL) {
		g_object_unref(ab_osmo->proxy);
		ab_osmo->proxy = NULL;
	}

	G_OBJECT_CLASS(libbalsa_address_book_osmo_parent_class)->finalize(object);
}


LibBalsaAddressBook *
libbalsa_address_book_osmo_new(const gchar *name)
{
	LibBalsaAddressBook *ab = NULL;
	LibBalsaAddressBookOsmo *ab_osmo;

	ab_osmo = LIBBALSA_ADDRESS_BOOK_OSMO(g_object_new(LIBBALSA_TYPE_ADDRESS_BOOK_OSMO, NULL));
	ab = LIBBALSA_ADDRESS_BOOK(ab_osmo);
	libbalsa_address_book_set_name(ab, name);

	return ab;
}


static LibBalsaABErr
libbalsa_address_book_osmo_load(LibBalsaAddressBook 		*ab,
                                const gchar 				*filter,
                                LibBalsaAddressBookLoadFunc callback,
                                gpointer					closure)
{
	LibBalsaABErr result;

	g_return_val_if_fail (LIBBALSA_IS_ADDRESS_BOOK_OSMO(ab), LBABERR_OK);

	if (callback == NULL) {
		result = LBABERR_OK;
	} else {
		LibBalsaAddressBookOsmo *ab_osmo;
		GError *error = NULL;
		GList *addresses;

		ab_osmo = LIBBALSA_ADDRESS_BOOK_OSMO(ab);

		addresses = osmo_read_addresses(ab_osmo, filter, &error);
		if (error != NULL) {
                        gchar *message;

			message =
                            g_strdup_printf(_("Reading Osmo contacts failed: %s"), error->message);
			libbalsa_address_book_set_status(ab, message);
                        g_free(message);
			g_error_free(error);
			result = LBABERR_CANNOT_SEARCH;
		} else {
			GList *this_addr;

			for (this_addr = addresses; this_addr != NULL; this_addr = this_addr->next) {
				callback(ab, LIBBALSA_ADDRESS(this_addr->data), closure);
			}
			callback(ab, NULL, closure);
			g_list_free_full(addresses, g_object_unref);
			libbalsa_address_book_set_status(ab, NULL);
			result = LBABERR_OK;
		}
	}

	return result;
}


/** \brief Utf8-safe strstr
 *
 * \param haystack utf8 "haystack" string
 * \param utf8_needle utf8 "needle" string as returned by g_utf8_casefold()
 * \return TRUE if needle is found anywhere in haystack
 */
static gboolean
utf8_strstr(const gchar *haystack, const gchar *utf8_needle)
{
	gboolean result;

	if (haystack != NULL) {
		gchar *test;

		test = g_utf8_casefold(haystack, -1);
		result = (strstr(test, utf8_needle) != NULL);
		g_free(test);
	} else {
		result = FALSE;
	}
	return result;
}


/** \brief Check for a pattern in a LibBalsaAddress
 *
 * \param address address object
 * \param utf8_needle utf8 "needle" string as returned by g_utf8_casefold()
 * \return TRUE if any address string field contains needle
 *
 * The fields checked are LibBalsaAddress::full_name, LibBalsaAddress::first_name, LibBalsaAddress::last_name,
 * LibBalsaAddress::nick_name and LibBalsaAddress::organization.
 */
static inline gboolean
utf8_lba_strstr(LibBalsaAddress *address, const gchar *utf8_needle)
{
    return utf8_strstr(libbalsa_address_get_full_name(address), utf8_needle)  ||
           utf8_strstr(libbalsa_address_get_first_name(address), utf8_needle) ||
           utf8_strstr(libbalsa_address_get_last_name(address), utf8_needle)  ||
           utf8_strstr(libbalsa_address_get_nick_name(address), utf8_needle)  ||
           utf8_strstr(libbalsa_address_get_organization(address), utf8_needle);
}


static GList *
libbalsa_address_book_osmo_alias_complete(LibBalsaAddressBook *ab,
                                          const gchar 		  *prefix)
{
	LibBalsaAddressBookOsmo *ab_osmo;
	GError *error = NULL;
	GList *addresses;
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_ADDRESS_BOOK_OSMO(ab), NULL);

	ab_osmo = LIBBALSA_ADDRESS_BOOK_OSMO(ab);

	if (!libbalsa_address_book_get_expand_aliases(ab) || strlen(prefix) < LOOKUP_MIN_LEN)
		return NULL;

	g_debug("%s: filter for %s", __func__, prefix);
	addresses = osmo_read_addresses(ab_osmo, prefix, &error);
	if (error != NULL) {
		libbalsa_address_book_set_status(ab, error->message);
		g_error_free(error);
	} else {
		GList *p;
		gchar *utf8_filter;

		utf8_filter = g_utf8_casefold(prefix, -1);
		for (p = addresses; p != NULL; p = p->next) {
			LibBalsaAddress *this_addr = LIBBALSA_ADDRESS(p->data);
			gboolean names_match;
                        guint n_addrs;
                        guint n;

			names_match = utf8_lba_strstr(this_addr, utf8_filter);
                        n_addrs = libbalsa_address_get_n_addrs(this_addr);
			for (n = 0; n < n_addrs; ++n) {
				const gchar *mail_addr =
                                    libbalsa_address_get_nth_addr(this_addr, n);

				if (names_match || (strstr(mail_addr, prefix) != NULL)) {
                                        const gchar *full_name;
					InternetAddress *addr;

                                        full_name = libbalsa_address_get_full_name(this_addr);
					g_debug("%s: found %s <%s>", __func__,
                                                full_name, mail_addr);
					addr = internet_address_mailbox_new(full_name,
                                                mail_addr);
					result = g_list_prepend(result, g_object_ref(addr));
				}
			}
		}

		g_free(utf8_filter);
		g_list_free_full(addresses, g_object_unref);

		result = g_list_reverse(result);
	}

	return result;
}


/** \brief Read filtered addresses from Osmo via DBus
 *
 * \param ab_osmo Osmo address book object
 * \param filter search filter, NULL or "" for all entries
 * \param error filled with error information on error
 * \return a list \ref LibBalsaAddress items on success or NULL on error or if no item matches the search filter
 *
 * Create the proxy LibBalsaAddressBookOsmo::proxy if required, and ask Osmo for addresses.  Only items with any mail address are
 * added to the returned list.  The caller can distinguish between an error and an empty query result by checking if error is not
 * NULL.
 *
 * \note The caller must free the returned list.
 */
static GList *
osmo_read_addresses(LibBalsaAddressBookOsmo *ab_osmo,
                    const gchar             *filter,
                    GError                  **error)
{
	GList *addresses = NULL;

	/* connect to DBus unless we already have a proxy */
	if (ab_osmo->proxy == NULL) {
		ab_osmo->proxy =
			g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, NULL, "org.clayo.osmo.Contacts",
									  	  "/org/clayo/osmo/Contacts", "org.clayo.osmo.Contacts", NULL, error);
	}

	/* proceed only if we have the proxy */
	if (ab_osmo->proxy != NULL) {
		GVariant *request;
		GVariant *reply;

		if (filter != NULL) {
			request = g_variant_new("(s)", filter);
		} else {
			request = g_variant_new("(s)", "");
		}
		reply = g_dbus_proxy_call_sync(ab_osmo->proxy, "Find", request, G_DBUS_CALL_FLAGS_NONE, -1, NULL, error);

		/* proceed only if we got a reply */
		if (reply != NULL) {
			gchar *vcards;
			GInputStream *stream;
			GDataInputStream *data;
			gboolean eos;

			/* create a stream from the VCards */
			g_variant_get(reply, "(s)", &vcards);
			stream = g_memory_input_stream_new_from_data(vcards, -1, NULL);
			data = g_data_input_stream_new(stream);
			g_data_input_stream_set_newline_type(data, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

			/* decode all returned VCard's, skip those without email addresses */
			eos = FALSE;
			do {
				LibBalsaAddress *this_addr;

				this_addr = rfc6350_parse_from_stream(data, &eos, error);
				if (this_addr != NULL) {
					if (libbalsa_address_get_addr(this_addr) != NULL) {
						addresses = g_list_prepend(addresses, this_addr);
					} else {
						g_object_unref(this_addr);
					}
				}
			} while (!eos && (*error == NULL));

			/* clean up */
			g_object_unref(data);
			g_object_unref(stream);
			g_free(vcards);
			g_variant_unref(reply);

			/* drop list on error, reverse order otherwise */
			if (addresses != NULL) {
				if (*error != NULL) {
					g_list_free_full(addresses, g_object_unref);
					addresses = NULL;
				} else {
					addresses = g_list_reverse(addresses);
				}
			}
		}
	}

	return addresses;
}

#endif /* HAVE_OSMO */
