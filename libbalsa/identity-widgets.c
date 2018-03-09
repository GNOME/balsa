/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "identity-widgets.h"

#include <glib/gi18n.h>
#include "libbalsa-conf.h"
#include "libbalsa-gpgme.h"
#include "misc.h"
#include "smtp-server.h"

/* Tree columns: */
enum {
    DEFAULT_COLUMN,
    NAME_COLUMN,
    IDENT_COLUMN,
    N_COLUMNS
};

/* Widget padding: */
static const guint padding = 6;

/*
 * Widget helpers
 */

static LibBalsaIdentity *
get_selected_identity(GtkTreeView * tree)
{
    GtkTreeSelection *select = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    LibBalsaIdentity *identity = NULL;

    if (gtk_tree_selection_get_selected(select, &model, &iter))
        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &identity, -1);

    return identity;
}

static gint
compare_identities(LibBalsaIdentity *id1, LibBalsaIdentity *id2)
{
    return g_ascii_strcasecmp(id1->identity_name, id2->identity_name);
}

static gboolean
select_identity(GtkTreeView * tree, LibBalsaIdentity * identity)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    gboolean valid;

    for (valid = gtk_tree_model_get_iter_first(model, &iter);
         valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaIdentity *tmp;

        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &tmp, -1);
        if (identity == tmp) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
            gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
            gtk_tree_path_free(path);

            return TRUE;
        }
    }

    return FALSE;
}

static void
identity_list_update_real(GtkTreeView * tree,
                          GList * identities,
                          LibBalsaIdentity * default_id)
{
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    GList *sorted, *list;
    LibBalsaIdentity *current;
    GtkTreeIter iter;

    current = get_selected_identity(tree);

    gtk_list_store_clear(store);

    sorted = g_list_sort(g_list_copy(identities),
                         (GCompareFunc) compare_identities);
    for (list = sorted; list != NULL; list = list->next) {
        LibBalsaIdentity* ident = LIBBALSA_IDENTITY(list->data);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           DEFAULT_COLUMN, ident == default_id,
                           NAME_COLUMN, ident->identity_name,
                           IDENT_COLUMN, ident,
                           -1);
    }
    g_list_free(sorted);

    if (!select_identity(tree, current))
        select_identity(tree, default_id);
}

/*
 * Common code for making a GtkTreeView list of identities:
 *
 * toggled_cb           callback for the "toggled" signal of the boolean
 *                      column;
 * toggled_data         user_data for the callback;
 * toggled_title        title for the boolean column.
 */
static GtkWidget *
libbalsa_identity_tree(GCallback toggled_cb, gpointer toggled_data,
                       gchar * toggled_title)
{
    GtkListStore *store;
    GtkWidget *tree;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    store = gtk_list_store_new(N_COLUMNS,
                               G_TYPE_BOOLEAN,
                               G_TYPE_STRING,
                               G_TYPE_POINTER);

    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect_swapped(renderer, "toggled",
                             toggled_cb, toggled_data);
    column =
        gtk_tree_view_column_new_with_attributes(toggled_title, renderer,
                                                 "radio", DEFAULT_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer,
                                                      "text", NAME_COLUMN,
                                                      NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    return tree;
}

/*
 * The Select Identity dialog; called from compose window.
 */

/* Info passed to callbacks: */
struct SelectDialogInfo_ {
    LibBalsaIdentityCallback update;
    gpointer data;
    GtkWidget *tree;
    GtkWidget *dialog;
    GtkWindow *parent;
    guint idle_handler_id;
};
typedef struct SelectDialogInfo_ SelectDialogInfo;

/* Forward references: */
static void sd_destroy_notify(SelectDialogInfo * sdi);
static void sd_response_cb(GtkWidget * dialog, gint response,
                           SelectDialogInfo * sdi);
static void sd_idle_add_response_ok(SelectDialogInfo * sdi);
static gboolean sd_response_ok(SelectDialogInfo * sdi);

/*
 * Public method: create and show the dialog.
 */
#define LIBBALSA_IDENTITY_SELECT_DIALOG_KEY "libbalsa-identity-select-dialog"
void
libbalsa_identity_select_dialog(GtkWindow * parent,
                                const gchar * prompt,
                                GList * identities,
                                LibBalsaIdentity * initial_id,
                                LibBalsaIdentityCallback update,
                                gpointer data)
{
    GtkWidget *dialog;
    GtkWidget *tree;
    SelectDialogInfo *sdi;
    GtkWidget *frame;

    /* Show only one dialog at a time. */
    sdi = g_object_get_data(G_OBJECT(parent),
                            LIBBALSA_IDENTITY_SELECT_DIALOG_KEY);
    if (sdi) {
        gtk_window_present(GTK_WINDOW(sdi->dialog));
        return;
    }

    sdi = g_new(SelectDialogInfo, 1);
    sdi->parent = parent;
    g_object_set_data_full(G_OBJECT(parent),
                           LIBBALSA_IDENTITY_SELECT_DIALOG_KEY,
                           sdi, (GDestroyNotify) sd_destroy_notify);
    sdi->update = update;
    sdi->data = data;
    sdi->idle_handler_id = 0;
    sdi->dialog = dialog =
        gtk_dialog_new_with_buttons(prompt, parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif

    g_signal_connect(dialog, "response",
                     G_CALLBACK(sd_response_cb), sdi);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    sdi->tree = tree =
        libbalsa_identity_tree(G_CALLBACK(sd_idle_add_response_ok), sdi,
                               _("Current"));
    g_signal_connect_swapped(tree, "row-activated",
                             G_CALLBACK(sd_idle_add_response_ok), sdi);
    identity_list_update_real(GTK_TREE_VIEW(tree), identities, initial_id);

    frame = gtk_frame_new(NULL);
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       frame);

    g_object_set(G_OBJECT(tree), "margin", padding, NULL);
    gtk_container_add(GTK_CONTAINER(frame), tree);

    gtk_widget_show(dialog);
    gtk_widget_grab_focus(tree);
}

/* GDestroyNotify for sdi. */
static void
sd_destroy_notify(SelectDialogInfo * sdi)
{
    libbalsa_clear_source_id(&sdi->idle_handler_id);
    g_free(sdi);
}

/* Callback for the dialog's "response" signal. */
static void
sd_response_cb(GtkWidget * dialog, gint response, SelectDialogInfo * sdi)
{
    if (response == GTK_RESPONSE_OK) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW(sdi->tree));
        GtkTreeModel *model;
        GtkTreeIter iter;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            LibBalsaIdentity *identity;

            gtk_tree_model_get(model, &iter, IDENT_COLUMN, &identity, -1);
            sdi->update(sdi->data, identity);
        }
    }

    /* Clear the data set on the parent window, so we know that the
     * dialog was destroyed. This will also trigger the GDestroyNotify
     * function, sd_destroy_notify.
     */
    g_object_set_data(G_OBJECT(sdi->parent),
                      LIBBALSA_IDENTITY_SELECT_DIALOG_KEY,
                      NULL);

    gtk_widget_destroy(dialog);
}

/* Helper for adding idles. */
static void
sd_idle_add_response_ok(SelectDialogInfo * sdi)
{
    if (!sdi->idle_handler_id)
        sdi->idle_handler_id =
            g_idle_add((GSourceFunc) sd_response_ok, sdi);
}

/* Idle handler for sending the OK response to the dialog. */
static gboolean
sd_response_ok(SelectDialogInfo * sdi)
{
    if (sdi->idle_handler_id) {
        sdi->idle_handler_id = 0;
        gtk_dialog_response(GTK_DIALOG(sdi->dialog), GTK_RESPONSE_OK);
    }
    return FALSE;
}

/*
 * End of the Select Identity dialog
 */

/*
 * The Manage Identities dialog; called from main window.
 */

typedef struct {
    GtkTreeView *tree;
    GtkWidget *dialog;
} IdentityDeleteInfo;

/* button actions */
static gboolean close_cb(GObject * dialog);
static void new_ident_cb(GtkTreeView * tree, GObject * dialog);
static void delete_ident_cb(GtkTreeView * tree, GtkWidget * dialog);
static void delete_ident_response(GtkWidget * confirm, gint response,
                                  IdentityDeleteInfo * di);
static void help_ident_cb(GtkWidget * widget);

static void set_default_ident_cb(GtkTreeView * tree, GtkTreePath * path,
                                 GtkTreeViewColumn * column,
                                 gpointer data);
static void config_frame_button_select_cb(GtkTreeSelection * selection,
                                          GtkDialog * dialog);

static void ident_dialog_add_checkbutton(GtkWidget *, gint, GtkDialog *,
                                         const gchar *, const gchar *,
					 gboolean sensitive);
static void ident_dialog_add_check_and_entry(GtkWidget *, gint, GtkDialog *,
                                             const gchar *, const gchar *);
static void ident_dialog_add_entry(GtkWidget *, gint, GtkDialog *,
                                   const gchar *, const gchar *);
static void ident_dialog_add_keysel_entry(GtkWidget   *grid,
							  	  	  	  gint         row,
										  GtkDialog   *dialog,
										  const gchar *label_name,
										  const gchar *entry_key);
typedef enum LibBalsaIdentityPathType_ {
    LBI_PATH_TYPE_FACE,
    LBI_PATH_TYPE_XFACE
} LibBalsaIdentityPathType;
static void ident_dialog_add_file_chooser_button(GtkWidget * grid,
                                                 gint row,
                                                 GtkDialog * dialog,
                                                 LibBalsaIdentityPathType
                                                 type);
static void ident_dialog_add_boxes(GtkWidget * grid, gint row,
                                   GtkDialog * dialog, const gchar * key1,
                                   const gchar * key2);
static const gchar *ident_dialog_get_text(GObject *, const gchar *);
static gboolean ident_dialog_get_bool(GObject *, const gchar *);
static gchar *ident_dialog_get_path(GObject * dialog, const gchar * key);
static gboolean ident_dialog_update(GObject *);
static void config_dialog_select(GtkTreeSelection * selection,
                                 GtkDialog * dialog);

static void display_frame_update(GObject * dialog, LibBalsaIdentity* ident);
static void display_frame_set_field(GObject * dialog, const gchar* key,
                                    const gchar* value);
static void display_frame_set_boolean(GObject * dialog, const gchar* key,
                                      gboolean value);
static void display_frame_set_path(GObject * dialog, const gchar * key,
                                   const gchar * value, gboolean use_chooser);


static void identity_list_update(GtkTreeView * tree);
static void set_identity_name_in_tree(GtkTreeView * tree,
				      LibBalsaIdentity * identity,
				      const gchar * name);
static void md_response_cb(GtkWidget * dialog, gint response,
                           GtkTreeView * tree);
static void md_name_changed(GtkEntry * name, GtkTreeView * tree);

static void ident_dialog_add_gpg_menu(GtkWidget * grid, gint row,
                                      GtkDialog * dialog,
                                      const gchar * label_name,
                                      const gchar * menu_key);
static void add_show_menu(const char *label, gpointer data,
                          GtkWidget * menu);
static void ident_dialog_free_values(GPtrArray * values);

static void display_frame_set_gpg_mode(GObject * dialog,
                                       const gchar * key, gint * value);

static void ident_dialog_add_smtp_menu(GtkWidget * grid, gint row,
                                       GtkDialog * dialog,
                                       const gchar * label_name,
                                       const gchar * menu_key,
				       GSList * smtp_servers);
static void display_frame_set_server(GObject * dialog,
                                     const gchar * key,
                                     LibBalsaSmtpServer * smtp_server);

static gpointer ident_dialog_get_value(GObject * dialog,
                                       const gchar * key);

/* Callback for the "toggled" signal of the "Default" column. */
static void
toggle_cb(GObject * dialog, gchar * path)
{
    GtkTreeView *tree = g_object_get_data(dialog, "tree");
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;

    /* Save any changes to current identity; if it's not valid, just
     * return. */
    if (!ident_dialog_update(dialog))
	return;

    if (gtk_tree_model_get_iter_from_string(model, &iter, path)) {
        LibBalsaIdentity *identity, **default_id;

        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &identity, -1);
        default_id = g_object_get_data(G_OBJECT(tree), "default-id");
        *default_id = identity;
        identity_list_update(tree);
    }
}

/*
 * Create and return a frame containing a list of the identities in
 * the application and a number of buttons to edit, create, and delete
 * identities.  Also provides a way to set the default identity.
 */
static GtkWidget*
libbalsa_identity_config_frame(GList** identities,
			       LibBalsaIdentity** defid, GtkWidget * dialog,
                               void (*cb)(gpointer), gpointer data)
{
    GtkWidget* config_frame = gtk_frame_new(NULL);
    GtkWidget *tree;

    tree = libbalsa_identity_tree(G_CALLBACK(toggle_cb), dialog,
                                  _("Default"));
    g_signal_connect(tree, "row-activated",
                     G_CALLBACK(set_default_ident_cb), NULL);
    g_object_set_data(G_OBJECT(tree), "identities", identities);
    g_object_set_data(G_OBJECT(tree), "default-id", defid);
    g_object_set_data(G_OBJECT(tree), "callback", cb);
    g_object_set_data(G_OBJECT(tree), "cb-data",  data);

    g_object_set(G_OBJECT(tree), "margin", 0, NULL); /* Seriously? */
    gtk_container_add(GTK_CONTAINER(config_frame), tree);

    identity_list_update(GTK_TREE_VIEW(tree));

    return config_frame;
}

/* identity_list_update:
 * Update the list of identities in the config frame, displaying the
 * available identities in the application, and which is default.
 */
static void
identity_list_update(GtkTreeView * tree)
{
    GList **identities =
        g_object_get_data(G_OBJECT(tree), "identities");
    LibBalsaIdentity **default_id =
        g_object_get_data(G_OBJECT(tree), "default-id");

    identity_list_update_real(tree, *identities, *default_id);
}

enum {
    IDENTITY_RESPONSE_HELP = GTK_RESPONSE_HELP,
    IDENTITY_RESPONSE_CLOSE = GTK_RESPONSE_CANCEL,
    IDENTITY_RESPONSE_NEW,
    IDENTITY_RESPONSE_REMOVE
};

/* callback for the "changed" signal */
static void
config_frame_button_select_cb(GtkTreeSelection * selection,
                              GtkDialog * dialog)
{
    config_dialog_select(selection, dialog);
}

/*
 * Callback for the close button.
 * Call ident_dialog_update to save any changes, and close the dialog if
 * OK.
 */
static gboolean
close_cb(GObject * dialog)
{
    return ident_dialog_update(dialog);
}

/*
 * Create a new identity
 */
static void
new_ident_cb(GtkTreeView * tree, GObject * dialog)
{
    LibBalsaIdentity *ident;
    GList **identities;
    GtkWidget *name_entry;
    void (*cb)(gpointer) = g_object_get_data(G_OBJECT(tree), "callback");
    gpointer data        = g_object_get_data(G_OBJECT(tree), "cb-data");

    /* Save any changes to current identity; if it's not valid, just
     * return. */
    if (!ident_dialog_update(dialog))
	return;

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    identities = g_object_get_data(G_OBJECT(tree), "identities");
    *identities = g_list_append(*identities, ident);
    identity_list_update(tree);
    /* select just added identity */
    select_identity(tree, ident);

    name_entry = g_object_get_data(dialog, "identity-name");
    gtk_widget_grab_focus(name_entry);
    cb(data);
}


/*
 * Helper: append a notebook page containing a table
 */
static GtkWidget*
append_ident_notebook_page(GtkNotebook * notebook,
			   const gchar * tab_label,
                           const gchar * footnote)
{
    GtkWidget *vbox;
    GtkWidget *grid;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    grid = libbalsa_create_grid();
    g_object_set(G_OBJECT(grid), "margin", padding, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), grid);
    if (footnote) {
	GtkWidget *label;

	label = gtk_label_new(footnote);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), label);
    }
    gtk_notebook_append_page(notebook, vbox, gtk_label_new(tab_label));

    return grid;
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static const struct {
    const gchar *mnemonic;
    const gchar *path_key;
    const gchar *box_key;
    const gchar *basename;
    const gchar *info;
} path_info[] = {
        /* Translators: please do not translate Face. */
    {N_("_Face Path"),
     "identity-facepath",
     "identity-facebox",
     ".face",
     "Face"},
        /* Translators: please do not translate Face. */
    {N_("_X-Face Path"),
     "identity-xfacepath",
     "identity-xfacebox",
     ".xface",
     "X-Face"}
};

#define LIBBALSA_IDENTITY_CHECK "libbalsa-identity-check"
static void md_sig_path_changed_cb(GtkToggleButton * sig_button,
                                   GObject * dialog);

static GtkWidget*
setup_ident_frame(GtkDialog * dialog, gboolean createp, gpointer tree,
                  GSList * smtp_servers)
{
    GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
    GtkWidget *grid;
    gint row;
    GObject *name;
    gpointer path;
    gchar *footnote;

    /* create the "General" tab */
    grid = append_ident_notebook_page(notebook, _("General"), NULL);
    row = 0;
    ident_dialog_add_entry(grid, row++, dialog, _("_Identity name:"),
		           "identity-name");
    ident_dialog_add_entry(grid, row++, dialog, _("_Full name:"),
                           "identity-fullname");
    ident_dialog_add_entry(grid, row++, dialog, _("_Mailing address:"),
                           "identity-address");
    ident_dialog_add_entry(grid, row++, dialog, _("Reply _to:"),
                           "identity-replyto");
    ident_dialog_add_entry(grid, row++, dialog, _("_Domain:"),
                           "identity-domain");

    /* create the "Messages" tab */
    grid = append_ident_notebook_page(notebook, _("Messages"), NULL);
    row = 0;
    ident_dialog_add_entry(grid, row++, dialog, _("_BCC:"),
                           "identity-bcc");
    ident_dialog_add_entry(grid, row++, dialog, _("Reply _string:"),
                           "identity-replystring");
    ident_dialog_add_entry(grid, row++, dialog, _("F_orward string:"),
                           "identity-forwardstring");
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("send messages in both plain text and _HTML format"),
                                 "identity-sendmpalternative", TRUE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("request positive (successful)"
                                   " _Delivery Status Notification by default"),
                                 "identity-requestdsn", TRUE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("request _Message Disposition Notification by default"),
                                 "identity-requestmdn", TRUE);
    ident_dialog_add_file_chooser_button(grid, row++, dialog,
                                         LBI_PATH_TYPE_FACE);
    ident_dialog_add_file_chooser_button(grid, row++, dialog,
                                         LBI_PATH_TYPE_XFACE);
    ident_dialog_add_boxes(grid, row++, dialog,
                           path_info[LBI_PATH_TYPE_FACE].box_key,
                           path_info[LBI_PATH_TYPE_XFACE].box_key);
    ident_dialog_add_smtp_menu(grid, row++, dialog, _("SMT_P server:"),
                               "identity-smtp-server", smtp_servers);

    /* create the "Signature" tab */
    grid = append_ident_notebook_page(notebook, _("Signature"), NULL);
    row = 0;
    ident_dialog_add_check_and_entry(grid, row++, dialog,
                                     _("Signature _path"),
                                     "identity-sigpath");
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                _("_Execute signature"),
				 "identity-sigexecutable", FALSE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("Incl_ude signature"),
                                 "identity-sigappend", FALSE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("Include signature when for_warding"),
                                 "identity-whenforward", FALSE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("Include signature when rep_lying"),
                                 "identity-whenreply", FALSE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("_Add signature separator"),
                                 "identity-sigseparator", FALSE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("Prepend si_gnature"),
                                 "identity-sigprepend", FALSE);

#ifdef HAVE_GPGME
    footnote = NULL;
#else
    footnote = _("Signing and encrypting messages are possible "
                 "only if Balsa is built with cryptographic support.");
#endif
    /* create the "Security" tab */
    grid =
        append_ident_notebook_page(notebook, _("Security"), footnote);
    row = 0;
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("sign messages by default"),
                                 "identity-gpgsign", TRUE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("encrypt messages by default"),
                                 "identity-gpgencrypt", TRUE);
    ident_dialog_add_gpg_menu(grid, row++, dialog,
				 _("default protocol"),
				 "identity-crypt-protocol");
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("always trust GnuPG keys to encrypt messages"),
                                 "identity-trust-always", TRUE);
    ident_dialog_add_checkbutton(grid, row++, dialog,
                                 _("remind me if messages can be encrypted"),
                                 "identity-warn-send-plain", TRUE);
    ident_dialog_add_keysel_entry(grid, row++, dialog,
                           	   	  _("use secret key with this id for signing GnuPG messages\n"
                           	   		"(leave empty for automatic selection)"),
								  "identity-keyid");
    ident_dialog_add_keysel_entry(grid, row++, dialog,
                           	   	  _("use certificate with this id for signing S/MIME messages\n"
                           	   		"(leave empty for automatic selection)"),
								  "identity-keyid-sm");
#ifndef HAVE_GPGME
    gtk_widget_set_sensitive(grid, FALSE);
#endif

    name = g_object_get_data(G_OBJECT(dialog), "identity-name");
    g_signal_connect(name, "changed",
                     G_CALLBACK(md_name_changed), tree);

    path = g_object_get_data(G_OBJECT(dialog), "identity-sigpath");
    g_signal_connect(g_object_get_data(G_OBJECT(path),
                                       LIBBALSA_IDENTITY_CHECK),
                     "toggled",
                     G_CALLBACK(md_sig_path_changed_cb), dialog);

    gtk_notebook_set_current_page(notebook, 0);

    return GTK_WIDGET(notebook);
}

/* Callback for the "changed" signal of the name entry; updates the name
 * in the tree. */
static void
md_name_changed(GtkEntry * name, GtkTreeView * tree)
{
    set_identity_name_in_tree(tree, get_selected_identity(tree),
			      gtk_entry_get_text(name));
}

/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key.  The check
 * button is initialized to the given value.
 */
static void
ident_dialog_add_checkbutton(GtkWidget * grid, gint row,
                             GtkDialog * dialog, const gchar * check_label,
                             const gchar * check_key, gboolean sensitive)
{
    GtkWidget *check;

    check = libbalsa_create_grid_check(check_label, grid, row, FALSE);
    g_object_set_data(G_OBJECT(dialog), check_key, check);
    gtk_widget_set_sensitive(check, sensitive);
}

/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key, which is
 * followed by a text entry.  The check button is initialized to the
 * given value.
 */
static void
ident_dialog_add_check_and_entry(GtkWidget * grid, gint row,
                                 GtkDialog * dialog, const gchar * check_label,
                                 const gchar * entry_key)
{
    GtkWidget *check, *entry;

    check = gtk_check_button_new_with_mnemonic(check_label);
    entry = gtk_entry_new();


    gtk_grid_attach(GTK_GRID(grid), check, 0, row, 1, 1);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);

    g_object_set_data(G_OBJECT(dialog), entry_key, entry);
    g_object_set_data(G_OBJECT(entry), LIBBALSA_IDENTITY_CHECK, check);
}


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */
static void
ident_dialog_add_entry(GtkWidget * grid, gint row, GtkDialog * dialog,
                       const gchar * label_name, const gchar * entry_key)
{
    GtkWidget *label;
    GtkWidget *entry;

    label = libbalsa_create_grid_label(label_name, grid, row);

    entry = libbalsa_create_grid_entry(grid, NULL, NULL, row, NULL, label);

    g_object_set_data(G_OBJECT(dialog), entry_key, entry);
    if (row == 0)
        gtk_widget_grab_focus(entry);
}


#ifdef HAVE_GPGME
static void
choose_key(GtkButton *button, gpointer user_data)
{
	const gchar *target;
	gpgme_protocol_t protocol;
	const gchar *email;
	gchar *keyid;
	GError *error = NULL;

	target = g_object_get_data(G_OBJECT(button), "target");
	if (strcmp(target, "identity-keyid") == 0) {
		protocol = GPGME_PROTOCOL_OpenPGP;
	} else {
		protocol = GPGME_PROTOCOL_CMS;
	}

	email = ident_dialog_get_text(G_OBJECT(user_data), "identity-address");
	keyid = libbalsa_gpgme_get_seckey(protocol, email, GTK_WINDOW(user_data), &error);
	if (keyid != NULL) {
		display_frame_set_field(G_OBJECT(user_data), target, keyid);
		g_free(keyid);
	}
	if (error != NULL) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error selecting key: %s"), error->message);
        g_clear_error(&error);
	}
}
#endif


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  A button
 * is added behind the entry to choose a key.
 */
static void
ident_dialog_add_keysel_entry(GtkWidget   *grid,
							  gint         row,
							  GtkDialog   *dialog,
                       	   	  const gchar *label_name,
							  const gchar *entry_key)
{
	GtkWidget *button;

	ident_dialog_add_entry(grid, row, dialog, label_name, entry_key);
	button = gtk_button_new_with_label(_("Choose…"));
#ifdef HAVE_GPGME
	g_object_set_data_full(G_OBJECT(button), "target", g_strdup(entry_key), (GDestroyNotify) g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(choose_key), dialog);
#endif
	gtk_grid_attach(GTK_GRID(grid), button, 2, row, 1, 1);
}


/*
 * Add a GtkFileChooserButton to the given dialog with a label next to it
 * explaining the contents.  A reference to the button is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */

/* Callbacks and helpers. */
static void
file_chooser_check_cb(GtkToggleButton * button, GtkWidget * chooser)
{
    gtk_widget_set_sensitive(chooser,
                             gtk_toggle_button_get_active(button));
    /* Force validation of current path, if any. */
    g_signal_emit_by_name(chooser, "selection-changed");
}

static void
md_face_path_changed(const gchar * filename, gboolean active,
                     LibBalsaIdentityPathType type, gpointer data)
{
    gchar *content;
    gsize size;
    GError *err = NULL;
    GtkWidget *image;
    GtkWidget *face_box;

    face_box = g_object_get_data(G_OBJECT(data), path_info[type].box_key);
    if (!active) {
        gtk_widget_hide(face_box);
        return;
    }

    content = libbalsa_get_header_from_path(path_info[type].info,
                                            filename, &size, &err);

    if (err) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error reading file %s: %s"), filename,
                             err->message);
        g_error_free(err);
        gtk_widget_hide(face_box);
        return;
    }

    if (size > 998) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                /* Translators: please do not translate Face. */
                             _("Face header file %s is too long "
                               "(%lu bytes)."), filename, (unsigned long)size);
        g_free(content);
        gtk_widget_hide(face_box);
        return;
    }

    if (libbalsa_text_attr_string(content)) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                /* Translators: please do not translate Face. */
                             _("Face header file %s contains "
                               "binary data."), filename);
        g_free(content);
        return;
    }

    if (type == LBI_PATH_TYPE_FACE)
        image = libbalsa_get_image_from_face_header(content, &err);
#if HAVE_COMPFACE
    else if (type == LBI_PATH_TYPE_XFACE)
        image = libbalsa_get_image_from_x_face_header(content, &err);
#endif                          /* HAVE_COMPFACE */
    else {
        gtk_widget_hide(face_box);
        g_free(content);
        return;
    }
    if (err) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                /* Translators: please do not translate Face. */
                             _("Error loading Face: %s"), err->message);
        g_error_free(err);
        g_free(content);
        return;
    }

    gtk_container_foreach(GTK_CONTAINER(face_box),
                          (GtkCallback) gtk_widget_destroy, NULL);
    gtk_box_pack_start(GTK_BOX(face_box), image);

    g_free(content);
}

/* Callback for the "selection-changed" signal of the signature path
 * file chooser; sets sensitivity of the signature-related buttons. */

static void
md_sig_path_changed(gboolean active, GObject * dialog)
{
    guint i;
    static gchar *button_key[] = {
        "identity-sigexecutable",
        "identity-sigappend",
        "identity-whenforward",
        "identity-whenreply",
        "identity-sigseparator",
        "identity-sigprepend",
        "identity-sigpath",
    };

    for (i = 0; i < G_N_ELEMENTS(button_key); i++) {
        GtkWidget *button = g_object_get_data(dialog, button_key[i]);
        gtk_widget_set_sensitive(button, active);
    }
}

static void
md_sig_path_changed_cb(GtkToggleButton *sig_button, GObject *dialog)
{
    md_sig_path_changed(gtk_toggle_button_get_active(sig_button), dialog);
}

#define LIBBALSA_IDENTITY_INFO "libbalsa-identity-info"
static void
file_chooser_cb(GtkWidget * chooser, gpointer data)
{
    gchar *filename;
    LibBalsaIdentityPathType type;
    GtkToggleButton *check;
    gboolean active;

    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    if (!filename || !*filename) {
        g_free(filename);
        return;
    }

    type = GPOINTER_TO_UINT(g_object_get_data
                            (G_OBJECT(chooser), LIBBALSA_IDENTITY_INFO));
    check = g_object_get_data(G_OBJECT(chooser), LIBBALSA_IDENTITY_CHECK);
    active = gtk_toggle_button_get_active(check);

#if 0
    if (type == LBI_PATH_TYPE_SIG)
        md_sig_path_changed(filename, active, data);
    else
#endif
        md_face_path_changed(filename, active, type, data);
    g_free(filename);
}

static void
ident_dialog_add_file_chooser_button(GtkWidget * grid, gint row,
                                     GtkDialog * dialog,
                                     LibBalsaIdentityPathType type)
{
    GtkWidget *check;
    gchar *filename;
    gchar *title;
    GtkWidget *button;

    check =
        gtk_check_button_new_with_mnemonic(_(path_info[type].mnemonic));
    gtk_grid_attach(GTK_GRID(grid), check, 0, row, 1, 1);

    filename =
        g_build_filename(g_get_home_dir(), path_info[type].basename, NULL);
    title = g_strdup_printf("Choose %s file", _(path_info[type].info));
    button = gtk_file_chooser_button_new(title,
                                         GTK_FILE_CHOOSER_ACTION_OPEN);
    g_free(title);
    gtk_file_chooser_set_show_hidden(GTK_FILE_CHOOSER(button), TRUE);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(button), filename);
    g_free(filename);

    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_vexpand(button, FALSE);
    gtk_grid_attach(GTK_GRID(grid), button, 1, row, 1, 1);

    g_object_set_data(G_OBJECT(dialog), path_info[type].path_key, button);
    g_object_set_data(G_OBJECT(button), LIBBALSA_IDENTITY_CHECK, check);
    g_object_set_data(G_OBJECT(button), LIBBALSA_IDENTITY_INFO,
                      GUINT_TO_POINTER(type));
    g_signal_connect(check, "toggled",
                     G_CALLBACK(file_chooser_check_cb), button);
    g_signal_connect(button, "selection-changed",
                     G_CALLBACK(file_chooser_cb), dialog);
}

static void
ident_dialog_add_boxes(GtkWidget * grid, gint row, GtkDialog * dialog,
                       const gchar * key1, const gchar *key2)
{
    GtkWidget *hbox, *vbox;

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_grid_attach(GTK_GRID(grid), hbox, 1, row, 1, 1);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(dialog), key1, vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(dialog), key2, vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox);
}

/* set_identity_name_in_tree:
 * update the tree to reflect the (possibly) new name of the identity
 */
static void
set_identity_name_in_tree(GtkTreeView * tree, LibBalsaIdentity * identity,
                  const gchar * name)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    gboolean valid;

    for (valid =
         gtk_tree_model_get_iter_first(model, &iter);
         valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaIdentity *tmp;

        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &tmp, -1);
        if (identity == tmp) {
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               NAME_COLUMN, name, -1);
            break;
        }
    }
}

/*
 * Update the identity object associated with the edit/new dialog,
 * validating along the way.  Correct validation results in a true
 * return value, otherwise it returns false.
 */

static gboolean
ident_dialog_update(GObject * dlg)
{
    LibBalsaIdentity* id;
    LibBalsaIdentity* exist_ident;
    InternetAddress *ia;
    GtkWidget *tree;
    GList **identities, *list;
    const gchar *text;
    gchar *dup;
    gchar *path;

    id = g_object_get_data(dlg, "identity");
    if (!id)
        return TRUE;
    tree = g_object_get_data(dlg, "tree");
    identities = g_object_get_data(G_OBJECT(tree), "identities");

    text = ident_dialog_get_text(dlg, "identity-name");
    g_return_val_if_fail(text != NULL, FALSE);

    if (text[0] == '\0') {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Error: The identity does not have a name"));
        return FALSE;
    }

    for (list = *identities; list != NULL; list = list->next) {
        exist_ident = list->data;

        if (g_ascii_strcasecmp(exist_ident->identity_name, text) == 0
            && id != exist_ident) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                 _("Error: An identity with that"
                                   " name already exists"));
            return FALSE;
        }
    }

    libbalsa_identity_set_identity_name(id, text);
    set_identity_name_in_tree(GTK_TREE_VIEW(tree), id, text);

    text = ident_dialog_get_text(dlg, "identity-address");
    g_return_val_if_fail(text != NULL, FALSE);
    ia = internet_address_mailbox_new(NULL, text);
    internet_address_set_name(ia, ident_dialog_get_text(dlg, "identity-fullname"));
    libbalsa_identity_set_address(id, ia);
    g_object_unref(ia);

    libbalsa_identity_set_replyto(id,
            ident_dialog_get_text(dlg, "identity-replyto"));
    libbalsa_identity_set_domain(id,
            ident_dialog_get_text(dlg, "identity-domain"));
    libbalsa_identity_set_bcc(id,
            ident_dialog_get_text(dlg, "identity-bcc"));
    libbalsa_identity_set_reply_string(id,
            ident_dialog_get_text(dlg, "identity-replystring"));
    libbalsa_identity_set_forward_string(id,
            ident_dialog_get_text(dlg, "identity-forwardstring"));
    libbalsa_identity_set_send_mp_alternative(id,
            ident_dialog_get_bool(dlg, "identity-sendmpalternative"));
    libbalsa_identity_set_smtp_server(id,
            ident_dialog_get_value(dlg, "identity-smtp-server"));

    libbalsa_identity_set_signature_path(id,
            ident_dialog_get_text(dlg, "identity-sigpath"));
    libbalsa_identity_set_sig_executable(id,
            ident_dialog_get_bool(dlg, "identity-sigexecutable"));
    libbalsa_identity_set_sig_sending(id,
            ident_dialog_get_bool(dlg, "identity-sigappend"));
    libbalsa_identity_set_sig_whenforward(id,
            ident_dialog_get_bool(dlg, "identity-sigwhenforward"));
    libbalsa_identity_set_sig_whenreply(id,
            ident_dialog_get_bool(dlg, "identity-sigwhenreply"));
    libbalsa_identity_set_sig_separator(id,
            ident_dialog_get_bool(dlg, "identity-sigseparator"));
    libbalsa_identity_set_sig_prepend(id,
            ident_dialog_get_bool(dlg, "identity-sigprepend"));

    path = ident_dialog_get_path(dlg, "identity-facepath");
    libbalsa_identity_set_face_path(id, path);
    g_free(path);

    path = ident_dialog_get_path(dlg, "identity-xfacepath");
    libbalsa_identity_set_x_face_path(id, path);
    g_free(path);

    libbalsa_identity_set_request_mdn(id,
            ident_dialog_get_bool(dlg, "identity-requestmdn"));
    libbalsa_identity_set_request_dsn(id,
            ident_dialog_get_bool(dlg, "identity-requestdsn"));

    libbalsa_identity_set_gpg_sign(id,
            ident_dialog_get_bool(dlg, "identity-gpgsign"));
    libbalsa_identity_set_gpg_encrypt(id,
            ident_dialog_get_bool(dlg, "identity-gpgencrypt"));
    libbalsa_identity_set_always_trust(id,
            ident_dialog_get_bool(dlg, "identity-trust-always"));
    libbalsa_identity_set_warn_send_plain(id,
            ident_dialog_get_bool(dlg, "identity-warn-send-plain"));
    libbalsa_identity_set_crypt_protocol(id,
            GPOINTER_TO_INT(ident_dialog_get_value (dlg, "identity-crypt-protocol")));

    dup = g_strdup(ident_dialog_get_text(dlg, "identity-keyid"));
    libbalsa_identity_set_force_gpg_key_id(id, g_strstrip(dup));
    g_free(dup);

    dup = g_strdup(ident_dialog_get_text(dlg, "identity-keyid-sm"));
    libbalsa_identity_set_force_smime_key_id(id, g_strstrip(dup));
    g_free(dup);

    return TRUE;
}


/*
 * Get the text from an entry in the editing/creation dialog.  The
 * given key accesses the entry using object data.
 */
static const gchar *
ident_dialog_get_text(GObject * dialog, const gchar * key)
{
    GtkEntry *entry;
    GtkToggleButton *check;

    entry = g_object_get_data(dialog, key);
    check = g_object_get_data(G_OBJECT(entry), LIBBALSA_IDENTITY_CHECK);
    if (check && !gtk_toggle_button_get_active(check))
        return NULL;
    return gtk_entry_get_text(entry);
}


/*
 * Get the value of a check button from the editing dialog.  The key
 * is used to retreive the reference to the check button using object
 * data
 */
static gboolean
ident_dialog_get_bool(GObject* dialog, const gchar* key)
{
    GtkToggleButton *button;

    button = g_object_get_data(dialog, key);
    return gtk_toggle_button_get_active(button);
}


/*
 * Get the path from a file chooser in the editing/creation dialog.  The
 * given key accesses the file chooser using object data.
 */
static gchar *
ident_dialog_get_path(GObject * dialog, const gchar * key)
{
    GtkWidget *chooser;

    chooser = g_object_get_data(dialog, key);
    if (!gtk_widget_get_sensitive(chooser))
        return NULL;

    return gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
}


/*
 * Set the default identity to the currently selected.
 */
static void
set_default_ident_cb(GtkTreeView * tree, GtkTreePath * path,
                     GtkTreeViewColumn * column, gpointer data)
{
    LibBalsaIdentity *ident, **default_id;

    default_id = g_object_get_data(G_OBJECT(tree), "default-id");
    ident = get_selected_identity(tree);
    g_return_if_fail(ident != NULL);
    *default_id = ident;

    identity_list_update(tree);
}


/*
 * Confirm the deletion of an identity, do the actual deletion here,
 * and close the dialog.
 */
static void
identity_delete_selected(GtkTreeView * tree, GtkWidget * dialog)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    LibBalsaIdentity *ident;
    GList **identities;
    void (*cb)(gpointer) = g_object_get_data(G_OBJECT(tree), "callback");
    gpointer data        = g_object_get_data(G_OBJECT(tree), "cb-data");

    /* Save the path to the current row. */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    path = gtk_tree_model_get_path(model, &iter);

    ident = get_selected_identity(tree);
    identities = g_object_get_data(G_OBJECT(tree), "identities");
    *identities = g_list_remove(*identities, ident);
    g_object_set_data(G_OBJECT(dialog), "identity", NULL);
    identity_list_update(tree);
    g_object_unref(ident);

    /* Select the row at the saved path, or the previous one. */
    if (gtk_tree_model_get_iter(model, &iter, path)
        || gtk_tree_path_prev(path)) {
        gtk_tree_view_set_cursor(tree, path, NULL, FALSE);
        gtk_tree_view_scroll_to_cell(tree, path, NULL,
                                     FALSE, 0, 0);
    }
    gtk_tree_path_free(path);
    gtk_widget_grab_focus(GTK_WIDGET(tree));
    cb(data);
}

/*
 * Delete the currently selected identity after confirming.
 */
static void
delete_ident_cb(GtkTreeView * tree, GtkWidget * dialog)
{
    LibBalsaIdentity* ident, **default_id;
    GtkWidget* confirm;
    IdentityDeleteInfo *di;

    ident = get_selected_identity(tree);
    default_id = g_object_get_data(G_OBJECT(tree), "default-id");
    g_return_if_fail(ident != *default_id);
    confirm = gtk_message_dialog_new(GTK_WINDOW(dialog),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_OK_CANCEL,
                                     _("Do you really want to delete"
                                       " the selected identity?"));
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(confirm, GTK_WINDOW(dialog));
#endif
    di = g_new(IdentityDeleteInfo, 1);
    di->tree = tree;
    di->dialog = dialog;
    g_signal_connect(confirm, "response",
                     G_CALLBACK(delete_ident_response), di);
    g_object_weak_ref(G_OBJECT(confirm), (GWeakNotify) g_free, di);
    gtk_widget_set_sensitive(dialog, FALSE);
    gtk_widget_show(confirm);
}

static void
delete_ident_response(GtkWidget * confirm, gint response,
                      IdentityDeleteInfo * di)
{
    if(response == GTK_RESPONSE_OK)
        identity_delete_selected(di->tree, di->dialog);
    gtk_widget_set_sensitive(di->dialog, TRUE);
    gtk_widget_destroy(confirm);
}

/*
 * Show the help file.
 */
static void
help_ident_cb(GtkWidget * widget)
{
    GError *err = NULL;

    gtk_show_uri_on_window(GTK_WINDOW(widget), "help:balsa/identities",
                           gtk_get_current_event_time(), &err);

    if (err) {
        g_print(_("Error displaying help for identities: %s\n"),
                err->message);
        g_error_free(err);
    }
}

/* libbalsa_identity_config_dialog displays an identity management
   dialog. The dialog has a specified parent, existing list of
   identites, the default one. Additionally, a callback is passed that
   will be executed when the identity list is modified: new entries
   are added or other entries are removed. */
static void
lbi_free_smtp_server_list(GSList ** smtp_server_list)
{
    g_slist_free_full(*smtp_server_list, g_object_unref);
    g_free(smtp_server_list);
}

void
libbalsa_identity_config_dialog(GtkWindow *parent, GList **identities,
				LibBalsaIdentity **default_id, GSList * smtp_servers,
				void (*changed_cb)(gpointer))
{
    static GtkWidget *dialog = NULL;
    GtkWidget* frame;
    GtkWidget* display_frame;
    GtkWidget* hbox;
    GtkTreeView* tree;
    GtkTreeSelection *select;
    GSList **smtp_server_list;

    /* Show only one dialog at a time. */
    if (dialog) {
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }

    dialog =
        gtk_dialog_new_with_buttons(_("Manage Identities"),
                                    parent, /* must NOT be modal */
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_Help"),             IDENTITY_RESPONSE_HELP,
                                    /* Translators: button "New" identity */
                                    C_("identity", "_New"), IDENTITY_RESPONSE_NEW,
                                    _("_Remove"),           IDENTITY_RESPONSE_REMOVE,
                                    _("_Close"),            IDENTITY_RESPONSE_CLOSE,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif

    frame = libbalsa_identity_config_frame(identities, default_id, dialog,
                                           changed_cb, parent);
    tree = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(frame)));

    g_signal_connect(dialog, "response",
                     G_CALLBACK(md_response_cb), tree);
    g_object_set_data(G_OBJECT(dialog), "tree", tree);
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer) & dialog);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    IDENTITY_RESPONSE_CLOSE);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, padding);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), hbox);
    gtk_box_pack_start(GTK_BOX(hbox), frame);

    smtp_server_list = g_new(GSList *, 1);
    *smtp_server_list = g_slist_copy(smtp_servers);
    g_slist_foreach(smtp_servers, (GFunc) g_object_ref, NULL);
    display_frame = setup_ident_frame(GTK_DIALOG(dialog),
                                      FALSE, tree, smtp_servers);
    g_object_weak_ref(G_OBJECT(display_frame),
	              (GWeakNotify) lbi_free_smtp_server_list,
		      smtp_server_list);

    gtk_widget_set_hexpand(display_frame, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame);

    select = gtk_tree_view_get_selection(tree);
    g_signal_connect(select, "changed",
                     G_CALLBACK(config_frame_button_select_cb), dialog);
    config_dialog_select(select, GTK_DIALOG(dialog));

    gtk_widget_show(dialog);
    gtk_widget_grab_focus(GTK_WIDGET(tree));
}

/* Callback for the "response" signal of the dialog. */
static void
md_response_cb(GtkWidget * dialog, gint response, GtkTreeView * tree)
{
    switch (response) {
    case IDENTITY_RESPONSE_CLOSE:
        if (close_cb(G_OBJECT(dialog)))
            break;
        return;
    case IDENTITY_RESPONSE_NEW:
        new_ident_cb(tree, G_OBJECT(dialog));
        return;
    case IDENTITY_RESPONSE_REMOVE:
        delete_ident_cb(tree, dialog);
        return;
    case IDENTITY_RESPONSE_HELP:
        help_ident_cb(dialog);
        return;
    default:
        break;
    }

    gtk_widget_destroy(dialog);
}

/* config_dialog_select
 *
 * called when the tree's selection changes
 * manage the button sensitivity, and update the display frame
 */
static void
config_dialog_select(GtkTreeSelection * selection, GtkDialog * dialog)
{
    LibBalsaIdentity *ident, **default_id;
    GtkTreeView *tree = gtk_tree_selection_get_tree_view(selection);

    ident = get_selected_identity(tree);
    default_id = g_object_get_data(G_OBJECT(tree), "default-id");
    gtk_dialog_set_response_sensitive(dialog, IDENTITY_RESPONSE_REMOVE,
                                      ident && ident != *default_id);
    display_frame_update(G_OBJECT(dialog), ident);
    g_object_set_data(G_OBJECT(dialog), "identity", ident);
}

static void
display_frame_update(GObject * dialog, LibBalsaIdentity* ident)
{
    GtkWidget *face_box;

    if (!ident)
        return;

    ident_dialog_update(dialog);
    display_frame_set_field(dialog, "identity-name", ident->identity_name);
    display_frame_set_field(dialog, "identity-fullname", ident->ia ? ident->ia->name : NULL);
    if (ident->ia && INTERNET_ADDRESS_IS_MAILBOX (ident->ia))
        display_frame_set_field(dialog, "identity-address",
                                INTERNET_ADDRESS_MAILBOX(ident->ia)->addr);
    else
        display_frame_set_field(dialog, "identity-address", NULL);

    display_frame_set_field(dialog, "identity-replyto", ident->replyto);
    display_frame_set_field(dialog, "identity-domain", ident->domain);
    display_frame_set_field(dialog, "identity-bcc", ident->bcc);
    display_frame_set_field(dialog, "identity-replystring",
                            ident->reply_string);
    display_frame_set_field(dialog, "identity-forwardstring",
                            ident->forward_string);
    display_frame_set_boolean(dialog, "identity-sendmpalternative",
                              ident->send_mp_alternative);
    display_frame_set_server(dialog, "identity-smtp-server",
                             ident->smtp_server);

    display_frame_set_path(dialog, "identity-sigpath",
                           ident->signature_path, FALSE);
    display_frame_set_boolean(dialog, "identity-sigexecutable", ident->sig_executable);

    display_frame_set_boolean(dialog, "identity-sigappend", ident->sig_sending);
    display_frame_set_boolean(dialog, "identity-whenforward",
                              ident->sig_whenforward);
    display_frame_set_boolean(dialog, "identity-whenreply",
                              ident->sig_whenreply);
    display_frame_set_boolean(dialog, "identity-sigseparator",
                              ident->sig_separator);
    display_frame_set_boolean(dialog, "identity-sigprepend",
                              ident->sig_prepend);

    face_box = g_object_get_data(G_OBJECT(dialog),
                                 path_info[LBI_PATH_TYPE_FACE].box_key);
    gtk_widget_hide(face_box);
    display_frame_set_path(dialog, path_info[LBI_PATH_TYPE_FACE].path_key,
                           ident->face, TRUE);

    face_box = g_object_get_data(G_OBJECT(dialog),
                                 path_info[LBI_PATH_TYPE_XFACE].box_key);
    gtk_widget_hide(face_box);
    display_frame_set_path(dialog, path_info[LBI_PATH_TYPE_XFACE].path_key,
                           ident->x_face, TRUE);
    display_frame_set_boolean(dialog, "identity-requestmdn",
                              ident->request_mdn);
    display_frame_set_boolean(dialog, "identity-requestdsn",
                              ident->request_dsn);

    display_frame_set_boolean(dialog, "identity-gpgsign",
                              ident->gpg_sign);
    display_frame_set_boolean(dialog, "identity-gpgencrypt",
                              ident->gpg_encrypt);
    display_frame_set_boolean(dialog, "identity-trust-always",
                              ident->always_trust);
    display_frame_set_boolean(dialog, "identity-warn-send-plain",
                              ident->warn_send_plain);
    display_frame_set_gpg_mode(dialog, "identity-crypt-protocol",
			   &ident->crypt_protocol);
    display_frame_set_field(dialog, "identity-keyid", ident->force_gpg_key_id);
    display_frame_set_field(dialog, "identity-keyid-sm", ident->force_smime_key_id);
}


static void
display_frame_set_field(GObject * dialog,
                        const gchar* key,
                        const gchar* value)
{
    GtkEntry *entry = g_object_get_data(dialog, key);

    gtk_entry_set_text(entry, value ? value : "");
}

static void
display_frame_set_boolean(GObject * dialog,
                          const gchar* key,
                          gboolean value)
{
    GtkToggleButton *check = g_object_get_data(dialog, key);

    gtk_toggle_button_set_active(check, value);
}

static void
display_frame_set_path(GObject * dialog,
                       const gchar* key,
                       const gchar* value, gboolean use_chooser)
{
    gboolean set = (value && *value);
    GtkWidget *chooser = g_object_get_data(dialog, key);
    GtkToggleButton *check =
        g_object_get_data(G_OBJECT(chooser), LIBBALSA_IDENTITY_CHECK);

    if (set) {
        if(use_chooser)
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), value);
        else
            gtk_entry_set_text(GTK_ENTRY(chooser), value);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(chooser), set);
    gtk_toggle_button_set_active(check, set);
}


static void
display_frame_set_gpg_mode(GObject * dialog, const gchar* key, gint * value)
{
    GtkComboBox *opt_menu = g_object_get_data(G_OBJECT(dialog), key);

    switch (*value)
        {
        case LIBBALSA_PROTECT_OPENPGP:
	    gtk_combo_box_set_active(opt_menu, 1);
            break;
        case LIBBALSA_PROTECT_SMIMEV3:
	    gtk_combo_box_set_active(opt_menu, 2);
            break;
        case LIBBALSA_PROTECT_RFC3156:
        default:
	    gtk_combo_box_set_active(opt_menu, 0);
            *value = LIBBALSA_PROTECT_RFC3156;
        }
}

/*
 * Add an option menu to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.
 */

static void
ident_dialog_add_gpg_menu(GtkWidget * grid, gint row, GtkDialog * dialog,
                          const gchar * label_name, const gchar * menu_key)
{
    GtkWidget *label;
    GtkWidget *opt_menu;
    GPtrArray *values;

    label = gtk_label_new_with_mnemonic(label_name);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    opt_menu = gtk_combo_box_text_new();
    values = g_ptr_array_sized_new(3);
    g_object_set_data_full(G_OBJECT(opt_menu), "identity-value", values,
                           (GDestroyNotify) ident_dialog_free_values);
    gtk_grid_attach(GTK_GRID(grid), opt_menu, 1, row, 1, 1);
    g_object_set_data(G_OBJECT(dialog), menu_key, opt_menu);

    add_show_menu(_("GnuPG MIME mode"),
                  GINT_TO_POINTER(LIBBALSA_PROTECT_RFC3156), opt_menu);
    add_show_menu(_("GnuPG OpenPGP mode"),
                  GINT_TO_POINTER(LIBBALSA_PROTECT_OPENPGP), opt_menu);
    add_show_menu(_("GpgSM S/MIME mode"),
                  GINT_TO_POINTER(LIBBALSA_PROTECT_SMIMEV3), opt_menu);
}

/* add_show_menu: helper function */
static void
add_show_menu(const char *label, gpointer data, GtkWidget * menu)
{
    GPtrArray *values =
        g_object_get_data(G_OBJECT(menu), "identity-value");

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(menu), label);
    g_ptr_array_add(values, data);
}

/* ident_dialog_free_values: helper function */
static void
ident_dialog_free_values(GPtrArray * values)
{
    g_ptr_array_free(values, TRUE);
}

static void
ident_dialog_add_smtp_menu(GtkWidget * grid, gint row, GtkDialog * dialog,
                           const gchar * label_name,
                           const gchar * menu_key, GSList * smtp_servers)
{
    GtkWidget *label;
    GtkWidget *combo_box;
    GSList *list;
    GPtrArray *values;

    label = gtk_label_new_with_mnemonic(label_name);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

    combo_box = gtk_combo_box_text_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), combo_box);
    values = g_ptr_array_sized_new(g_slist_length(smtp_servers));
    g_object_set_data_full(G_OBJECT(combo_box), "identity-value", values,
                           (GDestroyNotify) ident_dialog_free_values);
    gtk_grid_attach(GTK_GRID(grid), combo_box, 1, row, 1, 1);
    g_object_set_data(G_OBJECT(dialog), menu_key, combo_box);

    for (list = smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(list->data);
        add_show_menu(libbalsa_smtp_server_get_name(smtp_server),
                      smtp_server, combo_box);
    }
}

static void
display_frame_set_server(GObject * dialog, const gchar * key,
                         LibBalsaSmtpServer * smtp_server)
{
    GtkComboBox *combo_box = g_object_get_data(G_OBJECT(dialog), key);
    GPtrArray *values;
    guint i;

    values = g_object_get_data(G_OBJECT(combo_box), "identity-value");

    for (i = 0; i < values->len; i++) {
        if (g_ptr_array_index(values, i) == smtp_server)
            gtk_combo_box_set_active(combo_box, i);
    }
}

/*
 * Get the value of the active option menu item
 */
static gpointer
ident_dialog_get_value(GObject * dialog, const gchar * key)
{
    GtkWidget * menu;
    gint value;
    GPtrArray *values;

    menu = g_object_get_data(dialog, key);
    value = gtk_combo_box_get_active(GTK_COMBO_BOX(menu));
    values = g_object_get_data(G_OBJECT(menu), "identity-value");

    return g_ptr_array_index(values, value);
}

/*
 * End of the Manage Identities dialog
 */

/*
 * The Identities combo box
 */

GtkWidget *
libbalsa_identity_combo_box(GList       * identities,
                            const gchar * active_name,
                            GCallback     changed_cb,
                            gpointer      changed_data)
{
    GList *list;
    GtkListStore *store;
    GtkWidget *combo_box;
    GtkCellLayout *layout;
    GtkCellRenderer *renderer;

    /* For each identity, store the address, the identity name, and a
     * ref to the identity in a combo-box.
     * Note: we can't depend on identities staying in the same
     * order while the combo-box is open, so we need a ref to the
     * actual identity. */
    store = gtk_list_store_new(3,
                               G_TYPE_STRING,
                               G_TYPE_STRING,
                               G_TYPE_OBJECT);
    combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

    for (list = identities; list != NULL; list = list->next) {
        LibBalsaIdentity *ident;
        gchar *from;
        gchar *name;
        GtkTreeIter iter;

        ident = list->data;
        from = internet_address_to_string(ident->ia, FALSE);
	name = g_strconcat("(", ident->identity_name, ")", NULL);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, from,
                           1, name,
                           2, ident,
                           -1);

        g_free(from);
        g_free(name);

        if (g_strcmp0(active_name, ident->identity_name) == 0)
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(combo_box), &iter);
    }
    g_object_unref(store);

    layout = GTK_CELL_LAYOUT(combo_box);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(layout, renderer, TRUE);
    gtk_cell_layout_set_attributes(layout, renderer, "text", 0, NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(layout, renderer, FALSE);
    gtk_cell_layout_set_attributes(layout, renderer, "text", 1, NULL);

    g_signal_connect(combo_box, "changed", changed_cb, changed_data);

    return combo_box;
}
