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
 * An LDIF addressbook
 */

#include "config.h"

#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>

#include "address-book.h"
#include "address-book-ldif.h"
#include "information.h"

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Make an option */
#define CASE_INSENSITIVE_NAME

static GtkObjectClass *parent_class = NULL;

typedef struct _CompletionData CompletionData;
struct _CompletionData {
    gchar *string;
    LibBalsaAddress *address;
};

static void libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass *klass);
static void libbalsa_address_book_ldif_init(LibBalsaAddressBookLdif *ab);
static void libbalsa_address_book_ldif_destroy(GtkObject * object);

static void libbalsa_address_book_ldif_load(LibBalsaAddressBook * ab,
					     LibBalsaAddressBookLoadFunc callback,
					     gpointer closure);
static void libbalsa_address_book_ldif_store_address(LibBalsaAddressBook *ab,
						      LibBalsaAddress *new_address);

static void libbalsa_address_book_ldif_save_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static void libbalsa_address_book_ldif_load_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static GList *libbalsa_address_book_ldif_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix,
							 gchar ** new_prefix);

static gchar *build_name(gchar *id, gchar *givenname, gchar *surname);

static CompletionData *completion_data_new(LibBalsaAddress * address,
					   gboolean alias);
static void completion_data_free(CompletionData * data);
static gchar *completion_data_extract(CompletionData * data);
static gint address_compare(LibBalsaAddress *a, LibBalsaAddress *b);

static void load_ldif_file(LibBalsaAddressBook *ab);

static gboolean ldif_address_book_need_reload(LibBalsaAddressBookLdif *ab);


GtkType libbalsa_address_book_ldif_get_type(void)
{
    static GtkType address_book_ldif_type = 0;

    if (!address_book_ldif_type) {
	static const GtkTypeInfo address_book_ldif_info = {
	    "LibBalsaAddressBookLdif",
	    sizeof(LibBalsaAddressBookLdif),
	    sizeof(LibBalsaAddressBookLdifClass),
	    (GtkClassInitFunc) libbalsa_address_book_ldif_class_init,
	    (GtkObjectInitFunc) libbalsa_address_book_ldif_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_book_ldif_type =
	    gtk_type_unique(libbalsa_address_book_get_type(),
			    &address_book_ldif_info);
    }

    return address_book_ldif_type;

}

static void
libbalsa_address_book_ldif_class_init(LibBalsaAddressBookLdifClass *
				       klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(LIBBALSA_TYPE_ADDRESS_BOOK);

    object_class = GTK_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->destroy = libbalsa_address_book_ldif_destroy;

    address_book_class->load = libbalsa_address_book_ldif_load;
    address_book_class->store_address =
	libbalsa_address_book_ldif_store_address;

    address_book_class->save_config =
	libbalsa_address_book_ldif_save_config;
    address_book_class->load_config =
	libbalsa_address_book_ldif_load_config;

    address_book_class->alias_complete =
	libbalsa_address_book_ldif_alias_complete;

}

static void
libbalsa_address_book_ldif_init(LibBalsaAddressBookLdif * ab)
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
libbalsa_address_book_ldif_destroy(GtkObject * object)
{
    LibBalsaAddressBookLdif *addr_ldif;

    addr_ldif = LIBBALSA_ADDRESS_BOOK_LDIF(object);

    g_free(addr_ldif->path);

    g_list_foreach(addr_ldif->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_ldif->address_list);
    addr_ldif->address_list = NULL;

    g_list_foreach(addr_ldif->name_complete->items, 
		   (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_ldif->alias_complete->items, 
		   (GFunc)completion_data_free, NULL);

    g_completion_free(addr_ldif->name_complete);
    g_completion_free(addr_ldif->alias_complete);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

}

LibBalsaAddressBook *
libbalsa_address_book_ldif_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookLdif *abvc;
    LibBalsaAddressBook *ab;

    abvc = gtk_type_new(LIBBALSA_TYPE_ADDRESS_BOOK_LDIF);
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name   = g_strdup(name);
    abvc->path = g_strdup(path);

    return ab;
}

static gchar*
read_line(FILE* f)
{
    char buf[256];
    GString* res = g_string_new("");
    gchar* str;
    int len, emptyp = 1;
    
    while( fgets(buf, sizeof(buf), f)) {
	emptyp = 0;
	g_string_append(res, buf);
	if((len=strlen(buf))> 0 && buf[len-1] == '\n') break;
    }

    str = emptyp ? NULL : res->str;
    g_string_free(res, emptyp);
    return str;
}
	
    
static gboolean
ldif_address_book_need_reload(LibBalsaAddressBookLdif *ab)
{
    struct stat stat_buf;

    if ( stat(ab->path, &stat_buf) == -1 ) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("Could not stat ldif address book: %s"), 
			     ab->path);
	return FALSE;
    }
    if ( stat_buf.st_mtime > ab->mtime ) {
	ab->mtime = stat_buf.st_mtime;
	return TRUE;
    } else
	return FALSE;
}

static void
libbalsa_address_book_ldif_load(LibBalsaAddressBook * ab, 
				LibBalsaAddressBookLoadFunc callback, 
				gpointer closure)
{
    GList *lst;

    load_ldif_file(ab);

    lst = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->address_list;
    for (lst = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->address_list; 
	 lst; lst = g_list_next(lst)) {
	if ( callback )
	    callback(ab, LIBBALSA_ADDRESS(lst->data), closure);
    }
    callback(ab, NULL, closure);
}

/* address_new_prefill:
   takes over the string ownership!
*/
static LibBalsaAddress*
address_new_prefill(GList* address_list, gchar* nickn, gchar* givenn, 
		    gchar* surn, gchar* fulln, gchar* org, gchar* id)
{
    LibBalsaAddress* address = libbalsa_address_new();
    
    address->address_list = address_list;
    
    address->first_name = givenn ? givenn : (nickn ? nickn : g_strdup(""));
    address->last_name = surn ? surn : g_strdup("");
    address->full_name = fulln 
	? fulln : build_name(id, address->first_name, surn);
    address->organization = org ? org : g_strdup("");
    
    address->id = id ? id : 
	( address->full_name ? address->full_name : g_strdup(_("No-Id")));
    
    if (address->full_name == NULL)
	address->full_name = g_strdup(_("No-Name"));
    return address;
}
    
/* FIXME: Could stat the file to see if it has changed since last time 
   we read it 
*/
static void
load_ldif_file(LibBalsaAddressBook *ab)
{
    FILE *gc;
    gchar *line;
    gchar *id = NULL, *surname = NULL, *givenname = NULL, *nickname = NULL,
	*fullname = NULL, *organization = NULL;
    gint in_ldif = FALSE;
    GList *list = NULL;
    GList *completion_list = NULL;
    GList *address_list = NULL;
    CompletionData *cmp_data;

    LibBalsaAddressBookLdif *addr_ldif = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    if ( !ldif_address_book_need_reload(addr_ldif) )
	return;

    g_list_foreach(addr_ldif->address_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr_ldif->address_list);
    addr_ldif->address_list = NULL;
    
    g_list_foreach(addr_ldif->name_complete->items, 
		   (GFunc)completion_data_free, NULL);
    g_list_foreach(addr_ldif->alias_complete->items, 
		   (GFunc)completion_data_free, NULL);

    g_completion_clear_items(addr_ldif->name_complete);
    g_completion_clear_items(addr_ldif->alias_complete);
    
    gc = fopen(addr_ldif->path, "r");

    if (gc == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("Could not open LDIF address book %s."),
			     ab->name);
	return;
    }

    for (; (line=read_line(gc)) != NULL; g_free(line) ) {
	/*
	 * Check if it is a card.
	 */
	if (g_strncasecmp(line, "dn:", 3) == 0) {
	    in_ldif = TRUE;
	    id = g_strdup(g_strchug(line + 3));
	    if( id && *id == '\0' ) {
		g_free(id);
		id = NULL;
	    }
	    continue;
	}

	g_strchomp(line);

	/*
	 * We are done loading a card.
	 */
	if (line[0] == '\0') {
	    LibBalsaAddress *address;
	    if (address_list) {
		address = address_new_prefill(address_list, nickname, 
					      givenname, surname, fullname,
					      organization, id);
		list = g_list_append(list, address);
		address_list = NULL;
	    } else {            /* record without e-mail address, ignore */
		g_free(id);
		g_free(nickname);
		g_free(givenname);
		g_free(surname);
		g_free(organization);
	    }
	    nickname = givenname = surname = id = organization = NULL;
	    in_ldif = FALSE;
	    continue;
	}

	if (!in_ldif)
	    continue;

	if (g_strncasecmp(line, "cn:", 3) == 0) {
	    fullname = g_strdup(g_strchug(line + 3));
	    continue;
	}

	if (g_strncasecmp(line, "sn:", 3) == 0) {
	    surname = g_strdup(g_strchug(line + 3));
	    continue;
	}

	if (g_strncasecmp(line, "givenname:", 10) == 0) {
	    givenname = g_strdup(g_strchug(line + 10));
	    continue;
	}

	if (g_strncasecmp(line, "xmozillanickname:", 17) == 0) {
	    nickname = g_strdup(g_strchug(line + 17));
	    continue;
	}

	if (g_strncasecmp(line, "o:", 2) == 0) {
	    organization = g_strdup(g_strchug(line + 2));
	    continue;
	}

	if (g_strncasecmp(line, "member:", 2) == 0) {
	    gchar* str = strstr(line, "mail=");
	    if(str)
		address_list = g_list_append(address_list, 
					     g_strdup(g_strchug(str+5)));

	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_strncasecmp(line, "mail:", 5) == 0) {
	    address_list = g_list_append(address_list, 
					 g_strdup(g_strchug(line + 5)));
	}
    }
    fclose(gc);

    if(in_ldif) {
	LibBalsaAddress *address;
	if (address_list) {
	    address = address_new_prefill(address_list, nickname, givenname,
					  surname, fullname, organization,id);

	    /* FIXME: Split into Firstname and Lastname... */

	    list = g_list_append(list, address);
	} else {                /* record without e-mail address, ignore */
	    g_free(id);
	    g_free(nickname);
	    g_free(givenname);
	    g_free(surname);
	    g_free(organization);
	}
    }

    list = g_list_sort(list, (GCompareFunc)address_compare);
    addr_ldif->address_list = list;

    completion_list = NULL;
    for (;list; list = g_list_next(list)) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), FALSE);
	completion_list = g_list_prepend(completion_list, cmp_data);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_ldif->name_complete, completion_list);
    g_list_free(completion_list);

    completion_list = NULL;
    for(list = addr_ldif->address_list; list; list = g_list_next(list) ) {
	cmp_data = completion_data_new(LIBBALSA_ADDRESS(list->data), TRUE);
	completion_list = g_list_prepend(completion_list, cmp_data);
    }
    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_ldif->alias_complete, completion_list);
    g_list_free(completion_list);
}

/* build_name:
   Builds a full name and returns the pointer to the allocated memory chunk.
   Returns a NULL pointer if it couldn't figure out a name. 
*/
static gchar *
build_name(gchar *id, gchar *givenname, gchar *surname)
{
    gchar *name = NULL, *end = NULL;

    if(givenname && *givenname && surname && *surname) {
	name = g_new (gchar, strlen(givenname) + strlen(surname) + 2);
	strcpy(name, givenname);
	strcat(name, " ");
	strcat(name, surname);
    } else if(givenname && *givenname) {
	name = g_strdup(givenname);
    } else if(surname && *surname) {
	name = g_strdup(surname);
    } else if(id && *id) {
	/* Netscape LDIF files contain "cn=name,mail=email@address" for the id. */
	/* Try to strip the name out. */
	if (g_strncasecmp(id, "cn=", 3) == 0) {
	    id += 3;
	    while (*id && isspace (*id)) id++;
	}
	while ((end = strchr(id, ',')) != NULL) {
	    if (g_strncasecmp(end, ",mail=", 6) == 0) {
		*end = '\0';
		break;
	    }
	}
	if (*id)
	    name = g_strdup(id);
    }
    return name;
}

static void
libbalsa_address_book_ldif_store_address(LibBalsaAddressBook * ab,
					  LibBalsaAddress * new_address)
{
    GList *list;
    gchar *id;
    LibBalsaAddress *address;
    FILE *fp;

    if (new_address->id != NULL && *(new_address->id) != '\0') {
	id = g_strdup(new_address->id);
    } else {
	if (new_address->full_name != NULL && 
	    new_address->full_name[0] != '\0') {
	    id = g_strdup(new_address->full_name);
	} else {
	    id = build_name(NULL, new_address->first_name, 
			    new_address->last_name);
	    if (id == NULL) {
		id = g_strdup(_("No-Name"));
	    } else {
		if(id[0] == '\0') {
		    g_free(id);
		    id = g_strdup(_("No-Name"));
		}
	    }
	}
    }

    load_ldif_file(ab);

    list = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->address_list;
    while (list) {
	address = LIBBALSA_ADDRESS(list->data);

	if (g_strcasecmp(address->full_name, new_address->full_name) == 0) {
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
				 _("%s is already in address book."),
				 new_address->full_name);
	    g_free(id);
	    return;
	}
	list = g_list_next(list);
    }

    fp = fopen(LIBBALSA_ADDRESS_BOOK_LDIF(ab)->path, "a");
    if (fp == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Cannot open LDIF address book %s for saving\n"),
			     ab->name);
	g_free(id);
	return;
    }

    fprintf(fp, "\ndn: cn=%s", id);
    if (new_address->address_list && new_address->address_list->data) {
	fprintf(fp, ",mail=%s", (gchar *) new_address->address_list->data);
    }
    fprintf(fp, "\n");
    fprintf(fp, "cn: %s\n", id);
    g_free(id);
    if (new_address->first_name && *(new_address->first_name)) {
	fprintf(fp, "givenname: %s\n", new_address->first_name);
    }
    if (new_address->last_name && *(new_address->last_name)) {
	fprintf(fp, "sn: %s\n", new_address->last_name);
    }
    if (new_address->organization && *(new_address->organization)) {
	fprintf(fp, "o: %s\n", new_address->organization);
    }
    list = new_address->address_list;
    while (list) {
	if (list->data && *(gchar*)(list->data)) {
	    fprintf(fp, "mail: %s\n", (gchar *) list->data);
	}
	list = g_list_next(list);
    }
    fprintf(fp, "\n");
    fclose(fp);
}

static void
libbalsa_address_book_ldif_save_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookLdif *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab));

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    gnome_config_set_string("Path", vc->path);

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_ldif_load_config(LibBalsaAddressBook * ab,
					const gchar * prefix)
{
    LibBalsaAddressBookLdif *vc;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab));

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    g_free(vc->path);
    vc->path = gnome_config_get_string("Path");

    if (LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config)
	LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

static GList*
libbalsa_address_book_ldif_alias_complete(LibBalsaAddressBook * ab,
					  const gchar * prefix,
					  gchar ** new_prefix)
{
    LibBalsaAddressBookLdif *vc;
    GList *resa = NULL, *resb = NULL;
    GList *res = NULL;
    gchar *p1 = NULL, *p2 = NULL;
    LibBalsaAddress *addr1, *addr2;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_LDIF(ab), NULL);

    vc = LIBBALSA_ADDRESS_BOOK_LDIF(ab);

    if ( ab->expand_aliases == FALSE )
	return NULL;

    load_ldif_file(ab);

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
