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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book.h"

#include <glib.h>
#include <glib/gi18n.h>

#include "libbalsa-conf.h"

typedef struct _LibBalsaAddressBookPrivate LibBalsaAddressBookPrivate;

struct _LibBalsaAddressBookPrivate {
    /* The gnome_config prefix where we save this address book */
    gchar *config_prefix;
    gchar *name;
    gchar *ext_op_code;    /* extra description for last operation */
    gboolean is_expensive; /* is lookup to the address book expensive? 
			      e.g. LDAP address book */
    gboolean expand_aliases;

    gboolean dist_list_mode;
};

static void libbalsa_address_book_finalize(GObject * object);

static void libbalsa_address_book_real_save_config(LibBalsaAddressBook *
						   ab,
						   const gchar * group);
static void libbalsa_address_book_real_load_config(LibBalsaAddressBook *
						   ab,
						   const gchar * group);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(LibBalsaAddressBook, libbalsa_address_book, G_TYPE_OBJECT)

static void
libbalsa_address_book_class_init(LibBalsaAddressBookClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    klass->load = NULL;
    klass->add_address = NULL;
    klass->remove_address = NULL;
    klass->modify_address = NULL;
    klass->save_config = libbalsa_address_book_real_save_config;
    klass->load_config = libbalsa_address_book_real_load_config;
    klass->alias_complete = NULL;

    object_class->finalize = libbalsa_address_book_finalize;
}

static void
libbalsa_address_book_init(LibBalsaAddressBook * ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    priv->config_prefix = NULL;

    priv->name = NULL;
    priv->expand_aliases = TRUE;
    priv->dist_list_mode = FALSE;
    priv->is_expensive   = FALSE;
}

static void
libbalsa_address_book_finalize(GObject * object)
{
    LibBalsaAddressBook *ab = LIBBALSA_ADDRESS_BOOK(object);
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_free(priv->config_prefix);
    priv->config_prefix = NULL;

    g_free(priv->name);
    priv->name = NULL;

    G_OBJECT_CLASS(libbalsa_address_book_parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_new_from_config(const gchar * group)
{
    gchar *type_str;
    GType type;
    gboolean got_default;
    LibBalsaAddressBook *ab = NULL;

    libbalsa_conf_push_group(group);
    type_str = libbalsa_conf_get_string_with_default("Type", &got_default);

    if (got_default) {
        /* type entry missing, skip it */
	libbalsa_conf_pop_group();
	return ab;
    }

    type = g_type_from_name(type_str);
    if (type == 0) {
        /* Legacy: */
        if (strcmp(type_str, "LibBalsaAddressBookExtern") == 0) {
            type = g_type_from_name("LibBalsaAddressBookExternq");
        } else {
            /* type unknown, skip it */
            g_free(type_str);
            libbalsa_conf_pop_group();
            return ab;
        }
    }

    ab = g_object_new(type, NULL);
    libbalsa_address_book_load_config(ab, group);

    g_free(type_str);
    libbalsa_conf_pop_group();

    return ab;
}

LibBalsaABErr
libbalsa_address_book_load(LibBalsaAddressBook * ab,
                           const gchar * filter,
                           LibBalsaAddressBookLoadFunc callback,
                           gpointer closure)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);

    return LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->load(ab, filter, callback,
                                                     closure);
}

LibBalsaABErr
libbalsa_address_book_add_address(LibBalsaAddressBook * ab,
                                  LibBalsaAddress * address)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    if (LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->add_address == NULL) {
        return LBABERR_CANNOT_WRITE;
    }
    return LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->add_address(ab, address);
}

LibBalsaABErr
libbalsa_address_book_remove_address(LibBalsaAddressBook * ab,
                                     LibBalsaAddress * address)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    if (LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->remove_address == NULL) {
        return LBABERR_CANNOT_WRITE;
    }
    return LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->remove_address(ab,
                                                               address);
}

LibBalsaABErr
libbalsa_address_book_modify_address(LibBalsaAddressBook * ab,
                                     LibBalsaAddress * address,
                                     LibBalsaAddress * newval)
{
    LibBalsaABErr res;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    if (LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->modify_address == NULL) {
        return LBABERR_CANNOT_WRITE;
    }
    res =
        LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->modify_address(ab, address,
                                                            newval);
    if (res == LBABERR_OK)
        libbalsa_address_set_copy(address, newval);

    return res;
}

void
libbalsa_address_book_set_status(LibBalsaAddressBook * ab, const gchar *str)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    g_free(priv->ext_op_code);
    priv->ext_op_code = g_strdup(str);
}

void
libbalsa_address_book_save_config(LibBalsaAddressBook * ab,
                                  const gchar * group)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    libbalsa_conf_private_remove_group(group);
    libbalsa_conf_remove_group(group);

    libbalsa_conf_push_group(group);
    LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->save_config(ab, group);
    libbalsa_conf_pop_group();
}

void
libbalsa_address_book_load_config(LibBalsaAddressBook * ab,
				  const gchar * group)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    libbalsa_conf_push_group(group);
    LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->load_config(ab, group);
    libbalsa_conf_pop_group();

    if (priv->is_expensive < 0)
        priv->is_expensive = FALSE;
}

GList *
libbalsa_address_book_alias_complete(LibBalsaAddressBook * ab,
                                     const gchar * prefix)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    return LIBBALSA_ADDRESS_BOOK_GET_CLASS(ab)->alias_complete(ab, prefix);
}


static void
libbalsa_address_book_real_save_config(LibBalsaAddressBook * ab,
				       const gchar * group)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    libbalsa_conf_set_string("Type", g_type_name(G_OBJECT_TYPE(ab)));
    libbalsa_conf_set_string("Name", priv->name);
    libbalsa_conf_set_bool("ExpandAliases", priv->expand_aliases);
    libbalsa_conf_set_bool("IsExpensive", priv->is_expensive);
    libbalsa_conf_set_bool("DistListMode", priv->dist_list_mode);

    g_free(priv->config_prefix);
    priv->config_prefix = g_strdup(group);
}

static void
libbalsa_address_book_real_load_config(LibBalsaAddressBook * ab,
				       const gchar * group)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);
    gboolean def;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    g_free(priv->config_prefix);
    priv->config_prefix = g_strdup(group);

    priv->expand_aliases = libbalsa_conf_get_bool("ExpandAliases=false");

    priv->is_expensive =
        libbalsa_conf_get_bool_with_default("IsExpensive", &def);
    if (def)
        /* Default will be supplied by the backend, or in
         * libbalsa_address_book_load_config. */
        priv->is_expensive = -1;

    priv->dist_list_mode = libbalsa_conf_get_bool("DistListMode=false");

    g_free(priv->name);
    priv->name = libbalsa_conf_get_string("Name=Address Book");
}

const gchar*
libbalsa_address_book_strerror(LibBalsaAddressBook * ab, LibBalsaABErr err)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);
    const gchar *s;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    if(priv->ext_op_code)
	return priv->ext_op_code;

    switch(err) {
    case LBABERR_OK:             s= _("No error"); break;
    case LBABERR_CANNOT_READ:    s= _("Cannot read from address book"); break;
    case LBABERR_CANNOT_WRITE:   s= _("Cannot write to address book");  break;
    case LBABERR_CANNOT_CONNECT: s= _("Cannot connect to the server");  break; 
    case LBABERR_CANNOT_SEARCH:  s= _("Cannot search in the address book"); 
        break;
    case LBABERR_DUPLICATE:      s= _("Cannot add duplicate entry");    break;
    case LBABERR_ADDRESS_NOT_FOUND:
        s= _("Cannot find address in address book"); break;
    default: s= _("Unknown error"); break;
    }
    return s;
}

/*
 * Getters
 */

gboolean
libbalsa_address_book_get_expand_aliases(LibBalsaAddressBook * ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), FALSE);

    return priv->expand_aliases;
}

gboolean
libbalsa_address_book_get_is_expensive(LibBalsaAddressBook * ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), FALSE);

    return priv->is_expensive;
}

gboolean
libbalsa_address_book_get_dist_list_mode(LibBalsaAddressBook * ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), FALSE);

    return priv->dist_list_mode;
}

const gchar *
libbalsa_address_book_get_name(LibBalsaAddressBook *ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    return priv->name;
}

const gchar *
libbalsa_address_book_get_config_prefix(LibBalsaAddressBook *ab)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    return priv->config_prefix;
}

/*
 * Setters
 */

void
libbalsa_address_book_set_name(LibBalsaAddressBook *ab, const gchar *name)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    g_free(priv->name);
    priv->name = g_strdup(name);
}

void
libbalsa_address_book_set_is_expensive(LibBalsaAddressBook *ab, gboolean is_expensive)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    priv->is_expensive = !!is_expensive;
}

void
libbalsa_address_book_set_expand_aliases(LibBalsaAddressBook *ab, gboolean expand_aliases)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    priv->expand_aliases = !!expand_aliases;
}

void
libbalsa_address_book_set_dist_list_mode(LibBalsaAddressBook *ab, gboolean dist_list_mode)
{
    LibBalsaAddressBookPrivate *priv = libbalsa_address_book_get_instance_private(ab);

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    priv->dist_list_mode = !!dist_list_mode;
}
