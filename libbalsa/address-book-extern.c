/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>

#include "address-book.h"
#include "address-book-extern.h"
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

static void libbalsa_address_book_externq_class_init(LibBalsaAddressBookExternClass *klass);
static void libbalsa_address_book_externq_init(LibBalsaAddressBookExtern *ab);
static void libbalsa_address_book_externq_destroy(GtkObject * object);

static void libbalsa_address_book_externq_load(LibBalsaAddressBook * ab, 
					       LibBalsaAddressBookLoadFunc callback, 
					     gpointer closure);
static void libbalsa_address_book_externq_store_address(LibBalsaAddressBook *ab,
						      LibBalsaAddress *new_address);

static void libbalsa_address_book_externq_save_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static void libbalsa_address_book_externq_load_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static GList *libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix,
							 gchar ** new_prefix);

static gchar *extract_name(const gchar * string);

static CompletionData *completion_data_new(LibBalsaAddress * address,
					   gboolean alias);
static void completion_data_free(CompletionData * data);
static gchar *completion_data_extract(CompletionData * data);
static gint address_compare(LibBalsaAddress *a, LibBalsaAddress *b);

static void load_externq_file(LibBalsaAddressBook *ab);

static gboolean externq_address_book_need_reload(LibBalsaAddressBookExtern *ab);


GtkType libbalsa_address_book_externq_get_type(void)
{
    static GtkType address_book_externq_type = 0;

    if (!address_book_externq_type) {
	static const GtkTypeInfo address_book_externq_info = {
	    "LibBalsaAddressBookExtern",
	    sizeof(LibBalsaAddressBookExtern),
	    sizeof(LibBalsaAddressBookExternClass),
	    (GtkClassInitFunc) libbalsa_address_book_externq_class_init,
	    (GtkObjectInitFunc) libbalsa_address_book_externq_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_book_externq_type =
	    gtk_type_unique(libbalsa_address_book_get_type(),
			    &address_book_externq_info);
    }

    return address_book_externq_type;

}

static void
libbalsa_address_book_externq_class_init(LibBalsaAddressBookExternClass *
				       klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(LIBBALSA_TYPE_ADDRESS_BOOK);

    object_class = GTK_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->destroy = libbalsa_address_book_externq_destroy;

    address_book_class->load = libbalsa_address_book_externq_load;
    address_book_class->store_address =
	libbalsa_address_book_externq_store_address;

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
    ab->path = NULL;
    ab->address_list = NULL;
    ab->mtime = 0;

    ab->name_complete  = 
	g_completion_new((GCompletionFunc)completion_data_extract);
    ab->alias_complete = 
	g_completion_new((GCompletionFunc)completion_data_extract);
}

static void
libbalsa_address_book_externq_destroy(GtkObject * object)
{
    LibBalsaAddressBookExtern *addr_externq;

    addr_externq = LIBBALSA_ADDRESS_BOOK_EXTERN(object);

    g_free(addr_externq->path);

    g_list_foreach(addr_externq->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_externq->address_list);
    addr_externq->address_list = NULL;

    g_list_foreach(addr_externq->name_complete->items, 
		   (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_externq->alias_complete->items, 
		   (GFunc)completion_data_free, NULL);
    
    g_completion_free(addr_externq->name_complete);
    g_completion_free(addr_externq->alias_complete);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

}

LibBalsaAddressBook *
libbalsa_address_book_externq_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookExtern *abvc;
    LibBalsaAddressBook *ab;

    abvc = gtk_type_new(LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN);
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name = g_strdup(name);
    abvc->path = g_strdup(path);

    return ab;
}

static void
libbalsa_address_book_externq_load(LibBalsaAddressBook * ab, LibBalsaAddressBookLoadFunc callback, gpointer closure)
{
    GList *lst;

    load_externq_file(ab);

    for (lst = LIBBALSA_ADDRESS_BOOK_EXTERN(ab)->address_list; 
	 lst; lst = g_list_next(lst)) {
	if ( callback ) 
	    callback(ab, LIBBALSA_ADDRESS(lst->data), closure);
    }
    callback(ab, NULL, closure);
}

/* FIXME: Could stat the file to see if it has changed since last time
   we read it */
static void load_externq_file(LibBalsaAddressBook *ab)
{
    FILE *gc;
    gchar string[LINE_LEN];
    gchar name[LINE_LEN], email[LINE_LEN], tmp[LINE_LEN];
    gint in_externq = FALSE;
    GList *list = NULL;
    GList *completion_list = NULL;
    GList *address_list = NULL;
    CompletionData *cmp_data;

    LibBalsaAddressBookExtern *addr_externq;

    addr_externq = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    g_list_foreach(addr_externq->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_externq->address_list);
    addr_externq->address_list = NULL;

    g_list_foreach(addr_externq->name_complete->items, 
		   (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_externq->alias_complete->items, 
		   (GFunc)completion_data_free, NULL);

    g_completion_clear_items(addr_externq->name_complete);
    g_completion_clear_items(addr_externq->alias_complete);

    gc = popen(addr_externq->path,"r");

    if (gc == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("Could not open external query address book %s."), 
			     ab->name);
	return;
    }
	
    fgets(string, sizeof(string), gc);
    printf("%s\n", string);
	
    while (fgets(string, sizeof(string), gc)) {
	LibBalsaAddress *address;
	printf("%s\n", string);
	if(sscanf(string, "%[^\t]\t%[^\t]%[^\n]", &email, &name, &tmp) < 2)
	    continue;
	printf("%s,%s,%s\n",email,name,tmp);
	address = libbalsa_address_new();
	address->id = g_strdup(_("No-Id"));
	address->address_list = g_list_append(address_list, g_strdup(email));
	
	if (name)address->full_name = g_strdup(name);
	else address->full_name = g_strdup(_("No-Name"));
	list = g_list_prepend(list,address);
    }
    fclose(gc);
    
    list = g_list_sort(list, (GCompareFunc)address_compare);
    addr_externq->address_list = list;
    
    completion_list = NULL;
    while (list) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), FALSE);
	completion_list = g_list_prepend(completion_list, cmp_data);
	list = g_list_next(list);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_externq->name_complete, completion_list);
    g_list_free(completion_list);

    completion_list = NULL;
    for(list=addr_externq->address_list; list; list=g_list_next(list)) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), TRUE);
	completion_list = g_list_prepend(completion_list, cmp_data);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_externq->alias_complete, completion_list);
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

    for(cpt = 0; fld[cpt] != '\0'; cpt++)
	;

    if (cpt == 0)		/* insane empty name */
	return NULL;

    name_arr = g_malloc((cpt + 1) * sizeof(gchar *));

    j = 0;
    if (cpt > PREFIX && *fld[PREFIX] != '\0')
	name_arr[j++] = g_strdup(fld[PREFIX]);

    if (cpt > FIRST && *fld[FIRST] != '\0')
	name_arr[j++] = g_strdup(fld[FIRST]);

    if (cpt > MIDDLE && *fld[MIDDLE] != '\0')
	name_arr[j++] = g_strdup(fld[MIDDLE]);

    if (cpt > LAST && *fld[LAST] != '\0')
	name_arr[j++] = g_strdup(fld[LAST]);

    if (cpt > SUFFIX && *fld[SUFFIX] != '\0')
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
libbalsa_address_book_externq_store_address(LibBalsaAddressBook * ab,
					  LibBalsaAddress * new_address)
{
    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
			 _("Can't save %s: saving not yet supported"),
			 new_address->full_name);
}

static void
libbalsa_address_book_externq_save_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookExtern *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab));

    vc = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    gnome_config_set_string("Path", vc->path);

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

    g_free(vc->path);
    vc->path = gnome_config_get_string("Path");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList *
libbalsa_address_book_externq_alias_complete(LibBalsaAddressBook * ab,
					     const gchar * prefix, 
					     gchar ** new_prefix)
{
    LibBalsaAddressBookExtern *vc;
    GList *resa = NULL, *resb = NULL;
    GList *res = NULL;
    gchar *p1 = NULL, *p2 = NULL;
    LibBalsaAddress *addr1, *addr2;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_EXTERN(ab), NULL);

    vc = LIBBALSA_ADDRESS_BOOK_EXTERN(ab);

    if ( ab->expand_aliases == FALSE )
	return NULL;

    load_externq_file(ab);

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
    ret->string  = g_strdup(alias ? address->id : address->full_name);

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
