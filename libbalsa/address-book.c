/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "config.h"

#include <gtk/gtkmarshal.h>

#include <libgnome/libgnome.h>

#include "address-book.h"
#include "libbalsa-marshal.h"

static GObjectClass *parent_class = NULL;

static void libbalsa_address_book_class_init(LibBalsaAddressBookClass *
					     klass);
static void libbalsa_address_book_init(LibBalsaAddressBook * ab);
static void libbalsa_address_book_finalize(GObject * object);

static void libbalsa_address_book_real_save_config(LibBalsaAddressBook *
						   ab,
						   const gchar * prefix);
static void libbalsa_address_book_real_load_config(LibBalsaAddressBook *
						   ab,
						   const gchar * prefix);

enum {
    LOAD,
    ADD_ADDRESS,
    REMOVE_ADDRESS,
    MODIFY_ADDRESS,
    SAVE_CONFIG,
    LOAD_CONFIG,
    ALIAS_COMPLETE,
    LAST_SIGNAL
};

static guint libbalsa_address_book_signals[LAST_SIGNAL];

GType libbalsa_address_book_get_type(void)
{
    static GType address_book_type = 0;

    if (!address_book_type) {
	static const GTypeInfo address_book_info = {
	    sizeof(LibBalsaAddressBookClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBook),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_book_init
	};

	address_book_type =
	    g_type_register_static(G_TYPE_OBJECT,
                                   "LibBalsaAddressBook",
                                   &address_book_info, 0);
    }

    return address_book_type;
}

static void
libbalsa_address_book_class_init(LibBalsaAddressBookClass * klass)
{
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);

    libbalsa_address_book_signals[LOAD] =
	g_signal_new("load",
		       G_TYPE_FROM_CLASS(object_class),
		       G_SIGNAL_RUN_LAST,
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass, load),
                       NULL, NULL,
		       libbalsa_INT__POINTER_POINTER_POINTER,
                       G_TYPE_INT, 3,
		       G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER);
    libbalsa_address_book_signals[ADD_ADDRESS] =
	g_signal_new("add-address",
		       G_TYPE_FROM_CLASS(object_class),
		       G_SIGNAL_RUN_LAST,
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
                                       add_address),
                       NULL, NULL,
		       libbalsa_INT__OBJECT,
                       G_TYPE_INT, 1,
		       G_TYPE_OBJECT);
    libbalsa_address_book_signals[REMOVE_ADDRESS] =
	g_signal_new("remove-address",
		       G_TYPE_FROM_CLASS(object_class),
		       G_SIGNAL_RUN_LAST,
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
                                       remove_address),
                       NULL, NULL,
		       libbalsa_INT__OBJECT,
                       G_TYPE_INT, 1,
		       G_TYPE_OBJECT);
    libbalsa_address_book_signals[MODIFY_ADDRESS] =
	g_signal_new("modify-address",
		       G_TYPE_FROM_CLASS(object_class),
		       G_SIGNAL_RUN_LAST,
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
                                       modify_address),
                       NULL, NULL,
		       libbalsa_INT__OBJECT_OBJECT,
                       G_TYPE_INT, 2,
		       G_TYPE_OBJECT, G_TYPE_OBJECT);
    libbalsa_address_book_signals[SAVE_CONFIG] =
	g_signal_new("save-config",
                       G_TYPE_FROM_CLASS(object_class),
                       G_SIGNAL_RUN_FIRST,
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
					 save_config),
                       NULL, NULL,
		       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1,
		       G_TYPE_POINTER);
    libbalsa_address_book_signals[LOAD_CONFIG] =
	g_signal_new("load-config",
                       G_TYPE_FROM_CLASS(object_class),
                       G_SIGNAL_RUN_FIRST, 
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
					 load_config),
                       NULL, NULL,
		       g_cclosure_marshal_VOID__POINTER,
                       G_TYPE_NONE, 1,
		       G_TYPE_POINTER);
    libbalsa_address_book_signals[ALIAS_COMPLETE] =
	g_signal_new("alias-complete",
                       G_TYPE_FROM_CLASS(object_class),
                       G_SIGNAL_RUN_LAST, 
		       G_STRUCT_OFFSET(LibBalsaAddressBookClass,
					 alias_complete),
                       NULL, NULL,
		       libbalsa_POINTER__POINTER_POINTER, 
                       G_TYPE_POINTER, 2,
		       G_TYPE_POINTER, G_TYPE_POINTER);

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
    ab->config_prefix = NULL;

    ab->name = NULL;
    ab->expand_aliases = TRUE;
    ab->dist_list_mode = FALSE;
    ab->is_expensive   = FALSE;
}

static void
libbalsa_address_book_finalize(GObject * object)
{
    LibBalsaAddressBook *ab;

    ab = LIBBALSA_ADDRESS_BOOK(object);

    g_free(ab->config_prefix);
    ab->config_prefix = NULL;

    g_free(ab->name);
    ab->name = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_new_from_config(const gchar * prefix)
{
    gchar *type_str;
    GType type;
    gboolean got_default;
    LibBalsaAddressBook *address_book = NULL;

    gnome_config_push_prefix(prefix);
    type_str = gnome_config_get_string_with_default("Type", &got_default);

    if (got_default == TRUE) {
        /* type entry missing, skip it */
	gnome_config_pop_prefix();
	return NULL;
    }

    type = g_type_from_name(type_str);
    if (type == 0) {
        /* type unknown, skip it */
	g_free(type_str);
	gnome_config_pop_prefix();
	return NULL;
    }

    address_book = g_object_new(type, NULL);
    libbalsa_address_book_load_config(address_book, prefix);

    gnome_config_pop_prefix();
    g_free(type_str);

    return address_book;

}

LibBalsaABErr
libbalsa_address_book_load(LibBalsaAddressBook * ab,
                           const gchar *filter,
                           LibBalsaAddressBookLoadFunc callback,
                           gpointer closure)
{
    LibBalsaABErr res;
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);

    g_signal_emit(G_OBJECT(ab), libbalsa_address_book_signals[LOAD], 0,
                  filter, callback, closure, &res);

    return res;
}

LibBalsaABErr
libbalsa_address_book_add_address(LibBalsaAddressBook *ab,
                                  LibBalsaAddress *address)
{
    LibBalsaABErr res;
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    g_signal_emit(G_OBJECT(ab),
                  libbalsa_address_book_signals[ADD_ADDRESS], 0,
                  address, &res);
    return res;
}

LibBalsaABErr
libbalsa_address_book_remove_address(LibBalsaAddressBook *ab,
                                     LibBalsaAddress *address)
{
    LibBalsaABErr res;
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    g_signal_emit(G_OBJECT(ab),
                  libbalsa_address_book_signals[REMOVE_ADDRESS], 0,
                  address, &res);
    return res;
}

LibBalsaABErr
libbalsa_address_book_modify_address(LibBalsaAddressBook *ab,
                                     LibBalsaAddress *address,
                                     LibBalsaAddress *newval)
{
    LibBalsaABErr res;
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), LBABERR_OK);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), LBABERR_OK);

    g_signal_emit(G_OBJECT(ab),
                  libbalsa_address_book_signals[MODIFY_ADDRESS], 0,
                  address, newval, &res);
    if(res == LBABERR_OK)
        libbalsa_address_set_copy(address, newval);
    return res;
}

/* set_status takes over the string ownership */
void
libbalsa_address_book_set_status(LibBalsaAddressBook * ab, gchar *str)
{
    g_return_if_fail(ab);
    g_free(ab->ext_op_code);
    ab->ext_op_code = str;
}

void
libbalsa_address_book_save_config(LibBalsaAddressBook * ab,
                                  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_private_clean_section(prefix);
    gnome_config_clean_section(prefix);
    gnome_config_push_prefix(prefix);

    g_signal_emit(G_OBJECT(ab),
                  libbalsa_address_book_signals[SAVE_CONFIG], 0,
                  prefix);
    gnome_config_pop_prefix();
}

void
libbalsa_address_book_load_config(LibBalsaAddressBook * ab,
				  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_push_prefix(prefix);
    g_signal_emit(G_OBJECT(ab),
		  libbalsa_address_book_signals[LOAD_CONFIG], 0,
                  prefix);
    gnome_config_pop_prefix();
}

GList *
libbalsa_address_book_alias_complete(LibBalsaAddressBook *ab,
				     const gchar * prefix,
				     gchar ** new_prefix)
{
    GList *res = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    g_signal_emit(G_OBJECT(ab),
                  libbalsa_address_book_signals[ALIAS_COMPLETE], 0,
                  prefix, new_prefix, &res);
    return res;

}


gboolean libbalsa_address_is_dist_list(const LibBalsaAddressBook *ab,
				       const LibBalsaAddress *address)
{
    return (ab->dist_list_mode && g_list_length(address->address_list)>1);
}



static void
libbalsa_address_book_real_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_set_string("Type", g_type_name(G_OBJECT_TYPE(ab)));
    gnome_config_set_string("Name", ab->name);
    gnome_config_set_bool("ExpandAliases", ab->expand_aliases);
    gnome_config_set_bool("DistListMode", ab->dist_list_mode);

    g_free(ab->config_prefix);
    ab->config_prefix = g_strdup(prefix);
}

static void
libbalsa_address_book_real_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    g_free(ab->config_prefix);
    ab->config_prefix = g_strdup(prefix);

    ab->expand_aliases = gnome_config_get_bool("ExpandAliases=false");
    ab->dist_list_mode = gnome_config_get_bool("DistListMode=false");

    g_free(ab->name);
    ab->name = gnome_config_get_string("Name=Address Book");
}

const gchar*
libbalsa_address_book_strerror(LibBalsaAddressBook * ab, LibBalsaABErr err)
{
    const gchar *s;
    g_return_val_if_fail(ab, NULL);
    if(ab->ext_op_code)
	return ab->ext_op_code;

    switch(err) {
    case LBABERR_OK:             s= _("No error"); break;
    case LBABERR_CANNOT_READ:    s= _("Cannot read from address book"); break;
    case LBABERR_CANNOT_WRITE:   s= _("Cannot write to address book");  break;
    case LBABERR_CANNOT_CONNECT: s= _("Cannot connect to server");      break; 
    case LBABERR_CANNOT_SEARCH:  s= _("Cannot search in the address book"); 
        break;
    case LBABERR_DUPLICATE:      s= _("Cannot add duplicate entry");    break;
    default: s= _("Unknown error"); break;
    }
    return s;
}


