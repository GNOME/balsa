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

/*
 * A external source (program opened via popen) address book
 */

#include "config.h"

#define _POSIX_C_SOURCE 2

#include <stdio.h>
#include <sys/stat.h>
#include <libgnome/libgnome.h>

#include "address-book.h"
#include "address-book-extern.h"
#include "information.h"
#include "abook-completion.h"

/* FIXME: Arbitrary constant */
#define LINE_LEN 256

static LibBalsaAddressBookClass *parent_class = NULL;

static void libbalsa_address_book_externq_class_init(LibBalsaAddressBookExternClass *klass);
static void libbalsa_address_book_externq_init(LibBalsaAddressBookExtern *ab);
static void libbalsa_address_book_externq_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_externq_load(LibBalsaAddressBook* ab, 
                                                        const gchar *filter,
                                                        LibBalsaAddressBookLoadFunc 
                                                        callback, 
                                                        gpointer closure);
static LibBalsaABErr libbalsa_address_book_externq_add_address(LibBalsaAddressBook *ab,
                                                               LibBalsaAddress *address);

static LibBalsaABErr libbalsa_address_book_externq_remove_address(LibBalsaAddressBook *ab,
                                                                  LibBalsaAddress *address);

static LibBalsaABErr libbalsa_address_book_externq_modify_address(LibBalsaAddressBook *ab,
                                                                  LibBalsaAddress *address,
                                                                  LibBalsaAddress *newval);

static void libbalsa_address_book_externq_save_config(LibBalsaAddressBook *ab,
                                                      const gchar * prefix);
static void libbalsa_address_book_externq_load_config(LibBalsaAddressBook *ab,
                                                      const gchar * prefix);
static gboolean load_externq_file(LibBalsaAddressBook *ab);

static gboolean parse_externq_file(LibBalsaAddressBookExtern *addr_externq,
                                   gchar *pattern, GList** res);

static GList *libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook *ab, 
                                                           const gchar * prefix,
                                                           gchar ** new_prefix);

GType libbalsa_address_book_externq_get_type(void)
{
    static GType address_book_externq_type = 0;

    if (!address_book_externq_type) {
	static const GTypeInfo address_book_externq_info = {
	    sizeof(LibBalsaAddressBookExternClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_externq_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookExtern),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_book_externq_init
	};

	address_book_externq_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK,
	                           "LibBalsaAddressBookExtern",
			           &address_book_externq_info, 0);
    }

    return address_book_externq_type;

}

static void
libbalsa_address_book_externq_class_init(LibBalsaAddressBookExternClass *
                                         klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_externq_finalize;

    address_book_class->load = libbalsa_address_book_externq_load;
    address_book_class->add_address =
	libbalsa_address_book_externq_add_address;
    address_book_class->remove_address =
	libbalsa_address_book_externq_remove_address;
    address_book_class->modify_address =
	libbalsa_address_book_externq_modify_address;

    address_book_class->save_config =
	libbalsa_address_book_externq_save_config;
    address_book_class->load_config =
	libbalsa_address_book_externq_load_config;

    address_book_class->alias_complete =
	libbalsa_address_book_externq_alias_complete;

}

static void
libbalsa_address_book_externq_init(LibBalsaAddressBookExtern * ab)
{
    ab->load = NULL;
    ab->save = NULL;
    ab->address_list = NULL;
    ab->mtime = 0;
}

static void
libbalsa_address_book_externq_finalize(GObject * object)
{
    LibBalsaAddressBookExtern *addr_externq;

    addr_externq = LIBBALSA_ADDRESS_BOOK_EXTERN(object);

    g_free(addr_externq->load);
    g_free(addr_externq->save);
	
    g_list_foreach(addr_externq->address_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr_externq->address_list);
    addr_externq->address_list = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_externq_new(const gchar * name, const gchar * load,
                                  const gchar * save)
{
    LibBalsaAddressBookExtern *abvc;
    LibBalsaAddressBook *ab;

    abvc =
        LIBBALSA_ADDRESS_BOOK_EXTERN(g_object_new
                                     (LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN,
                                      NULL));
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name = g_strdup(name);
    abvc->load = g_strdup(load);
    abvc->save = g_strdup(save);

    return ab;
}

static LibBalsaABErr
libbalsa_address_book_externq_load(LibBalsaAddressBook * ab, 
                                   const gchar *filter,
                                   LibBalsaAddressBookLoadFunc callback, 
                                   gpointer closure)
{
    GList *lst;

    if(!load_externq_file(ab)) return LBABERR_CANNOT_READ;

    for (lst = LIBBALSA_ADDRESS_BOOK_EXTERN(ab)->address_list; 
	 lst; lst = g_list_next(lst)) {
	if (callback) 
	    callback(ab, LIBBALSA_ADDRESS(lst->data), closure);
    }
    if(callback) callback(ab, NULL, closure);
    return LBABERR_OK;
}

static gboolean
load_externq_file(LibBalsaAddressBook *ab)
{
    LibBalsaAddressBookExtern *addr_externq;
    addr_externq = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);
	
    /* Erase the current address list */
    g_list_foreach(addr_externq->address_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr_externq->address_list);
    return parse_externq_file(addr_externq, " ", &addr_externq->address_list);
}

static gboolean
parse_externq_file(LibBalsaAddressBookExtern *addr_externq,
                   gchar *pattern, GList** list)
{
    FILE *gc;
    gchar string[LINE_LEN];
    char name[LINE_LEN], email[LINE_LEN], tmp[LINE_LEN];
    gchar command[LINE_LEN];
    GList *address_list = NULL;

    *list = NULL;
    /* Start the program */
    g_snprintf(command, sizeof(command), "%s \"%s\"", 
               addr_externq->load, pattern);
    
    gc = popen(command,"r");

    if (gc == NULL) 
        return FALSE;

    fgets(string, sizeof(string), gc);
    /* The first line should be junk, just debug output */
#ifdef DEBUG
    printf("%s\n", string);
#endif
	
    while (fgets(string, sizeof(string), gc)) {
        LibBalsaAddress *address;
#ifdef DEBUG
        printf("%s\n", string);
#endif
        if(sscanf(string, "%[^\t]\t%[^\t]%[^\n]", email, name, tmp)<2)
            continue;
#ifdef DEBUG
        printf("%s,%s,%s\n",email,name,tmp);
#endif
        address = libbalsa_address_new();
        /* The externq database doesn't support Id's, sorry! */
        address->nick_name = g_strdup(_("No-Id"));
        address->address_list = g_list_append(address_list, g_strdup(email));

        if (name)address->full_name = g_strdup(name);
        else address->full_name = g_strdup(_("No-Name"));
        *list = g_list_prepend(*list, address);
    }
    fclose(gc);
    
    *list = g_list_sort(*list, (GCompareFunc)address_compare);
    return TRUE;
}

static LibBalsaABErr
libbalsa_address_book_externq_add_address(LibBalsaAddressBook * ab,
                                          LibBalsaAddress * new_address)
{
    gchar command[LINE_LEN];
    LibBalsaAddressBookExtern *ex;
    FILE *gc; 
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab), LBABERR_OK);

    ex = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    g_snprintf(command, sizeof(command), "%s \"%s\" \"%s\" \"%s\"", 
               ex->save, 
               (gchar *)g_list_first(new_address->address_list)->data, 
               new_address->full_name, "TODO");

    if( (gc = popen(command, "r")) == NULL)
        return LBABERR_CANNOT_WRITE;
    if(fclose(gc) != 0) 
        return LBABERR_CANNOT_WRITE;
    return LBABERR_OK;
}

static LibBalsaABErr
libbalsa_address_book_externq_remove_address(LibBalsaAddressBook *ab,
                                             LibBalsaAddress *address)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_externq_modify_address(LibBalsaAddressBook *ab,
                                             LibBalsaAddress *address,
                                             LibBalsaAddress *newval)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static void
libbalsa_address_book_externq_save_config(LibBalsaAddressBook * ab,
                                          const gchar * prefix)
{
    LibBalsaAddressBookExtern *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab));

    vc = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    gnome_config_set_string("Load", vc->load);
    gnome_config_set_string("Save", vc->save);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_externq_load_config(LibBalsaAddressBook * ab,
                                          const gchar * prefix)
{
    LibBalsaAddressBookExtern *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab));

    vc = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    g_free(vc->load);
    vc->load = gnome_config_get_string("Load");
    vc->save = gnome_config_get_string("Save");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList*
libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook * ab,
                                             const gchar * prefix, 
                                             gchar ** new_prefix)
{
    LibBalsaAddressBookExtern *ex;
    GList *res = NULL;
    if(new_prefix) *new_prefix = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab), NULL);

    ex = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    if ( !ab->expand_aliases )
	return NULL;

    if(!parse_externq_file(ex, (gchar *)prefix, &res))
        return NULL;
	
    g_list_reverse(res);

    if(res != NULL && new_prefix)
        *new_prefix = libbalsa_address_to_gchar((LibBalsaAddress *)
                                                g_list_first(res)->data, 0);

    return res;
}
