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
 * A VCard (eg GnomeCard) addressbook
 */

#include "config.h"

#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>

#include "address-book.h"
#include "address-book-vcard.h"
#include "information.h"

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Arbitrary constant */
#define LINE_LEN 256
/* FIXME: Make an option */
#define CASE_INSENSITIVE_NAME

static GtkObjectClass *parent_class = NULL;

typedef struct _CompletionData CompletionData;
struct _CompletionData {
    gchar *string;
    LibBalsaAddress *address;
};

static void libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *klass);
static void libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard *ab);
static void libbalsa_address_book_vcard_destroy(GtkObject * object);

static void libbalsa_address_book_vcard_load(LibBalsaAddressBook * ab, 
					     LibBalsaAddressBookLoadFunc callback, 
					     gpointer closure);
static void libbalsa_address_book_vcard_store_address(LibBalsaAddressBook *ab,
						      LibBalsaAddress *new_address);

static void libbalsa_address_book_vcard_save_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static void libbalsa_address_book_vcard_load_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static GList *libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix,
							 gchar ** new_prefix);

static gchar *extract_name(const gchar * string);

static CompletionData *completion_data_new(LibBalsaAddress * address,
					   gboolean alias);
static void completion_data_free(CompletionData * data);
static gchar *completion_data_extract(CompletionData * data);
static gint address_compare(LibBalsaAddress *a, LibBalsaAddress *b);

static void load_vcard_file(LibBalsaAddressBook *ab);

static gboolean vcard_address_book_need_reload(LibBalsaAddressBookVcard *ab);


GtkType libbalsa_address_book_vcard_get_type(void)
{
    static GtkType address_book_vcard_type = 0;

    if (!address_book_vcard_type) {
	static const GtkTypeInfo address_book_vcard_info = {
	    "LibBalsaAddressBookVcard",
	    sizeof(LibBalsaAddressBookVcard),
	    sizeof(LibBalsaAddressBookVcardClass),
	    (GtkClassInitFunc) libbalsa_address_book_vcard_class_init,
	    (GtkObjectInitFunc) libbalsa_address_book_vcard_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_book_vcard_type =
	    gtk_type_unique(libbalsa_address_book_get_type(),
			    &address_book_vcard_info);
    }

    return address_book_vcard_type;

}

static void
libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *
				       klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(LIBBALSA_TYPE_ADDRESS_BOOK);

    object_class = GTK_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->destroy = libbalsa_address_book_vcard_destroy;

    address_book_class->load = libbalsa_address_book_vcard_load;
    address_book_class->store_address =
	libbalsa_address_book_vcard_store_address;

    address_book_class->save_config =
	libbalsa_address_book_vcard_save_config;
    address_book_class->load_config =
	libbalsa_address_book_vcard_load_config;

    address_book_class->alias_complete =
	libbalsa_address_book_vcard_alias_complete;

}

static void
libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard * ab)
{
    ab->path = NULL;
    ab->address_list = NULL;
    ab->mtime = 0;

    ab->name_complete  = g_completion_new((GCompletionFunc)completion_data_extract);
    ab->alias_complete = g_completion_new((GCompletionFunc)completion_data_extract);
}

static void
libbalsa_address_book_vcard_destroy(GtkObject * object)
{
    LibBalsaAddressBookVcard *addr_vcard;

    addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(object);

    g_free(addr_vcard->path);

    g_list_foreach(addr_vcard->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_vcard->address_list);
    addr_vcard->address_list = NULL;

    g_list_foreach(addr_vcard->name_complete->items, (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_vcard->alias_complete->items, (GFunc)completion_data_free, NULL);
    
    g_completion_free(addr_vcard->name_complete);
    g_completion_free(addr_vcard->alias_complete);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

}

LibBalsaAddressBook *
libbalsa_address_book_vcard_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookVcard *abvc;
    LibBalsaAddressBook *ab;

    abvc = gtk_type_new(LIBBALSA_TYPE_ADDRESS_BOOK_VCARD);
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name = g_strdup(name);
    abvc->path = g_strdup(path);

    return ab;
}

static gboolean 
vcard_address_book_need_reload(LibBalsaAddressBookVcard *ab)
{
    struct stat stat_buf;

    if ( stat(ab->path, &stat_buf) == -1 ) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Could not stat vcard address book: %s"), ab->path);
	return FALSE;
    }
    if ( stat_buf.st_mtime > ab->mtime ) {
	ab->mtime = stat_buf.st_mtime;
	return TRUE;
    } else {
	return FALSE;
    }
}

static void
libbalsa_address_book_vcard_load(LibBalsaAddressBook * ab, LibBalsaAddressBookLoadFunc callback, gpointer closure)
{
    GList *lst;

    load_vcard_file(ab);

    lst = LIBBALSA_ADDRESS_BOOK_VCARD(ab)->address_list;
    while (lst) {
	if ( callback ) 
	    callback(ab, LIBBALSA_ADDRESS(lst->data), closure);
	lst = g_list_next(lst);
    }
    callback(ab, NULL, closure);
}

/* FIXME: Could stat the file to see if it has changed since last time we read it */
static void load_vcard_file(LibBalsaAddressBook *ab)
{
    FILE *gc;
    gchar string[LINE_LEN];
    gchar *name = NULL, *id = NULL;
    gint in_vcard = FALSE;
    GList *list = NULL;
    GList *completion_list = NULL;
    GList *address_list = NULL;
    CompletionData *cmp_data;

    LibBalsaAddressBookVcard *addr_vcard;

    addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    if ( vcard_address_book_need_reload(addr_vcard) == FALSE ) 
	return;

    g_list_foreach(addr_vcard->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_vcard->address_list);
    addr_vcard->address_list = NULL;

    g_list_foreach(addr_vcard->name_complete->items, (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_vcard->alias_complete->items, (GFunc)completion_data_free, NULL);

    g_completion_clear_items(addr_vcard->name_complete);
    g_completion_clear_items(addr_vcard->alias_complete);

    gc = fopen(addr_vcard->path, "r");

    if (gc == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Could not open vCard address book %s."), 
			     ab->name);
	return;
    }

    while (fgets(string, sizeof(string), gc)) {
	/*
	 * Check if it is a card.
	 */
	if (g_strncasecmp(string, "BEGIN:VCARD", 11) == 0) {
	    in_vcard = TRUE;
	    continue;
	}

	/*
	 * We are done loading a card.
	 */
	if (g_strncasecmp(string, "END:VCARD", 9) == 0) {
	    LibBalsaAddress *address;
	    if (address_list) {
		address = libbalsa_address_new();

		address->id = id ? id : g_strdup(_("No-Id"));

		address->address_list = address_list;

		if (name)
		    address->full_name = name;
		else if (id)
		    address->full_name = g_strdup(id);
		else
		    address->full_name = g_strdup(_("No-Name"));


		/* FIXME: Split into Firstname and Lastname... */

		list = g_list_prepend(list, address);
		address_list = NULL;
	    } else {		/* record without e-mail address, ignore */
		g_free(name);
		g_free(id);
	    }
	    name = NULL;
	    id = NULL;
	    in_vcard = FALSE;
	    continue;
	}

	if (!in_vcard)
	    continue;

	g_strchomp(string);

	if (g_strncasecmp(string, "FN:", 3) == 0) {
	    id = g_strdup(string + 3);
	    continue;
	}

	if (g_strncasecmp(string, "N:", 2) == 0) {
	    name = extract_name(string + 2);
	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_strncasecmp(string, "EMAIL;", 6) == 0) {
	    gchar *ptr = strchr(string, ':');
	    if (ptr) {
		address_list =
		    g_list_append(address_list, g_strdup(ptr + 1));
	    }
	}
    }
    fclose(gc);

    list = g_list_sort(list, (GCompareFunc)address_compare);
    addr_vcard->address_list = list;

    completion_list = NULL;
    while ( list ) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), FALSE);
	completion_list = g_list_prepend(completion_list, cmp_data);
	list = g_list_next(list);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_vcard->name_complete, completion_list);
    g_list_free(completion_list);

    completion_list = NULL;
    list = addr_vcard->address_list;
    while( list ) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), TRUE);
	completion_list = g_list_prepend(completion_list, cmp_data);
	list = g_list_next(list);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_vcard->alias_complete, completion_list);
    g_list_free(completion_list);
}

static gchar *
extract_name(const gchar * string)
/* Extract full name in order from <string> that has GnomeCard format
   and returns the pointer to the allocated memory chunk.
*/
{
    enum GCardFieldOrder { LAST = 0, FIRST, MIDDLE, PREFIX, SUFFIX };
    gint cpt, j;
    gchar **fld, **name_arr;
    gchar *res = NULL;

    fld = g_strsplit(string, ";", 5);

    cpt = 0;
    while (fld[cpt] != NULL)
	cpt++;

    if (cpt == 0)		/* insane empty name */
	return NULL;

    name_arr = g_malloc((cpt + 1) * sizeof(gchar *));

    j = 0;
    if (cpt > PREFIX && strlen(fld[PREFIX]) != 0)
	name_arr[j++] = g_strdup(fld[PREFIX]);

    if (cpt > FIRST && strlen(fld[FIRST]) != 0)
	name_arr[j++] = g_strdup(fld[FIRST]);

    if (cpt > MIDDLE && strlen(fld[MIDDLE]) != 0)
	name_arr[j++] = g_strdup(fld[MIDDLE]);

    if (cpt > LAST && strlen(fld[LAST]) != 0)
	name_arr[j++] = g_strdup(fld[LAST]);

    if (cpt > SUFFIX && strlen(fld[SUFFIX]) != 0)
	name_arr[j++] = g_strdup(fld[SUFFIX]);

    name_arr[j] = NULL;

    g_strfreev(fld);

    /* collect the data to one string */
    res = g_strjoinv(" ", name_arr);
    while (j-- > 0)
	g_free(name_arr[j]);

    g_free(name_arr);

    return res;
}

static void
libbalsa_address_book_vcard_store_address(LibBalsaAddressBook * ab,
					  LibBalsaAddress * new_address)
{
    GList *list;
    LibBalsaAddress *address;
    FILE *fp;

    load_vcard_file(ab);

    list = LIBBALSA_ADDRESS_BOOK_VCARD(ab)->address_list;
    while (list) {
	address = LIBBALSA_ADDRESS(list->data);

	if (g_strcasecmp(address->full_name, new_address->full_name) == 0) {
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
				 _("%s is already in address book."),
				 new_address->full_name);
	    return;
	}
	list = g_list_next(list);
    }

    fp = fopen(LIBBALSA_ADDRESS_BOOK_VCARD(ab)->path, "a");
    if (fp == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Cannot open vCard address book %s for saving\n"),
			     ab->name);
	return;
    }

    fprintf(fp, "\nBEGIN:VCARD\n");
    if (new_address->full_name) 
	fprintf(fp, "FN:%s\n", new_address->full_name);
    if (new_address->first_name && new_address->last_name)
	fprintf(fp, "N:%s;%s\n", new_address->last_name,
		new_address->first_name);
    if (new_address->organization)
	fprintf(fp, "ORG:%s\n", new_address->organization);
    list = new_address->address_list;
    while (list) {
	fprintf(fp, "EMAIL;INTERNET:%s\n", (gchar *) list->data);
	list = g_list_next(list);
    }
    fprintf(fp, "END:VCARD\n");
    fclose(fp);
}

static void
libbalsa_address_book_vcard_save_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookVcard *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab));

    vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    gnome_config_set_string("Path", vc->path);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_vcard_load_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookVcard *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab));

    vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    g_free(vc->path);
    vc->path = gnome_config_get_string("Path");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList *libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix, 
							 gchar ** new_prefix)
{
    LibBalsaAddressBookVcard *vc;
    GList *resa = NULL, *resb = NULL;
    GList *res = NULL;
    gchar *p1 = NULL, *p2 = NULL;
    LibBalsaAddress *addr1, *addr2;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab), NULL);

    vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    if ( ab->expand_aliases == FALSE )
	return NULL;

    load_vcard_file(ab);

    resa = g_completion_complete(vc->name_complete, (gchar*)prefix, &p1);
    resb = g_completion_complete(vc->alias_complete, (gchar*)prefix, &p2);

    if ( p1 && p2 ) {
	if ( strlen(p1) > strlen(p2) ) {
	    *new_prefix = p1;
	    g_free(p2);
	} else {
	    *new_prefix = p2;
	    g_free(p1);
	}
    } else {
	*new_prefix = p1?p1:p2;
    }

    /*
      Extract a list of addresses.
      pick any of them if two addresses point to the same structure.
      pick addr1 if it is available and there is no addr2 
                    or it is smaller than addr1.
      in other case, pick addr2 (one of addr1 or addr2 must be not-null).
    */
    while ( resa || resb ) {
	addr1 = resa ? ((CompletionData*)resa->data)->address : NULL;
	addr2 = resb ? ((CompletionData*)resb->data)->address : NULL;
	
	if (addr1 == addr2) {
	    res = g_list_prepend(res, addr1);
	    gtk_object_ref(GTK_OBJECT(addr1));
	    resa = g_list_next(resa);
	    resb = g_list_next(resb);
	} else if (resa != NULL && 
		   (resb == NULL || address_compare(addr1, addr2) > 0) ) {
	    res = g_list_prepend(res, addr1);
	    gtk_object_ref(GTK_OBJECT(addr1));
	    resa = g_list_next(resa);
	} else {
	    res = g_list_prepend(res, addr2);
	    gtk_object_ref(GTK_OBJECT(addr2));
	    resb = g_list_next(resb);
	}
    }
    res = g_list_reverse(res);

    return res;
}

/*
 * Create a new CompletionData
 */
static CompletionData *
completion_data_new(LibBalsaAddress * address, gboolean alias)
{
    CompletionData *ret;

    ret = g_new0(CompletionData, 1);

    /*  gtk_object_ref(GTK_OBJECT(address)); */
    ret->address = address;

    if (alias)
	ret->string = g_strdup(address->id);
    else
	ret->string = g_strdup(address->full_name);

#ifdef CASE_INSENSITIVE_NAME
    g_strup(ret->string);
#endif

    return ret;
}

/*
 * Free a CompletionData
 */
static void
completion_data_free(CompletionData * data)
{
    /*  gtk_object_unref(GTK_OBJECT(data->address)); */

    g_free(data->string);
    g_free(data);
}

/*
 * The GCompletionFunc
 */
static gchar *
completion_data_extract(CompletionData * data)
{
    return data->string;
}

static gint
address_compare(LibBalsaAddress *a, LibBalsaAddress *b)
{
    g_return_val_if_fail(a != NULL, -1);
    g_return_val_if_fail(b != NULL, 1);

    return g_strcasecmp(a->full_name, b->full_name);
}

