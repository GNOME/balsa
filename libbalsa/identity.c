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

#include <libgnome/libgnome.h>

#ifdef HAVE_GPGME
#  include "rfc3156.h"
#endif
#include "identity.h"
#include "information.h"

/*
 * The class.
 */

static GtkObjectClass* parent_class;

/* Forward references. */
static void libbalsa_identity_class_init(LibBalsaIdentityClass* klass);
static void libbalsa_identity_init(LibBalsaIdentity* ident);
static void libbalsa_identity_finalize(GObject* object);

GType
libbalsa_identity_get_type()
{
    static GType libbalsa_identity_type = 0;

    if (!libbalsa_identity_type) {
        static const GTypeInfo libbalsa_identity_info = {
            sizeof(LibBalsaIdentityClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_identity_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaIdentity),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_identity_init,
        };

        libbalsa_identity_type =
            g_type_register_static(G_TYPE_OBJECT,
                                   "LibBalsaIdentity",
                                   &libbalsa_identity_info, 0);
    }
    
    return libbalsa_identity_type;
}

static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GObjectClass* object_class;

    parent_class = g_type_class_peek_parent(klass);

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
    ident->address = libbalsa_address_new();
    ident->replyto = NULL;
    ident->domain = NULL;
    ident->bcc = NULL;
    ident->reply_string = g_strdup(_("Re:"));
    ident->forward_string = g_strdup(_("Fwd:"));
    ident->signature_path = NULL;
    ident->sig_executable = FALSE;
    ident->sig_sending = TRUE;
    ident->sig_whenforward = TRUE;
    ident->sig_whenreply = TRUE;
    ident->sig_separator = TRUE;
    ident->sig_prepend = FALSE;
#ifdef HAVE_GPGME
    ident->gpg_sign = FALSE;
    ident->gpg_encrypt = FALSE;
    ident->crypt_protocol = LIBBALSA_PROTECT_OPENPGP;
#endif
}

/* 
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_finalize(GObject * object)
{
    LibBalsaIdentity *ident = LIBBALSA_IDENTITY(object);

    g_object_unref(ident->address);
    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/*
 * Public methods.
 */

/* 
 * Create a new object with the default identity name.  Does not add
 * it to the list of identities for the application.
 */
GObject* 
libbalsa_identity_new(void) 
{
    return libbalsa_identity_new_with_name(_("New Identity"));
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GObject*
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(g_object_new(LIBBALSA_TYPE_IDENTITY, NULL));
    libbalsa_identity_set_identity_name(ident, ident_name);

    return G_OBJECT(ident);
}


void
libbalsa_identity_set_identity_name(LibBalsaIdentity* ident, const gchar* name)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->identity_name);
    ident->identity_name = g_strdup(name);
}


void
libbalsa_identity_set_address(LibBalsaIdentity* ident, LibBalsaAddress* ad)
{
    g_return_if_fail(ident != NULL);
    
    g_object_unref(ident->address);
    ident->address = ad;
}


void
libbalsa_identity_set_replyto(LibBalsaIdentity* ident, const gchar* address)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->replyto);
    ident->replyto = g_strdup(address);
}


void
libbalsa_identity_set_domain(LibBalsaIdentity* ident, const gchar* dom)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->domain);
    ident->domain = g_strdup(dom);
}


void 
libbalsa_identity_set_bcc(LibBalsaIdentity* ident, const gchar* bcc)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->bcc);
    ident->bcc = g_strdup(bcc);
}


void 
libbalsa_identity_set_reply_string(LibBalsaIdentity* ident, const gchar* reply)
{
    g_return_if_fail(ident != NULL);

    g_free(ident->reply_string);
    ident->reply_string = g_strdup(reply);
}


void 
libbalsa_identity_set_forward_string(LibBalsaIdentity* ident, const gchar* forward)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->forward_string);
    ident->forward_string = g_strdup(forward);
}


void
libbalsa_identity_set_signature_path(LibBalsaIdentity* ident, const gchar* path)
{
    g_return_if_fail(ident != NULL);
    
    g_free(ident->signature_path);
    ident->signature_path = g_strdup(path);
}


void
libbalsa_identity_set_sig_executable(LibBalsaIdentity* ident, gboolean sig_executable)
{
    g_return_if_fail(ident != NULL);
    ident->sig_executable = sig_executable;
}


void
libbalsa_identity_set_sig_sending(LibBalsaIdentity* ident, gboolean sig_sending)
{
    g_return_if_fail(ident != NULL);
    ident->sig_sending = sig_sending;
}


void
libbalsa_identity_set_sig_whenforward(LibBalsaIdentity* ident, gboolean forward)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenforward = forward;
}


void 
libbalsa_identity_set_sig_whenreply(LibBalsaIdentity* ident, gboolean reply)
{
    g_return_if_fail(ident != NULL);
    ident->sig_whenreply = reply;
}


void
libbalsa_identity_set_sig_separator(LibBalsaIdentity* ident, gboolean separator)
{
    g_return_if_fail(ident != NULL);
    ident->sig_separator = separator;
}


void 
libbalsa_identity_set_sig_prepend(LibBalsaIdentity* ident, gboolean prepend)
{
    g_return_if_fail(ident != NULL);
    ident->sig_prepend = prepend;
}


/* Used by both dialogs: */

/* Widget padding: */
static const guint padding = 4;

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
        gdk_window_raise(sdi->dialog->window);
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
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    NULL);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(sd_response_cb), sdi);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    sdi->tree = tree =
        libbalsa_identity_tree(G_CALLBACK(sd_idle_add_response_ok), sdi,
                               _("Current"));
    g_signal_connect_swapped(G_OBJECT(tree), "row-activated",
                             G_CALLBACK(sd_idle_add_response_ok), sdi);
    identity_list_update_real(GTK_TREE_VIEW(tree), identities, initial_id);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                       frame, TRUE, TRUE, 0);
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
    gdk_threads_enter();
    if (sdi->idle_handler_id) {
        sdi->idle_handler_id = 0;
        gtk_dialog_response(GTK_DIALOG(sdi->dialog), GTK_RESPONSE_OK);
    }
    gdk_threads_leave();
    return FALSE;
}

/*
 * The Manage Identities dialog; called from main window.
 */

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))
typedef struct _IdentityDeleteInfo IdentityDeleteInfo;

/* button actions */
static gboolean close_cb(GtkWidget * dialog);
static void new_ident_cb(GtkTreeView * tree, GtkWidget * dialog);
static void delete_ident_cb(GtkTreeView * tree, GtkWidget * dialog);
static void delete_ident_response(GtkWidget * confirm, gint response,
                                  IdentityDeleteInfo * di);
static void help_ident_cb(void);

static void set_default_ident_cb(GtkTreeView * tree, GtkTreePath * path,
                                 GtkTreeViewColumn * column,
                                 gpointer data);
static void config_frame_button_select_cb(GtkTreeSelection * selection,
                                          GtkDialog * dialog);

static void ident_dialog_add_checkbutton(GtkWidget *, gint, GtkDialog *,
                                         const gchar *, const gchar *);
static void ident_dialog_add_entry(GtkWidget *, gint, GtkDialog *,
                                   const gchar *, const gchar *);
static gchar *ident_dialog_get_text(GtkDialog *, const gchar *);
static gboolean ident_dialog_get_bool(GtkDialog *, const gchar *);
static gboolean ident_dialog_update(GtkDialog *);
static void config_dialog_select(GtkTreeSelection * selection,
                                 GtkDialog * dialog);

static void display_frame_update(GtkDialog * dialog, LibBalsaIdentity* ident);
static void display_frame_set_field(GtkDialog * dialog, const gchar* key, 
                                    const gchar* value);
static void display_frame_set_boolean(GtkDialog * dialog, const gchar* key, 
                                      gboolean value);


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
static void md_sig_path_changed(GtkEntry * sig_path, GObject * dialog);

#ifdef HAVE_GPGME
static void add_show_menu(const char* label, gint value, GtkWidget* menu);
static void ident_dialog_add_option_menu(GtkWidget * table, gint row,
                                         GtkDialog * dialog,
                                         const gchar * label_name,
                                         const gchar * menu_key);
static gint ident_dialog_get_menu(GtkDialog * dialog, const gchar * key);
static void display_frame_set_menu(GtkDialog * dialog, const gchar* key,
                                   gint * value);
#endif /* HAVE_GPGME */


/* Callback for the "toggled" signal of the "Default" column. */
static void
toggle_cb(GtkWidget * dialog, gchar * path)
{
    GtkTreeView *tree = g_object_get_data(G_OBJECT(dialog), "tree");
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;

    /* Save any changes to current identity; if it's not valid, just
     * return. */
    if (!ident_dialog_update(GTK_DIALOG(dialog)))
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
    g_signal_connect_swapped(G_OBJECT(renderer), "toggled",
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
    g_signal_connect(G_OBJECT(tree), "row-activated",
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
    GtkTreeSelection *select =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    LibBalsaIdentity *identity = NULL;

    if (gtk_tree_selection_get_selected(select, &model, &iter))
        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &identity, -1);

    return identity;
}

enum {
    IDENTITY_RESPONSE_CLOSE,
    IDENTITY_RESPONSE_NEW,
    IDENTITY_RESPONSE_REMOVE,
    IDENTITY_RESPONSE_HELP
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
close_cb(GtkWidget * dialog)
{
    return ident_dialog_update(GTK_DIALOG(dialog));
}

/*
 * Create a new identity
 */
static void
new_ident_cb(GtkTreeView * tree, GtkWidget * dialog)
{
    LibBalsaIdentity *ident;
    GList **identities;
    GtkWidget *name_entry;
    void (*cb)(gpointer) = g_object_get_data(G_OBJECT(tree), "callback");
    gpointer data        = g_object_get_data(G_OBJECT(tree), "cb-data");

    /* Save any changes to current identity; if it's not valid, just
     * return. */
    if (!ident_dialog_update(GTK_DIALOG(dialog)))
	return;

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    identities = g_object_get_data(G_OBJECT(tree), "identities");
    *identities = g_list_append(*identities, ident);
    identity_list_update(tree);
    /* select just added identity */
    select_identity(tree, ident);

    name_entry = g_object_get_data(G_OBJECT(dialog), "identity-name");
    gtk_widget_grab_focus(name_entry);
    cb(data);
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static GtkWidget*
setup_ident_frame(GtkDialog * dialog, gboolean createp, gpointer tree)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
#ifdef HAVE_GPGME
    GtkWidget *table = gtk_table_new(18, 2, FALSE);
#else
    GtkWidget *table = gtk_table_new(15, 2, FALSE);
#endif
    gint row = 0;
    GObject *name;
    GObject *sig_path;

    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_container_set_border_width(GTK_CONTAINER(table), padding);

    gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_table_set_row_spacings(GTK_TABLE(table), padding);
    gtk_table_set_col_spacings(GTK_TABLE(table), padding);

    ident_dialog_add_entry(table, row++, dialog, _("_Identity Name:"), 
		           "identity-name");
    ident_dialog_add_entry(table, row++, dialog, _("_Full Name:"), 
                           "identity-fullname");
    ident_dialog_add_entry(table, row++, dialog, _("_Mailing Address:"), 
                           "identity-address");
    ident_dialog_add_entry(table, row++, dialog, _("Reply _To:"), 
                           "identity-replyto");
    ident_dialog_add_entry(table, row++, dialog, _("_Domain:"), 
                           "identity-domain");
    ident_dialog_add_entry(table, row++, dialog, _("_Bcc:"), 
                           "identity-bcc");
    ident_dialog_add_entry(table, row++, dialog, _("Reply _String:"), 
                           "identity-replystring");
    ident_dialog_add_entry(table, row++, dialog, _("F_orward String:"), 
                           "identity-forwardstring");
    ident_dialog_add_entry(table, row++, dialog, _("Signature _Path:"), 
                           "identity-sigpath");
    
    ident_dialog_add_checkbutton(table, row++, dialog,
                                _("_Execute Signature"),
				"identity-sigexecutable");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Incl_ude Signature"), 
                                 "identity-sigappend");
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("Include Signature When For_warding"),
                                 "identity-whenforward");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Include Signature When Rep_lying"),
                                 "identity-whenreply");
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("_Add Signature Separator"),
                                 "identity-sigseparator");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Prepend Si_gnature"),
                                 "identity-sigprepend");

#ifdef HAVE_GPGME
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("sign messages by default"),
                                 "identity-gpgsign");
    gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(dialog),
							  "identity-gpgsign")),
			     TRUE);
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("encrypt messages by default"),
                                 "identity-gpgencrypt");
    gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(dialog),
							  "identity-gpgencrypt")),
			     TRUE);
    ident_dialog_add_option_menu(table, row++, dialog,
				 _("default crypto protocol"),
				 "identity-crypt-protocol");
    gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(G_OBJECT(dialog),
							  "identity-crypt-protocol")),
			     TRUE);
#endif

    name = g_object_get_data(G_OBJECT(dialog), "identity-name");
    g_signal_connect(name, "changed",
                     G_CALLBACK(md_name_changed), tree);
    sig_path = g_object_get_data(G_OBJECT(dialog), "identity-sigpath");
    g_signal_connect(sig_path, "changed",
                     G_CALLBACK(md_sig_path_changed), dialog);

    return GTK_WIDGET(frame);
}

/* Callback for the "changed" signal of the name entry; updates the name
 * in the tree. */
static void
md_name_changed(GtkEntry * name, GtkTreeView * tree)
{
    set_identity_name_in_tree(tree, get_selected_identity(tree),
			      gtk_entry_get_text(name));
}

/* Callback for the "changed" signal of the signature path entry; sets
 * sensitivity of the signature-related buttons; checks only for a
 * non-empty string, not for a valid path. */
static void
md_sig_path_changed(GtkEntry * sig_path, GObject * dialog)
{
    guint i;
    static gchar *button_key[] = {
        "identity-sigexecutable",
        "identity-sigappend",
        "identity-whenforward",
        "identity-whenreply",
        "identity-sigseparator",
        "identity-sigprepend",
    };
    gboolean has_sig = *gtk_entry_get_text(sig_path);

    for (i = 0; i < ELEMENTS(button_key); i++) {
        GtkWidget *button = g_object_get_data(dialog, button_key[i]);
        gtk_widget_set_sensitive(button, has_sig);
    }
}

/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key.  The check
 * button is initialized to the given value.
 */
static void
ident_dialog_add_checkbutton(GtkWidget * table, gint row,
                             GtkDialog * dialog, const gchar * check_label,
                             const gchar * check_key)
{
    GtkWidget *check;

    check = gtk_check_button_new_with_mnemonic(check_label);
    gtk_table_attach_defaults(GTK_TABLE(table), check, 0, 2, row, row + 1);
    g_object_set_data(G_OBJECT(dialog), check_key, check);
    gtk_widget_set_sensitive(check, FALSE);
}


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */
static void
ident_dialog_add_entry(GtkWidget * table, gint row, GtkDialog * dialog,
                       const gchar * label_name, const gchar * entry_key)
{
    GtkWidget *label;
    GtkWidget *entry;

    label = gtk_label_new_with_mnemonic(label_name);
    gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, row, row + 1);

    entry = gtk_entry_new();
    gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, row, row + 1);

    g_object_set_data(G_OBJECT(dialog), entry_key, entry);
    if (row == 0)
        gtk_widget_grab_focus(entry);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
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
ident_dialog_update(GtkDialog* dlg)
{
    LibBalsaIdentity* id;
    LibBalsaIdentity* exist_ident;
    LibBalsaAddress* address;
    GtkWidget *tree;
    GList **identities, *list;
    gchar* text;
    
    id = g_object_get_data(G_OBJECT(dlg), "identity");
    if (!id)
        return TRUE;
    tree = g_object_get_data(G_OBJECT(dlg), "tree");
    identities = g_object_get_data(G_OBJECT(tree), "identities");

    text = ident_dialog_get_text(dlg, "identity-name");
    g_return_val_if_fail(text != NULL, FALSE);

    if (text[0] == '\0') {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Error: The identity does not have a name"));
        return FALSE;
    }

    for (list = *identities; list; list = g_list_next(list)) {
        exist_ident = LIBBALSA_IDENTITY(list->data);
        
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

    text = ident_dialog_get_text(dlg, "identity-fullname");
    g_return_val_if_fail(text != NULL, FALSE);
    address = libbalsa_address_new();
    address->full_name = text;
    
    text = ident_dialog_get_text(dlg, "identity-address");
    address->address_list = g_list_append(address->address_list, text);
    libbalsa_identity_set_address(id, address);

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
    g_free(id->signature_path);
    id->signature_path  = ident_dialog_get_text(dlg, "identity-sigpath");
    
    id->sig_executable  = ident_dialog_get_bool(dlg, "identity-sigexecutable");
    id->sig_sending     = ident_dialog_get_bool(dlg, "identity-sigappend");
    id->sig_whenforward = ident_dialog_get_bool(dlg, "identity-whenforward");
    id->sig_whenreply   = ident_dialog_get_bool(dlg, "identity-whenreply");
    id->sig_separator   = ident_dialog_get_bool(dlg, "identity-sigseparator");
    id->sig_prepend     = ident_dialog_get_bool(dlg, "identity-sigprepend");
   
#ifdef HAVE_GPGME
    id->gpg_sign        = ident_dialog_get_bool(dlg, "identity-gpgsign");
    id->gpg_encrypt     = ident_dialog_get_bool(dlg, "identity-gpgencrypt");
    id->crypt_protocol  = ident_dialog_get_menu(dlg, "identity-crypt-protocol");
#endif
   
    return TRUE;
}


/* 
 * Get the text from an entry in the editing/creation dialog.  The
 * given key accesses the entry using object data.
 */
static gchar*
ident_dialog_get_text(GtkDialog* dialog, const gchar* key)
{
    GtkEntry* entry;
    gchar* text;
    
    entry = GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), key));
    text = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);

    return text;
}


/*
 * Get the value of a check button from the editing dialog.  The key
 * is used to retreive the reference to the check button using object
 * data
 */
static gboolean
ident_dialog_get_bool(GtkDialog* dialog, const gchar* key)
{
    GtkCheckButton* button;
    gboolean value;
    
    button = GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), key));
    value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    
    return value;
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
    g_signal_connect(G_OBJECT(confirm), "response",
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
help_ident_cb(void)
{
    GError *err = NULL;

    gnome_help_display("balsa", "identities", &err);

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
void
libbalsa_identity_config_dialog(GtkWindow *parent, GList **identities,
				LibBalsaIdentity **default_id,
                                void (*changed_cb)(gpointer))
{
    static GtkWidget *dialog = NULL;
    GtkWidget* frame;
    GtkWidget* display_frame;
    GtkWidget* hbox;
    GtkTreeView* tree;
    GtkTreeSelection *select;

    /* Show only one dialog at a time. */
    if (dialog) {
        gdk_window_raise(dialog->window);
        return;
    }

    dialog =
        gtk_dialog_new_with_buttons(_("Manage Identities"),
                                    parent, /* must NOT be modal */
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_HELP, IDENTITY_RESPONSE_HELP,
                                    GTK_STOCK_NEW, IDENTITY_RESPONSE_NEW,
                                    GTK_STOCK_REMOVE, IDENTITY_RESPONSE_REMOVE,
                                    GTK_STOCK_CLOSE, IDENTITY_RESPONSE_CLOSE,
                                    NULL);

    frame = libbalsa_identity_config_frame(identities, default_id, dialog,
                                           changed_cb, parent);
    tree = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(frame)));

    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(md_response_cb), tree);
    g_object_set_data(G_OBJECT(dialog), "tree", tree);
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer) & dialog);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    IDENTITY_RESPONSE_CLOSE);

    hbox = gtk_hbox_new(FALSE, padding);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);

    display_frame = setup_ident_frame(GTK_DIALOG(dialog),
                                      FALSE, tree);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    g_signal_connect(G_OBJECT(select), "changed",
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
        if (close_cb(dialog))
            break;
        return;
    case IDENTITY_RESPONSE_NEW:
        new_ident_cb(tree, dialog);
        return;
    case IDENTITY_RESPONSE_REMOVE:
        delete_ident_cb(tree, dialog);
        return;
    case IDENTITY_RESPONSE_HELP:
        help_ident_cb();
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
    display_frame_update(dialog, ident);
    g_object_set_data(G_OBJECT(dialog), "identity", ident);
}

static void 
display_frame_update(GtkDialog * dialog, LibBalsaIdentity* ident)
{
    if (!ident)
        return;

    ident_dialog_update(dialog);
    display_frame_set_field(dialog, "identity-name", ident->identity_name);
    display_frame_set_field(dialog, "identity-fullname", 
                            ident->address->full_name);
    if (ident->address->address_list)
        display_frame_set_field(dialog, "identity-address", 
                                (gchar*)ident->address->address_list->data);
    else
        display_frame_set_field(dialog, "identity-address", NULL);
    
    display_frame_set_field(dialog, "identity-replyto", ident->replyto);
    display_frame_set_field(dialog, "identity-domain", ident->domain);
    display_frame_set_field(dialog, "identity-bcc", ident->bcc);
    display_frame_set_field(dialog, "identity-replystring", 
                            ident->reply_string);
    display_frame_set_field(dialog, "identity-forwardstring", 
                            ident->forward_string);
    display_frame_set_field(dialog, "identity-sigpath", ident->signature_path);

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

#ifdef HAVE_GPGME
    display_frame_set_boolean(dialog, "identity-gpgsign", 
                              ident->gpg_sign);    
    display_frame_set_boolean(dialog, "identity-gpgencrypt", 
                              ident->gpg_encrypt);    
    display_frame_set_menu(dialog, "identity-crypt-protocol",
			   &ident->crypt_protocol);
#endif
}


static void
display_frame_set_field(GtkDialog * dialog,
                        const gchar* key,
                        const gchar* value)
{
    GtkEntry *entry = GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), key));
    
    gtk_entry_set_text(entry, value ? value : "");
}

static void
display_frame_set_boolean(GtkDialog * dialog,
                          const gchar* key,
                          gboolean value)
{
    GtkCheckButton *check =
        GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), key));
    
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), value);
}


/* libbalsa_identity_new_config:
   factory-type method creating new Identity object from given
   configuration data.
*/
LibBalsaIdentity*
libbalsa_identity_new_config(const gchar* prefix, const gchar* name)
{
    LibBalsaIdentity* ident;
    gchar* tmpstr;
    
    g_return_val_if_fail(prefix != NULL, NULL);

    gnome_config_push_prefix(prefix);

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name(name));
    ident->address->full_name = gnome_config_get_string("FullName");
    ident->address->address_list = 
        g_list_append(ident->address->address_list, 
                      gnome_config_get_string("Address"));
    ident->replyto = gnome_config_get_string("ReplyTo");
    ident->domain = gnome_config_get_string("Domain");
    ident->bcc = gnome_config_get_string("Bcc");

    /* 
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory. 
     */
    if ((tmpstr = gnome_config_get_string("ReplyString"))) {
        g_free(ident->reply_string);
        ident->reply_string = tmpstr;
    }
    
    if ((tmpstr = gnome_config_get_string("ForwardString"))) {
        g_free(ident->forward_string);
        ident->forward_string = tmpstr;
    }
    
    ident->signature_path = gnome_config_get_string("SignaturePath");
    ident->sig_executable = gnome_config_get_bool("SigExecutable");
    ident->sig_sending = gnome_config_get_bool("SigSending");
    ident->sig_whenforward = gnome_config_get_bool("SigForward");
    ident->sig_whenreply = gnome_config_get_bool("SigReply");
    ident->sig_separator = gnome_config_get_bool("SigSeparator");
    ident->sig_prepend = gnome_config_get_bool("SigPrepend");

#ifdef HAVE_GPGME
    ident->gpg_sign = gnome_config_get_bool("GpgSign");
    ident->gpg_encrypt = gnome_config_get_bool("GpgEncrypt");
    ident->crypt_protocol = gnome_config_get_int("CryptProtocol=16");
#endif

    gnome_config_pop_prefix();

    return ident;
}

void 
libbalsa_identity_save(LibBalsaIdentity* ident, const gchar* prefix)
{
    g_return_if_fail(ident);

    gnome_config_push_prefix(prefix);
    gnome_config_set_string("FullName", ident->address->full_name);
    
    if (ident->address->address_list != NULL)
        gnome_config_set_string("Address", ident->address->address_list->data);

    gnome_config_set_string("ReplyTo", ident->replyto);
    gnome_config_set_string("Domain", ident->domain);
    gnome_config_set_string("Bcc", ident->bcc);
    gnome_config_set_string("ReplyString", ident->reply_string);
    gnome_config_set_string("ForwardString", ident->forward_string);
    gnome_config_set_string("SignaturePath", ident->signature_path);

    gnome_config_set_bool("SigExecutable", ident->sig_executable);
    gnome_config_set_bool("SigSending", ident->sig_sending);
    gnome_config_set_bool("SigForward", ident->sig_whenforward);
    gnome_config_set_bool("SigReply", ident->sig_whenreply);
    gnome_config_set_bool("SigSeparator", ident->sig_separator);
    gnome_config_set_bool("SigPrepend", ident->sig_prepend);

#ifdef HAVE_GPGME
    gnome_config_set_bool("GpgSign", ident->gpg_sign);
    gnome_config_set_bool("GpgEncrypt", ident->gpg_encrypt);
    gnome_config_set_int("CryptProtocol", ident->crypt_protocol);
#endif

    gnome_config_pop_prefix();
}


#ifdef HAVE_GPGME
/* collected helper stuff for GPGME support */

void
libbalsa_identity_set_gpg_sign(LibBalsaIdentity* ident, gboolean sign)
{
    g_return_if_fail(ident != NULL);
    ident->gpg_sign = sign;
}


void
libbalsa_identity_set_gpg_encrypt(LibBalsaIdentity* ident, gboolean encrypt)
{
    g_return_if_fail(ident != NULL);
    ident->gpg_encrypt = encrypt;
}


void
libbalsa_identity_set_crypt_protocol(LibBalsaIdentity* ident, gint protocol)
{
    g_return_if_fail(ident != NULL);
    ident->crypt_protocol = protocol;
}


static void
add_show_menu(const char* label, gint value, GtkWidget* menu)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    GArray *values = g_object_get_data(G_OBJECT(menu), "identity-value");
    gtk_combo_box_append_text(GTK_COMBO_BOX(menu), label);
    g_array_append_val(values, value);
#else
    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);

    gtk_widget_show(menu_item);
    g_object_set_data(G_OBJECT(menu_item), "identity-value",
                      GINT_TO_POINTER(value));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
}


/*
 * Add an option menu to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.
 */
static void
ident_dialog_free_values(GArray * values)
{
    g_array_free(values, TRUE);
}

static void
ident_dialog_add_option_menu(GtkWidget * table, gint row, GtkDialog * dialog,
                             const gchar * label_name, const gchar * menu_key)
{
    GtkWidget *label;
    GtkWidget *opt_menu;
#if GTK_CHECK_VERSION(2, 4, 0)
    GArray *values;
#else
    GtkWidget *menu;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

    label = gtk_label_new_with_mnemonic(label_name);
    gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, row, row + 1);

#if GTK_CHECK_VERSION(2, 4, 0)
    opt_menu = gtk_combo_box_new_text();
    values = g_array_new(FALSE, FALSE, sizeof(gint));
    g_object_set_data_full(G_OBJECT(opt_menu), "identity-value", values,
                           (GDestroyNotify) ident_dialog_free_values);
#else
    opt_menu = gtk_option_menu_new ();
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_table_attach_defaults(GTK_TABLE(table), opt_menu, 1, 2, row, row + 1);
    g_object_set_data(G_OBJECT(dialog), menu_key, opt_menu);

#if GTK_CHECK_VERSION(2, 4, 0)
    add_show_menu(_("GnuPG using MIME mode"), LIBBALSA_PROTECT_RFC3156,
                  opt_menu);
    add_show_menu(_("GnuPG using OpenPGP mode"), LIBBALSA_PROTECT_OPENPGP,
                  opt_menu);
#ifdef HAVE_SMIME
    add_show_menu(_("S/MIME mode"), LIBBALSA_PROTECT_SMIMEV3, opt_menu);
#endif
#else
    menu = gtk_menu_new();
    add_show_menu(_("GnuPG using MIME mode"), LIBBALSA_PROTECT_RFC3156, menu);
    add_show_menu(_("GnuPG using OpenPGP mode"), LIBBALSA_PROTECT_OPENPGP, menu);
#ifdef HAVE_SMIME
    add_show_menu(_("S/MIME mode"), LIBBALSA_PROTECT_SMIMEV3, menu);
#endif
    gtk_option_menu_set_menu(GTK_OPTION_MENU(opt_menu), menu);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
}


/*
 * Get the value of the active option menu item
 */
static gint
ident_dialog_get_menu(GtkDialog * dialog, const gchar * key)
{
    GtkWidget * menu;
    gint value;
#if GTK_CHECK_VERSION(2, 4, 0)
    GArray *values;

    menu = g_object_get_data(G_OBJECT(dialog), key);
    value = gtk_combo_box_get_active(GTK_COMBO_BOX(menu));
    values = g_object_get_data(G_OBJECT(menu), "identity-value");
    value = g_array_index(values, guint, value);
#else
    
    menu = gtk_option_menu_get_menu (GTK_OPTION_MENU(g_object_get_data(G_OBJECT(dialog), key)));
    value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_menu_get_active (GTK_MENU(menu))), "identity-value"));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    
    return value;
}


static void
display_frame_set_menu(GtkDialog * dialog, const gchar* key, gint * value)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    GtkComboBox *opt_menu = g_object_get_data(G_OBJECT(dialog), key);
#else
    GtkOptionMenu * opt_menu =
        GTK_OPTION_MENU(g_object_get_data(G_OBJECT(dialog), key));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
 
    switch (*value)
        {
        case LIBBALSA_PROTECT_OPENPGP:
#if GTK_CHECK_VERSION(2, 4, 0)
	    gtk_combo_box_set_active(opt_menu, 1);
#else
            gtk_option_menu_set_history(opt_menu, 1);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
            break;
#ifdef HAVE_SMIME
        case LIBBALSA_PROTECT_SMIMEV3:
#if GTK_CHECK_VERSION(2, 4, 0)
	    gtk_combo_box_set_active(opt_menu, 2);
#else
            gtk_option_menu_set_history(opt_menu, 2);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
            break;
#endif
        case LIBBALSA_PROTECT_RFC3156:
        default:
#if GTK_CHECK_VERSION(2, 4, 0)
	    gtk_combo_box_set_active(opt_menu, 0);
#else
            gtk_option_menu_set_history(opt_menu, 0);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
            *value = LIBBALSA_PROTECT_RFC3156;
        }
}

#endif  /* HAVE_GPGME */
