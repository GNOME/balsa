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

#include "config.h"

#include <glib.h>
#include <string.h>

#include "libbalsa.h"

static GtkObjectClass *parent_class = NULL;

static void libbalsa_contact_class_init(LibBalsaContactClass *klass);
static void libbalsa_contact_init(LibBalsaContact *contact);
static void libbalsa_contact_destroy (GtkObject *object);

GtkType
libbalsa_contact_get_type (void)
{
	static GtkType contact_type = 0;

	if (!contact_type) {
		static const GtkTypeInfo contact_info = {
			"LibBalsaContact",
			sizeof (LibBalsaContact),
			sizeof (LibBalsaContactClass),
			(GtkClassInitFunc) libbalsa_contact_class_init,
			(GtkObjectInitFunc) libbalsa_contact_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		contact_type = gtk_type_unique(gtk_object_get_type(), &contact_info);
	}

	return contact_type;
}

LibBalsaContact *
libbalsa_contact_new (void)
{
	LibBalsaContact *contact;

	contact = gtk_type_new(libbalsa_contact_get_type());

	return contact;
}

static void libbalsa_contact_class_init(LibBalsaContactClass *klass)
{
	GtkObjectClass *object_class;

	parent_class = gtk_type_class(GTK_TYPE_OBJECT);

	object_class = GTK_OBJECT_CLASS(klass);

	object_class->destroy = libbalsa_contact_destroy;
}

static void libbalsa_contact_init(LibBalsaContact *contact)
{
	contact->card_name = NULL;
	contact->first_name = NULL;
	contact->last_name = NULL;
	contact->organization = NULL;
	contact->email_address = NULL;
}

static void libbalsa_contact_destroy (GtkObject *object)
{
	LibBalsaContact *contact;

	contact = LIBBALSA_CONTACT(object);

	g_free(contact->card_name);	contact->card_name = NULL;
	g_free(contact->first_name);	contact->first_name = NULL;
	g_free(contact->last_name);	contact->last_name = NULL;
	g_free(contact->organization);	contact->organization = NULL;
	g_free(contact->email_address);	contact->email_address = NULL;

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));

}

void
libbalsa_contact_list_free(GList * contact_list)
{
	GList *list;

	for (list = g_list_first (contact_list); list; list = g_list_next (list)) {
		if(list->data) 
			gtk_object_destroy(GTK_OBJECT(list->data));
	}

	g_list_free (contact_list);
}

gint
libbalsa_contact_store(LibBalsaContact *contact, const gchar *fname)
{
	FILE *gc; 
	gchar string[256];
	gint in_vcard = FALSE;

	g_return_val_if_fail(fname, LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE);

	if(strlen(contact->card_name) == 0)
		return LIBBALSA_CONTACT_CARD_NAME_FIELD_EMPTY;

	gc = fopen(fname, "r+");
    
	if (!gc) 
		return LIBBALSA_CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE; 
            
	while (fgets(string, sizeof(string), gc)) 
	{ 
		if ( g_strncasecmp(string, "BEGIN:VCARD", 11) == 0 ) {
			in_vcard = TRUE;
			continue;
		}
                
		if ( g_strncasecmp(string, "END:VCARD", 9) == 0 ) {
			in_vcard = FALSE;
			continue;
		}
        
		if (!in_vcard) continue;
        
		g_strchomp(string);
                
		if ( g_strncasecmp(string, "FN:", 3) == 0 )
		{
			gchar *id = g_strdup(string+3);
			if(g_strcasecmp(id, contact->card_name) == 0)
			{
				g_free(id);
				fclose(gc);
				return LIBBALSA_CONTACT_CARD_NAME_EXISTS;
			}
			g_free(id);
			continue;
		}
	}

	fprintf(gc, "\nBEGIN:VCARD\n");
	fprintf(gc, g_strdup_printf( "FN:%s\n", contact->card_name));

	if(strlen(contact->first_name) || strlen(contact->last_name))
		fprintf(gc, g_strdup_printf( "N:%s;%s\n", contact->last_name, contact->first_name));

	if(strlen(contact->organization))
		fprintf(gc, g_strdup_printf( "ORG:%s\n", contact->organization));
            
	if(strlen(contact->email_address))
		fprintf(gc, g_strdup_printf( "EMAIL;INTERNET:%s\n", contact->email_address));
            
	fprintf(gc, "END:VCARD\n");
    
	fclose(gc);
	return LIBBALSA_CONTACT_CARD_STORED_SUCCESSFULLY;
}
