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

#include "config.h"
#include "identity.h"

#define gnome_stock_button_with_label(p,l) gtk_button_new_with_label(l)
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static GtkObjectClass* parent_class;
static const gchar* default_ident_name = N_("New Identity");
static const guint padding = 4;

static void libbalsa_identity_class_init(LibBalsaIdentityClass* klass);
static void libbalsa_identity_init(LibBalsaIdentity* ident);

static void libbalsa_identity_finalize(GObject* object);

/* button callbacks */
static gboolean close_cb(gpointer data);
static gboolean new_ident_cb(gpointer);
static gboolean delete_ident_cb(gpointer);
static gboolean help_ident_cb(gpointer);

static void set_default_ident_cb(GtkTreeView * tree, GtkTreePath * path,
                                 GtkTreeViewColumn * column,
                                 gpointer data);
static void identity_list_update(GtkTreeView * tree);
static void config_frame_button_select_cb(GtkTreeSelection * treeselection,
                                          gpointer user_data);

static void ident_dialog_add_checkbutton(GtkWidget *, gint, GtkDialog *,
                                         const gchar *, const gchar *);
static void ident_dialog_add_entry(GtkWidget *, gint, GtkDialog *,
                                   const gchar *, const gchar *);
static gchar *ident_dialog_get_text(GtkDialog *, const gchar *);
static gboolean ident_dialog_get_bool(GtkDialog *, const gchar *);
static gboolean ident_dialog_update(GtkDialog *);
static void config_dialog_select(GtkTreeSelection * selection,
                                    gpointer data);

static void display_frame_update(GtkDialog * dialog, LibBalsaIdentity* ident);
static void display_frame_set_field(GtkDialog * dialog, const gchar* key, 
                                    const gchar* value);
static void display_frame_set_boolean(GtkDialog * dialog, const gchar* key, 
                                      gboolean value);


static void select_dialog_row_cb(GtkTreeSelection * selection,
                                 LibBalsaIdentity ** identity);
static void activate_close(GtkTreeView * treeview, GtkTreeIter * arg1,
                           GtkTreePath * arg2, gpointer user_data);
static gboolean select_identity(GtkTreeView * tree,
                                LibBalsaIdentity * identity);
static LibBalsaIdentity *get_selected_identity(GtkTreeView * tree);
static GtkWidget *libbalsa_identity_tree(GList ** identities,
                                         LibBalsaIdentity ** defid,
                                         GCallback user_function,
                                         gpointer user_data,
                                         GtkSelectionMode mode);


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
 * Class inititialization function, set defaults for new objects
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
}


/* 
 * Create a new object with the identity name "Default".  Does not add
 * it to the list of identities for the application.
 */
GObject* 
libbalsa_identity_new(void) 
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(g_object_new(LIBBALSA_TYPE_IDENTITY, NULL));
    ident->identity_name = g_strdup(default_ident_name);
    return G_OBJECT(ident);
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
    ident->identity_name = g_strdup(ident_name);
    return G_OBJECT(ident);
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

enum{
    DEFAULT_COLUMN,
    NAME_COLUMN,
    IDENT_COLUMN,
    N_COLUMNS
};

static void
toggle_cb(GtkCellRendererToggle * cellrenderertoggle, gchar * path,
          gpointer user_data)
{
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_from_string(model, &iter, path)) {
        LibBalsaIdentity *identity, **default_id;

        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &identity, -1);
        default_id = g_object_get_data(G_OBJECT(tree), "default-id");
        *default_id = identity;
        identity_list_update(tree);
    }
}

static GtkWidget *
libbalsa_identity_tree(GList** identities,
                       LibBalsaIdentity** defid,
                       GCallback user_function, gpointer user_data,
                       GtkSelectionMode mode)
{
    GtkListStore *store;
    GtkWidget *tree;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;

    store = gtk_list_store_new(N_COLUMNS,
                               G_TYPE_BOOLEAN,
                               G_TYPE_STRING,
                               G_TYPE_POINTER);

    tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (store);
    g_object_set_data(G_OBJECT(tree), "identities", identities);
    g_object_set_data(G_OBJECT(tree), "default-id", defid);

    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes ("Current", renderer,
                                                       "radio", DEFAULT_COLUMN,
                                                       NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                       "text", NAME_COLUMN,
                                                       NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW (tree));
    gtk_tree_selection_set_mode(select, mode);
    g_signal_connect(G_OBJECT(select), "changed",
                           user_function, user_data);

    identity_list_update(GTK_TREE_VIEW(tree));
    return tree;
}

/* idle handler for destroying a dialog when it contains a GtkTreeView
 * (these seem to to use an idle-layout that may be called after the 
 * widget is destroyed) */
static gboolean
destroy_dialog(GtkWidget *widget)
{
    gdk_threads_enter();
    if (GTK_IS_WIDGET(widget))
        gtk_widget_destroy(widget);
    gdk_threads_leave();
    return FALSE;
}

LibBalsaIdentity*
libbalsa_identity_select_dialog(GtkWindow* parent, const gchar* prompt,
				GList** identities, LibBalsaIdentity** defid)
{
    LibBalsaIdentity *identity = NULL;
    GtkWidget *tree;
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* dialog;

    tree = libbalsa_identity_tree(identities, defid,
                                  G_CALLBACK(select_dialog_row_cb),
                                  &identity, GTK_SELECTION_SINGLE);
    dialog = gtk_dialog_new_with_buttons(prompt, 
                                         parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_OK,
                                         NULL);
    g_signal_connect(G_OBJECT(tree), "row-activated",
                     G_CALLBACK(activate_close), dialog);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       frame1, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame1), tree);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), padding);
    gtk_widget_show_all(frame1);

    gtk_widget_grab_focus(GTK_WIDGET(tree));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK
            || identity == *defid)
        identity = NULL;
    gtk_idle_add((GtkFunction) destroy_dialog, dialog);
    
    return identity;
}

/* callback for the selection "changed" signal:
 * save the selected identity */
static void
select_dialog_row_cb(GtkTreeSelection * selection,
                     LibBalsaIdentity ** identity)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, IDENT_COLUMN, identity, -1);
}

/* callback for the "row-activated" signal:
 * close the dialog */
static void
activate_close(GtkTreeView * treeview, GtkTreeIter * arg1,
               GtkTreePath * arg2, gpointer user_data)
{
    gtk_dialog_response(GTK_DIALOG(user_data), GTK_RESPONSE_OK);
}

/*
 * Create and return a frame containing a list of the identities in
 * the application and a number of buttons to edit, create, and delete
 * identities.  Also provides a way to set the default identity.
 */
static GtkWidget* 
libbalsa_identity_config_frame(GList** identities,
			       LibBalsaIdentity** defid)
{

    GtkWidget* config_frame = gtk_frame_new(NULL);
    GtkWidget *tree;

    gtk_container_set_border_width(GTK_CONTAINER(config_frame), 0);
    
    tree =
        libbalsa_identity_tree(identities, defid,
                               G_CALLBACK(config_frame_button_select_cb),
                               config_frame, GTK_SELECTION_BROWSE);
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
 * We need to tempararily switch to selection mode single to avoid row 
 * autoselection when there is no data attached to it yet.
 */
static void
identity_list_update(GtkTreeView * tree)
{
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(tree));
    LibBalsaIdentity* ident;
    GList **identities, *sorted, *list;
    LibBalsaIdentity **default_id, *current;
    GtkTreeIter iter;

    identities = g_object_get_data(G_OBJECT(tree), "identities");
    default_id = g_object_get_data(G_OBJECT(tree), "default-id");

    current = get_selected_identity(tree);

    gtk_list_store_clear(store);
    
    sorted = g_list_copy(*identities);
    sorted = g_list_sort(sorted, (GCompareFunc) compare_identities);
    for (list = sorted; list; list = g_list_next(list)) {
        ident = LIBBALSA_IDENTITY(list->data);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           DEFAULT_COLUMN, ident == *default_id,
                           NAME_COLUMN, ident->identity_name,
                           IDENT_COLUMN, ident,
                           -1);
    }
    g_list_free(sorted);

    if (!select_identity(tree, current))
        select_identity(tree, *default_id);
}

static gboolean
select_identity(GtkTreeView * tree, LibBalsaIdentity * identity)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeSelection *select =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    GtkTreeIter iter;
    gboolean valid;
    
    for (valid =
         gtk_tree_model_get_iter_first(model, &iter);
         valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaIdentity *tmp;
        
        gtk_tree_model_get(model, &iter, IDENT_COLUMN, &tmp, -1);
        if (identity == tmp) {
            gtk_tree_selection_select_iter(select, &iter);
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
    IDENTITY_RESPONSE_DELETE,
    IDENTITY_RESPONSE_HELP
};
    
/* callback for the "changed" signal */
static void
config_frame_button_select_cb(GtkTreeSelection * selection,
                              gpointer user_data)
{
    GtkWidget *widget = gtk_widget_get_ancestor(GTK_WIDGET(user_data),
                                                GTK_TYPE_DIALOG);
    GtkTreeIter iter;
    GtkTreeModel *model;
    gboolean state;

    if (!widget)
        return;

    state = gtk_tree_selection_get_selected(selection, &model, &iter);
    gtk_dialog_set_response_sensitive((GtkDialog *) widget,
                                      IDENTITY_RESPONSE_DELETE, state);

    config_dialog_select(selection, (GtkDialog *) widget);
}

/*
 * Callback for the close button.
 * Call ident_dialog_update to save any changes, and close the dialog if
 * OK.
 */
static gboolean
close_cb(gpointer data)
{
    GtkWidget *w = GTK_WIDGET(data);
    GtkDialog *dialog =
        GTK_DIALOG(gtk_widget_get_ancestor(w, GTK_TYPE_DIALOG));

    return ident_dialog_update(dialog);
}

/*
 * Create a new identity
 */
static gboolean
new_ident_cb(gpointer user_data)
{
    LibBalsaIdentity *ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GList **identities =
        g_object_get_data(G_OBJECT(tree), "identities");

    *identities = g_list_append(*identities, ident);
    identity_list_update(tree);
    /* select just added identity */
    select_identity(tree, ident);

    return FALSE;
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static GtkWidget*
setup_ident_frame(GtkDialog * dialog, gboolean createp, gpointer tree)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
    GtkWidget *table = gtk_table_new(15, 2, FALSE);
    gint row = 0;

    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_container_set_border_width(GTK_CONTAINER(table), padding);

    gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_table_set_row_spacings(GTK_TABLE(table), padding);
    gtk_table_set_col_spacings(GTK_TABLE(table), padding);

    ident_dialog_add_entry(table, row++, dialog, _("Identity Name:"), 
		           "identity-name");
    ident_dialog_add_entry(table, row++, dialog, _("Full Name:"), 
                           "identity-fullname");
    ident_dialog_add_entry(table, row++, dialog, _("Mailing Address:"), 
                           "identity-address");
    ident_dialog_add_entry(table, row++, dialog, _("Reply To:"), 
                           "identity-replyto");
    ident_dialog_add_entry(table, row++, dialog, _("Domain:"), 
                           "identity-domain");
    ident_dialog_add_entry(table, row++, dialog, _("Bcc:"), 
                           "identity-bcc");
    ident_dialog_add_entry(table, row++, dialog, _("Reply String:"), 
                           "identity-replystring");
    ident_dialog_add_entry(table, row++, dialog, _("Forward String:"), 
                           "identity-forwardstring");
    ident_dialog_add_entry(table, row++, dialog, _("Signature Path:"), 
                           "identity-sigpath");
    
    ident_dialog_add_checkbutton(table, row++, dialog,
                                _("Execute Signature"),
				"identity-sigexecutable");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Include Signature"), 
                                 "identity-sigappend");
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("Include Signature When Forwarding"),
                                 "identity-whenforward");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Include Signature When Replying"),
                                 "identity-whenreply");
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("Add Signature Separator"),
                                 "identity-sigseparator");
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Prepend Signature"),
                                 "identity-sigprepend");

    return GTK_WIDGET(frame);
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

    check = gtk_check_button_new_with_label(check_label);
    gtk_table_attach_defaults(GTK_TABLE(table), check, 0, 2, row, row + 1);
    g_object_set_data(G_OBJECT(dialog), check_key, check);
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

    label = gtk_label_new(label_name);
    gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, row, row + 1);

    entry = gtk_entry_new();
    gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, row, row + 1);

    g_object_set_data(G_OBJECT(dialog), entry_key, entry);
    if (row == 0)
        gtk_widget_grab_focus(entry);
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
    GtkWidget* error, *tree;
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
        error =
            gtk_message_dialog_new(GTK_WINDOW(dlg),
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE,
                                   _("Error: The identity "
                                     "does not have a name"));
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
        return FALSE;
    }

    for (list = *identities; list; list = g_list_next(list)) {
        exist_ident = LIBBALSA_IDENTITY(list->data);
        
        if (g_ascii_strcasecmp(exist_ident->identity_name, text) == 0
            && id != exist_ident) {
            error = 
                gtk_message_dialog_new(GTK_WINDOW(dlg),
                                       GTK_DIALOG_MODAL,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_CLOSE,
                                       _("Error: An identity with that"
                                         " name already exists"));
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
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

    /* should never be true with the _BROWSE selection mode
     * as long as there are any elements on the list */

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
identity_delete_selected(GtkTreeView *tree)
{
    LibBalsaIdentity* ident;
    GList **identities;
    GtkWidget *dialog =
        gtk_widget_get_ancestor(GTK_WIDGET(tree), GTK_TYPE_DIALOG);

    ident = get_selected_identity(tree);
    identities = g_object_get_data(G_OBJECT(tree), "identities");
    *identities = g_list_remove(*identities, ident);
    g_object_set_data(G_OBJECT(dialog), "identity", NULL);
    identity_list_update(tree);
    g_object_unref(ident);
}

/* 
 * Delete the currently selected identity after confirming. 
 */
static gboolean
delete_ident_cb(gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkWidget* confirm;
    GtkWidget* parent;
    
    ident = get_selected_identity(tree);
    default_id = g_object_get_data(G_OBJECT(tree), "default-id");
    g_return_val_if_fail(ident != *default_id, FALSE);
    parent = gtk_widget_get_ancestor(GTK_WIDGET(tree), GTK_TYPE_WINDOW);
    confirm = gtk_message_dialog_new(GTK_WINDOW(parent),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_OK_CANCEL,
                                     _("Do you really want to delete"
                                       " the selected identity?"));
                      
    if(gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_OK)
        identity_delete_selected(tree);
    gtk_widget_destroy(confirm);

    return FALSE;
}

/*
 * Show the help file.
 */
static gboolean
help_ident_cb(gpointer user_data)
{
    static const gchar file_name[] = "identities.html";
    GError *err = NULL;

    gnome_help_display(file_name, NULL, &err);

    if (err) {
        g_print(_("Error displaying %s: %s\n"), file_name,
                err->message);
        g_error_free(err);
    }

    return FALSE;
}

/* in src/balsa-app.c: */
extern GtkWidget *balsa_stock_button_with_label(const gchar *,
                                                const gchar *);

gint
libbalsa_identity_config_dialog(GtkWindow *parent, GList **identities,
				LibBalsaIdentity **default_id)
{
    GtkWidget* dialog;
    GtkWidget* frame = libbalsa_identity_config_frame(identities, 
						      default_id);
    GtkWidget* display_frame;
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkTreeView* tree;
    gint i;

    /* the order here must match the IDENTITY_RESPONSE enum: */
    const static struct {
        gboolean (*cb)(gpointer user_data);
        const gchar *pixmap;
    } buttons[] = {
	{ close_cb,        GTK_STOCK_CLOSE  },
	{ new_ident_cb,    GTK_STOCK_NEW    },
	{ delete_ident_cb, GTK_STOCK_REMOVE },
	{ help_ident_cb,   GTK_STOCK_HELP   }
    };
    GtkTreeSelection *select;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    tree = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(frame)));
    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    model = gtk_tree_view_get_model(tree);
    g_return_val_if_fail(gtk_tree_selection_get_selected(select,
                                                         &model,
                                                         &iter),
                         GTK_RESPONSE_NONE);

    dialog = gtk_dialog_new_with_buttons(_("Manage Identities"),
                                         parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         NULL);
    g_object_set_data(G_OBJECT(dialog), "tree", tree);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);

    display_frame = setup_ident_frame(GTK_DIALOG(dialog),
                                      FALSE, tree);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    g_object_set_data(G_OBJECT(tree), "frame", display_frame);
    g_signal_connect(G_OBJECT(tree), "row-activated",
                     G_CALLBACK(set_default_ident_cb), NULL);
    column = gtk_tree_view_get_column (tree, DEFAULT_COLUMN);
    renderer = gtk_tree_view_column_get_cell_renderers(column)->data;
    g_signal_connect(G_OBJECT(renderer), "toggled",
                     G_CALLBACK(toggle_cb), tree);
    gtk_tree_view_column_set_title(column, "Default");

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));

    gtk_widget_grab_focus(GTK_WIDGET(tree));

    for(i = 0; i < (gint) ELEMENTS(buttons); i++)
        gtk_dialog_add_button(GTK_DIALOG(dialog), buttons[i].pixmap, i);

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->action_area));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    IDENTITY_RESPONSE_CLOSE);
    config_dialog_select(select, dialog);

    while ((i = gtk_dialog_run(GTK_DIALOG(dialog))) >= 0) {
        if (i < (gint) ELEMENTS(buttons) && buttons[i].cb(tree))
            break;
    }

    gtk_widget_destroy(dialog);

    return i;
}

/* config_dialog_select
 *
 * called when the tree's selection changes
 * manage the button sensitivity, and update the display frame
 */
static void
config_dialog_select(GtkTreeSelection * selection, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkTreeView *tree = gtk_tree_selection_get_tree_view(selection);
    GtkDialog* dialog = GTK_DIALOG(user_data);

    ident = get_selected_identity(tree);
    if (!ident)
        return;
    
    /* disable default identity selection and deletion */
    default_id = g_object_get_data(G_OBJECT(tree), "default-id");
    gtk_dialog_set_response_sensitive(dialog, IDENTITY_RESPONSE_DELETE,
                                      ident != *default_id);
    display_frame_update(dialog, ident);
    g_object_set_data(G_OBJECT(dialog), "identity", ident);
}

static void 
display_frame_update(GtkDialog * dialog, LibBalsaIdentity* ident)
{
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
}


static void
display_frame_set_field(GtkDialog * dialog, const gchar* key, const gchar* value)
{
    GtkEntry *entry = GTK_ENTRY(g_object_get_data(G_OBJECT(dialog), key));
    
    gtk_entry_set_text(entry, value ? value : "");
}

static void
display_frame_set_boolean(GtkDialog * dialog, const gchar* key, gboolean value)
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

    gnome_config_pop_prefix();
}

