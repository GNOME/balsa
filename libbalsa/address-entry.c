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
 * A subclass of gtkentry to support alias completion.
 */

#include "config.h"

#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>
#include "address-entry.h"

static GtkWidgetClass *parent_class = NULL;

/*
 * Functions all GtkObjects need.
 */
static void libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass);
static void libbalsa_address_entry_init(LibBalsaAddressEntry *ab);
static void libbalsa_address_entry_destroy(GtkObject * object);

/*
 * stuff that we define for the user.
 */


/*
 * This function must return the type info for Gtk
 */
GtkType libbalsa_address_entry_get_type(void)
{
    static GtkType address_entry_type = 0;

    if (!address_entry_type) {
	static const GtkTypeInfo address_entry_info = {
	    "LibBalsaAddressEntry",
	    sizeof(LibBalsaAddressEntry),
	    sizeof(LibBalsaAddressEntryClass),
	    (GtkClassInitFunc) libbalsa_address_entry_class_init,
	    (GtkObjectInitFunc) libbalsa_address_entry_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_entry_type =
	    gtk_type_unique(GTK_TYPE_ENTRY, &address_entry_info);
    }

    return address_entry_type;
}


/*
 * This initialises the class.
 */
static void
libbalsa_address_entry_class_init(LibBalsaAddressEntryClass *klass)
{
    GtkEntryClass *gtk_entry_class;
    GtkWidgetClass *gtk_widget_class;
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);
    gtk_widget_class = GTK_WIDGET_CLASS(klass);
    gtk_entry_class = GTK_ENTRY_CLASS(klass);
    parent_class = gtk_type_class(GTK_TYPE_ENTRY);

    object_class->destroy = libbalsa_address_entry_destroy;
}


/*
 * This gets called by GTK internals rather than the user.
 */
static void
libbalsa_address_entry_init(LibBalsaAddressEntry *entry)
{
    entry->input = NULL;

    return;
}


/*
 * This gets called by GTK internals rather than the user.
 */
static void
libbalsa_address_entry_destroy(GtkObject * object)
{
    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}


/*
 * Allocate a new libbalsa_address_entry() for use.
 */
GtkWidget *
libbalsa_address_entry_new(void)
{
    LibBalsaAddressEntry *entry;

    entry = gtk_type_new(LIBBALSA_TYPE_ADDRESS_ENTRY);
    return GTK_WIDGET(entry);
}


inputData *
libbalsa_address_entry_get_input(LibBalsaAddressEntry *entry)
{
    g_return_val_if_fail(entry != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(entry), NULL);

    return entry->input;
}


void
libbalsa_address_entry_set_input(LibBalsaAddressEntry *entry, inputData *data)
{
    g_return_if_fail(entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(entry));

    entry->input = data;
}


/*
 * libbalsa_address_entry_show()
 *
 * Shows the current input to the user.  It shows what the user typed,
 * what it will expand to, and highlight that section.
 *
 * FIXME: Adding the lists together should be easier.
 * FIXME: Add colour support.
 */
void
libbalsa_address_entry_show(LibBalsaAddressEntry *entry)
{
    gchar *show;
    GList *list;
    emailData *addy;
    gchar *out;
    gchar *str;
    gint cursor;
    gboolean found;
    inputData *input;
        
    g_return_if_fail(entry != NULL);
    g_return_if_fail(LIBBALSA_IS_ADDRESS_ENTRY(entry));
    g_return_if_fail(entry->input != NULL);

    input = entry->input;
    show = g_strdup("");
    cursor = 0;
    found = FALSE;
    for (list = g_list_first(input->list);
         list != NULL;
	 list = g_list_next(list)) {
	/*
	 * Is it a normal string, or is it a match that requires ()
	 */
        addy = (emailData *)list->data;
	if (addy->match != NULL) {
            str = g_strdup("");
	    out = g_strconcat(str, addy->user, " (", addy->match, ")", NULL);
	    g_free(str);
	} else {
	    out = g_strdup(addy->user);
	}
	/*
	 * Copy the string, adding a delimiter if need be.
	 */
	str = g_strdup(show);
	if (g_list_next(list) != NULL) {
	    show = g_strconcat(str, out, ", ", NULL);
	} else {
	    show = g_strconcat(str, out, NULL);
	}
	g_free(str);
	/*
	 * Check for the cursor position.
	 */
	if (!found) {
	    if (list != input->active) {
		cursor += strlen(out);
	    	if (g_list_next(list) != NULL) cursor += 2;
	    } else {
		found = TRUE;
		cursor += addy->cursor;
	    }
	}
	g_free(out);
    }
    /*
     * Show it...
     */
    gtk_editable_set_position(GTK_EDITABLE(entry), cursor);
    gtk_entry_set_text(GTK_ENTRY(entry), show);
}


