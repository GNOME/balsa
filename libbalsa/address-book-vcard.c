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
 * A VCard (eg GnomeCard) addressbook.
 * assumes that the file charset is in current locale.
 * the strings are converted to UTF-8.
 * FIXME: verify assumption agains RFC.
 * Obviously, the best method would be to have file encoded in UTF-8.
 */

#include "config.h"

#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

#include <libgnome/libgnome.h>

#include "address-book.h"
#include "address-book-vcard.h"
#include "abook-completion.h"

/* FIXME: Perhaps the whole thing could be rewritten to use a g_scanner ?? */

/* FIXME: Arbitrary constant */
#define LINE_LEN 256

static LibBalsaAddressBookClass *parent_class = NULL;

static void libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *klass);
static void libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard *ab);
static void libbalsa_address_book_vcard_finalize(GObject * object);

static LibBalsaABErr libbalsa_address_book_vcard_load(LibBalsaAddressBook* ab,
                                                      const gchar *filter,
                                                      LibBalsaAddressBookLoadFunc
                                                      callback, 
                                                      gpointer closure);
static LibBalsaABErr libbalsa_address_book_vcard_add_address(LibBalsaAddressBook *ab,
                                                             LibBalsaAddress *address);
static LibBalsaABErr libbalsa_address_book_vcard_remove_address(LibBalsaAddressBook *ab,
                                                                LibBalsaAddress *address);
static LibBalsaABErr libbalsa_address_book_vcard_modify_address(LibBalsaAddressBook *ab,
                                                                LibBalsaAddress *address,
                                                                LibBalsaAddress *newval);

static void libbalsa_address_book_vcard_save_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static void libbalsa_address_book_vcard_load_config(LibBalsaAddressBook *ab,
						    const gchar * prefix);
static GList *libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
							 const gchar * prefix,
							 gchar ** new_prefix);

static gboolean load_vcard_file(LibBalsaAddressBook *ab);

static gchar *
extract_name(const gchar * string);

static gboolean vcard_address_book_need_reload(LibBalsaAddressBookVcard *ab);


GType libbalsa_address_book_vcard_get_type(void)
{
    static GType address_book_vcard_type = 0;

    if (!address_book_vcard_type) {
	static const GTypeInfo address_book_vcard_info = {
	    sizeof(LibBalsaAddressBookVcardClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_address_book_vcard_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaAddressBookVcard),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_address_book_vcard_init
	};

	address_book_vcard_type =
            g_type_register_static(LIBBALSA_TYPE_ADDRESS_BOOK,
	                           "LibBalsaAddressBookVcard",
			           &address_book_vcard_info, 0);
    }

    return address_book_vcard_type;

}

static void
libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *
				       klass)
{
    LibBalsaAddressBookClass *address_book_class;
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);
    address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

    object_class->finalize = libbalsa_address_book_vcard_finalize;

    address_book_class->load = libbalsa_address_book_vcard_load;
    address_book_class->add_address =
	libbalsa_address_book_vcard_add_address;
    address_book_class->remove_address =
	libbalsa_address_book_vcard_remove_address;
    address_book_class->modify_address =
	libbalsa_address_book_vcard_modify_address;

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

    ab->name_complete =
	g_completion_new((GCompletionFunc)completion_data_extract);
    g_completion_set_compare(ab->name_complete, strncmp_word);
}

static void
libbalsa_address_book_vcard_finalize(GObject * object)
{
    LibBalsaAddressBookVcard *addr_vcard;

    addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(object);

    g_free(addr_vcard->path);

    g_list_foreach(addr_vcard->address_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr_vcard->address_list);
    addr_vcard->address_list = NULL;

    g_list_foreach(addr_vcard->name_complete->items, (GFunc)completion_data_free, NULL);
    g_completion_free(addr_vcard->name_complete);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaAddressBook *
libbalsa_address_book_vcard_new(const gchar * name, const gchar * path)
{
    LibBalsaAddressBookVcard *abvc;
    LibBalsaAddressBook *ab;

    abvc =
        LIBBALSA_ADDRESS_BOOK_VCARD(g_object_new
                                    (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD,
                                     NULL));
    ab = LIBBALSA_ADDRESS_BOOK(abvc);

    ab->name = g_strdup(name);
    abvc->path = g_strdup(path);

    return ab;
}

/* returns true if the book has changed or there is an error */
static gboolean 
vcard_address_book_need_reload(LibBalsaAddressBookVcard *ab)
{
    struct stat stat_buf;

    if ( stat(ab->path, &stat_buf) == -1 )
	return TRUE;

    if ( stat_buf.st_mtime > ab->mtime ) {
	ab->mtime = stat_buf.st_mtime;
	return TRUE;
    } else {
	return FALSE;
    }
}

static gboolean
starts_from(const gchar *str, const gchar *filter_hi)
{
    if(!str) return FALSE;
    while(*str && *filter_hi &&
          g_unichar_toupper(g_utf8_get_char(str)) == 
          g_utf8_get_char(filter_hi)) {
        str       = g_utf8_next_char(str);
        filter_hi = g_utf8_next_char(filter_hi);
    }
           
    return *filter_hi == '\0';
}
 

static LibBalsaABErr
libbalsa_address_book_vcard_load(LibBalsaAddressBook * ab,
                                 const gchar *filter,
                                 LibBalsaAddressBookLoadFunc callback,
                                 gpointer closure)
{
    GList *lst;
    int len = filter ? strlen(filter) : 0;
    gchar *filter_hi = NULL;

    if(!load_vcard_file(ab)) return LBABERR_CANNOT_READ;
    if(len)
        filter_hi = g_utf8_strup(filter, -1);

    for(lst = LIBBALSA_ADDRESS_BOOK_VCARD(ab)->address_list;
        lst; 
        lst = g_list_next(lst) ) {
        LibBalsaAddress *adr = LIBBALSA_ADDRESS(lst->data);
        if(callback &&(!len ||
                       starts_from(adr->last_name, filter_hi) ||
                       starts_from(adr->full_name, filter_hi) ) )
	    callback(ab, adr, closure);
    }
    if(callback) callback(ab, NULL, closure);
    if(len)
        g_free(filter_hi);
    return LBABERR_OK;
}

static gchar *
validate_vcard_string(gchar * vcstr)
{
    gchar * utf8res;
    gsize b_written;

    /* check if it's a utf8 clean string and return it in this case */
    if (g_utf8_validate(vcstr, -1, NULL))
	return vcstr;

    /* try to convert from the user's locale setting */
    utf8res = g_locale_to_utf8(vcstr, -1, NULL, &b_written, NULL);
    if (!utf8res)
	return vcstr;

    g_free(vcstr);
    return utf8res;
}

/* To create distribution lists based on the ORG field:
 * #define MAKE_GROUP_BY_ORGANIZATION TRUE
 */
#if MAKE_GROUP_BY_ORGANIZATION
static void
group_address(const gchar * group_name, GSList * group_addresses,
              GList ** completion_list)
{
    GSList *l;
    CompletionData *cmp_data;
    InternetAddress *ia;

    if (!group_addresses || !group_addresses->next)
        return;

    ia = internet_address_new_group(group_name);
    for (l = group_addresses; l; l = l->next) {
        GList *mailbox;
        LibBalsaAddress *address = LIBBALSA_ADDRESS(l->data);

        for (mailbox = address->address_list; mailbox;
             mailbox = mailbox->next) {
            InternetAddress *member =
                internet_address_new_name(address->full_name,
                                          mailbox->data);
            internet_address_add_member(ia, member);
            internet_address_unref(member);
        }
    }
    g_slist_free(group_addresses);

    cmp_data = completion_data_new(ia, NULL);
    internet_address_unref(ia);
    *completion_list = g_list_prepend(*completion_list, cmp_data);
}
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

/* FIXME: Could stat the file to see if it has changed since last time
   we read it */
static gboolean
load_vcard_file(LibBalsaAddressBook *ab)
{
    FILE *gc;
    gchar string[LINE_LEN];
    gchar *name = NULL, *id = NULL, *org = NULL;
    gint in_vcard = FALSE;
    GList *list = NULL;
    GList *completion_list = NULL;
    GList *address_list = NULL;
    CompletionData *cmp_data;
#if MAKE_GROUP_BY_ORGANIZATION
    GHashTable *group_table;
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

    LibBalsaAddressBookVcard *addr_vcard;

    addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    if (!vcard_address_book_need_reload(addr_vcard)) 
	return TRUE;

    g_list_foreach(addr_vcard->address_list, (GFunc) g_object_unref, NULL);
    g_list_free(addr_vcard->address_list);
    addr_vcard->address_list = NULL;

    g_list_foreach(addr_vcard->name_complete->items,
                   (GFunc)completion_data_free, NULL);
    g_completion_clear_items(addr_vcard->name_complete);

    if( (gc = fopen(addr_vcard->path, "r")) == NULL)
	return FALSE;

    while (fgets(string, sizeof(string), gc)) {
	/*
	 * Check if it is a card.
	 */
	if (g_ascii_strncasecmp(string, "BEGIN:VCARD", 11) == 0) {
	    in_vcard = TRUE;
	    continue;
	}

	/*
	 * We are done loading a card.
	 */
	if (g_ascii_strncasecmp(string, "END:VCARD", 9) == 0) {
	    LibBalsaAddress *address;
	    if (address_list) {
		address = libbalsa_address_new();

		address->nick_name = id ? id : g_strdup(_("No-Id"));

		address->address_list = g_list_reverse(address_list);

		if (name)
		    address->full_name = name;
                else if (id)
		    address->full_name = g_strdup(id);
		else
		    address->full_name = g_strdup(_("No-Name"));

		/* FIXME: Split into Firstname, Middlename and Lastname... */

		address->organization = org;

		list = g_list_prepend(list, address);
		address_list = NULL;
	    } else {		/* record without e-mail address, ignore */
		g_free(name);
		g_free(id);
		g_free(org);
	    }
	    name = NULL;
	    id = NULL;
	    org = NULL;
	    in_vcard = FALSE;
	    continue;
	}

	if (!in_vcard)
	    continue;

	g_strchomp(string);

	if (g_ascii_strncasecmp(string, "FN:", 3) == 0) {
	    id = g_strdup(string + 3);
	    id = validate_vcard_string(id);
	    continue;
	}

	if (g_ascii_strncasecmp(string, "N:", 2) == 0) {
	    name = extract_name(string + 2);
	    name = validate_vcard_string(name);
	    continue;
	}

	if (g_ascii_strncasecmp(string, "ORG:", 4) == 0) {
	    org = g_strdup(string + 4);
	    org = validate_vcard_string(org);
	    continue;
	}

	/*
	 * fetch all e-mail fields
	 */
	if (g_ascii_strncasecmp(string, "EMAIL;", 6) == 0) {
	    gchar *ptr = strchr(string+6, ':');
	    if (ptr) {
		address_list =
		    g_list_prepend(address_list, g_strdup(ptr + 1));
	    }
	}
    }
    fclose(gc);

    if (!list)
        return TRUE;

    list = g_list_sort(list, (GCompareFunc)address_compare);
    addr_vcard->address_list = list;

    completion_list = NULL;
#if MAKE_GROUP_BY_ORGANIZATION
    group_table =
	g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
    for (;list; list = list->next) {
	LibBalsaAddress *address = list->data;
#if MAKE_GROUP_BY_ORGANIZATION
	gchar **groups, **group;
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
	GList *l;

	if (address->address_list->next && ab->dist_list_mode) {
	    /* Create a group address. */
	    InternetAddress *ia =
		internet_address_new_group(address->full_name);

	    for (l = address->address_list; l; l = l->next) {
		InternetAddress *member =
		    internet_address_new_name(NULL, l->data);
		internet_address_add_member(ia, member);
		internet_address_unref(member);
	    }
	    cmp_data = completion_data_new(ia, address->nick_name);
	    completion_list = g_list_prepend(completion_list, cmp_data);
	    internet_address_unref(ia);
	} else {
	    /* Create name addresses. */
	    GList *l;

	    for (l = address->address_list; l; l = l->next) {
		InternetAddress *ia =
		    internet_address_new_name(address->full_name, l->data);
		cmp_data = completion_data_new(ia, address->nick_name);
		completion_list = g_list_prepend(completion_list, cmp_data);
		internet_address_unref(ia);
	    }
	}

#if MAKE_GROUP_BY_ORGANIZATION
	if (!address->organization || !*address->organization)
	    continue;
	groups = g_strsplit(address->organization, ";", 0);
	for (group = groups; *group; group++) {
	    gchar *group_name;
	    GSList *group_addresses;

	    g_strstrip(*group);
            group_name = group == groups ? g_strdup(*group) :
                g_strconcat(*groups, " ", *group, NULL);
	    group_addresses = g_hash_table_lookup(group_table, group_name);
	    group_addresses = g_slist_prepend(group_addresses, address);
	    g_hash_table_replace(group_table, group_name, group_addresses);
	}
	g_strfreev(groups);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */
    }

#if MAKE_GROUP_BY_ORGANIZATION
    g_hash_table_foreach(group_table, (GHFunc) group_address,
	                 &completion_list);
    g_hash_table_destroy(group_table);
#endif                          /* MAKE_GROUP_BY_ORGANIZATION */

    completion_list = g_list_reverse(completion_list);
    g_completion_add_items(addr_vcard->name_complete, completion_list);
    g_list_free(completion_list);

    return TRUE;
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

static LibBalsaABErr
libbalsa_address_book_vcard_add_address(LibBalsaAddressBook * ab,
                                        LibBalsaAddress * new_address)
{
    GList *list;
    LibBalsaAddress *address;
    FILE *fp;
    LibBalsaABErr res = LBABERR_OK;

    load_vcard_file(ab); /* Ignore reading error, we may be adding */
                         /* the first address. */

    list = LIBBALSA_ADDRESS_BOOK_VCARD(ab)->address_list;
    while (list) {
	address = LIBBALSA_ADDRESS(list->data);

	if (g_ascii_strcasecmp(address->full_name, new_address->full_name)==0)
	    return LBABERR_DUPLICATE;
	list = g_list_next(list);
    }

    fp = fopen(LIBBALSA_ADDRESS_BOOK_VCARD(ab)->path, "a");
    if (fp == NULL) 
        return LBABERR_CANNOT_WRITE;

    fprintf(fp, "BEGIN:VCARD\n");
    if (new_address->full_name && *new_address->full_name != '\0')
	fprintf(fp, "FN:%s\n", new_address->full_name);
    if (new_address->first_name && *new_address->first_name != '\0') {
	if (new_address->last_name && *new_address->last_name != '\0') {
	    if (new_address->middle_name
		&& *new_address->middle_name != '\0')
		fprintf(fp, "N:%s;%s;%s\n", new_address->last_name,
			new_address->first_name, new_address->middle_name);
	    else
		fprintf(fp, "N:%s;%s\n", new_address->last_name,
			new_address->first_name);
	} else
	    fprintf(fp, "N:;%s\n", new_address->first_name);
    }
    if (new_address->organization && *new_address->organization != '\0')
	fprintf(fp, "ORG:%s\n", new_address->organization);

    for (list = new_address->address_list; list; list = g_list_next(list))
	fprintf(fp, "EMAIL;INTERNET:%s\n", (gchar *) list->data);

    res = fprintf(fp, "END:VCARD\n\n") > 0 
        ? LBABERR_OK : LBABERR_CANNOT_WRITE;
    fclose(fp);
    return res;
}

static LibBalsaABErr
libbalsa_address_book_vcard_remove_address(LibBalsaAddressBook *ab,
                                           LibBalsaAddress *address)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
}

static LibBalsaABErr
libbalsa_address_book_vcard_modify_address(LibBalsaAddressBook *ab,
                                           LibBalsaAddress *address,
                                           LibBalsaAddress *newval)
{
    /* FIXME: implement */
    return LBABERR_CANNOT_WRITE;
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

static GList *
libbalsa_address_book_vcard_alias_complete(LibBalsaAddressBook * ab,
					   const gchar * prefix, 
					   char ** new_prefix)
{
    LibBalsaAddressBookVcard *vc;
    GList *list;
    GList *res = NULL;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab), NULL);

    vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

    if ( ab->expand_aliases == FALSE )
	return NULL;

    load_vcard_file(ab);

    for (list = g_completion_complete(vc->name_complete, (gchar *) prefix,
                                      new_prefix);
         list; list = list->next) {
	InternetAddress *ia = ((CompletionData *) list->data)->ia;
	internet_address_ref(ia);
        res = g_list_prepend(res, ia);
    }

    return g_list_reverse(res);
}

