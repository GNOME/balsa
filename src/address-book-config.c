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

#ifdef HAVE_WEBDAV
	struct {
		GtkWidget *domain_url;
		GtkWidget *user;
		GtkWidget *passwd;
		GtkWidget *probe;
		GtkWidget *addressbook;
		GtkWidget *remote_name;
		GtkWidget *refresh_period;
		GtkWidget *force_mget;
	} carddav;
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
#ifdef HAVE_OSMO
static GtkWidget *create_osmo_dialog(AddressBookConfig *abc);
#endif
#ifdef HAVE_WEBDAV
static GtkWidget *create_carddav_dialog(AddressBookConfig *abc);
#endif

static void help_button_cb(AddressBookConfig * abc);
static gboolean handle_close(AddressBookConfig * abc);
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

static void
local_dialog_changed(AddressBookConfig * abc)
{
    gchar *file_name;
    const gchar *book_name;
    gboolean apply_ok;

    file_name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
    book_name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));

    apply_ok = (file_name != NULL && file_name[0] != '\0' &&
                book_name != NULL && book_name[0] != '\0');
    g_free(file_name);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(abc->window), GTK_RESPONSE_APPLY, apply_ok);
}

static gboolean
local_dialog_idle(gpointer user_data)
{
    AddressBookConfig *abc = user_data;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(abc->window), GTK_RESPONSE_APPLY, FALSE);

    return G_SOURCE_REMOVE;
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
    g_signal_connect_swapped(dialog, "selection-changed",
                             G_CALLBACK(local_dialog_changed), abc);

    size_group = libbalsa_create_size_group(dialog);

    grid = libbalsa_create_grid();
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), grid);
    label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
    gtk_size_group_add_widget(size_group, label);
    abc->name_entry =
        libbalsa_create_grid_entry(grid, NULL, NULL, 0, name, label);
    g_signal_connect_swapped(abc->name_entry, "changed",
                             G_CALLBACK(local_dialog_changed), abc);

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

    /* Make the "apply" button insensitive after GtkFileChooser has set
     * its sensitivity: */
    g_timeout_add(500, local_dialog_idle, abc);

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
#ifdef HAVE_OSMO
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_OSMO) {
    	return create_osmo_dialog(abc);
#endif
#ifdef HAVE_WEBDAV
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV) {
    	return create_carddav_dialog(abc);
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
    size_group = libbalsa_create_size_group(dialog);

    grid = libbalsa_create_grid();
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), grid);
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

/* externq_dialog_changed
 *
 * callback (swapped) for the "changed" signal of abc->name_entry,
 * and the "file-set" signals of abc->ab_specific.externq.load and
 * abc->ab_specific.externq.save.
 *
 * If the name is non-empty and the files are set and anything has been
 * changed, enable the relevant "Add" or "Apply" button.
 */
static void
externq_dialog_changed(AddressBookConfig * abc)
{
    gchar *load;
    gchar *save;
    const gchar *name;
    gboolean apply_ok;

    load = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->ab_specific.externq.load));
    save = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->ab_specific.externq.save));
    name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));

    apply_ok = (load != NULL && load[0] != '\0' &&
                save != NULL && save[0] != '\0' &&
                name != NULL && name[0] != '\0');
    g_free(load);
    g_free(save);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(abc->window), GTK_RESPONSE_APPLY, apply_ok);
}

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
    g_signal_connect_swapped(abc->name_entry, "changed",
                             G_CALLBACK(externq_dialog_changed), abc);

    label = gtk_label_new(_("Load program location:"));
    gtk_widget_set_halign(label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
    abc->ab_specific.externq.load =
        gtk_file_chooser_button_new
        (_("Select load program for address book"),
         GTK_FILE_CHOOSER_ACTION_OPEN);
    g_signal_connect_swapped(abc->ab_specific.externq.load, "file-set",
                             G_CALLBACK(externq_dialog_changed), abc);

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
    g_signal_connect_swapped(abc->ab_specific.externq.save, "file-set",
                             G_CALLBACK(externq_dialog_changed), abc);

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

    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, FALSE);

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
    libbalsa_entry_config_passwd(GTK_ENTRY(abc->ab_specific.ldap.passwd));

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

#ifdef HAVE_WEBDAV
static void
carddav_dlg_widget_active(GtkEditable G_GNUC_UNUSED *editable, gpointer user_data)
{
	AddressBookConfig *abc = (AddressBookConfig *) user_data;

	if (abc->ab_specific.carddav.probe != NULL) {
		const gchar *domain_url;
		const gchar *user;
		const gchar *passwd;
		gboolean have_basic;
		GtkTreeIter iter;
		gboolean probe_done;

		domain_url = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.domain_url));
		user = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.user));
		passwd = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.passwd));
		have_basic = (domain_url != NULL) && (domain_url[0] != '\0') &&
			(user != NULL) && (user[0] != '\0') &&
			(passwd != NULL) && (passwd[0] != '\0');
		probe_done = gtk_tree_model_get_iter_first(gtk_combo_box_get_model(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name)),
			&iter);
		gtk_widget_set_sensitive(abc->ab_specific.carddav.probe, have_basic);
		gtk_widget_set_sensitive(abc->ab_specific.carddav.remote_name, have_basic & probe_done);
		gtk_widget_set_sensitive(abc->ab_specific.carddav.refresh_period, have_basic & probe_done);
		gtk_widget_set_sensitive(abc->ab_specific.carddav.force_mget, have_basic & probe_done);
		if (GTK_IS_DIALOG(abc->window)) {
			gtk_dialog_set_response_sensitive(GTK_DIALOG(abc->window), GTK_RESPONSE_APPLY, have_basic & probe_done);
		}
	}
}

static void
carddav_run_probe(GtkButton G_GNUC_UNUSED *button, gpointer user_data)
{
	AddressBookConfig *abc = (AddressBookConfig *) user_data;
	const gchar *domain_or_url;
	const gchar *username;
	const gchar *password;
	GList *addressbooks;
	GError *error = NULL;

	domain_or_url = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.domain_url));
	username = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.user));
	password = gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.passwd));
	addressbooks = libbalsa_carddav_list(domain_or_url, username, password, &error);
	if (error != NULL) {
		const gchar *ptext;
		gchar *stext;
		GtkWidget *dialog;

		if ((error->domain == G_RESOLVER_ERROR) && (error->code == G_RESOLVER_ERROR_NOT_FOUND)) {
			ptext = _("DNS service record not found");
			stext = g_strdup_printf(_("No DNS service record for the domain “%s” and service “carddavs” found. "
									  "Please enter the “https://” URI manually."),
				gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.domain_url)));
		} else if ((error->domain == WEBDAV_ERROR_QUARK) && (error->code == 401)) {
			ptext = _("Authorization failed");
			stext = g_strdup_printf(_("The server rejected the authorization for “%s”. "
									  "Please check the user name and the pass phrase"),
				gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.user)));
		} else {
			ptext = _("Error");
			stext = g_strdup(error->message);
		}
		dialog = gtk_message_dialog_new(NULL, libbalsa_dialog_flags() | GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
			"%s", ptext);
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", stext);
		(void) gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		g_free(stext);
		g_error_free(error);
	} else {
		GtkListStore *store;
		GList *this_ab;

		store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name)));
		gtk_list_store_clear(store);
		for (this_ab = addressbooks; this_ab != NULL; this_ab = this_ab->next) {
			GtkTreeIter iter;
			libbalsa_webdav_resource_t *res = (libbalsa_webdav_resource_t *) this_ab->data;

			gtk_list_store_append(store, &iter);
			g_debug("%s: '%s': %s", __func__, res->name, res->href);
			gtk_list_store_set(store, &iter, 0, res->name, 1, res->href, -1);
			libbalsa_webdav_resource_free(res);
		}
		gtk_combo_box_set_active(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name), 0);
		g_list_free(addressbooks);
		carddav_dlg_widget_active(NULL, abc);
	}
}

static GtkWidget *
create_carddav_dialog(AddressBookConfig *abc)
{
	GtkWidget *grid = libbalsa_create_grid();
	LibBalsaAddressBookCarddav *ab;
	GtkWidget *label;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkWidget *box;
	GtkWidget *dialog;

	ab = (LibBalsaAddressBookCarddav *) abc->address_book; /* may be NULL */

	/* address book name */
	label = libbalsa_create_grid_label(_("A_ddress Book Name:"), grid, 0);
	abc->name_entry = libbalsa_create_grid_entry(grid, NULL, NULL, 0,
		(ab != NULL) ? libbalsa_address_book_get_name(LIBBALSA_ADDRESS_BOOK(ab)) : "", label);

	label = libbalsa_create_grid_label(_("D_omain or URL:"), grid, 1);
	abc->ab_specific.carddav.domain_url = libbalsa_create_grid_entry(grid, G_CALLBACK(carddav_dlg_widget_active), abc, 1,
		(ab != NULL) ? libbalsa_address_book_carddav_get_base_dom_url(ab) : "", label);

	label = libbalsa_create_grid_label(_("_User Name:"), grid, 2);
	abc->ab_specific.carddav.user = libbalsa_create_grid_entry(grid, G_CALLBACK(carddav_dlg_widget_active), abc, 2,
		(ab != NULL) ? libbalsa_address_book_carddav_get_user(ab) : "", label);

	label = libbalsa_create_grid_label(_("_Pass Phrase:"), grid, 3);
	abc->ab_specific.carddav.passwd = libbalsa_create_grid_entry(grid, G_CALLBACK(carddav_dlg_widget_active), abc, 3,
		(ab != NULL) ? libbalsa_address_book_carddav_get_password(ab) : "", label);
	libbalsa_entry_config_passwd(GTK_ENTRY(abc->ab_specific.carddav.passwd));

	abc->ab_specific.carddav.probe = gtk_button_new_from_icon_name("system-run", GTK_ICON_SIZE_MENU);
	g_signal_connect(abc->ab_specific.carddav.probe, "clicked", G_CALLBACK(carddav_run_probe), abc);
	gtk_button_set_label(GTK_BUTTON(abc->ab_specific.carddav.probe), _("probe…"));
	gtk_button_set_always_show_image(GTK_BUTTON(abc->ab_specific.carddav.probe), TRUE);
	gtk_grid_attach(GTK_GRID(grid), abc->ab_specific.carddav.probe, 0, 4, 2, 1);

	label = libbalsa_create_grid_label(_("_CardDAV address book name:"), grid, 5);
	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	abc->ab_specific.carddav.remote_name = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);
	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(abc->ab_specific.carddav.remote_name), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(abc->ab_specific.carddav.remote_name), renderer, "text", 0, NULL);
	gtk_grid_attach(GTK_GRID(grid), abc->ab_specific.carddav.remote_name, 1, 5, 1, 1);

	label = libbalsa_create_grid_label(_("_Refresh period:"), grid, 6);
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	abc->ab_specific.carddav.refresh_period = gtk_spin_button_new_with_range(1.0, 30.0, 1.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(abc->ab_specific.carddav.refresh_period),
		(ab != NULL) ? libbalsa_address_book_carddav_get_refresh(ab) : 30.0);
	gtk_spin_button_set_digits(GTK_SPIN_BUTTON(abc->ab_specific.carddav.refresh_period), 0);
	gtk_container_add(GTK_CONTAINER(box), abc->ab_specific.carddav.refresh_period);
	gtk_container_add(GTK_CONTAINER(box), gtk_label_new(_("minutes")));
	gtk_grid_attach(GTK_GRID(grid), box, 1, 6, 1, 1);

	abc->ab_specific.carddav.force_mget = libbalsa_create_grid_check(_("Force _Multiget for non-standard server"), grid, 7,
		(ab != NULL) ? libbalsa_address_book_carddav_get_force_mget(ab) : FALSE);

	add_radio_buttons(grid, 8, abc);
	gtk_widget_show(grid);
	dialog = create_generic_dialog(abc, "CardDAV");
	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY, ab != NULL);
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
	if (ab != NULL) {
		carddav_run_probe(NULL, abc);
	}
	carddav_dlg_widget_active(NULL, abc);

	return dialog;
}
#endif	/* HAVE_WEBDAV */

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
 *              FALSE   if an error was detected and the user wants to
 *                      correct it.
 */
static gboolean
handle_close(AddressBookConfig * abc)
{
    gboolean ok = TRUE;

    if (abc->address_book == NULL)
        ok = create_book(abc);
    else
        modify_book(abc);

    return ok;
}

static gboolean
create_book(AddressBookConfig * abc)
{
    LibBalsaAddressBook *address_book = NULL;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(abc->name_entry));

    if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_VCARD) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
        address_book = libbalsa_address_book_vcard_new(name, path);
        g_free(path);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ) {
        gchar *load =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->ab_specific.externq.load));
        gchar *save =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->ab_specific.externq.save));
        address_book = libbalsa_address_book_externq_new(name, load, save);
        g_free(load);
        g_free(save);
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_LDIF) {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(abc->window));
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
#ifdef HAVE_OSMO
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_OSMO) {
    	address_book = libbalsa_address_book_osmo_new(name);
#endif
#if HAVE_WEBDAV
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV) {
		GtkTreeIter iter;
		gchar *carddav_name;
		gchar *carddav_uri;

		gtk_combo_box_get_active_iter(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name), &iter);
		gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name)), &iter,
			0, &carddav_name, 1, &carddav_uri, -1);
		address_book = libbalsa_address_book_carddav_new(name,
			gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.domain_url)),
			gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.user)),
			gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.passwd)),
			carddav_uri,
			carddav_name,
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(abc->ab_specific.carddav.refresh_period)),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->ab_specific.carddav.force_mget)));
		g_free(carddav_name);
		g_free(carddav_uri);
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
#if HAVE_WEBDAV
    } else if (abc->type == LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV) {
		LibBalsaAddressBookCarddav *carddav;
		GtkTreeIter iter;
		gchar *carddav_name;
		gchar *carddav_uri;

		gtk_combo_box_get_active_iter(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name), &iter);
		gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(abc->ab_specific.carddav.remote_name)), &iter,
			0, &carddav_name, 1, &carddav_uri, -1);
		carddav = LIBBALSA_ADDRESS_BOOK_CARDDAV(address_book);
		libbalsa_address_book_carddav_set_base_dom_url(carddav, gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.domain_url)));
		libbalsa_address_book_carddav_set_user(carddav, gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.user)));
		libbalsa_address_book_carddav_set_password(carddav, gtk_entry_get_text(GTK_ENTRY(abc->ab_specific.carddav.passwd)));
		libbalsa_address_book_carddav_set_full_url(carddav, carddav_uri);
		libbalsa_address_book_carddav_set_carddav_name(carddav, carddav_name);
		libbalsa_address_book_carddav_set_refresh(carddav,
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(abc->ab_specific.carddav.refresh_period)));
		libbalsa_address_book_carddav_set_force_mget(carddav,
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abc->ab_specific.carddav.force_mget)));
		g_free(carddav_name);
		g_free(carddav_uri);
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
add_vcard_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    abc->window = create_vcard_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_externq_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ;
    abc->window = create_externq_dialog(abc);
    gtk_widget_show_all(abc->window);
}

static void
add_ldif_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
    abc->window = create_ldif_dialog(abc);
    gtk_widget_show_all(abc->window);
}

#ifdef ENABLE_LDAP
static void
add_ldap_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
    abc->window = create_ldap_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
static void
add_gpe_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
    abc->window = create_gpe_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_SQLITE */

#ifdef HAVE_OSMO
static void
add_osmo_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_OSMO;
    abc->window = create_osmo_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_OSMO */

#ifdef HAVE_WEBDAV
static void
add_carddav_cb(GtkWidget * widget, AddressBookConfig * abc)
{
    abc->type = LIBBALSA_TYPE_ADDRESS_BOOK_CARDDAV;
    abc->window = create_carddav_dialog(abc);
    gtk_widget_show_all(abc->window);
}
#endif /* HAVE_WEBDAV */

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
        gtk_menu_item_new_with_label(_("vCard Address Book (GnomeCard)"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_vcard_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem =
        gtk_menu_item_new_with_label(_("External query (a program)"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_externq_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("LDIF Address Book"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_ldif_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

#ifdef ENABLE_LDAP
    menuitem = gtk_menu_item_new_with_label(_("LDAP Address Book"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_ldap_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* ENABLE_LDAP */

#ifdef HAVE_SQLITE
    menuitem = gtk_menu_item_new_with_label(_("GPE Address Book"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_gpe_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif /* HAVE_SQLITE */

#ifdef HAVE_OSMO
    menuitem = gtk_menu_item_new_with_label(_("Osmo Address Book"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_osmo_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif

#ifdef HAVE_WEBDAV
    menuitem = gtk_menu_item_new_with_label(_("CardDAV Address Book"));
    g_signal_connect(menuitem, "activate",
                     G_CALLBACK(add_carddav_cb), abc);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
#endif

    return menu;
}
