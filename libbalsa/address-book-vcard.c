/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

#include "address-book.h"
#include "address-book-vcard.h"
#include "information.h"

/* FIXME: Arbitrary constant */
/* Perhaps the whole thing could be rewritten to use a g_scanner ?? */
#define LINE_LEN 256

static GtkObjectClass *parent_class = NULL;

static void libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *klass);
static void libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard *ab);
static void libbalsa_address_book_vcard_destroy (GtkObject *object);

static void libbalsa_address_book_vcard_load(LibBalsaAddressBook *ab);
static void libbalsa_address_book_vcard_store_address(LibBalsaAddressBook *ab, LibBalsaAddress *new_address);

static void libbalsa_address_book_vcard_save_config(LibBalsaAddressBook *ab, const gchar *prefix);
static void libbalsa_address_book_vcard_load_config(LibBalsaAddressBook *ab, const gchar *prefix);

static gchar *extract_name(const gchar *string);

GtkType
libbalsa_address_book_vcard_get_type (void)
{
	static GtkType address_book_vcard_type = 0;

	if (!address_book_vcard_type) {
		static const GtkTypeInfo address_book_vcard_info = {
			"LibBalsaAddressBookVcard",
			sizeof (LibBalsaAddressBookVcard),
			sizeof (LibBalsaAddressBookVcardClass),
			(GtkClassInitFunc) libbalsa_address_book_vcard_class_init,
			(GtkObjectInitFunc) libbalsa_address_book_vcard_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		address_book_vcard_type = gtk_type_unique(libbalsa_address_book_get_type(), &address_book_vcard_info);
	}

	return address_book_vcard_type;

}

static void
libbalsa_address_book_vcard_class_init(LibBalsaAddressBookVcardClass *klass)
{
	LibBalsaAddressBookClass *address_book_class;
	GtkObjectClass *object_class;

	parent_class = gtk_type_class(LIBBALSA_TYPE_ADDRESS_BOOK);

	object_class = GTK_OBJECT_CLASS(klass);
	address_book_class = LIBBALSA_ADDRESS_BOOK_CLASS(klass);

	object_class->destroy = libbalsa_address_book_vcard_destroy;

	address_book_class->load = libbalsa_address_book_vcard_load;
	address_book_class->store_address = libbalsa_address_book_vcard_store_address;

	address_book_class->save_config = libbalsa_address_book_vcard_save_config;
	address_book_class->load_config = libbalsa_address_book_vcard_load_config;
}

static void
libbalsa_address_book_vcard_init(LibBalsaAddressBookVcard *ab)
{
	ab->path = NULL;
}

static void
libbalsa_address_book_vcard_destroy (GtkObject *object)
{
	LibBalsaAddressBookVcard *addr_vcard;

	addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(object);

	g_free(addr_vcard->path);

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));

}

LibBalsaAddressBook*
libbalsa_address_book_vcard_new (const gchar *name, const gchar *path)
{
	LibBalsaAddressBookVcard *abvc;
	LibBalsaAddressBook *ab;

	abvc = gtk_type_new(LIBBALSA_TYPE_ADDRESS_BOOK_VCARD);
	ab = LIBBALSA_ADDRESS_BOOK(abvc);

	ab->name = g_strdup(name);
	abvc->path = g_strdup(path);

	return ab;
}

static void
libbalsa_address_book_vcard_load(LibBalsaAddressBook *ab)
{
	FILE *gc; 
	gchar string[LINE_LEN];
	gchar *name = NULL, *id = NULL;
	gint in_vcard = FALSE;
	GList* list = NULL;
	GList* address_list = NULL;

	LibBalsaAddressBookVcard *addr_vcard;

	addr_vcard = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

	g_list_foreach(ab->address_list, (GFunc)gtk_object_unref, NULL);
	g_list_free(ab->address_list);
	ab->address_list = NULL;

	gc = fopen(addr_vcard->path,"r"); 

	if (gc == NULL) 
		return;

	while (fgets (string, sizeof(string), gc)) { 
		/*
		 * Check if it is a card.
		 */
		if (g_strncasecmp (string, "BEGIN:VCARD", 11) == 0 ) {
			in_vcard = TRUE;
			continue;
		}
		
		/*
		 * We are done loading a card.
		 */
		if (g_strncasecmp (string, "END:VCARD", 9) == 0) {
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
					address->full_name = g_strdup( _("No-Name") );


				/* FIXME: Split into Firstname and Lastname... */
				
				list = g_list_append (list, address);
				address_list = NULL;
			} else { /* record without e-mail address, ignore */
				g_free (name);
				g_free (id);
			} 
			name = NULL;
			id = NULL;
			in_vcard = FALSE;
			continue;
		}
		
		if (!in_vcard) continue;
		
		g_strchomp(string);
		
		if (g_strncasecmp(string, "FN:", 3) == 0) {
			id = g_strdup(string+3);
			continue;
		}

		if (g_strncasecmp(string, "N:", 2) == 0) {
			name = extract_name(string+2);
			continue;
		}

		/*
		 * fetch all e-mail fields
		 */
		if (g_strncasecmp (string, "EMAIL;",6) == 0) {
			gchar * ptr = strchr(string,':');
			if(ptr) {
				address_list = g_list_append(address_list, g_strdup(ptr+1));
			}
		}
	}	 
	fclose(gc); 

	ab->address_list = list;
}

static gchar * 
extract_name(const gchar *string)
/* Extract full name in order from <string> that has GnomeCard format
   and returns the pointer to the allocated memory chunk.
*/
{
	enum GCardFieldOrder { LAST=0, FIRST, MIDDLE, PREFIX, SUFFIX };
	gint cpt, j;
	gchar ** fld, **name_arr;
	gchar * res = NULL;

	fld = g_strsplit(string, ";", 5);

	cpt = 0;
	while(fld[cpt] != NULL)
		cpt++;

	if(cpt==0) /* insane empty name */ 
		return NULL;

	name_arr = g_malloc((cpt+1)*sizeof(gchar*));

	j = 0;
	if(cpt>PREFIX && strlen (fld[PREFIX]) != 0)
		name_arr[j++] = g_strdup(fld[PREFIX]);
      
	if(cpt>FIRST && strlen (fld[FIRST]) != 0)
		name_arr[j++] = g_strdup(fld[FIRST]);

	if(cpt>MIDDLE && strlen (fld[MIDDLE]) != 0)
		name_arr[j++] = g_strdup(fld[MIDDLE]);

	if(cpt>LAST && strlen (fld[LAST]) != 0)
		name_arr[j++] = g_strdup(fld[LAST]);

	if(cpt>SUFFIX && strlen (fld[SUFFIX]) != 0)
		name_arr[j++] = g_strdup(fld[SUFFIX]);

	name_arr[j] = NULL;

	g_strfreev(fld);

	/* collect the data to one string */
	res = g_strjoinv(" ", name_arr);
	while(j-- > 0)
		g_free(name_arr[j]);

	g_free(name_arr);

	return res;
}

static void
libbalsa_address_book_vcard_store_address(LibBalsaAddressBook *ab, LibBalsaAddress *new_address)
{
	GList *list;
	gchar *output;
	LibBalsaAddress *address;
	FILE *fp;

	libbalsa_address_book_load(ab);
	
	list = ab->address_list;
	while(list) {
		address = LIBBALSA_ADDRESS(list->data);

		if ( g_strcasecmp(address->full_name, new_address->full_name) == 0) {
			libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, _("%s is already in address book."), new_address->full_name);
			return;
		}
		list = g_list_next(list);
	}

	fp = fopen(LIBBALSA_ADDRESS_BOOK_VCARD(ab)->path, "a");
	if ( fp == NULL ) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Cannot open vCard address book %s\n"), ab->name);
		return;
	}

	fprintf(fp, "\nBEGIN:VCARD\n");
	if ( new_address->full_name ) {
		output = g_strdup_printf("FN:%s\n", new_address->full_name);
		fprintf(fp, output);
		g_free(output);
	}
	if ( new_address->first_name && new_address->last_name ) {
		output = g_strdup_printf( "N:%s;%s\n", new_address->last_name, new_address->first_name);
		fprintf(fp,output);
		g_free(output);
	}
	if ( new_address->organization ) {
		output = g_strdup_printf("ORG:%s\n", new_address->organization);
		fprintf(fp, output);
		g_free(output);
	}
	list = new_address->address_list;
	while(list) {
		output = g_strdup_printf("EMAIL;INTERNET:%s\n", (gchar*)list->data);
		fprintf(fp, output);
		g_free(output);
		list = g_list_next(list);
	}
	fprintf(fp, "END:VCARD\n");
	fclose(fp);
}

static void
libbalsa_address_book_vcard_save_config(LibBalsaAddressBook *ab, const gchar *prefix)
{
	LibBalsaAddressBookVcard *vc;

	g_return_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab) );

	vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

	gnome_config_set_string("Path", vc->path);

	if ( LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config )
		LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->save_config(ab, prefix);
}

static void
libbalsa_address_book_vcard_load_config(LibBalsaAddressBook *ab, const gchar *prefix)
{
	LibBalsaAddressBookVcard *vc;
	
	g_return_if_fail ( LIBBALSA_IS_ADDRESS_BOOK_VCARD(ab) );
	
	vc = LIBBALSA_ADDRESS_BOOK_VCARD(ab);

	g_free(vc->path);
	vc->path = gnome_config_get_string("Path");

	if ( LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config )
		LIBBALSA_ADDRESS_BOOK_CLASS(parent_class)->load_config(ab, prefix);
}

