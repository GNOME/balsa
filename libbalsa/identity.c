/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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
#include "identity.h"

#include "rfc3156.h"
#include "libbalsa.h"
#include "information.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>
#include "misc.h"

#include "libbalsa-gpgme.h"

#include <string.h>
#include "smtp-server.h"

/*
 * The class.
 */

struct _LibBalsaIdentity {
    GObject object;

    gchar *identity_name;

    InternetAddress *ia;
    gchar *replyto;
    gchar *domain;
    gchar *bcc;
    gchar *reply_string;
    gchar *forward_string;
    gboolean send_mp_alternative;

    gchar *signature_path;
    gboolean sig_executable;
    gboolean sig_sending;
    gboolean sig_whenforward;
    gboolean sig_whenreply;
    gboolean sig_separator;
    gboolean sig_prepend;
    gchar *face;
    gchar *x_face;
    gboolean request_mdn;
    gboolean request_dsn;

    gboolean gpg_sign;
    gboolean gpg_encrypt;
    gboolean always_trust;
    gboolean warn_send_plain;
    guint crypt_protocol;
    gchar *force_gpg_key_id;
    gchar *force_smime_key_id;
#ifdef ENABLE_AUTOCRYPT
    AutocryptMode autocrypt_mode;
#endif

    LibBalsaSmtpServer *smtp_server;
};

/* Forward references. */
static void libbalsa_identity_finalize(GObject* object);

G_DEFINE_TYPE(LibBalsaIdentity, libbalsa_identity, G_TYPE_OBJECT)

static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GObjectClass* object_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = libbalsa_identity_finalize;
}

/*
 * Instance inititialization function: set defaults for new objects.
 */
static void
libbalsa_identity_init(LibBalsaIdentity* ident)
{
    ident->identity_name = NULL;
    ident->ia = NULL;
    ident->replyto = NULL;
    ident->domain = NULL;
    ident->bcc = NULL;
    ident->reply_string = g_strdup(_("Re:"));
    ident->forward_string = g_strdup(_("Fwd:"));
    ident->send_mp_alternative = FALSE;
    ident->signature_path = NULL;
    ident->sig_executable = FALSE;
    ident->sig_sending = TRUE;
    ident->sig_whenforward = TRUE;
    ident->sig_whenreply = TRUE;
    ident->sig_separator = TRUE;
    ident->sig_prepend = FALSE;
    ident->gpg_sign = FALSE;
    ident->gpg_encrypt = FALSE;
    ident->always_trust = FALSE;
    ident->warn_send_plain = TRUE;
    ident->crypt_protocol = LIBBALSA_PROTECT_OPENPGP;
    ident->force_gpg_key_id = NULL;
    ident->force_smime_key_id = NULL;
#ifdef ENABLE_AUTOCRYPT
    ident->autocrypt_mode = AUTOCRYPT_DISABLE;
#endif
    ident->request_mdn = FALSE;
    ident->request_dsn = FALSE;
    /*
    ident->face = NULL;
    ident->x_face = NULL;
    */
}

/*
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_finalize(GObject * object)
{
    LibBalsaIdentity *ident = LIBBALSA_IDENTITY(object);

    if (ident->ia)
	g_object_unref(ident->ia);
    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);
    if (ident->smtp_server)
        g_object_unref(ident->smtp_server);
    g_free(ident->face);
    g_free(ident->x_face);
    g_free(ident->force_gpg_key_id);
    g_free(ident->force_smime_key_id);

    G_OBJECT_CLASS(libbalsa_identity_parent_class)->finalize(object);
}

/*
 * Public methods.
 */

/*
 * Create a new object with the default identity name.  Does not add
 * it to the list of identities for the application.
 */
LibBalsaIdentity *
libbalsa_identity_new(void)
{
    return libbalsa_identity_new_with_name(_("New Identity"));
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
LibBalsaIdentity *
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity *ident;

    ident = g_object_new(LIBBALSA_TYPE_IDENTITY, NULL);
    ident->identity_name = g_strdup(ident_name);

    return ident;
}


void
libbalsa_identity_set_address(LibBalsaIdentity * ident,
                              InternetAddress * ia)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    if (ident->ia)
	g_object_unref(ident->ia);
    ident->ia = ia;
}


void
libbalsa_identity_set_domain(LibBalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(LIBBALSA_IS_IDENTITY(ident));

    g_free(ident->domain);
    ident->domain = g_strdup(dom);
}


/** Returns a signature for given identity, adding a signature prefix
    if needed. */
gchar*
libbalsa_identity_get_signature(LibBalsaIdentity * ident, GError ** error)
{
    gchar *ret = NULL, *path;
    gchar *retval;

    if (ident->signature_path == NULL || *ident->signature_path == '\0')
	return NULL;

    path = libbalsa_expand_path(ident->signature_path);
    if (ident->sig_executable) {
        gchar *argv[] = {"/bin/sh", "-c", path, NULL};
        gchar *standard_error = NULL;

        if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                          &ret, &standard_error, NULL, error)) {
            g_prefix_error(error, _("Error executing signature generator “%s”: "),
                           ident->signature_path);
        } else if (standard_error != NULL) {
            g_set_error(error, LIBBALSA_ERROR_QUARK, -1,
                        _("Error executing signature generator “%s”: %s"),
                        ident->signature_path, standard_error);
        }
        g_free(standard_error);
    } else {
    	if (!g_file_get_contents(path, &ret, NULL, error)) {
            g_prefix_error(error, _("Cannot read signature file “%s”: "),
                           ident->signature_path);
    	}
    }
    g_free(path);

    if ((error != NULL && *error != NULL) || ret == NULL)
        return NULL;

    if (!libbalsa_utf8_sanitize(&ret, FALSE, NULL)) {
        g_set_error(error, LIBBALSA_ERROR_QUARK, -1,
                    _("Signature in “%s” is not a UTF-8 text."),
                    ident->signature_path);
    }

    /* Prepend the separator if needed... */

    if (ident->sig_separator
        && !(g_str_has_prefix(ret, "--\n") || g_str_has_prefix(ret, "-- \n"))) {
        retval = g_strconcat("\n-- \n", ret, NULL);
    } else {
        retval = g_strconcat("\n", ret, NULL);
    }
    g_free(ret);

    return retval;
}

void
libbalsa_identity_set_smtp_server(LibBalsaIdentity * ident,
                                  LibBalsaSmtpServer * smtp_server)
{
    g_return_if_fail(ident != NULL);

    if (ident->smtp_server)
	g_object_unref(ident->smtp_server);
    ident->smtp_server = smtp_server;
    if (smtp_server)
	g_object_ref(smtp_server);
}


/* Used by both dialogs: */

/* Widget padding: */
static const guint padding = 6;

/* Forward references: */
static void identity_list_update_real(GtkTreeView * tree,
                                      GList * identities,
                                      LibBalsaIdentity * default_id);
static GtkWidget *libbalsa_identity_tree(GCallback toggled_cb,
                                         gpointer toggled_data,
                                         gchar * toggled_title);

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

/* Tree columns: */
enum {
    DEFAULT_COLUMN,
    NAME_COLUMN,
    IDENT_COLUMN,
    N_COLUMNS
};

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
        gtk_window_present_with_time(GTK_WINDOW(sdi->dialog),
                                     gtk_get_current_event_time());
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
    gtk_widget_set_valign(frame, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER
                       (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       frame);
    gtk_container_add(GTK_CONTAINER(frame), tree);
    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);

    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(tree);
}

/* GDestroyNotify for sdi. */
static void
sd_destroy_notify(SelectDialogInfo * sdi)
{
    if (sdi->idle_handler_id) {
        g_source_remove(sdi->idle_handler_id);
        sdi->idle_handler_id = 0;
    }
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
 * The Manage Identities dialog; called from main window.
 */

typedef struct _IdentityDeleteInfo IdentityDeleteInfo;

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
static gchar *ident_dialog_get_text(GObject *, const gchar *);
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
static gboolean select_identity(GtkTreeView * tree,
                                LibBalsaIdentity * identity);
static LibBalsaIdentity *get_selected_identity(GtkTreeView * tree);
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
#ifdef ENABLE_AUTOCRYPT
static void ident_dialog_add_autocrypt_menu(GtkWidget   *grid,
											gint         row,
											GtkDialog   *dialog,
											const gchar *label_name,
											const gchar *menu_key);
static void display_frame_set_autocrypt_mode(GObject       *dialog,
											 const gchar   *key,
											 AutocryptMode *value);
#endif
static void add_show_menu(const char *label, gpointer data,
                          GtkWidget * menu);
static void ident_dialog_free_values(GPtrArray * values);

static void display_frame_set_gpg_mode(GObject * dialog,
                                       const gchar * key, guint * value);

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

    gtk_container_set_border_width(GTK_CONTAINER(config_frame), 0);

    tree = libbalsa_identity_tree(G_CALLBACK(toggle_cb), dialog,
                                  _("Default"));
    g_signal_connect(tree, "row-activated",
                     G_CALLBACK(set_default_ident_cb), NULL);
    g_object_set_data(G_OBJECT(tree), "identities", identities);
    g_object_set_data(G_OBJECT(tree), "default-id", defid);
    g_object_set_data(G_OBJECT(tree), "callback", cb);
    g_object_set_data(G_OBJECT(tree), "cb-data",  data);

    gtk_container_add(GTK_CONTAINER(config_frame), tree);

    identity_list_update(GTK_TREE_VIEW(tree));

    return config_frame;
}

static gint
compare_identities(LibBalsaIdentity *id1, LibBalsaIdentity *id2)
{
    return g_ascii_strcasecmp(id1->identity_name, id2->identity_name);
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
    for (list = sorted; list; list = g_list_next(list)) {
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
append_ident_notebook_page(GtkNotebook *notebook,
			   const gchar * tab_label)
{
    GtkWidget *vbox;
    GtkWidget *grid;

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    grid = libbalsa_create_grid();
    gtk_container_set_border_width(GTK_CONTAINER(grid), padding);
    gtk_container_add(GTK_CONTAINER(vbox), grid);
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

    /* create the "General" tab */
    grid = append_ident_notebook_page(notebook, _("General"));
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
    grid = append_ident_notebook_page(notebook, _("Messages"));
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
    grid = append_ident_notebook_page(notebook, _("Signature"));
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

    /* create the "Security" tab */
    grid =
        append_ident_notebook_page(notebook, _("Security"));
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
#ifdef ENABLE_AUTOCRYPT
    ident_dialog_add_autocrypt_menu(grid, row++, dialog,
		 	 	 	 	 	 	 	_("Autocrypt mode"),
									"identity-autocrypt");
#endif

    name = g_object_get_data(G_OBJECT(dialog), "identity-name");
    g_signal_connect(name, "changed",
                     G_CALLBACK(md_name_changed), tree);

    path = g_object_get_data(G_OBJECT(dialog), "identity-sigpath");
    g_signal_connect(g_object_get_data(path,
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


static void
choose_key(GtkButton *button, gpointer user_data)
{
	const gchar *target;
	gpgme_protocol_t protocol;
	gchar *email;
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
	g_object_set_data_full(G_OBJECT(button), "target", g_strdup(entry_key), (GDestroyNotify) g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(choose_key), dialog);
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
    gtk_container_add(GTK_CONTAINER(face_box), image);
    gtk_widget_show_all(face_box);

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
file_chooser_cb(GtkFileChooser * chooser, gpointer data)
{
    gchar *filename;
    LibBalsaIdentityPathType type;
    GtkToggleButton *check;
    gboolean active;

    filename = gtk_file_chooser_get_filename(chooser);
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
    gtk_widget_set_vexpand(button, TRUE);
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
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(dialog), key2, vbox);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
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
    gchar* text;

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

    for (list = *identities; list; list = g_list_next(list)) {
        exist_ident = list->data;

        if (g_ascii_strcasecmp(exist_ident->identity_name, text) == 0
            && id != exist_ident) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                 _("Error: An identity with that"
                                   " name already exists"));
            return FALSE;
        }
    }

    g_free(id->identity_name); id->identity_name = text;
    set_identity_name_in_tree(GTK_TREE_VIEW(tree), id, text);

    text = ident_dialog_get_text(dlg, "identity-address");
    g_return_val_if_fail(text != NULL, FALSE);
    ia = internet_address_mailbox_new(NULL, text);
    g_free(text);

    text = ident_dialog_get_text(dlg, "identity-fullname");
    internet_address_set_name(ia, text);
    libbalsa_identity_set_address(id, ia);
    g_free(text);

    g_free(id->replyto);
    id->replyto         = ident_dialog_get_text(dlg, "identity-replyto");
    g_free(id->domain);
    id->domain          = ident_dialog_get_text(dlg, "identity-domain");
    g_free(id->bcc);
    id->bcc             = ident_dialog_get_text(dlg, "identity-bcc");
    g_free(id->reply_string);
    id->reply_string    = ident_dialog_get_text(dlg, "identity-replystring");
    g_free(id->forward_string);
    id->forward_string  = ident_dialog_get_text(dlg, "identity-forwardstring");
    id->send_mp_alternative = ident_dialog_get_bool(dlg, "identity-sendmpalternative");
    if(id->smtp_server) g_object_unref(id->smtp_server);
    id->smtp_server = ident_dialog_get_value(dlg, "identity-smtp-server");
    g_object_ref(id->smtp_server);

    g_free(id->signature_path);
    id->signature_path  = ident_dialog_get_text(dlg, "identity-sigpath");

    id->sig_executable  = ident_dialog_get_bool(dlg, "identity-sigexecutable");
    id->sig_sending     = ident_dialog_get_bool(dlg, "identity-sigappend");
    id->sig_whenforward = ident_dialog_get_bool(dlg, "identity-whenforward");
    id->sig_whenreply   = ident_dialog_get_bool(dlg, "identity-whenreply");
    id->sig_separator   = ident_dialog_get_bool(dlg, "identity-sigseparator");
    id->sig_prepend     = ident_dialog_get_bool(dlg, "identity-sigprepend");

    g_free(id->face);
    id->face            = ident_dialog_get_path(dlg, "identity-facepath");
    g_free(id->x_face);
    id->x_face          = ident_dialog_get_path(dlg, "identity-xfacepath");
    id->request_mdn     = ident_dialog_get_bool(dlg, "identity-requestmdn");
    id->request_dsn     = ident_dialog_get_bool(dlg, "identity-requestdsn");

    id->gpg_sign        = ident_dialog_get_bool(dlg, "identity-gpgsign");
    id->gpg_encrypt     = ident_dialog_get_bool(dlg, "identity-gpgencrypt");
    id->always_trust    = ident_dialog_get_bool(dlg, "identity-trust-always");
    id->warn_send_plain = ident_dialog_get_bool(dlg, "identity-warn-send-plain");
    id->crypt_protocol  = GPOINTER_TO_UINT(ident_dialog_get_value
                                          (dlg, "identity-crypt-protocol"));
    g_free(id->force_gpg_key_id);
    id->force_gpg_key_id = g_strstrip(ident_dialog_get_text(dlg, "identity-keyid"));
    g_free(id->force_smime_key_id);
    id->force_smime_key_id = g_strstrip(ident_dialog_get_text(dlg, "identity-keyid-sm"));
#ifdef ENABLE_AUTOCRYPT
    id->autocrypt_mode  = GPOINTER_TO_INT(ident_dialog_get_value(dlg, "identity-autocrypt"));
#endif

    return TRUE;
}


/*
 * Get the text from an entry in the editing/creation dialog.  The
 * given key accesses the entry using object data.
 */
static gchar*
ident_dialog_get_text(GObject * dialog, const gchar * key)
{
    GtkEditable *entry;
    GtkToggleButton *check;

    entry = g_object_get_data(dialog, key);
    check = g_object_get_data(G_OBJECT(entry), LIBBALSA_IDENTITY_CHECK);
    if (check && !gtk_toggle_button_get_active(check))
        return NULL;
    return gtk_editable_get_chars(entry, 0, -1);
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
struct _IdentityDeleteInfo {
    GtkTreeView *tree;
    GtkWidget *dialog;
};
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
    di = g_new(IdentityDeleteInfo, 1);
    di->tree = tree;
    di->dialog = dialog;
    g_signal_connect(confirm, "response",
                     G_CALLBACK(delete_ident_response), di);
    g_object_weak_ref(G_OBJECT(confirm), (GWeakNotify) g_free, di);
    gtk_widget_set_sensitive(dialog, FALSE);
    gtk_widget_show_all(confirm);
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
    	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
    		_("Error displaying help for identities: %s"),
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
        gtk_window_present_with_time(GTK_WINDOW(dialog),
                                     gtk_get_current_event_time());
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

    frame = libbalsa_identity_config_frame(identities, default_id, dialog,
                                           changed_cb, parent);
    tree = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(frame)));

    g_signal_connect(dialog, "response",
                     G_CALLBACK(md_response_cb), tree);
    g_object_set_data(G_OBJECT(dialog), "tree", tree);
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer *) & dialog);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    IDENTITY_RESPONSE_CLOSE);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, padding);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER
                       (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       hbox);

    gtk_container_add(GTK_CONTAINER(hbox), frame);

    smtp_server_list = g_new(GSList *, 1);
    *smtp_server_list = g_slist_copy(smtp_servers);
    g_slist_foreach(smtp_servers, (GFunc) g_object_ref, NULL);
    display_frame = setup_ident_frame(GTK_DIALOG(dialog),
                                      FALSE, tree, smtp_servers);
    g_object_weak_ref(G_OBJECT(display_frame),
	              (GWeakNotify) lbi_free_smtp_server_list,
		      smtp_server_list);

    gtk_widget_set_hexpand(display_frame, TRUE);
    gtk_widget_set_halign(display_frame, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), display_frame);

    select = gtk_tree_view_get_selection(tree);
    g_signal_connect(select, "changed",
                     G_CALLBACK(config_frame_button_select_cb), dialog);
    config_dialog_select(select, GTK_DIALOG(dialog));

    gtk_widget_show_all(dialog);
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
#ifdef ENABLE_AUTOCRYPT
    display_frame_set_autocrypt_mode(dialog, "identity-autocrypt", &ident->autocrypt_mode);
#endif
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


/* libbalsa_identity_new_from_config:
   factory-type method creating new Identity object from given
   configuration data.
*/
LibBalsaIdentity*
libbalsa_identity_new_from_config(const gchar* name)
{
    LibBalsaIdentity* ident;
    gchar *fname, *email;
    gchar* tmpstr;

    fname = libbalsa_conf_get_string("FullName");
    email = libbalsa_conf_get_string("Address");

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name(name));
    ident->ia = internet_address_mailbox_new (fname, email);
    g_free(fname);
    g_free(email);

    ident->replyto = libbalsa_conf_get_string("ReplyTo");
    ident->domain = libbalsa_conf_get_string("Domain");
    ident->bcc = libbalsa_conf_get_string("Bcc");

    /*
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory.
     */
    if ((tmpstr = libbalsa_conf_get_string("ReplyString"))) {
        g_free(ident->reply_string);
        ident->reply_string = tmpstr;
    }

    if ((tmpstr = libbalsa_conf_get_string("ForwardString"))) {
        g_free(ident->forward_string);
        ident->forward_string = tmpstr;
    }
    ident->send_mp_alternative =
        libbalsa_conf_get_bool("SendMultipartAlternative");

    ident->signature_path = libbalsa_conf_get_string("SignaturePath");
    ident->sig_executable = libbalsa_conf_get_bool("SigExecutable");
    ident->sig_sending = libbalsa_conf_get_bool("SigSending");
    ident->sig_whenforward = libbalsa_conf_get_bool("SigForward");
    ident->sig_whenreply = libbalsa_conf_get_bool("SigReply");
    ident->sig_separator = libbalsa_conf_get_bool("SigSeparator");
    ident->sig_prepend = libbalsa_conf_get_bool("SigPrepend");
    ident->face = libbalsa_conf_get_string("FacePath");
    ident->x_face = libbalsa_conf_get_string("XFacePath");
    ident->request_mdn = libbalsa_conf_get_bool("RequestMDN");
    ident->request_dsn = libbalsa_conf_get_bool("RequestDSN");

    ident->gpg_sign = libbalsa_conf_get_bool("GpgSign");
    ident->gpg_encrypt = libbalsa_conf_get_bool("GpgEncrypt");
    ident->always_trust = libbalsa_conf_get_bool("GpgTrustAlways");
    ident->warn_send_plain = libbalsa_conf_get_bool("GpgWarnSendPlain=true");
    ident->crypt_protocol = (guint) libbalsa_conf_get_int("CryptProtocol=16");
    ident->force_gpg_key_id = libbalsa_conf_get_string("ForceKeyID");
    ident->force_smime_key_id = libbalsa_conf_get_string("ForceKeyIDSMime");
#ifdef ENABLE_AUTOCRYPT
    ident->autocrypt_mode = libbalsa_conf_get_int("Autocrypt=0");
#endif

    return ident;
}

void
libbalsa_identity_save(LibBalsaIdentity* ident, const gchar* group)
{
    g_return_if_fail(ident);

    libbalsa_conf_push_group(group);
    libbalsa_conf_set_string("FullName", ident->ia ? ident->ia->name : NULL);

    if (ident->ia && INTERNET_ADDRESS_IS_MAILBOX (ident->ia))
        libbalsa_conf_set_string("Address", INTERNET_ADDRESS_MAILBOX(ident->ia)->addr);

    libbalsa_conf_set_string("ReplyTo", ident->replyto);
    libbalsa_conf_set_string("Domain", ident->domain);
    libbalsa_conf_set_string("Bcc", ident->bcc);
    libbalsa_conf_set_string("ReplyString", ident->reply_string);
    libbalsa_conf_set_string("ForwardString", ident->forward_string);
    libbalsa_conf_set_bool("SendMultipartAlternative", ident->send_mp_alternative);
    libbalsa_conf_set_string("SmtpServer",
                             libbalsa_smtp_server_get_name(ident->
                                                           smtp_server));

    libbalsa_conf_set_string("SignaturePath", ident->signature_path);
    libbalsa_conf_set_bool("SigExecutable", ident->sig_executable);
    libbalsa_conf_set_bool("SigSending", ident->sig_sending);
    libbalsa_conf_set_bool("SigForward", ident->sig_whenforward);
    libbalsa_conf_set_bool("SigReply", ident->sig_whenreply);
    libbalsa_conf_set_bool("SigSeparator", ident->sig_separator);
    libbalsa_conf_set_bool("SigPrepend", ident->sig_prepend);
    if (ident->face)
        libbalsa_conf_set_string("FacePath", ident->face);
    if (ident->x_face)
        libbalsa_conf_set_string("XFacePath", ident->x_face);
    libbalsa_conf_set_bool("RequestMDN", ident->request_mdn);
    libbalsa_conf_set_bool("RequestDSN", ident->request_dsn);

    libbalsa_conf_set_bool("GpgSign", ident->gpg_sign);
    libbalsa_conf_set_bool("GpgEncrypt", ident->gpg_encrypt);
    libbalsa_conf_set_bool("GpgTrustAlways", ident->always_trust);
    libbalsa_conf_set_bool("GpgWarnSendPlain", ident->warn_send_plain);
    libbalsa_conf_set_int("CryptProtocol", (gint) ident->crypt_protocol);
    libbalsa_conf_set_string("ForceKeyID", ident->force_gpg_key_id);
    libbalsa_conf_set_string("ForceKeyIDSMime", ident->force_smime_key_id);
#ifdef ENABLE_AUTOCRYPT
    libbalsa_conf_set_int("Autocrypt", ident->autocrypt_mode);
#endif

    libbalsa_conf_pop_group();
}


/* collected helper stuff for GPGME support */


static void
display_frame_set_gpg_mode(GObject * dialog, const gchar* key, guint * value)
{
    GtkComboBox *opt_menu = g_object_get_data(G_OBJECT(dialog), key);

    switch (*value)
        {
        case LIBBALSA_PROTECT_OPENPGP:
	    gtk_combo_box_set_active(opt_menu, 1);
            break;
        case LIBBALSA_PROTECT_SMIME:
	    gtk_combo_box_set_active(opt_menu, 2);
            break;
        case LIBBALSA_PROTECT_RFC3156:
        default:
	    gtk_combo_box_set_active(opt_menu, 0);
            *value = LIBBALSA_PROTECT_RFC3156;
        }
}

#ifdef ENABLE_AUTOCRYPT
static void
display_frame_set_autocrypt_mode(GObject * dialog, const gchar* key, AutocryptMode * value)
{
    GtkComboBox *opt_menu = g_object_get_data(G_OBJECT(dialog), key);

    switch (*value)
        {
        case AUTOCRYPT_NOPREFERENCE:
	    gtk_combo_box_set_active(opt_menu, 1);
            break;
        case AUTOCRYPT_PREFER_ENCRYPT:
	    gtk_combo_box_set_active(opt_menu, 2);
            break;
        default:
	    gtk_combo_box_set_active(opt_menu, 0);
            *value = AUTOCRYPT_DISABLE;
        }
}

static void
ident_dialog_add_autocrypt_menu(GtkWidget * grid, gint row, GtkDialog * dialog,
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

    add_show_menu(_("disabled"),
                  GINT_TO_POINTER(AUTOCRYPT_DISABLE), opt_menu);
    add_show_menu(_("enabled, no preference"),
                  GINT_TO_POINTER(AUTOCRYPT_NOPREFERENCE), opt_menu);
    add_show_menu(_("enabled, prefer encryption"),
                  GINT_TO_POINTER(AUTOCRYPT_PREFER_ENCRYPT), opt_menu);
}
#endif

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
                  GUINT_TO_POINTER(LIBBALSA_PROTECT_RFC3156), opt_menu);
    add_show_menu(_("GnuPG OpenPGP mode"),
                  GUINT_TO_POINTER(LIBBALSA_PROTECT_OPENPGP), opt_menu);
    add_show_menu(_("GpgSM S/MIME mode"),
                  GUINT_TO_POINTER(LIBBALSA_PROTECT_SMIME), opt_menu);
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
        from = internet_address_to_string(ident->ia, NULL, FALSE);
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

/*
 * Getters
 */

gboolean
libbalsa_identity_get_sig_prepend(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->sig_prepend;
}

gboolean
libbalsa_identity_get_sig_whenreply(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->sig_whenreply;
}

gboolean
libbalsa_identity_get_sig_whenforward(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->sig_whenforward;
}

gboolean
libbalsa_identity_get_sig_sending(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->sig_sending;
}

gboolean
libbalsa_identity_get_send_mp_alternative(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->send_mp_alternative;
}

gboolean
libbalsa_identity_get_request_mdn(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->request_mdn;
}

gboolean
libbalsa_identity_get_request_dsn(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->request_dsn;
}

gboolean
libbalsa_identity_get_warn_send_plain(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->warn_send_plain;
}

gboolean
libbalsa_identity_get_always_trust(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->always_trust;
}

gboolean
libbalsa_identity_get_gpg_sign(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->gpg_sign;
}

gboolean
libbalsa_identity_get_gpg_encrypt(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), FALSE);

    return ident->gpg_encrypt;
}

gboolean
libbalsa_identity_get_sig_executable(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), 0);

    return ident->sig_executable;
}

gboolean
libbalsa_identity_get_sig_separator(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), 0);

    return ident->sig_separator;
}

guint
libbalsa_identity_get_crypt_protocol(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), 0);

    return ident->crypt_protocol;
}

const gchar *
libbalsa_identity_get_identity_name(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->identity_name;
}

const gchar *
libbalsa_identity_get_force_gpg_key_id(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->force_gpg_key_id;
}

const gchar *
libbalsa_identity_get_force_smime_key_id(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->force_smime_key_id;
}

const gchar *
libbalsa_identity_get_replyto(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->replyto;
}

const gchar *
libbalsa_identity_get_bcc(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->bcc;
}

const gchar *
libbalsa_identity_get_reply_string(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->reply_string;
}

const gchar *
libbalsa_identity_get_forward_string(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->forward_string;
}

const gchar *
libbalsa_identity_get_domain(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->domain;
}

const gchar *
libbalsa_identity_get_face_path(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->face;
}

const gchar *
libbalsa_identity_get_x_face_path(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->x_face;
}

const gchar *
libbalsa_identity_get_signature_path(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->signature_path;
}

InternetAddress *
libbalsa_identity_get_address(LibBalsaIdentity *ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->ia;
}

LibBalsaSmtpServer *
libbalsa_identity_get_smtp_server(LibBalsaIdentity * ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), NULL);

    return ident->smtp_server;
}

#ifdef ENABLE_AUTOCRYPT
AutocryptMode
libbalsa_identity_get_autocrypt_mode(LibBalsaIdentity * ident)
{
    g_return_val_if_fail(LIBBALSA_IS_IDENTITY(ident), AUTOCRYPT_DISABLE);

    return ident->autocrypt_mode;
}
#endif
