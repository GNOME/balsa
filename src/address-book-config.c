/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

    gchar* link_id;
    GtkType create_type;

    union {
	struct {
	    GtkWidget *path;
	} vcard;
	struct {
		GtkWidget *load;
		GtkWidget *save;
	} externq;
	struct {
	    GtkWidget *path;
	} ldif;
	
#ifdef ENABLE_LDAP
	struct {
	    GtkWidget *host_name;
	    GtkWidget *base_dn;
	} ldap;
#endif
    } ab_specific;

    LibBalsaAddressBook *address_book;
};
enum AddressBookConfigResponse {
    ABC_RESPONSE_FORWARD,
    ABC_RESPONSE_ADD,
    ABC_RESPONSE_UPDATE
};

static GtkWidget *create_choice_page(AddressBookConfig * abc);
static GtkWidget *create_vcard_page(AddressBookConfig * abc);
static GtkWidget *create_externq_page(AddressBookConfig * abc);
static GtkWidget *create_ldif_page(AddressBookConfig * abc);
#ifdef ENABLE_LDAP
static GtkWidget *create_ldap_page(AddressBookConfig * abc);
#endif

static void abc_response_cb(GtkDialog* d, gint respo, AddressBookConfig * abc);
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
balsa_address_book_config_new(LibBalsaAddressBook * address_book,
                              GtkWindow* parent)
{
    AddressBookConfig *abc;
    GtkWidget *bbox;
    GtkWidget *page;
    const gchar *name;
    gint num, response;

    abc = g_new0(AddressBookConfig, 1);
    abc->address_book = address_book;
    abc->cancelled = TRUE;

    abc->window = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(abc->window),
                         _("Address Book Configuration")); 
    gtk_window_set_transient_for(GTK_WINDOW(abc->window), parent);
    gtk_window_set_wmclass(GTK_WINDOW(abc->window), 
			   "address_book_config_dialog", "Balsa");

    g_signal_connect(G_OBJECT(abc->window), "response", 
                     G_CALLBACK(abc_response_cb), abc);

    abc->notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(abc->window), 5);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(abc->window)->vbox),
		       abc->notebook, TRUE, TRUE, 0);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(abc->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(abc->notebook), FALSE);

    bbox = GTK_DIALOG(abc->window)->action_area;
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
    gtk_box_set_spacing(GTK_BOX(bbox), 5);

    if (address_book == NULL) {
	abc->continue_button = 
	    gtk_dialog_add_button(GTK_DIALOG(abc->window),
                                  GTK_STOCK_GO_FORWARD, ABC_RESPONSE_FORWARD);
	page = create_choice_page(abc);
    } else {
	abc->continue_button = 
	    gtk_dialog_add_button(GTK_DIALOG(abc->window), _("Update"),
                                  ABC_RESPONSE_UPDATE);

	if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book)) {
	    page = create_vcard_page(abc);
	}else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERN(address_book)) {
		page = create_externq_page(abc);
	}else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book)) {
	    page = create_ldif_page(abc);
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
    gtk_notebook_set_current_page(GTK_NOTEBOOK(abc->notebook), num);

    gtk_dialog_add_buttons(GTK_DIALOG(abc->window),
                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                           GTK_STOCK_HELP,   GTK_RESPONSE_HELP,
                           NULL);

    gtk_widget_show_all(abc->notebook);
    if (address_book)
	gtk_widget_grab_focus(abc->name_entry);

    while( (response=gtk_dialog_run(GTK_DIALOG(abc->window)))
           == GTK_RESPONSE_HELP || response == ABC_RESPONSE_FORWARD)
        ;
    if(response == GTK_RESPONSE_CANCEL) {
	gtk_widget_destroy(abc->window);
	g_free(abc);
	return NULL;
    }

    name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));
    if (address_book == NULL) {
	if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
	    gchar *path =
		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
					       (abc->ab_specific.vcard.path),
					       FALSE);
	    if (path != NULL) 
		address_book = libbalsa_address_book_vcard_new(name, path);
            g_free(path);
 	} else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
	    gchar *load =
 		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
 					       (abc->ab_specific.externq.load),
					       FALSE);
		gchar *save = 
 		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
 					       (abc->ab_specific.externq.save),
					       FALSE);
 	    if (load != NULL && save != NULL) 
 		address_book = libbalsa_address_book_externq_new(name, load, save);
	} else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
	    gchar *path =
		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
					       (abc->ab_specific.ldif.path), FALSE);
	    if (path != NULL)
		address_book = libbalsa_address_book_ldif_new(name, path);
            g_free(path);
#ifdef ENABLE_LDAP
	} else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
	    const gchar *host_name =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.host_name));
	    const gchar *base_dn =
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
    } else { /* We are modifying an existing address book */
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
		vcard->path = path;
	    }
 	} else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERN(address_book)) {
 	    LibBalsaAddressBookExtern *externq;
 	    gchar *load =
 		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
 					       (abc->ab_specific.externq.load),
					       FALSE);
 	    gchar *save =
 		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
 					       (abc->ab_specific.externq.save),
					       FALSE);
 
 	    externq = LIBBALSA_ADDRESS_BOOK_EXTERN(address_book);
 	    if (load) {
 		g_free(externq->load);
 		externq->load = load;;
	    }
 	    if (save) {
 		g_free(externq->save);
 		externq->save = save;
	    }
	} else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book)) {
	    LibBalsaAddressBookLdif *ldif;
	    gchar *path =
		gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
					       (abc->ab_specific.ldif.path), 
					       FALSE);

	    ldif = LIBBALSA_ADDRESS_BOOK_LDIF(address_book);
	    if (path) {
		g_free(ldif->path);
		ldif->path = path;
	    }
#ifdef ENABLE_LDAP
	} else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book)) {
	    LibBalsaAddressBookLdap *ldap;
	    const gchar *host_name =
		gtk_entry_get_text(GTK_ENTRY
				   (abc->ab_specific.ldap.host_name));
	    const gchar *base_dn =
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

    abc->link_id = "CHOICE";

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_show(vbox);

    label = gtk_label_new(_("New Address Book type:"));
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    /* radio buttons ... */

    /* ... vCard Address Book */
    radio_button =
	gtk_radio_button_new_with_label(NULL,
					_("VCard Address Book (GnomeCard)"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio_button), "clicked",
		     G_CALLBACK(set_the_page), (gpointer) abc);
    g_object_set_data(G_OBJECT(radio_button), "user_data",
                      GINT_TO_POINTER(LIBBALSA_TYPE_ADDRESS_BOOK_VCARD));
    gtk_widget_show(radio_button);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_button), TRUE);
    abc->create_type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;

	/* ... External query Address Book */
    radio_button = gtk_radio_button_new_with_label
	(gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_button)),
	 _("External query (a program)"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio_button), "clicked",
		     G_CALLBACK(set_the_page), (gpointer) abc);
    g_object_set_data(G_OBJECT(radio_button), "user_data",
                      GINT_TO_POINTER(LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN));
    gtk_widget_show(radio_button);


    /* ... LDIF Address Book */
    radio_button = gtk_radio_button_new_with_label
	(gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_button)),
	 _("LDIF Address Book"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio_button), "clicked",
		     G_CALLBACK(set_the_page), (gpointer) abc);
    g_object_set_data(G_OBJECT(radio_button), "user_data",
                      GINT_TO_POINTER(LIBBALSA_TYPE_ADDRESS_BOOK_LDIF));
    gtk_widget_show(radio_button);

    /* ... LDAP. */
    /* The dialog will look weird if LDAP isn't enabled
     * since there will be only one option...
     * I intend to add Pine support though...*/
#if ENABLE_LDAP
    radio_button = gtk_radio_button_new_with_label
	(gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button)),
	 _("LDAP Address Book"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio_button), "clicked",
		     G_CALLBACK(set_the_page), (gpointer) abc);
    g_object_set_data(G_OBJECT(radio_button), "user_data",
                      GINT_TO_POINTER(LIBBALSA_TYPE_ADDRESS_BOOK_LDAP));
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
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget *label;
    LibBalsaAddressBookVcard* ab;
    ab = (LibBalsaAddressBookVcard*)abc->address_book; /* may be NULL */

    abc->link_id = "VCARD";
    table = gtk_table_new(2, 3, FALSE);

    /* mailbox name */

    label = create_label(_("_Address Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new(_("File name"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 10, 10);

    abc->ab_specific.vcard.path =
	gnome_file_entry_new("VcardAddressBookPath",
			     _("Select path for VCARD address book"));
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.vcard.path, 1, 2,
		     1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? abc->address_book->expand_aliases : TRUE);

    if (ab) {
	GtkWidget *entry;
	entry = GTK_WIDGET(gnome_file_entry_gtk_entry
			   (GNOME_FILE_ENTRY(abc->ab_specific.vcard.path)));
	gtk_entry_set_text(GTK_ENTRY(entry), ab->path);
    }
    gtk_widget_show_all(table);
    return table;

}

static GtkWidget *
create_externq_page(AddressBookConfig * abc)
{
    GtkWidget *table;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget *label;
    LibBalsaAddressBookExtern* ab;

    ab = (LibBalsaAddressBookExtern*)abc->address_book; /* may be NULL */
    abc->link_id = "EXTERN";
    table = gtk_table_new(3, 3, FALSE);

    /* mailbox name */

    label = create_label(_("_Address Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new(_("Load program location"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 10, 10);

    abc->ab_specific.externq.load =
	gnome_file_entry_new("ExternAddressBookLoadPath",
			     _("Select load program for address book"));
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.externq.load, 1, 2,
		     1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  abc->ab_specific.externq.load);

    label = gtk_label_new(_("Save program location"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
		     GTK_FILL, GTK_FILL, 10, 10);
    abc->ab_specific.externq.save =
	gnome_file_entry_new("ExternAddressBookSavePath",
			     _("Select save program for address book"));
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.externq.save, 1, 2,
		     2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  abc->ab_specific.externq.save);
    
    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? abc->address_book->expand_aliases : TRUE);

    if (ab) {
	GtkWidget *entry;
	entry = GTK_WIDGET(gnome_file_entry_gtk_entry
			   (GNOME_FILE_ENTRY(abc->ab_specific.externq.load)));
	gtk_entry_set_text(GTK_ENTRY(entry), ab->load);

	entry = GTK_WIDGET(gnome_file_entry_gtk_entry
			   (GNOME_FILE_ENTRY(abc->ab_specific.externq.save)));
	gtk_entry_set_text(GTK_ENTRY(entry), ab->save);
    }
    gtk_widget_show_all(table);
    return table;

}


static GtkWidget *
create_ldif_page(AddressBookConfig * abc)
{
    GtkWidget *table;
    GtkWidget *label;
    GtkDialog* mcw = GTK_DIALOG(abc->window);

    abc->link_id = "LDIF";
    table = gtk_table_new(2, 3, FALSE);

    /* mailbox name */

    label = create_label(_("_Address Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   abc->address_book 
				   ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new_with_mnemonic(_("_File name"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 10, 10);

    abc->ab_specific.ldif.path =
	gnome_file_entry_new("LdifAddressBookPath",
			     _("Select path for LDIF address book"));
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.ldif.path, 1, 2,
		     1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  abc->ab_specific.ldif.path);
    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 2,
		     abc->address_book ? 
		     abc->address_book->expand_aliases : TRUE);

    if (abc->address_book) {
	LibBalsaAddressBookLdif *ldif;
	GtkWidget *entry;

	ldif = LIBBALSA_ADDRESS_BOOK_LDIF(abc->address_book);
	entry =
	    GTK_WIDGET(gnome_file_entry_gtk_entry
		       (GNOME_FILE_ENTRY(abc->ab_specific.ldif.path)));

	gtk_entry_set_text(GTK_ENTRY(abc->name_entry),
			   abc->address_book->name);
	gtk_entry_set_text(GTK_ENTRY(entry), ldif->path);
    }

    gtk_widget_show_all(table);
    return table;

}

#ifdef ENABLE_LDAP
static GtkWidget *
create_ldap_page(AddressBookConfig * abc)
{
    GtkWidget *table = gtk_table_new(2, 3, FALSE);

    LibBalsaAddressBookLdap* ab;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget* label;
    gchar *host = libbalsa_guess_ldap_server();
    gchar *base = libbalsa_guess_ldap_base();
    gchar *name = libbalsa_guess_ldap_name();

    ab = (LibBalsaAddressBookLdap*)abc->address_book; /* may be NULL */

    abc->link_id = "LDAP";
    /* mailbox name */

    label = create_label(_("_Address Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name : name, 
				   label);

    label = create_label(_("_Host Name"), table, 1);
    abc->ab_specific.ldap.host_name = 
	create_entry(mcw, table, NULL, NULL, 1, 
		     ab ? ab->host : host, label);

    label = create_label(_("_Base Domain Name"), table, 2);
    abc->ab_specific.ldap.base_dn = 
	create_entry(mcw, table, NULL, NULL, 2, 
		     ab ? ab->base_dn : base, label);

    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? abc->address_book->expand_aliases : TRUE);

    gtk_widget_show(table);

    g_free(base);
    g_free(name);
    g_free(host);
    
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
            GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                              "user_data"));
	abc->create_type = type;
    }
}


static void
help_button_cb(AddressBookConfig * abc)
{
#if BALSA_MAJOR < 2
    static GnomeHelpMenuEntry help_entry = { NULL, NULL };
    help_entry.name = gnome_app_id;
    help_entry.path = g_strconcat("ab-conf.html#", abc->link_id, NULL);
    gnome_help_display(NULL, &help_entry);
    g_free(help_entry.path);
#else
    GError *err = NULL;

    gnome_help_display("ab-conf.html", abc->link_id, &err);

    if (err) {
        g_print(_("Error displaying %s: %s\n"), abc->link_id,
                err->message);
        g_error_free(err);
    }
#endif                          /* BALSA_MAJOR < 2 */
}

static void
next_button_cb(AddressBookConfig * abc)
{
    GtkWidget *bbox, *page;
    gint num;

    bbox = GTK_DIALOG(abc->window)->action_area;

    gtk_widget_destroy(abc->continue_button);
    abc->continue_button =
        gtk_dialog_add_button(GTK_DIALOG(abc->window),
                              _("Add"), ABC_RESPONSE_ADD);
    gtk_box_reorder_child(GTK_BOX(bbox), abc->continue_button, 0);

    if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD)
	page = create_vcard_page(abc);
	else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN)
	page = create_externq_page(abc);
    else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF)
	page = create_ldif_page(abc);
#ifdef ENABLE_LDAP
    else if (abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP)
	page = create_ldap_page(abc);
#endif
    else {
	g_assert_not_reached(); page = NULL;
    }

    gtk_notebook_append_page(GTK_NOTEBOOK(abc->notebook), page, NULL);

    num = gtk_notebook_page_num(GTK_NOTEBOOK(abc->notebook), page);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(abc->notebook), num);
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
	    ask = gtk_message_dialog_new(GTK_WINDOW(abc->window),
                                         GTK_DIALOG_MODAL,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_YES_NO, 
                                         msg);
	    g_free(msg);
	    gtk_dialog_set_default_response(GTK_DIALOG(ask), GTK_RESPONSE_YES);
	    clicked_button = gtk_dialog_run(GTK_DIALOG(ask));
            gtk_widget_destroy(ask);
	    if(clicked_button == 0) return;
	    else abc->cancelled = TRUE;
	} else g_free(path);
    }
    else if( (abc->address_book &&
	 LIBBALSA_IS_ADDRESS_BOOK_LDIF(abc->address_book)) ||
	(!abc->address_book&&
	 abc->create_type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF)) {
	gchar *path =
	    gnome_file_entry_get_full_path(
		GNOME_FILE_ENTRY(abc->ab_specific.ldif.path), FALSE);

	if(!path) {
	    gchar *msg = g_strdup_printf(
		_("The address book file path '%s' is not correct.\n"
		  "Do you want to correct the file name?"),
		gtk_entry_get_text(GTK_ENTRY(
		    gnome_file_entry_gtk_entry(
			GNOME_FILE_ENTRY(abc->ab_specific.ldif.path)))));
	    ask = gtk_message_dialog_new(GTK_WINDOW(abc->window),
                                         GTK_DIALOG_MODAL, 
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_OK_CANCEL, msg);
	    g_free(msg);
	    gtk_dialog_set_default_response(GTK_DIALOG(ask), 
                                            GTK_RESPONSE_CANCEL);
	    clicked_button = gtk_dialog_run(GTK_DIALOG(ask));
            gtk_widget_destroy(ask);
	    if(clicked_button == GTK_RESPONSE_OK) return;
	    else abc->cancelled = TRUE;
	} else g_free(path);
    }
}

static void
abc_response_cb(GtkDialog* d, gint respo, AddressBookConfig * abc)
{
    switch(respo) {
    case ABC_RESPONSE_FORWARD: next_button_cb(abc);   break;
    case ABC_RESPONSE_ADD:
    case ABC_RESPONSE_UPDATE:  handle_close(abc);     break;
    case GTK_RESPONSE_HELP:    help_button_cb(abc);   break;
    case GTK_RESPONSE_CANCEL:  /* NO-OP */ break;
    }
}

