/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
	    GtkWidget *host;
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
#ifdef HAVE_OSMO
static GtkWidget *create_osmo_dialog(AddressBookConfig *abc);
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
        gtk_window_present_with_time(GTK_WINDOW(abc->window),
                                     gtk_get_current_event_time());
        return;
    }

    abc = g_new0(AddressBookConfig, 1);
    g_object_set_data_full(G_OBJECT(address_book), "balsa-abc", abc, g_free);
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
    g_object_weak_ref(G_OBJECT(abc->window), (GWeakNotify) g_free, abc);

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

    if (abc->address_book)
        g_object_set_data(G_OBJECT(abc->address_book), "balsa-abc", NULL);

    gtk_widget_destroy(dialog);
}

/* Radio buttons */
static void
add_radio_buttons(GtkWidget * grid, gint row, AddressBookConfig * abc)
{
    GtkWidget *label;
    GSList *radio_group;
    GtkWidget *button;

    label = gtk_label_new(_("Suggest complete addresses:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
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
    if (abc->address_book != NULL) {
        if (!libbalsa_address_book_get_expand_aliases(abc->address_book))
            button = abc->never;
        else if (libbalsa_address_book_get_is_expensive(abc->address_book))
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
    if (ab != NULL) {
        title = g_strdup_printf(_("Modify %s Address Book"), type);
        action = _("_Apply");
        name = libbalsa_address_book_get_name(ab);
    } else {
        title = g_strdup_printf(_("Add %s Address Book"), type);
        action = _("_Add");
        name = NULL;
    }

    dialog =
        gtk_file_chooser_dialog_new(title, abc->parent,
                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Help"),   GTK_RESPONSE_HELP,
                                    action,       GTK_RESPONSE_APPLY,
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
        LibBalsaAddressBookText *abt = (LibBalsaAddressBookText *) ab;
        const gchar *path = libbalsa_address_book_text_get_path(abt);
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
    g_signal_connect(dialog, "response",
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
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ) {
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
#ifdef HAVE_OSMO
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_OSMO) {
    	return create_osmo_dialog(abc);
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
        action = _("_Apply");
    } else {
        title = g_strdup_printf(_("Add %s Address Book"), type);
        action = _("_Add");
    }

    dialog =
        gtk_dialog_new_with_buttons(title, abc->parent,
                                    libbalsa_dialog_flags(),
                                    _("_Help"), GTK_RESPONSE_HELP,
                                    action, GTK_RESPONSE_APPLY,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    g_free(title);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, abc->parent);
#endif
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);
    gtk_container_set_border_width(GTK_CONTAINER
                                   (gtk_dialog_get_content_area
                                    (GTK_DIALOG(dialog))), 12);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}

#ifdef HAVE_OSMO
static GtkWidget *
create_osmo_dialog(AddressBookConfig *abc)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    gchar *title;
    const gchar *action;
    const gchar *name;
    GtkWidget *grid;
    GtkWidget *label;
    LibBalsaAddressBook *ab;
    GtkSizeGroup *size_group;

    ab = abc->address_book;
    if (ab) {
        title = g_strdup_printf(_("Modify Osmo Address Book"));
        action = _("_Apply");
        name = libbalsa_address_book_get_name(ab);
    } else {
        title = g_strdup_printf(_("Add Osmo Address Book"));
        action = _("_Add");
        name = NULL;
    }

    dialog =
        gtk_dialog_new_with_buttons(title, abc->parent,
                                    libbalsa_dialog_flags(),
                                    _("_Help"), GTK_RESPONSE_HELP,
                                    action, GTK_RESPONSE_APPLY,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    g_free(title);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, abc->parent);
#endif
    size_group = libbalsa_create_size_group(dialog);

    grid = libbalsa_create_grid();
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), grid);
    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    gtk_size_group_add_widget(size_group, label);
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0, name, label);
    add_radio_buttons(grid, 1, abc);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(edit_book_response), abc);

    return dialog;
}
#endif /* HAVE_OSMO */

static GtkWidget *
create_externq_dialog(AddressBookConfig * abc)
{
    GtkWidget *dialog;
    GtkWidget *grid;
    GtkWidget *label;
    LibBalsaAddressBookExternq* ab_externq;

    ab_externq = (LibBalsaAddressBookExternq*)abc->address_book; /* may be NULL */
    grid = libbalsa_create_grid();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);

    /* mailbox name */

    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0,
				   ab_externq != NULL ?
                                   libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab_externq)) : NULL,
				   label);

    label = gtk_label_new(_("Load program location:"));
    gtk_widget_set_halign(label, GTK_ALIGN_END);
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
    gtk_widget_set_halign(label, GTK_ALIGN_START);
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

    if (ab_externq != NULL) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.load),
                                      libbalsa_address_book_externq_get_load(ab_externq));
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER
                                      (abc->ab_specific.externq.save),
                                      libbalsa_address_book_externq_get_save(ab_externq));
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
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0, 
				   ab != NULL ?
                                   libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab)) : name,
				   label);

    label = libbalsa_create_grid_label(_("_LDAP Server URI"), grid, 1);
    abc->ab_specific.ldap.host = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 1, 
		     ab ? libbalsa_address_book_ldap_get_host(ab) : host, label);

    label = libbalsa_create_grid_label(_("Base Domain _Name"), grid, 2);
    abc->ab_specific.ldap.base_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 2, 
		     ab ? libbalsa_address_book_ldap_get_base_dn(ab) : base, label);

    label = libbalsa_create_grid_label(_("_User Name (Bind DN)"), grid, 3);
    abc->ab_specific.ldap.bind_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 3, 
		     ab ? libbalsa_address_book_ldap_get_bind_dn(ab) : "", label);

    label = libbalsa_create_grid_label(_("_Password"), grid, 4);
    abc->ab_specific.ldap.passwd = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 4, 
		     ab ? libbalsa_address_book_ldap_get_passwd(ab) : "", label);
    gtk_entry_set_visibility(GTK_ENTRY(abc->ab_specific.ldap.passwd), FALSE);

    label = libbalsa_create_grid_label(_("_User Address Book DN"), grid, 5);
    abc->ab_specific.ldap.book_dn = 
	libbalsa_create_grid_entry(grid, NULL, NULL, 5,
		     ab ? libbalsa_address_book_ldap_get_book_dn(ab) : "", label);

    abc->ab_specific.ldap.enable_tls =
	libbalsa_create_grid_check(_("Enable _TLS"), grid, 6,
		     ab ? libbalsa_address_book_ldap_get_enable_tls(ab) : FALSE);

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
    GtkWidget *grid = libbalsa_create_grid();

    LibBalsaAddressBook* ab;
    GtkWidget* label;

    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);

    ab = (LibBalsaAddressBook*)abc->address_book; /* may be NULL */

    /* mailbox name */

    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0,
				   ab != NULL ?  libbalsa_address_book_get_name(ab) :
                                   _("GPE Address Book"),
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
    GError *err = NULL;

    gtk_show_uri_on_window(GTK_WINDOW(abc->window),
                           "help:balsa/preferences-address-books",
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
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ) {
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
                                 _("No path found. "
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
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ) {
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
        const gchar *host =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.host));
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
            libbalsa_address_book_ldap_new(name, host, base_dn,
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
#ifdef HAVE_OSMO
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_OSMO) {
    	address_book = libbalsa_address_book_osmo_new(name);
#endif
    } else
        g_assert_not_reached();

    if (address_book != NULL) {
        libbalsa_address_book_set_expand_aliases(address_book,
            !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->never)));
        libbalsa_address_book_set_is_expensive(address_book,
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->on_request)));
        abc->callback(address_book, TRUE);
    }

    return address_book != NULL;
}

static void
modify_book(AddressBookConfig * abc)
{
    LibBalsaAddressBook *address_book = abc->address_book;

    libbalsa_address_book_set_name(address_book,
        gtk_entry_get_text(GTK_ENTRY(abc->name_entry)));

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

        if (path != NULL)
            libbalsa_address_book_text_set_path(ab_text, path);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ) {
        LibBalsaAddressBookExternq *ab_externq;
        gchar *load =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.load));
        gchar *save =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER
                                           (abc->ab_specific.externq.save));

        ab_externq = LIBBALSA_ADDRESS_BOOK_EXTERNQ(address_book);
        if (load) {
            libbalsa_address_book_externq_set_load(ab_externq, load);
            g_free(load);
        }
        if (save) {
            libbalsa_address_book_externq_set_save(ab_externq, save);
            g_free(save);
        }
#ifdef ENABLE_LDAP
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDAP) {
        LibBalsaAddressBookLdap *ldap;
        const gchar *host =
            gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.ldap.host));
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

        libbalsa_address_book_ldap_set_host(ldap, host);
        libbalsa_address_book_ldap_set_base_dn(ldap, base_dn);
        libbalsa_address_book_ldap_set_bind_dn(ldap, bind_dn);
        libbalsa_address_book_ldap_set_passwd(ldap, passwd);
        libbalsa_address_book_ldap_set_book_dn(ldap,
                                               book_dn && *book_dn ? book_dn : bind_dn);
        libbalsa_address_book_ldap_set_enable_tls(ldap, enable_tls);
        libbalsa_address_book_ldap_close_connection(ldap);
#endif
#if HAVE_SQLITE
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_GPE) {
#endif /* HAVE_SQLITE */
#if HAVE_OSMO
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_OSMO) {
    	/* nothing to do here */
#endif
    } else
        g_assert_not_reached();

    libbalsa_address_book_set_expand_aliases(address_book,
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->never)));
    libbalsa_address_book_set_is_expensive(address_book,
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->on_request)));

    abc->callback(address_book, FALSE);
}

/* Pref manager callbacks */

static void
add_vcard_cb(GSimpleAction *action,
             GVariant      *parameter,
             gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    abc->window = create_vcard_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_externq_cb(GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ;
    abc->window = create_externq_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_ldif_cb(GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
    abc->window = create_ldif_dialog(abc);
    gtk_widget_show_all(abc->window);
}

#ifdef ENABLE_LDAP
static void
add_ldap_cb(GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
    abc->window = create_ldap_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
static void
add_gpe_cb(GSimpleAction *action,
           GVariant      *parameter,
           gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
    abc->window = create_gpe_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_SQLITE */

#ifdef HAVE_RUBRICA
static void
add_rubrica_cb(GSimpleAction *action,
               GVariant      *parameter,
               gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA;
    abc->window = create_rubrica_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_SQLITE */

#ifdef HAVE_OSMO
static void
add_osmo_cb(GSimpleAction *action,
            GVariant      *parameter,
            gpointer       user_data)
{
    AddressBookConfig *abc = user_data;

    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_OSMO;
    abc->window = create_osmo_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_OSMO */

GMenuModel *
balsa_address_book_add_menu(BalsaAddressBookCallback callback,
                            GtkWindow               *parent)
{
    AddressBookConfig *abc;
    GSimpleActionGroup *simple;
    GMenu *menu;
    static const GActionEntry address_book_entries[] = {
        {"add-vcard", add_vcard_cb},
        {"add-externq", add_externq_cb},
        {"add-ldif", add_ldif_cb},
#ifdef ENABLE_LDAP
        {"add-ldap", add_ldap_cb},
#endif /* ENABLE_LDAP */
#ifdef HAVE_SQLITE
        {"add-gpe", add_gpe_cb},
#endif /* HAVE_SQLITE */
#ifdef HAVE_RUBRICA
        {"add-rubrica", add_rubrica_cb},
#endif /* HAVE_RUBRICA */
#ifdef HAVE_OSMO
        {"add-osmo", add_osmo_cb},
#endif /* HAVE_OSMO */
    };

    abc = g_new0(AddressBookConfig, 1);
    abc->callback = callback;
    abc->parent = parent;
    g_object_weak_ref(G_OBJECT(parent), (GWeakNotify) g_free, abc);

    simple = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(simple),
                                    address_book_entries,
                                    G_N_ELEMENTS(address_book_entries),
                                    abc);
    gtk_widget_insert_action_group(GTK_WIDGET(parent),
                                   "address-book",
                                   G_ACTION_GROUP(simple));
    g_object_unref(simple);

    menu = g_menu_new();
    g_menu_append(menu, _("vCard Address Book (GnomeCard)"), "address-book.add-vcard");
    g_menu_append(menu, _("External query (a program)"), "address-book.add-externq");
    g_menu_append(menu, _("LDIF Address Book"), "address-book.add-ldif");

#ifdef ENABLE_LDAP
    g_menu_append(menu, _("LDAP Address Book"), "address-book.add-ldap");
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
    g_menu_append(menu, _("GPE Address Book"), "address-book.add-gpe");
#endif /* HAVE_SQLITE */

#ifdef HAVE_RUBRICA
    g_menu_append(menu, _("Rubrica2 Address Book"), "address-book.add-rubrica");
#endif /* HAVE_RUBRICA */

#ifdef HAVE_OSMO
    g_menu_append(menu, _("Osmo Address Book"), "address-book.add-osmo");
#endif

    return G_MENU_MODEL(menu);
}
