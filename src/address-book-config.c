/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#include <gnome.h>

#include "balsa-app.h"
#include "address-book-config.h"

typedef struct _AddressBookConfig AddressBookConfig;
struct _AddressBookConfig {
    GtkWidget *window;
    GtkWidget *notebook;

    /* This button is the next/ok/update button */
    GtkWidget *continue_button;

    /* Set to true when cancel pressed. */
    gboolean cancelled;

    GtkWidget *name_entry;
    GtkWidget *expand_aliases_button;

    GtkType create_type;

    union {
	struct {
	    GtkWidget *path;
	} vcard;
#ifdef ENABLE_LDAP
	struct {
	    GtkWidget *host_name;
	    GtkWidget *base_dn;
	} ldap;
#endif
    } ab_specific;

    LibBalsaAddressBook *address_book;
};

static GtkWidget *create_choice_page(AddressBookConfig * abc);
static GtkWidget *create_vcard_page(AddressBookConfig * abc);
#ifdef ENABLE_LDAP
static GtkWidget *create_ldap_page(AddressBookConfig * abc);
#endif

static void cancel_button_cb(GtkWidget * button, AddressBookConfig * abc);
static void next_button_cb(GtkWidget * button, AddressBookConfig * abc);
static void add_button_cb(GtkWidget * button, AddressBookConfig * abc);
static void update_button_cb(GtkWidget * button, AddressBookConfig * abc);

static void set_the_page(GtkWidget * button, AddressBookConfig * abc);

/*
 * Create and run the address book configuration window.
 *
 * address_book is the address book to configure. If it is 
 * NULL then a new address book is created, the user is prompted
 * for the kind of address book to create.
 *
 * Returns the configured/new mailbox if the user presses Update/Add
 * or NULL if the user cancels.
 * 
 * The address book returned will have been setup.
 *
 */
LibBalsaAddressBook *
balsa_address_book_config_new(LibBalsaAddressBook * address_book)
{
    AddressBookConfig *abc;
    GtkWidget *bbox;
    GtkWidget *button;
    GtkWidget *page;
    gchar *name;
    gint num;

    abc = g_new0(AddressBookConfig, 1);
    abc->address_book = address_book;
    abc->cancelled = TRUE;

    abc->window = gnome_dialog_new(_("Address Book Configuration"), NULL);
    gnome_dialog_set_parent(GNOME_DIALOG(abc->window),
			    GTK_WINDOW(balsa_app.main_window));
    gnome_dialog_close_hides(GNOME_DIALOG(abc->window), TRUE);
    gtk_window_set_wmclass(GTK_WINDOW(abc->window), "address_book_config_dialog", "Balsa");

    abc->notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(abc->window), 5);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(abc->window)->vbox),
		       abc->notebook, TRUE, TRUE, 0);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(abc->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(abc->notebook), FALSE);

    bbox = GNOME_DIALOG(abc->window)->action_area;
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
    gtk_button_box_set_child_size(GTK_BUTTON_BOX(bbox),
				  BALSA_BUTTON_WIDTH / 2,
				  BALSA_BUTTON_HEIGHT / 2);

    if (address_book == NULL) {
	button = gnome_stock_button(GNOME_STOCK_BUTTON_NEXT);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(next_button_cb),
			   (gpointer) abc);
	gtk_container_add(GTK_CONTAINER(bbox), button);
	gtk_widget_show(button);
	abc->continue_button = button;

	page = create_choice_page(abc);

    } else {
	GtkWidget *pixmap;
	pixmap = gnome_stock_pixmap_widget(NULL, GNOME_STOCK_PIXMAP_SAVE);
	button = gnome_pixmap_button(pixmap, _("Update"));
	gtk_container_add(GTK_CONTAINER(bbox), button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   (GtkSignalFunc) update_button_cb,
			   (gpointer) abc);
	gtk_widget_show(button);
	abc->continue_button = button;

	if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book)) {
	    page = create_vcard_page(abc);
#ifdef ENABLE_LDAP
	} else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book)) {
	    page = create_ldap_page(abc);
#endif
	} else {
	    g_assert_not_reached();
	    page = NULL;
	}
    }

    gtk_notebook_append_page(GTK_NOTEBOOK(abc->notebook), page, NULL);

    num = gtk_notebook_page_num(GTK_NOTEBOOK(abc->notebook), page);
    gtk_notebook_set_page(GTK_NOTEBOOK(abc->notebook), num);

    button = gnome_stock_button(GNOME_STOCK_BUTTON_CANCEL);
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(cancel_button_cb), (gpointer) abc);
    gtk_widget_show(button);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    gtk_widget_show_all(abc->notebook);
    if (address_book)
	gtk_widget_grab_focus(abc->name_entry);

    gnome_dialog_run(GNOME_DIALOG(abc->window));

    if (abc->cancelled) {
	gtk_widget_destroy(abc->window);
	g_free(abc);
	return NULL;
    }

    name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));
    if (address_book == NULL) {
	if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
	    gchar *path =
		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
					       (abc->ab_specific.vcard.path), FALSE);
	    if (path != NULL) 
		address_book = libbalsa_address_book_vcard_new(name, path);
#ifdef ENABLE_LDAP
	} else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
	    gchar *host_name =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.host_name));
	    gchar *base_dn =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.base_dn));
	    address_book =
		libbalsa_address_book_ldap_new(name, host_name, base_dn);
#endif
	} else
	    g_assert_not_reached();
	address_book->expand_aliases =
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
					 (abc->expand_aliases_button));
    } else {
	/* We are modifying an existing address book */
	g_free(address_book->name);
	address_book->name =
	    g_strdup(gtk_entry_get_text(GTK_ENTRY(abc->name_entry)));

	if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book)) {
	    LibBalsaAddressBookVcard *vcard;
	    gchar *path =
		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
					       (abc->ab_specific.vcard.path), FALSE);

	    vcard = LIBBALSA_ADDRESS_BOOK_VCARD(address_book);
	    if (path) {
		g_free(vcard->path);
		vcard->path = g_strdup(path);
	    }
#ifdef ENABLE_LDAP
	} else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book)) {
	    LibBalsaAddressBookLdap *ldap;
	    gchar *host_name =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.host_name));
	    gchar *base_dn =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.base_dn));

	    ldap = LIBBALSA_ADDRESS_BOOK_LDAP(address_book);

	    g_free(ldap->host);
	    ldap->host = g_strdup(host_name);
	    g_free(ldap->base_dn);
	    ldap->base_dn = g_strdup(base_dn);
#endif
	} else
	    g_assert_not_reached();

	address_book->expand_aliases =
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
					 (abc->expand_aliases_button));
    }
    gtk_widget_destroy(abc->window);
    g_free(abc);
    return address_book;
}

static GtkWidget *
create_choice_page(AddressBookConfig * abc)
{
    GtkWidget *label;
    GtkWidget *vbox;
    GtkWidget *radio_button;

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_show(vbox);

    label = gtk_label_new(_("New Address Book type:"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    /* radio buttons ... */

    /* ... vCard Address Book */
    radio_button =
	gtk_radio_button_new_with_label(NULL,
					_
					("VCard Address Book (GnomeCard)"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(radio_button), "clicked",
		       GTK_SIGNAL_FUNC(set_the_page), (gpointer) abc);
    gtk_object_set_user_data(GTK_OBJECT(radio_button),
			     GINT_TO_POINTER
			     (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD));
    gtk_widget_show(radio_button);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_button), TRUE);
    abc->create_type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;

    /* ... LDAP. */
    /* The dialog will look weird if LDAP isn't enabled
     * since there will be only one option...
     * I intend to add Pine support though...*/
#if ENABLE_LDAP
    radio_button = gtk_radio_button_new_with_label
	(gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button)),
	 _("LDAP Address Book"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(radio_button), "clicked",
		       GTK_SIGNAL_FUNC(set_the_page), (gpointer) abc);
    gtk_object_set_user_data(GTK_OBJECT(radio_button),
			     GINT_TO_POINTER
			     (LIBBALSA_TYPE_ADDRESS_BOOK_LDAP));
    gtk_widget_show(radio_button);
#else
    label = gtk_label_new(_("Balsa is not compiled with LDAP support"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);
#endif


    return vbox;

}

static GtkWidget *
create_vcard_page(AddressBookConfig * abc)
{
    GtkWidget *table;
    GtkWidget *label;

    table = gtk_table_new(2, 3, FALSE);

    /* mailbox name */

    label = gtk_label_new(_("Address Book Name:"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
		     GTK_FILL, GTK_FILL, 10, 10);

    abc->name_entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), abc->name_entry, 1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    label = gtk_label_new(_("File name"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 10, 10);

    abc->ab_specific.vcard.path =
	gnome_file_entry_new("VCARD ADDRESS BOOK PATH",
			     "Select path for address book");
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.vcard.path, 1, 2,
		     1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    abc->expand_aliases_button =
	gtk_check_button_new_with_label(_("Expand aliases as you type"));
    gtk_table_attach(GTK_TABLE(table), abc->expand_aliases_button, 1, 2, 2,
		     3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    if (abc->address_book) {
	LibBalsaAddressBookVcard *vcard;
	GtkWidget *entry;

	vcard = LIBBALSA_ADDRESS_BOOK_VCARD(abc->address_book);
	entry =
	    GTK_WIDGET(gnome_file_entry_gtk_entry
		       (GNOME_FILE_ENTRY(abc->ab_specific.vcard.path)));

	gtk_entry_set_text(GTK_ENTRY(abc->name_entry),
			   abc->address_book->name);
	gtk_entry_set_text(GTK_ENTRY(entry), vcard->path);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				     (abc->expand_aliases_button),
				     abc->address_book->expand_aliases);
    }

    gtk_widget_show_all(table);
    return table;

}

#ifdef ENABLE_LDAP
static GtkWidget *
create_ldap_page(AddressBookConfig * abc)
{
    GtkWidget *table;
    GtkWidget *label;

    table = gtk_table_new(2, 3, FALSE);

    /* mailbox name */

    label = gtk_label_new(_("Address Book Name:"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
		     GTK_FILL, GTK_FILL, 10, 10);
    gtk_widget_show(label);

    abc->name_entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), abc->name_entry, 1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_widget_show(abc->name_entry);

    label = gtk_label_new(_("Host name"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 10, 10);
    gtk_widget_show(label);

    abc->ab_specific.ldap.host_name = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.ldap.host_name, 1,
		     2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_widget_show(abc->ab_specific.ldap.host_name);

    label = gtk_label_new(_("Base Domain Name"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
		     GTK_FILL, GTK_FILL, 10, 10);
    gtk_widget_show(label);

    abc->ab_specific.ldap.base_dn = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.ldap.base_dn, 1, 2,
		     2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_widget_show(abc->ab_specific.ldap.base_dn);

    abc->expand_aliases_button =
	gtk_check_button_new_with_label(_("Expand aliases as you type"));
    gtk_table_attach(GTK_TABLE(table), abc->expand_aliases_button, 1, 2, 3,
		     4, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    if (abc->address_book) {
	LibBalsaAddressBookLdap *ldap;
	ldap = LIBBALSA_ADDRESS_BOOK_LDAP(abc->address_book);

	gtk_entry_set_text(GTK_ENTRY(abc->name_entry),
			   abc->address_book->name);
	gtk_entry_set_text(GTK_ENTRY(abc->ab_specific.ldap.host_name),
			   ldap->host);
	gtk_entry_set_text(GTK_ENTRY(abc->ab_specific.ldap.base_dn),
			   ldap->base_dn);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				     (abc->expand_aliases_button),
				     abc->address_book->expand_aliases);
    }

    gtk_widget_show(table);
    return table;
}
#endif

static void
set_the_page(GtkWidget * button, AddressBookConfig * abc)
{
    /* This checks if we are being activated or deactivated 
     * if we are being activiated, set the type field
     */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
	GtkType type =
	    GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(button)));
	abc->create_type = type;
    }
}

static void
cancel_button_cb(GtkWidget * button, AddressBookConfig * abc)
{
    abc->cancelled = TRUE;
    gnome_dialog_close(GNOME_DIALOG(abc->window));
}


static void
next_button_cb(GtkWidget * button, AddressBookConfig * abc)
{
    GtkWidget *pixmap, *bbox, *page;
    gint num;

    bbox = GNOME_DIALOG(abc->window)->action_area;

    gtk_widget_destroy(abc->continue_button);
    pixmap = gnome_stock_pixmap_widget(NULL, GNOME_STOCK_PIXMAP_NEW);
    abc->continue_button = gnome_pixmap_button(pixmap, _("Add"));
    gtk_signal_connect(GTK_OBJECT(abc->continue_button), "clicked",
		       GTK_SIGNAL_FUNC(add_button_cb), (gpointer) abc);

    gtk_container_add(GTK_CONTAINER(bbox), abc->continue_button);
    gtk_box_reorder_child(GTK_BOX(bbox), abc->continue_button, 0);

    gtk_widget_show(abc->continue_button);

    if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD)
	page = create_vcard_page(abc);
#ifdef ENABLE_LDAP
    else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP)
	page = create_ldap_page(abc);
#endif
    else {
	g_assert_not_reached(); page = NULL;
    }

    gtk_notebook_append_page(GTK_NOTEBOOK(abc->notebook), page, NULL);

    num = gtk_notebook_page_num(GTK_NOTEBOOK(abc->notebook), page);
    gtk_notebook_set_page(GTK_NOTEBOOK(abc->notebook), num);
    gtk_widget_grab_focus(abc->name_entry);
}

/* handle_close:
   handle the request to add/update the address book data.
   NOTE: create_type cannot be made the switch select expression.
*/
static void
handle_close(AddressBookConfig * abc)
{
    GtkWidget *ask;
    gint clicked_button;

    abc->cancelled = FALSE;

    if( (abc->address_book && 
	 LIBBALSA_IS_ADDRESS_BOOK_VCARD(abc->address_book)) ||
	(!abc->address_book&&
	 abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD)) {
	gchar *path =
	    gnome_file_entry_get_full_path(
		GNOME_FILE_ENTRY(abc->ab_specific.vcard.path), FALSE);
	
	if(!path) {
	    gchar *msg = g_strdup_printf(
		_("The address book file path '%s' is not correct.\n"
		  "Do you want to correct the file name?"), 
		gtk_entry_get_text(GTK_ENTRY(
		    gnome_file_entry_gtk_entry(
			GNOME_FILE_ENTRY(abc->ab_specific.vcard.path)))));
	    ask = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_QUESTION,
					GNOME_STOCK_BUTTON_OK,
					GNOME_STOCK_BUTTON_CANCEL, NULL);
	    g_free(msg);
	    gnome_dialog_set_default(GNOME_DIALOG(ask), 1);
	    gnome_dialog_set_parent(GNOME_DIALOG(ask),GTK_WINDOW(abc->window));
	    clicked_button = gnome_dialog_run_and_close(GNOME_DIALOG(ask));
	    if(clicked_button == 0) return;
	    else abc->cancelled = TRUE;
	} else g_free(path);
    }

    gnome_dialog_close(GNOME_DIALOG(abc->window));
}
static void
add_button_cb(GtkWidget * button, AddressBookConfig * abc)
{
    handle_close(abc);
}

static void
update_button_cb(GtkWidget * button, AddressBookConfig * abc)
{
    handle_close(abc);
}
