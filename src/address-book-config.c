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

#include "config.h"

#include <gnome.h>

#include "balsa-app.h"
#include "address-book-config.h"
#include "address-book-gpe.h"

typedef struct _AddressBookConfig AddressBookConfig;
struct _AddressBookConfig {
    GtkWidget *window;
    GtkWidget *notebook;

    /* This button is the next/ok/update button */
    GtkWidget *continue_button;

    GtkWidget *name_entry;
    GtkWidget *expand_aliases_button;

    gchar* link_id;
    GType type;

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
	    GtkWidget *bind_dn;
	    GtkWidget *passwd;
	    GtkWidget *enable_tls;
	} ldap;
#endif
    } ab_specific;

    LibBalsaAddressBook *address_book;
    void (*callback) (LibBalsaAddressBook * address_book,
                      gboolean append);
};
enum AddressBookConfigResponse {
    ABC_RESPONSE_FORWARD,
    ABC_RESPONSE_ADD,
    ABC_RESPONSE_UPDATE
};

static GtkWidget *create_choice_page(AddressBookConfig * abc);
static GtkWidget *create_page_from_type(AddressBookConfig * abc);
static GtkWidget *create_vcard_page(AddressBookConfig * abc);
static GtkWidget *create_externq_page(AddressBookConfig * abc);
static GtkWidget *create_ldif_page(AddressBookConfig * abc);
#ifdef ENABLE_LDAP
static GtkWidget *create_ldap_page(AddressBookConfig * abc);
#endif
#ifdef HAVE_SQLITE
static GtkWidget *create_gpe_page(AddressBookConfig * abc);
#endif
static void set_the_page(GtkWidget * button, AddressBookConfig * abc);

static void abc_response_cb(GtkDialog* d, gint respo, AddressBookConfig * abc);
static void help_button_cb(AddressBookConfig * abc);
static void next_button_cb(AddressBookConfig * abc);
static gboolean handle_close(AddressBookConfig * abc);
static gboolean bad_path(GnomeFileEntry * entry, GtkWindow * window,
                         gint type);
static void create_book(AddressBookConfig * abc);
static void modify_book(AddressBookConfig * abc);

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
void
balsa_address_book_config_new(LibBalsaAddressBook * address_book,
                              void (*callback) (LibBalsaAddressBook *
                                                address_book,
                                                gboolean append),
                              GtkWindow* parent)
{
    AddressBookConfig *abc;
    GtkWidget *bbox;
    GtkWidget *page;
    gint num;

    abc = g_new0(AddressBookConfig, 1);
    abc->address_book = address_book;

    abc->window =
        gtk_dialog_new_with_buttons("", parent, /* must NOT be modal */
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_HELP,   GTK_RESPONSE_HELP,
                                    NULL);
    gtk_window_set_wmclass(GTK_WINDOW(abc->window), 
			   "address_book_config_dialog", "Balsa");

    g_signal_connect(G_OBJECT(abc->window), "response", 
                     G_CALLBACK(abc_response_cb), abc);

    abc->notebook = gtk_notebook_new();
    abc->callback = callback;
    gtk_container_set_border_width(GTK_CONTAINER(abc->window), 5);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(abc->window)->vbox),
		       abc->notebook, TRUE, TRUE, 0);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(abc->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(abc->notebook), FALSE);

    bbox = GTK_DIALOG(abc->window)->action_area;
    if (address_book == NULL) {
        gtk_window_set_title(GTK_WINDOW(abc->window),
                             _("Add Address Book"));
	abc->continue_button = 
	    gtk_dialog_add_button(GTK_DIALOG(abc->window),
                                  GTK_STOCK_GO_FORWARD,
                                  ABC_RESPONSE_FORWARD);
	page = create_choice_page(abc);
    } else {
        gtk_window_set_title(GTK_WINDOW(abc->window),
                             _("Modify Address Book"));
	abc->continue_button = 
	    gtk_dialog_add_button(GTK_DIALOG(abc->window),
                                  GTK_STOCK_APPLY,
                                  ABC_RESPONSE_UPDATE);

        abc->type = G_TYPE_FROM_INSTANCE(address_book);
        page = create_page_from_type(abc);
    }
    gtk_box_reorder_child(GTK_BOX(bbox), abc->continue_button, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(abc->notebook), page, NULL);

    num = gtk_notebook_page_num(GTK_NOTEBOOK(abc->notebook), page);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(abc->notebook), num);

    gtk_widget_show_all(abc->notebook);
    if (address_book)
	gtk_widget_grab_focus(abc->name_entry);

    gtk_widget_show_all(GTK_WIDGET(abc->window));
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
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;

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
	(gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_button)),
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
#ifdef HAVE_SQLITE
    radio_button = gtk_radio_button_new_with_label
	(gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_button)),
	 _("GPE Address Book"));
    gtk_box_pack_start(GTK_BOX(vbox), radio_button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio_button), "clicked",
		     G_CALLBACK(set_the_page), (gpointer) abc);
    g_object_set_data(G_OBJECT(radio_button), "user_data",
                      GINT_TO_POINTER(LIBBALSA_TYPE_ADDRESS_BOOK_GPE));
    gtk_widget_show(radio_button);
#else
    /* Should we inform about missing SQLite support? */
#endif


    return vbox;

}

static GtkWidget *
create_page_from_type(AddressBookConfig * abc)
{
    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        return create_vcard_page(abc);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        return create_externq_page(abc);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        return create_ldif_page(abc);
#ifdef ENABLE_LDAP
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
        return create_ldap_page(abc);
#endif
#ifdef HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
        return create_gpe_page(abc);
#endif
    } else {
        g_assert_not_reached();
    }

    return NULL;
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

    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new_with_mnemonic(_("_File Name"));
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

    label = create_label(_("A_ddress Book Name"), table, 0);
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

    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   abc->address_book 
				   ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new_with_mnemonic(_("_File Name"));
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
    GtkWidget *table = gtk_table_new(2, 6, FALSE);

    LibBalsaAddressBookLdap* ab;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget* label;
    gchar *host = libbalsa_guess_ldap_server();
    gchar *base = libbalsa_guess_ldap_base();
    gchar *name = libbalsa_guess_ldap_name();

    ab = (LibBalsaAddressBookLdap*)abc->address_book; /* may be NULL */

    abc->link_id = "LDAP";
    /* mailbox name */

    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name : name, 
				   label);

    label = create_label(_("_Host Name"), table, 1);
    abc->ab_specific.ldap.host_name = 
	create_entry(mcw, table, NULL, NULL, 1, 
		     ab ? ab->host : host, label);

    label = create_label(_("Base Domain _Name"), table, 2);
    abc->ab_specific.ldap.base_dn = 
	create_entry(mcw, table, NULL, NULL, 2, 
		     ab ? ab->base_dn : base, label);

    label = create_label(_("_User Name (Bind DN)"), table, 3);
    abc->ab_specific.ldap.bind_dn = 
	create_entry(mcw, table, NULL, NULL, 3, 
		     ab ? ab->bind_dn : "", label);

    label = create_label(_("_Password"), table, 4);
    abc->ab_specific.ldap.passwd = 
	create_entry(mcw, table, NULL, NULL, 4, 
		     ab ? ab->passwd : "", label);
    gtk_entry_set_visibility(GTK_ENTRY(abc->ab_specific.ldap.passwd), FALSE);

    abc->ab_specific.ldap.enable_tls =
	create_check(mcw, _("Enable _TLS"), table, 5,
		     ab ? ab->enable_tls : FALSE);

    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 6,
		     ab ? abc->address_book->expand_aliases : TRUE);

    gtk_widget_show(table);

    g_free(base);
    g_free(name);
    g_free(host);
    
    return table;
}
#endif

#ifdef HAVE_SQLITE
static GtkWidget *
create_gpe_page(AddressBookConfig * abc)
{
    GtkWidget *table = gtk_table_new(2, 6, FALSE);

    LibBalsaAddressBookLdap* ab;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget* label;

    ab = (LibBalsaAddressBookLdap*)abc->address_book; /* may be NULL */

    abc->link_id = "GPE";
    /* mailbox name */

    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? abc->address_book->name 
                                   : _("GPE Address Book"), 
				   label);
    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? abc->address_book->expand_aliases : TRUE);
    gtk_widget_show_all(table);

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
	abc->type = type;
    }
}

static void
abc_response_cb(GtkDialog * d, gint response, AddressBookConfig * abc)
{
    switch (response) {
    case GTK_RESPONSE_HELP:
        help_button_cb(abc);
        return;
    case ABC_RESPONSE_FORWARD:
        next_button_cb(abc);
        return;
    case ABC_RESPONSE_ADD:
    case ABC_RESPONSE_UPDATE:
        if (handle_close(abc))
            break;
        else
            return;
    default:
        break;
    }

    gtk_widget_destroy(abc->window);
    g_free(abc);
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
	balsa_information(LIBBALSA_INFORMATION_WARNING,
		_("Error displaying %s: %s\n"), abc->link_id,
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
                              _("_Add"), ABC_RESPONSE_ADD);
    gtk_box_reorder_child(GTK_BOX(bbox), abc->continue_button, 0);

    page = create_page_from_type(abc);

    gtk_notebook_append_page(GTK_NOTEBOOK(abc->notebook), page, NULL);

    num = gtk_notebook_page_num(GTK_NOTEBOOK(abc->notebook), page);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(abc->notebook), num);
    gtk_widget_grab_focus(abc->name_entry);
}

enum {
    ADDRESS_BOOK_CONFIG_PATH_FILE,
    ADDRESS_BOOK_CONFIG_PATH_LOAD,
    ADDRESS_BOOK_CONFIG_PATH_SAVE
};

/* handle_close:
   handle the request to add/update the address book data.
   NOTE: type cannot be made the switch select expression.

 * returns:     TRUE    if the close was successful, and it's OK to quit
 *              FALSE   if a bad path was detected and the user wants to
 *                      correct it.
 */
static gboolean
handle_close(AddressBookConfig * abc)
{
    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        if (bad_path(GNOME_FILE_ENTRY(abc->ab_specific.vcard.path),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_FILE))
            return FALSE;
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        if (bad_path(GNOME_FILE_ENTRY(abc->ab_specific.ldif.path),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_FILE))
            return FALSE;
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        if (bad_path(GNOME_FILE_ENTRY(abc->ab_specific.externq.load),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_LOAD))
            return FALSE;
        if (bad_path(GNOME_FILE_ENTRY(abc->ab_specific.externq.save),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_SAVE))
            return FALSE;
    }

    if (abc->address_book == NULL)
        create_book(abc);
    else
        modify_book(abc);

    return TRUE;
}

/* bad_path:
 *
 * Returns TRUE if the path is bad and the user wants to correct it
 */
static gboolean
bad_path(GnomeFileEntry * entry, GtkWindow * window, gint type)
{
    const gchar *name;
    gchar *message, *question;
    GtkWidget *ask;
    gint clicked_button;
    gchar *path = gnome_file_entry_get_full_path(entry, TRUE);

    if (path) {
        g_free(path);
        return FALSE;
    }

    switch (type) {
        case ADDRESS_BOOK_CONFIG_PATH_FILE:
            message =
                _("The address book file path \"%s\" is not correct. %s");
            break;
        case ADDRESS_BOOK_CONFIG_PATH_LOAD:
            message = _("The load program path \"%s\" is not correct. %s");
            break;
        case ADDRESS_BOOK_CONFIG_PATH_SAVE:
            message = _("The save program path \"%s\" is not correct. %s");
            break;
        default:
            message = _("The path \"%s\" is not correct. %s");
            break;
    }
    question = _("Do you want to correct the path?");
    name = gtk_entry_get_text(GTK_ENTRY(gnome_file_entry_gtk_entry(entry)));
    ask = gtk_message_dialog_new(window,
				 GTK_DIALOG_MODAL|
				 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                 message, name, question);
    gtk_dialog_set_default_response(GTK_DIALOG(ask), GTK_RESPONSE_YES);
    clicked_button = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);
    return clicked_button == GTK_RESPONSE_YES;
}

static void
create_book(AddressBookConfig * abc)
{
    LibBalsaAddressBook *address_book = NULL;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));

    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        gchar *path =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.vcard.path),
                                           FALSE);
        if (path != NULL)
            address_book = libbalsa_address_book_vcard_new(name, path);
        g_free(path);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        gchar *load =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.load),
                                           FALSE);
        gchar *save =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.save),
                                           FALSE);
        if (load != NULL && save != NULL)
            address_book =
                libbalsa_address_book_externq_new(name, load, save);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        gchar *path =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.ldif.
                                            path), FALSE);
        if (path != NULL)
            address_book = libbalsa_address_book_ldif_new(name, path);
        g_free(path);
#ifdef ENABLE_LDAP
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
        const gchar *host_name =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.host_name));
        const gchar *base_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.base_dn));
        const gchar *bind_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.bind_dn));
        const gchar *passwd =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.passwd));
        gboolean enable_tls =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (abc->ab_specific.ldap.enable_tls));
        address_book =
            libbalsa_address_book_ldap_new(name, host_name, base_dn,
                                           bind_dn, passwd, enable_tls);
#endif
#ifdef HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
        address_book =
            libbalsa_address_book_gpe_new(name);
#endif
    } else
        g_assert_not_reached();
    if (address_book) {
        address_book->expand_aliases =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (abc->expand_aliases_button));
        abc->callback(address_book, TRUE);
    }
}

static void
modify_book(AddressBookConfig * abc)
{
    LibBalsaAddressBook *address_book = abc->address_book;

    g_free(address_book->name);
    address_book->name =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(abc->name_entry)));

    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        LibBalsaAddressBookVcard *vcard;
        gchar *path =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.vcard.
                                            path), FALSE);

        vcard = LIBBALSA_ADDRESS_BOOK_VCARD(address_book);
        if (path) {
            g_free(vcard->path);
            vcard->path = path;
        }
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
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
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
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
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
        LibBalsaAddressBookLdap *ldap;
        const gchar *host_name =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.host_name));
        const gchar *base_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.base_dn));
        const gchar *bind_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.bind_dn));
        const gchar *passwd =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.passwd));
        gboolean enable_tls =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (abc->ab_specific.ldap.enable_tls));

        ldap = LIBBALSA_ADDRESS_BOOK_LDAP(address_book);

        g_free(ldap->host);     ldap->host = g_strdup(host_name);
        g_free(ldap->base_dn);  ldap->base_dn = g_strdup(base_dn);
        g_free(ldap->bind_dn);  ldap->bind_dn = g_strdup(bind_dn);
        g_free(ldap->passwd);   ldap->passwd  = g_strdup(passwd);
        ldap->enable_tls = enable_tls;
        libbalsa_address_book_ldap_close_connection(ldap);
#endif
    } else
        g_assert_not_reached();

    address_book->expand_aliases =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (abc->expand_aliases_button));
    abc->callback(address_book, FALSE);
}
