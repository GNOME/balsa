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

#include <gnome.h>

#include "address-book.h"
#include "misc.h"

static GtkObjectClass *parent_class = NULL;

static void libbalsa_address_book_class_init(LibBalsaAddressBookClass *
					     klass);
static void libbalsa_address_book_init(LibBalsaAddressBook * ab);
static void libbalsa_address_book_destroy(GtkObject * object);

static void libbalsa_address_book_real_save_config(LibBalsaAddressBook *
						   ab,
						   const gchar * prefix);
static void libbalsa_address_book_real_load_config(LibBalsaAddressBook *
						   ab,
						   const gchar * prefix);

enum {
    LOAD,
    STORE_ADDRESS,
    SAVE_CONFIG,
    LOAD_CONFIG,
    ALIAS_COMPLETE,
    LAST_SIGNAL
};

static guint libbalsa_address_book_signals[LAST_SIGNAL];

GtkType libbalsa_address_book_get_type(void)
{
    static GtkType address_book_type = 0;

    if (!address_book_type) {
	static const GtkTypeInfo address_book_info = {
	    "LibBalsaAddressBook",
	    sizeof(LibBalsaAddressBook),
	    sizeof(LibBalsaAddressBookClass),
	    (GtkClassInitFunc) libbalsa_address_book_class_init,
	    (GtkObjectInitFunc) libbalsa_address_book_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_book_type =
	    gtk_type_unique(gtk_object_get_type(), &address_book_info);
    }

    return address_book_type;
}

static void
libbalsa_address_book_class_init(LibBalsaAddressBookClass * klass)
{
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(GTK_TYPE_OBJECT);

    object_class = GTK_OBJECT_CLASS(klass);

    libbalsa_address_book_signals[LOAD] =
	gtk_signal_new("load",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaAddressBookClass, load),
		       gtk_marshal_NONE__POINTER_POINTER, GTK_TYPE_NONE, 2,
		       GTK_TYPE_POINTER, GTK_TYPE_POINTER);
    libbalsa_address_book_signals[STORE_ADDRESS] =
	gtk_signal_new("store-address",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaAddressBookClass,
					 store_address),
		       gtk_marshal_NONE__OBJECT, GTK_TYPE_NONE, 1,
		       GTK_TYPE_OBJECT);
    libbalsa_address_book_signals[SAVE_CONFIG] =
	gtk_signal_new("save-config", GTK_RUN_FIRST, object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaAddressBookClass,
					 save_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);
    libbalsa_address_book_signals[LOAD_CONFIG] =
	gtk_signal_new("load-config", GTK_RUN_FIRST, object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaAddressBookClass,
					 load_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);
    libbalsa_address_book_signals[ALIAS_COMPLETE] =
	gtk_signal_new("alias-complete", GTK_RUN_LAST, object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaAddressBookClass,
					 alias_complete),
		       libbalsa_marshall_POINTER__POINTER_POINTER, GTK_TYPE_POINTER, 2,
		       GTK_TYPE_POINTER, GTK_TYPE_POINTER);

    gtk_object_class_add_signals(object_class,
				 libbalsa_address_book_signals,
				 LAST_SIGNAL);

    klass->load = NULL;
    klass->store_address = NULL;
    klass->save_config = libbalsa_address_book_real_save_config;
    klass->load_config = libbalsa_address_book_real_load_config;
    klass->alias_complete = NULL;

    object_class->destroy = libbalsa_address_book_destroy;
}

static void
libbalsa_address_book_init(LibBalsaAddressBook * ab)
{
    ab->config_prefix = NULL;

    ab->name = NULL;
    ab->expand_aliases = FALSE;
    ab->dist_list_mode = FALSE;
    ab->is_expensive   = FALSE;
}

static void
libbalsa_address_book_destroy(GtkObject * object)
{
    LibBalsaAddressBook *ab;

    ab = LIBBALSA_ADDRESS_BOOK(object);

    g_free(ab->config_prefix);
    ab->config_prefix = NULL;

    g_free(ab->name);
    ab->name = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

}

LibBalsaAddressBook *
libbalsa_address_book_new_from_config(const gchar * prefix)
{
    gchar *type_str;
    GtkType type;
    gboolean got_default;
    LibBalsaAddressBook *address_book = NULL;

    gnome_config_push_prefix(prefix);
    type_str = gnome_config_get_string_with_default("Type", &got_default);

    if (got_default == TRUE) {
        /* type entry missing, skip it */
	gnome_config_pop_prefix();
	return NULL;
    }

    type = gtk_type_from_name(type_str);
    if (type == 0) {
        /* type unknown, skip it */
	g_free(type_str);
	gnome_config_pop_prefix();
	return NULL;
    }

    address_book = gtk_type_new(type);
    libbalsa_address_book_load_config(address_book, prefix);

    gnome_config_pop_prefix();
    g_free(type_str);

    return address_book;

}

void
libbalsa_address_book_load(LibBalsaAddressBook * ab, LibBalsaAddressBookLoadFunc callback, gpointer closure)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gtk_signal_emit(GTK_OBJECT(ab), libbalsa_address_book_signals[LOAD], callback, closure);
}

void
libbalsa_address_book_store_address(LibBalsaAddressBook * ab,
				    LibBalsaAddress * address)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));
    g_return_if_fail(LIBBALSA_IS_ADDRESS(address));

    gtk_signal_emit(GTK_OBJECT(ab),
		    libbalsa_address_book_signals[STORE_ADDRESS], address);
}


void
libbalsa_address_book_save_config(LibBalsaAddressBook * ab,
				  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_push_prefix(prefix);
    gtk_signal_emit(GTK_OBJECT(ab),
		    libbalsa_address_book_signals[SAVE_CONFIG], prefix);
    gnome_config_pop_prefix();
}

void
libbalsa_address_book_load_config(LibBalsaAddressBook * ab,
				  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_push_prefix(prefix);
    gtk_signal_emit(GTK_OBJECT(ab),
		    libbalsa_address_book_signals[LOAD_CONFIG], prefix);
    gnome_config_pop_prefix();
}

GList *
libbalsa_address_book_alias_complete(LibBalsaAddressBook *ab,
				     const gchar * prefix,
				     gchar ** new_prefix)
{
    GList *res = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab), NULL);

    gtk_signal_emit(GTK_OBJECT(ab), libbalsa_address_book_signals[ALIAS_COMPLETE], prefix, new_prefix, &res);
    return res;

}


gboolean libbalsa_address_is_dist_list(const LibBalsaAddressBook *ab,
				       const LibBalsaAddress *address)
{
    return (address->member_list!=NULL || 
	    (ab->dist_list_mode && g_list_length(address->address_list)>1));
}



static void
libbalsa_address_book_real_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(ab));

    gnome_config_set_string("Type", gtk_type_name(GTK_OBJECT_TYPE(ab)));
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

