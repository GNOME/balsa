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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif						/* HAVE_CONFIG_H */


#if defined(HAVE_WEBDAV)


#include <glib/gi18n.h>
#include "libbalsa-carddav.h"
#include "address-book-carddav.h"
#include "libbalsa-conf.h"
#include "information.h"

#if defined(HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif	/* defined(HAVE_LIBSECRET) */


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "address-book"


struct _LibBalsaAddressBookCarddav {
	LibBalsaAddressBook parent;

	LibBalsaCarddav *carddav;

	gchar *base_dom_url;
	gchar *user;
	gchar *password;
	gchar *full_url;
	gchar *carddav_name;
	guint refresh_minutes;
	gboolean force_mget;
};


G_DEFINE_TYPE(LibBalsaAddressBookCarddav, libbalsa_address_book_carddav, LIBBALSA_TYPE_ADDRESS_BOOK);


static void libbalsa_address_book_carddav_finalize(GObject *object);
static LibBalsaABErr libbalsa_address_book_carddav_load(LibBalsaAddressBook        *ab,
														const gchar                *filter,
														LibBalsaAddressBookLoadFunc callback,
														gpointer                    data);
static GList *libbalsa_address_book_carddav_alias_complete(LibBalsaAddressBook *ab,
														   const gchar         *prefix);

static void libbalsa_address_book_carddav_save_config(LibBalsaAddressBook *ab,
													  const gchar         *prefix);
static void libbalsa_address_book_carddav_load_config(LibBalsaAddressBook *ab,
													  const gchar         *prefix);
static void libbalsa_address_book_carddav_store_passwd(LibBalsaAddressBookCarddav *ab_carddav);
static void libbalsa_address_book_carddav_load_passwd(LibBalsaAddressBookCarddav *ab_carddav);

static LibBalsaABErr libbalsa_address_book_carddav_add_address(LibBalsaAddressBook *ab,
															   LibBalsaAddress     *address);
static inline gboolean utf8_strstr(const gchar *haystack,
								   const gchar *utf8_needle);


#if defined(HAVE_LIBSECRET)
static const SecretSchema carddav_schema = {
	"org.gnome.Balsa.CarddavPassword", SECRET_SCHEMA_NONE,
	{
		{ "url", SECRET_SCHEMA_ATTRIBUTE_STRING },
		{ "user", SECRET_SCHEMA_ATTRIBUTE_STRING },
		{ NULL, 0 }
	}
};
#endif                          /* defined(HAVE_LIBSECRET) */


static void
libbalsa_address_book_carddav_class_init(LibBalsaAddressBookCarddavClass *klass)
{
	LibBalsaAddressBookClass *address_book_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);
	address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

	object_class->finalize = libbalsa_address_book_carddav_finalize;

	address_book_class->load = libbalsa_address_book_carddav_load;
	address_book_class->save_config = libbalsa_address_book_carddav_save_config;
	address_book_class->load_config = libbalsa_address_book_carddav_load_config;
	address_book_class->add_address = libbalsa_address_book_carddav_add_address;

	address_book_class->alias_complete = libbalsa_address_book_carddav_alias_complete;
}


static void
libbalsa_address_book_carddav_init(LibBalsaAddressBookCarddav *ab)
{
	libbalsa_address_book_set_is_expensive(LIBBALSA_ADDRESS_BOOK(ab), TRUE);
}


static void
libbalsa_address_book_carddav_finalize(GObject *object)
{
	LibBalsaAddressBookCarddav *ab_carddav;

	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(object);
	if (ab_carddav->carddav != NULL) {
		g_object_unref(ab_carddav->carddav);
	}
	g_free(ab_carddav->base_dom_url);
	g_free(ab_carddav->user);
#if defined(HAVE_LIBSECRET)
	secret_password_free(ab_carddav->password);
#else
	g_free(ab_carddav->password);
#endif
	g_free(ab_carddav->full_url);
	g_free(ab_carddav->carddav_name);

	G_OBJECT_CLASS(libbalsa_address_book_carddav_parent_class)->finalize(object);
}


static LibBalsaABErr
libbalsa_address_book_carddav_load(LibBalsaAddressBook *ab, const gchar *filter, LibBalsaAddressBookLoadFunc callback,
	gpointer data)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), LBABERR_OK);

	if (callback != NULL) {
		LibBalsaAddressBookCarddav *ab_carddav;
		gchar *filter_cf;
		const GList *addresses;

		if (filter != NULL) {
			filter_cf = g_utf8_casefold(filter, 1);
		} else {
			filter_cf = NULL;
		}

		ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(ab);
		for (addresses = libbalsa_carddav_lock_addrlist(ab_carddav->carddav); addresses != NULL; addresses = addresses->next) {
			LibBalsaAddress *this_addr = LIBBALSA_ADDRESS(addresses->data);

			if ((filter_cf == NULL) ||
				utf8_strstr(libbalsa_address_get_full_name(this_addr), filter_cf) ||
				utf8_strstr(libbalsa_address_get_last_name(this_addr), filter_cf)) {
				callback(ab, this_addr, data);
			}
		}
		libbalsa_carddav_unlock_addrlist(ab_carddav->carddav);
		g_free(filter_cf);
		callback(ab, NULL, data);
	}
	return LBABERR_OK;
}


static GList *
libbalsa_address_book_carddav_alias_complete(LibBalsaAddressBook *ab, const gchar *prefix)
{
	LibBalsaAddressBookCarddav *ab_carddav;
	const GList *cd_addrs;
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(ab);

	if (ab_carddav->carddav == NULL) {
		ab_carddav->carddav = libbalsa_carddav_new(ab_carddav->full_url, ab_carddav->user, ab_carddav->password,
			ab_carddav->refresh_minutes * 60U, ab_carddav->force_mget);
	}

	for (cd_addrs = libbalsa_carddav_lock_addrlist(ab_carddav->carddav); cd_addrs != NULL; cd_addrs = cd_addrs->next) {
		LibBalsaAddress *this_addr = LIBBALSA_ADDRESS(cd_addrs->data);
		const gchar *full_name;
		guint n;
		guint addr_cnt;

		full_name = libbalsa_address_get_full_name(this_addr);
		addr_cnt = libbalsa_address_get_n_addrs(this_addr);
		if (utf8_strstr(full_name, prefix) ||
			utf8_strstr(libbalsa_address_get_first_name(this_addr), prefix) ||
			utf8_strstr(libbalsa_address_get_last_name(this_addr), prefix) ||
			utf8_strstr(libbalsa_address_get_nick_name(this_addr), prefix) ||
			utf8_strstr(libbalsa_address_get_organization(this_addr), prefix)) {
			for (n = 0U; n < addr_cnt; n++) {
				result = g_list_prepend(result,
					internet_address_mailbox_new(full_name, libbalsa_address_get_nth_addr(this_addr, n)));
			}
		} else {
			for (n = 0U; n < addr_cnt; n++) {
				const gchar *this_mbox;

				this_mbox = libbalsa_address_get_nth_addr(this_addr, n);
				if (utf8_strstr(this_mbox, prefix)) {
					result = g_list_prepend(result, internet_address_mailbox_new(full_name, this_mbox));
				}
			}
		}
	}
	libbalsa_carddav_unlock_addrlist(ab_carddav->carddav);

	return g_list_reverse(result);
}


LibBalsaAddressBook *
libbalsa_address_book_carddav_new(const gchar *name, const gchar *base_dom_url, const gchar *user, const gchar *password,
	const gchar *full_uri, const gchar *carddav_name, guint refresh_minutes, gboolean force_mget)
{
	LibBalsaAddressBookCarddav *ab_carddav;
	LibBalsaAddressBook *ab;

	g_return_val_if_fail((full_uri != NULL) && (user != NULL), NULL);

	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(g_object_new(LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV, NULL));
	ab = LIBBALSA_ADDRESS_BOOK(ab_carddav);
	libbalsa_address_book_set_name(ab, name);
	ab_carddav->base_dom_url = g_strdup(base_dom_url);
	ab_carddav->user = g_strdup(user);
	ab_carddav->password = g_strdup(password);
	ab_carddav->full_url = g_strdup(full_uri);
	ab_carddav->carddav_name = g_strdup(carddav_name);
	ab_carddav->refresh_minutes = refresh_minutes;
	ab_carddav->force_mget = force_mget;
	ab_carddav->carddav = libbalsa_carddav_new(ab_carddav->full_url, ab_carddav->user, ab_carddav->password,
				ab_carddav->refresh_minutes * 60U, ab_carddav->force_mget);
	return ab;
}


const gchar *
libbalsa_address_book_carddav_get_base_dom_url(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	return ab->base_dom_url;
}


const gchar *
libbalsa_address_book_carddav_get_user(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	return ab->user;
}


const gchar *
libbalsa_address_book_carddav_get_password(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	return ab->password;
}


const gchar *
libbalsa_address_book_carddav_get_full_url(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	return ab->full_url;
}


const gchar *
libbalsa_address_book_carddav_get_carddav_name(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), NULL);
	return ab->carddav_name;
}


guint
libbalsa_address_book_carddav_get_refresh(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), 0U);
	return ab->refresh_minutes;
}


gboolean
libbalsa_address_book_carddav_get_force_mget(LibBalsaAddressBookCarddav *ab)
{
	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), FALSE);
	return ab->force_mget;
}


void
libbalsa_address_book_carddav_set_base_dom_url(LibBalsaAddressBookCarddav *ab, const gchar *base_dom_url)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab) && (base_dom_url != NULL));

	g_free(ab->base_dom_url);
	ab->base_dom_url = g_strdup(base_dom_url);
}


void
libbalsa_address_book_carddav_set_user(LibBalsaAddressBookCarddav *ab, const gchar *user)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab) && (user != NULL));

	g_free(ab->user);
	ab->user = g_strdup(user);
	if (ab->carddav != NULL) {
		g_object_unref(ab->carddav);
		ab->carddav = NULL;
	}
}


void
libbalsa_address_book_carddav_set_password(LibBalsaAddressBookCarddav *ab, const gchar *password)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab) && (password != NULL));

#if defined(HAVE_LIBSECRET)
	secret_password_free(ab->password);
#else
	g_free(ab->password);
#endif
	ab->password = g_strdup(password);
	if (ab->carddav != NULL) {
		g_object_unref(ab->carddav);
		ab->carddav = NULL;
	}
}


void
libbalsa_address_book_carddav_set_full_url(LibBalsaAddressBookCarddav *ab, const gchar *full_url)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab) && (full_url != NULL));

	g_free(ab->full_url);
	ab->full_url = g_strdup(full_url);
	if (ab->carddav != NULL) {
		g_object_unref(ab->carddav);
		ab->carddav = NULL;
	}
}


void
libbalsa_address_book_carddav_set_carddav_name(LibBalsaAddressBookCarddav *ab, const gchar *carddav_name)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab) && (carddav_name != NULL));

	g_free(ab->carddav_name);
	ab->carddav_name = g_strdup(carddav_name);
}


void
libbalsa_address_book_carddav_set_refresh(LibBalsaAddressBookCarddav *ab, guint refresh_minutes)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab));

	ab->refresh_minutes = refresh_minutes;
}


void
libbalsa_address_book_carddav_set_force_mget(LibBalsaAddressBookCarddav *ab, gboolean force_mget)
{
	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab));

	ab->force_mget = force_mget;
	if (ab->carddav != NULL) {
		g_object_unref(ab->carddav);
		ab->carddav = NULL;
	}
}


static void
libbalsa_address_book_carddav_save_config(LibBalsaAddressBook *ab, const gchar *prefix)
{
	LibBalsaAddressBookClass *parent_class = LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_carddav_parent_class);
	LibBalsaAddressBookCarddav *ab_carddav;

	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab));
	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(ab);

	libbalsa_conf_set_string("Domain", ab_carddav->base_dom_url);
	libbalsa_conf_private_set_string("User", ab_carddav->user, FALSE);
	libbalsa_address_book_carddav_store_passwd(ab_carddav);
	libbalsa_conf_set_string("URL", ab_carddav->full_url);
	libbalsa_conf_set_string("CarddavName", ab_carddav->carddav_name);
	libbalsa_conf_set_int("Refresh", ab_carddav->refresh_minutes);
	libbalsa_conf_set_bool("ForceMGet", ab_carddav->force_mget);
	if (parent_class->save_config != NULL) {
		parent_class->save_config(ab, prefix);
	}
}


static void
libbalsa_address_book_carddav_load_config(LibBalsaAddressBook *ab, const gchar *prefix)
{
	LibBalsaAddressBookClass *parent_class = LIBBALSA_ADDRESS_BOOK_CLASS(libbalsa_address_book_carddav_parent_class);
	LibBalsaAddressBookCarddav *ab_carddav;

	g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab));
	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(ab);

	ab_carddav->base_dom_url = libbalsa_conf_get_string("Domain");
	ab_carddav->user = libbalsa_conf_private_get_string("User", FALSE);
	ab_carddav->full_url = libbalsa_conf_get_string("URL");
	libbalsa_address_book_carddav_load_passwd(ab_carddav);
	ab_carddav->carddav_name = libbalsa_conf_get_string("CarddavName");
	ab_carddav->refresh_minutes = libbalsa_conf_get_int("Refresh");
	ab_carddav->force_mget = libbalsa_conf_get_bool("ForceMGet");
	if (parent_class->load_config != NULL) {
		parent_class->load_config(ab, prefix);
	}

	if (libbalsa_address_book_get_is_expensive(ab) < 0) {
		libbalsa_address_book_set_is_expensive(ab, TRUE);
	}

	ab_carddav->carddav = libbalsa_carddav_new(ab_carddav->full_url, ab_carddav->user, ab_carddav->password,
		ab_carddav->refresh_minutes * 60U, ab_carddav->force_mget);
}


static void
libbalsa_address_book_carddav_store_passwd(LibBalsaAddressBookCarddav *ab_carddav)
{
#if defined(HAVE_LIBSECRET)
	if (libbalsa_conf_use_libsecret()) {
		GError *error = NULL;

		secret_password_store_sync(&carddav_schema, NULL, _("Balsa passwords"), ab_carddav->password, NULL, &error,
			"url", ab_carddav->full_url,
			"user", ab_carddav->user,
			NULL);
		if (error != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 address book name; #2 error message */
				_("Error saving credentials for address book “%s” in Secret Service: %s"),
				libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab_carddav)), error->message);
			g_error_free(error);
		} else {
			libbalsa_conf_clean_key("Passwd");
		}
	} else {
		libbalsa_conf_private_set_string("Passwd", ab_carddav->password, TRUE);
	}
#else		/* !HAVE_LIBSECRET */
	libbalsa_conf_private_set_string("Passwd", ab_carddav->password, TRUE);
#endif
}


static void
libbalsa_address_book_carddav_load_passwd(LibBalsaAddressBookCarddav *ab_carddav)
{
#if defined(HAVE_LIBSECRET)
	if (libbalsa_conf_use_libsecret()) {
		GError *error = NULL;

		ab_carddav->password = secret_password_lookup_sync(&carddav_schema, NULL, &error,
			"url", ab_carddav->full_url,
			"user", ab_carddav->user,
			NULL);
		if (error != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 address book name; #2 error message */
				_("Error loading credentials for address book “%s” from Secret Service: %s"),
				libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab_carddav)), error->message);
			g_error_free(error);
		}

		/* check the config file if the returned password is NULL, make sure to remove it from the config file otherwise */
		if (ab_carddav->password == NULL) {
			ab_carddav->password = libbalsa_conf_private_get_string("Passwd", TRUE);
			if (ab_carddav->password != NULL) {
				libbalsa_address_book_carddav_store_passwd(ab_carddav);
			}
		} else {
			libbalsa_conf_clean_key("Passwd");
		}
	} else {
		ab_carddav->password = libbalsa_conf_private_get_string("Passwd", TRUE);
	}
#else
	ab_carddav->password = libbalsa_conf_private_get_string("Passwd", TRUE);
#endif		/* HAVE_LIBSECRET */
}


static LibBalsaABErr
libbalsa_address_book_carddav_add_address(LibBalsaAddressBook *ab, LibBalsaAddress *address)
{
	LibBalsaAddressBookCarddav *ab_carddav;
	GError *error = NULL;
	LibBalsaABErr result;

	g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_CARDDAV(ab), LBABERR_OK);
	ab_carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(ab);
	g_return_val_if_fail(ab_carddav->carddav != NULL, LBABERR_OK);

	/* ensure no thread is running */
	if (!libbalsa_carddav_add_address(ab_carddav->carddav, address, &error)) {
		libbalsa_address_book_set_status(ab, (error != NULL) ? error->message : _("unknown"));
		g_error_free(error);
		result = LBABERR_CANNOT_WRITE;
	} else {
		result = LBABERR_OK;
	}
	return result;
}


/** @brief Case-insensitive UTF-8 strstr
 *
 * @param[in] haystack string to check
 * @param[in] utf8_needle string to detect, @em must be created by calling g_utf8_normalize() and g_utf8_casefold()
 * @return @c TRUE iff @em haystack contains @em utf8_needle
 */
static inline gboolean
utf8_strstr(const gchar *haystack, const gchar *utf8_needle)
{
	gboolean result;

	if (haystack != NULL) {
		gchar *norm_str;
		gchar *test;

		norm_str = g_utf8_normalize(haystack, -1, G_NORMALIZE_ALL);
		test = g_utf8_casefold(norm_str, -1);
		g_free(norm_str);
		result = (strstr(test, utf8_needle) != NULL);
		g_free(test);
	} else {
		result = FALSE;
	}
	return result;
}


#endif		/* defined(HAVE_WEBDAV) */
