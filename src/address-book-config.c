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

#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(2, 6, 0)
#undef GTK_DISABLE_DEPRECATED
#include <gnome.h>
#endif

#include "balsa-app.h"
#include "address-book-config.h"
#include "address-book-gpe.h"
#include "i18n.h"

typedef struct _AddressBookConfig AddressBookConfig;
struct _AddressBookConfig {
    GtkWidget *window;

    GtkWidget *name_entry;
    GtkWidget *expand_aliases_button;

    GType type;

    union {
	struct {
		GtkWidget *load;
		GtkWidget *save;
	} externq;
	
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
    BalsaAddressBookCallback *callback;
    GtkWindow* parent;
};

static GtkWidget *create_dialog_from_type(AddressBookConfig * abc);
static GtkWidget *create_local_dialog(AddressBookConfig * abc);
static GtkWidget *create_externq_dialog(AddressBookConfig * abc);
#ifdef ENABLE_LDAP
static GtkWidget *create_ldap_dialog(AddressBookConfig * abc);
#endif
#ifdef HAVE_SQLITE
static GtkWidget *create_gpe_dialog(AddressBookConfig * abc);
#endif

static void help_button_cb(AddressBookConfig * abc);
static gboolean handle_close(AddressBookConfig * abc);
static gboolean bad_path(gchar * path, GtkWindow * window, gint type);
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
                              BalsaAddressBookCallback callback,
                              GtkWindow* parent)
{
    AddressBookConfig *abc;

    abc = g_new0(AddressBookConfig, 1);
    abc->address_book = address_book;
    abc->callback = callback;
    abc->type = G_TYPE_FROM_INSTANCE(address_book);
    abc->window = create_dialog_from_type(abc);

    if (address_book)
	gtk_widget_grab_focus(abc->name_entry);

    gtk_widget_show_all(GTK_WIDGET(abc->window));
}

static void
edit_book_response(GtkWidget * dialog, gint response,
                   AddressBookConfig * abc)
{
    switch (response) {
    case GTK_RESPONSE_HELP:
        help_button_cb(abc);
        return;
    case GTK_RESPONSE_APPLY:
        if (handle_close(abc))
            break;
        else
            return;
    default:
        break;
    }

    gtk_widget_destroy(dialog);
    g_free(abc);
}

static GtkWidget *
create_local_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    const gchar *title;
    const gchar *action;
    const gchar *name;
    GtkWidget *table;
    GtkWidget *label;
    LibBalsaAddressBook *ab;

    ab = abc->address_book;
    if (ab) {
        title = _("Modify Address Book");
        action = GTK_STOCK_APPLY;
        name = ab->name;
    } else {
        title = _("Add Address Book");
        action = GTK_STOCK_ADD;
        name = NULL;
    }

    dialog =
        gtk_file_chooser_dialog_new(title, abc->parent,
                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                    GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                    action, GTK_RESPONSE_APPLY,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    NULL);

    table = gtk_table_new(2, 2, FALSE);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), table);
    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry =
        create_entry(NULL, table, NULL, NULL, 0, name, label);
    abc->expand_aliases_button =
        create_check(NULL, _("_Expand aliases as you type"), table, 1,
                     ab ? ab->expand_aliases : TRUE);

    if (ab) {
        const gchar *path;

        if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD)
            path = LIBBALSA_ADDRESS_BOOK_VCARD(ab)->path;
        else
            path = LIBBALSA_ADDRESS_BOOK_LDIF(ab)->path;
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path);
    }
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}

static GtkWidget *
create_vcard_dialog(AddressBookConfig * abc)
{
    return create_local_dialog(abc);
}

static GtkWidget *
create_ldif_dialog(AddressBookConfig * abc)
{
    return create_local_dialog(abc);
}

static GtkWidget *
create_dialog_from_type(AddressBookConfig * abc)
{
    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        return create_vcard_dialog(abc);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        return create_externq_dialog(abc);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        return create_ldif_dialog(abc);
#ifdef ENABLE_LDAP
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
        return create_ldap_dialog(abc);
#endif
#ifdef HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
        return create_gpe_dialog(abc);
#endif
    } else {
        g_assert_not_reached();
    }

    return NULL;
}

static GtkWidget *
create_generic_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    const gchar *title;
    const gchar *action;
    const gchar *name;
    LibBalsaAddressBook *ab;

    ab = abc->address_book;
    if (ab) {
        title = _("Modify Address Book");
        action = GTK_STOCK_APPLY;
        name = ab->name;
    } else {
        title = _("Add Address Book");
        action = GTK_STOCK_ADD;
        name = NULL;
    }

    dialog =
        gtk_dialog_new_with_buttons(title, abc->parent, (GtkDialogFlags) 0,
                                    GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                    action, GTK_RESPONSE_APPLY,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    NULL);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}

static GtkWidget *
create_externq_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *table;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget *label;
    LibBalsaAddressBookExtern* ab;

    ab = (LibBalsaAddressBookExtern*)abc->address_book; /* may be NULL */
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
#if GTK_CHECK_VERSION(2, 6, 0)
    abc->ab_specific.externq.load =
        gtk_file_chooser_button_new
        (_("Select load program for address book"),
         GTK_FILE_CHOOSER_ACTION_OPEN);
#else /* GTK_CHECK_VERSION(2, 6, 0) */
    abc->ab_specific.externq.load =
	gnome_file_entry_new("ExternAddressBookLoadPath",
			     _("Select load program for address book"));
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.externq.load, 1, 2,
		     1, 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  abc->ab_specific.externq.load);

    label = gtk_label_new(_("Save program location"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
		     GTK_FILL, GTK_FILL, 10, 10);
#if GTK_CHECK_VERSION(2, 6, 0)
    abc->ab_specific.externq.save =
        gtk_file_chooser_button_new
        (_("Select save program for address book"),
         GTK_FILE_CHOOSER_ACTION_OPEN);
#else /* GTK_CHECK_VERSION(2, 6, 0) */
    abc->ab_specific.externq.save =
	gnome_file_entry_new("ExternAddressBookSavePath",
			     _("Select save program for address book"));
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
    gtk_table_attach(GTK_TABLE(table), abc->ab_specific.externq.save, 1, 2,
		     2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  abc->ab_specific.externq.save);
    
    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? abc->address_book->expand_aliases : TRUE);

    if (ab) {
#if GTK_CHECK_VERSION(2, 6, 0)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.load),
                                      ab->load);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.save),
                                      ab->save);
#else /* GTK_CHECK_VERSION(2, 6, 0) */
	GtkWidget *entry;
	entry = GTK_WIDGET(gnome_file_entry_gtk_entry
			   (GNOME_FILE_ENTRY(abc->ab_specific.externq.load)));
	gtk_entry_set_text(GTK_ENTRY(entry), ab->load);

	entry = GTK_WIDGET(gnome_file_entry_gtk_entry
			   (GNOME_FILE_ENTRY(abc->ab_specific.externq.save)));
	gtk_entry_set_text(GTK_ENTRY(entry), ab->save);
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
    }

    dialog = create_generic_dialog(abc);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    return dialog;
}

#ifdef ENABLE_LDAP
static GtkWidget *
create_ldap_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *table = gtk_table_new(2, 6, FALSE);

    LibBalsaAddressBookLdap* ab;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget* label;
    gchar *host = libbalsa_guess_ldap_server();
    gchar *base = libbalsa_guess_ldap_base();
    gchar *name = libbalsa_guess_ldap_name();

    ab = (LibBalsaAddressBookLdap*)abc->address_book; /* may be NULL */

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
    
    dialog = create_generic_dialog(abc);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    return dialog;
}
#endif

#ifdef HAVE_SQLITE
static GtkWidget *
create_gpe_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *table = gtk_table_new(2, 6, FALSE);

    LibBalsaAddressBook* ab;
    GtkDialog* mcw = GTK_DIALOG(abc->window);
    GtkWidget* label;

    ab = (LibBalsaAddressBook*)abc->address_book; /* may be NULL */

    /* mailbox name */

    label = create_label(_("A_ddress Book Name"), table, 0);
    abc->name_entry = create_entry(mcw, table, NULL, NULL, 0, 
				   ab ? ab->name : _("GPE Address Book"), 
				   label);
    abc->expand_aliases_button =
	create_check(mcw, _("_Expand aliases as you type"), table, 3,
		     ab ? ab->expand_aliases : TRUE);

    dialog = create_generic_dialog(abc);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    return dialog;
}
#endif

static void
help_button_cb(AddressBookConfig * abc)
{
    GError *err = NULL;

    gnome_help_display("balsa", "preferences-1", &err);

    if (err) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
		_("Error displaying help: %s\n"),
		err->message);
        g_error_free(err);
    }
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
chooser_bad_path(GtkFileChooser * chooser, GtkWindow * window, gint type)
{
    return bad_path(gtk_file_chooser_get_filename(chooser), window, type);
}

#if !GTK_CHECK_VERSION(2, 6, 0)
static gboolean
entry_bad_path(GnomeFileEntry * entry, GtkWindow * window, gint type)
{
    return bad_path(gnome_file_entry_get_full_path(entry, TRUE), window,
                    type);
}
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */

static gboolean
handle_close(AddressBookConfig * abc)
{
    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->window),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_FILE))
            return FALSE;
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->window),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_FILE))
            return FALSE;
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
#if GTK_CHECK_VERSION(2, 6, 0)
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->ab_specific.externq.load),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_LOAD))
            return FALSE;
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->ab_specific.externq.save),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_SAVE))
            return FALSE;
#else /* GTK_CHECK_VERSION(2, 6, 0) */
        if (entry_bad_path(GNOME_FILE_ENTRY(abc->ab_specific.externq.load),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_LOAD))
            return FALSE;
        if (entry_bad_path(GNOME_FILE_ENTRY(abc->ab_specific.externq.save),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_SAVE))
            return FALSE;
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
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
bad_path(gchar * path, GtkWindow * window, gint type)
{
#if !GTK_CHECK_VERSION(2, 6, 0)
    gchar *message, *question;
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
    GtkWidget *ask;
    gint clicked_button;

#if GTK_CHECK_VERSION(2, 6, 0)
    if (path) {
        g_free(path);
        return FALSE;
    }
    ask = gtk_message_dialog_new(window,
				 GTK_DIALOG_MODAL|
				 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                 _("No path found.  "
				   "Do you want to give one?"));
#else /* GTK_CHECK_VERSION(2, 6, 0) */
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
    ask = gtk_message_dialog_new(window,
				 GTK_DIALOG_MODAL|
				 GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                 message, path, question);
    g_free(path);
#endif /* GTK_CHECK_VERSION(2, 6, 0) */

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
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
        if (path != NULL)
            address_book = libbalsa_address_book_vcard_new(name, path);
        g_free(path);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
#if GTK_CHECK_VERSION(2, 6, 0)
        gchar *load =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.load));
        gchar *save =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.save));
#else /* GTK_CHECK_VERSION(2, 6, 0) */
        gchar *load =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.load),
                                           FALSE);
        gchar *save =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.save),
                                           FALSE);
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
        if (load != NULL && save != NULL)
            address_book =
                libbalsa_address_book_externq_new(name, load, save);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        gchar *path =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
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
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));

        vcard = LIBBALSA_ADDRESS_BOOK_VCARD(address_book);
        if (path) {
            g_free(vcard->path);
            vcard->path = path;
        }
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        LibBalsaAddressBookExtern *externq;
#if GTK_CHECK_VERSION(2, 6, 0)
        gchar *load =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.load));
        gchar *save =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.save));
#else /* GTK_CHECK_VERSION(2, 6, 0) */
        gchar *load =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.load),
                                           FALSE);
        gchar *save =
            gnome_file_entry_get_full_path(GNOME_FILE_ENTRY
                                           (abc->ab_specific.externq.save),
                                           FALSE);
#endif /* GTK_CHECK_VERSION(2, 6, 0) */

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
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));

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

/* Pref manager callbacks */

static void
add_vcard_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    abc->window = create_local_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_externq_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    abc->window = create_externq_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_ldif_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
    abc->window = create_local_dialog(abc);
    gtk_widget_show_all(abc->window);
}

#ifdef ENABLE_LDAP
static void
add_ldap_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
    abc->window = create_ldap_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
static void
add_gpe_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
    abc->window = create_gpe_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_SQLITE */

GtkWidget *
balsa_address_book_add_menu(BalsaAddressBookCallback callback,
                            GtkWindow * parent)
{
    GtkWidget *menu;
    GtkWidget *menuitem;
    AddressBookConfig *abc;

    menu = gtk_menu_new();
    abc = g_new0(AddressBookConfig, 1);
    abc->callback = callback;
    abc->parent = parent;
    g_object_weak_ref(G_OBJECT(menu), (GWeakNotify) g_free, abc);

    menuitem =
        gtk_menu_item_new_with_label(_("VCard Address Book (GnomeCard)"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_vcard_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem =
        gtk_menu_item_new_with_label(_("External query (a program)"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_externq_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("LDIF Address Book"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_ldif_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

#ifdef ENABLE_LDAP
    menuitem = gtk_menu_item_new_with_label(_("LDAP Address Book"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_ldap_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
    menuitem = gtk_menu_item_new_with_label(_("LDAP Address Book"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_gpe_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* HAVE_SQLITE */

    return menu;
}
