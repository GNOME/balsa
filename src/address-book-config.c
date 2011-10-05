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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-book-config.h"

#include <gtk/gtk.h>

#include "balsa-app.h"
#include <glib/gi18n.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

typedef struct _AddressBookConfig AddressBookConfig;
struct _AddressBookConfig {
    GtkWidget *window;

    GtkWidget *name_entry;
    GtkWidget *as_i_type;
    GtkWidget *on_request;
    GtkWidget *never;

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
	    GtkWidget *book_dn;
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
static GtkWidget *create_local_dialog(AddressBookConfig * abc,
                                      const gchar * type);
static GtkWidget *create_externq_dialog(AddressBookConfig * abc);
#ifdef ENABLE_LDAP
static GtkWidget *create_ldap_dialog(AddressBookConfig * abc);
#endif
#ifdef HAVE_SQLITE
static GtkWidget *create_gpe_dialog(AddressBookConfig * abc);
#endif
#ifdef HAVE_RUBRICA
static GtkWidget *create_rubrica_dialog(AddressBookConfig * abc);
#endif

static void help_button_cb(AddressBookConfig * abc);
static gboolean handle_close(AddressBookConfig * abc);
static gboolean bad_path(gchar * path, GtkWindow * window, gint type);
static gboolean create_book(AddressBookConfig * abc);
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

    abc = g_object_get_data(G_OBJECT(address_book), "balsa-abc");
    if (abc) {
        /* Only one dialog per address book. */
        gtk_window_present(GTK_WINDOW(abc->window));
        return;
    }

    abc = g_new0(AddressBookConfig, 1);
    g_object_set_data(G_OBJECT(address_book), "balsa-abc", abc);
    abc->address_book = address_book;
    abc->callback = callback;
    abc->type = G_TYPE_FROM_INSTANCE(address_book);
    abc->window = create_dialog_from_type(abc);

    if (address_book)
	gtk_widget_grab_focus(abc->name_entry);

    gtk_widget_show_all(GTK_WIDGET(abc->window));
}

void
balsa_address_book_config_new_from_type(GType type,
                                        BalsaAddressBookCallback callback,
                                        GtkWindow* parent)
{
    AddressBookConfig *abc;

    abc = g_new0(AddressBookConfig, 1);
    abc->address_book = NULL;
    abc->callback = callback;
    abc->type = type;
    abc->window = create_dialog_from_type(abc);

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
    if (abc->address_book)
        g_object_set_data(G_OBJECT(abc->address_book), "balsa-abc", NULL);
    g_free(abc);
}

/* Radio buttons */
static void
add_radio_buttons(GtkWidget * grid, gint row, AddressBookConfig * abc)
{
    GtkWidget *label;
    GSList *radio_group;
    GtkWidget *button;

    label = gtk_label_new(_("Suggest complete addresses:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 2, 1);

    abc->as_i_type =
        gtk_radio_button_new_with_label(NULL, _("as I type"));
    radio_group =
        gtk_radio_button_get_group(GTK_RADIO_BUTTON(abc->as_i_type));
    ++row;
    gtk_grid_attach(GTK_GRID(grid), abc->as_i_type, 0, row, 2, 1);

    abc->on_request =
        gtk_radio_button_new_with_label(radio_group,
                                        _("when I hit the Escape key"));
    radio_group =
        gtk_radio_button_get_group(GTK_RADIO_BUTTON(abc->on_request));
    ++row;
    gtk_grid_attach(GTK_GRID(grid), abc->on_request, 0, row, 2, 1);

    abc->never =
        gtk_radio_button_new_with_label(radio_group, _("never"));
    ++row;
    gtk_grid_attach(GTK_GRID(grid), abc->never, 0, row, 2, 1);

    button = abc->as_i_type;
    if (abc->address_book) {
        if (!abc->address_book->expand_aliases)
            button = abc->never;
        else if (abc->address_book->is_expensive)
            button = abc->on_request;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
}

static GtkWidget *
create_local_dialog(AddressBookConfig * abc, const gchar * type)
{
    GtkWidget *dialog;
    gchar *title;
    const gchar *action;
    const gchar *name;
    GtkWidget *grid;
    GtkWidget *label;
    LibBalsaAddressBook *ab;
    GtkSizeGroup *size_group;

    ab = abc->address_book;
    if (ab) {
        title = g_strdup_printf(_("Modify %s Address Book"), type);
        action = GTK_STOCK_APPLY;
        name = ab->name;
    } else {
        title = g_strdup_printf(_("Add %s Address Book"), type);
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
    g_free(title);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, abc->parent);
#endif
    size_group = libbalsa_create_size_group(dialog);

    grid = libbalsa_create_grid();
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), grid);
    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    gtk_size_group_add_widget(size_group, label);
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0, name, label);

    add_radio_buttons(grid, 1, abc);

    if (ab) {
        const gchar *path = LIBBALSA_ADDRESS_BOOK_TEXT(ab)->path;
        gchar *folder;
        gchar *utf8name;

        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path);
        /* Name entry will be blank unless we set it. */
        folder = g_path_get_basename(path);
        utf8name = g_filename_to_utf8(folder, -1, NULL, NULL, NULL);
        g_free(folder);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
                                          utf8name);
        g_free(utf8name);
    }
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}

static GtkWidget *
create_vcard_dialog(AddressBookConfig * abc)
{
    return create_local_dialog(abc, "VCARD");
}

static GtkWidget *
create_ldif_dialog(AddressBookConfig * abc)
{
    return create_local_dialog(abc, "LDIF");
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
#ifdef HAVE_RUBRICA
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA) {
        return create_rubrica_dialog(abc);
#endif
    } else {
        g_assert_not_reached();
    }

    return NULL;
}

static GtkWidget *
create_generic_dialog(AddressBookConfig * abc, const gchar * type)
{
    GtkWidget *dialog;
    gchar *title;
    const gchar *action;
    LibBalsaAddressBook *ab;

    ab = abc->address_book;
    if (ab) {
        title = g_strdup_printf(_("Modify %s Address Book"), type);
        action = GTK_STOCK_APPLY;
    } else {
        title = g_strdup_printf(_("Add %s Address Book"), type);
        action = GTK_STOCK_ADD;
    }

    dialog =
        gtk_dialog_new_with_buttons(title, abc->parent, 0,
                                    GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                    action, GTK_RESPONSE_APPLY,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    NULL);
    g_free(title);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, abc->parent);
#endif
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);
    gtk_container_set_border_width(GTK_CONTAINER
                                   (gtk_dialog_get_content_area
                                    (GTK_DIALOG(dialog))), 12);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}

static GtkWidget *
create_externq_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *grid;
    GtkWidget *label;
    LibBalsaAddressBookExtern* ab;

    ab = (LibBalsaAddressBookExtern*)abc->address_book; /* may be NULL */
    grid = libbalsa_create_grid();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);

    /* mailbox name */

    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    abc->name_entry = libbalsa_create_grid_entry(grid, NULL, NULL, 0, 
				   ab ? abc->address_book->name : NULL, 
				   label);

    label = gtk_label_new(_("Load program location:"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
    abc->ab_specific.externq.load =
        gtk_file_chooser_button_new
        (_("Select load program for address book"),
         GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_widget_set_hexpand(abc->ab_specific.externq.load, TRUE);
    gtk_grid_attach(GTK_GRID(grid), abc->ab_specific.externq.load,
                    1, 1, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label),
                                  abc->ab_specific.externq.load);

    label = gtk_label_new(_("Save program location:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
    abc->ab_specific.externq.save =
        gtk_file_chooser_button_new
        (_("Select save program for address book"),
         GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_widget_set_hexpand(abc->ab_specific.externq.save, TRUE);
    gtk_grid_attach(GTK_GRID(grid), abc->ab_specific.externq.save,
                    1, 2, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label),
                                  abc->ab_specific.externq.save);

    add_radio_buttons(grid, 3, abc);

    if (ab) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.load),
                                      ab->load);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.save),
                                      ab->save);
    }

    dialog = create_generic_dialog(abc, "Extern");
    gtk_container_add(GTK_CONTAINER
                      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid);
    return dialog;
}

#ifdef ENABLE_LDAP
static GtkWidget *
create_ldap_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *grid = libbalsa_create_grid();

    LibBalsaAddressBookLdap* ab;
    GtkWidget* label;
    gchar *host = g_strdup_printf("ldap://%s", libbalsa_guess_ldap_server());
    gchar *base = libbalsa_guess_ldap_base();
    gchar *name = libbalsa_guess_ldap_name();

    ab = (LibBalsaAddressBookLdap*)abc->address_book; /* may be NULL */

    /* mailbox name */

    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    abc->name_entry = libbalsa_create_grid_entry(grid, NULL, NULL, 0, 
				   ab ? abc->address_book->name : name, 
				   label);

    label = libbalsa_create_grid_label(_("_Host Name"), grid, 1);
    abc->ab_specific.ldap.host_name = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 1, 
		     ab ? ab->host : host, label);

    label = libbalsa_create_grid_label(_("Base Domain _Name"), grid, 2);
    abc->ab_specific.ldap.base_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 2, 
		     ab ? ab->base_dn : base, label);

    label = libbalsa_create_grid_label(_("_User Name (Bind DN)"), grid, 3);
    abc->ab_specific.ldap.bind_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 3, 
		     ab ? ab->bind_dn : "", label);

    label = libbalsa_create_grid_label(_("_Password"), grid, 4);
    abc->ab_specific.ldap.passwd = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 4, 
		     ab ? ab->passwd : "", label);
    gtk_entry_set_visibility(GTK_ENTRY(abc->ab_specific.ldap.passwd), FALSE);

    label = libbalsa_create_grid_label(_("_User Address Book DN"), grid, 5);
    abc->ab_specific.ldap.book_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 5,
		     ab ? ab->priv_book_dn : "", label);

    abc->ab_specific.ldap.enable_tls =
	libbalsa_create_grid_check(_("Enable _TLS"), grid, 6,
		     ab ? ab->enable_tls : FALSE);

    add_radio_buttons(grid, 7, abc);

    gtk_widget_show(grid);

    g_free(base);
    g_free(name);
    g_free(host);
    
    dialog = create_generic_dialog(abc, "LDAP");
    gtk_container_add(GTK_CONTAINER
                      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid);
    return dialog;
}
#endif

#ifdef HAVE_SQLITE
static GtkWidget *
create_gpe_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *grid = libbalsa_create_grid(3, 2);

    LibBalsaAddressBook* ab;
    GtkWidget* label;

    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);

    ab = (LibBalsaAddressBook*)abc->address_book; /* may be NULL */

    /* mailbox name */

    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    abc->name_entry = libbalsa_create_grid_entry(grid, NULL, NULL, 0, 
				   ab ? ab->name : _("GPE Address Book"), 
				   label);

    add_radio_buttons(grid, 1, abc);

    dialog = create_generic_dialog(abc, "GPE");
    gtk_container_add(GTK_CONTAINER
                      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      grid);
    return dialog;
}
#endif

#if HAVE_RUBRICA
static GtkWidget *
create_rubrica_dialog(AddressBookConfig * abc)
{
    return create_local_dialog(abc, "Rubrica");
}
#endif

static void
help_button_cb(AddressBookConfig * abc)
{
    GdkScreen *screen;
    GError *err = NULL;

    screen = gtk_widget_get_screen(abc->window);
    gtk_show_uri(screen, "ghelp:balsa?preferences-address-books",
                 gtk_get_current_event_time(), &err);

    if (err) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
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
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->ab_specific.externq.load),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_LOAD))
            return FALSE;
        if (chooser_bad_path(GTK_FILE_CHOOSER(abc->ab_specific.externq.save),
                     GTK_WINDOW(abc->window),
                     ADDRESS_BOOK_CONFIG_PATH_SAVE))
            return FALSE;
    }

    if (abc->address_book == NULL)
        return create_book(abc);
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
    GtkWidget *ask;
    gint clicked_button;

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
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(ask, window);
#endif
    gtk_dialog_set_default_response(GTK_DIALOG(ask), GTK_RESPONSE_YES);
    clicked_button = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);
    return clicked_button == GTK_RESPONSE_YES;
}

static gboolean
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
#define GET_FILENAME(chooser) \
  gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser))
        gchar *load = GET_FILENAME(abc->ab_specific.externq.load);
        gchar *save = GET_FILENAME(abc->ab_specific.externq.save);
        if (load != NULL && save != NULL)
            address_book =
                libbalsa_address_book_externq_new(name, load, save);
        g_free(load);
        g_free(save);
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
        const gchar *book_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.book_dn));
        gboolean enable_tls =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (abc->ab_specific.ldap.enable_tls));
        address_book =
            libbalsa_address_book_ldap_new(name, host_name, base_dn,
                                           bind_dn, passwd, book_dn,
                                           enable_tls);
#endif
#ifdef HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
        address_book =
            libbalsa_address_book_gpe_new(name);
#endif
#ifdef HAVE_RUBRICA
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA) {
        gchar *path =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
        if (path != NULL)
            address_book = libbalsa_address_book_rubrica_new(name, path);
        g_free(path);
#endif
    } else
        g_assert_not_reached();

    if (address_book) {
        address_book->expand_aliases =
            !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->never));
        address_book->is_expensive =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->on_request));
        abc->callback(address_book, TRUE);
    }

    return address_book != NULL;
}

static void
modify_book(AddressBookConfig * abc)
{
    LibBalsaAddressBook *address_book = abc->address_book;

    g_free(address_book->name);
    address_book->name =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(abc->name_entry)));

    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD
        || abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF
#ifdef HAVE_RUBRICA
        || abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA
#endif /* HAVE_RUBRICA */
        ) {
        LibBalsaAddressBookText *ab_text =
            LIBBALSA_ADDRESS_BOOK_TEXT(address_book);
        gchar *path =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));

        if (path) {
            g_free(ab_text->path);
            ab_text->path = path;
        }
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN) {
        LibBalsaAddressBookExtern *externq;
        gchar *load =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.load));
        gchar *save =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.save));

        externq = LIBBALSA_ADDRESS_BOOK_EXTERN(address_book);
        if (load) {
            g_free(externq->load);
            externq->load = load;;
        }
        if (save) {
            g_free(externq->save);
            externq->save = save;
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
        const gchar *book_dn =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.book_dn));
        gboolean enable_tls =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (abc->ab_specific.ldap.enable_tls));

        ldap = LIBBALSA_ADDRESS_BOOK_LDAP(address_book);

        g_free(ldap->host);     ldap->host = g_strdup(host_name);
        g_free(ldap->base_dn);  ldap->base_dn = g_strdup(base_dn);
        g_free(ldap->bind_dn);  ldap->bind_dn = g_strdup(bind_dn);
        g_free(ldap->passwd);   ldap->passwd  = g_strdup(passwd);
        g_free(ldap->priv_book_dn);
        ldap->priv_book_dn = g_strdup(book_dn && *book_dn ? book_dn : bind_dn);
        ldap->enable_tls = enable_tls;
        libbalsa_address_book_ldap_close_connection(ldap);
#endif
#if HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
#endif /* HAVE_SQLITE */
    } else
        g_assert_not_reached();

    address_book->expand_aliases =
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->never));
    address_book->is_expensive =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->on_request));

    abc->callback(address_book, FALSE);
}

/* Pref manager callbacks */

static void
add_vcard_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    abc->window = create_vcard_dialog(abc);
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
    abc->window = create_ldif_dialog(abc);
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

#ifdef HAVE_RUBRICA
static void
add_rubrica_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    g_object_weak_unref(G_OBJECT(gtk_widget_get_parent(widget)),
                        (GWeakNotify) g_free, abc);
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA;
    abc->window = create_rubrica_dialog(abc);
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
    menuitem = gtk_menu_item_new_with_label(_("GPE Address Book"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_gpe_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* HAVE_SQLITE */

#ifdef HAVE_RUBRICA
    menuitem = gtk_menu_item_new_with_label(_("Rubrica2 Address Book"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(add_rubrica_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* HAVE_RUBRICA */

    return menu;
}
