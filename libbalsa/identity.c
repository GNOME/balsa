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

static void libbalsa_identity_destroy(GtkObject* object);

static void new_ident_cb(GtkButton* , gpointer);
static void edit_ident_cb(GtkButton* , gpointer);
static void set_default_ident_cb(GtkButton* , gpointer);
static void delete_ident_cb(GtkButton* , gpointer);
static void identity_list_update(GtkTreeView * tree);
static void config_frame_button_select_cb(GtkTreeSelection * treeselection,
                                          gpointer user_data);

static GtkWidget* setup_ident_dialog(GtkWindow* parent, gboolean createp, 
				     LibBalsaIdentity*, gpointer);

static void ident_dialog_add_checkbutton(GtkWidget *, gint, GtkDialog *,
                                         const gchar *, const gchar *,
                                         gboolean);
static void ident_dialog_add_entry(GtkWidget *, gint, GtkDialog *,
                                   const gchar *, const gchar *, gchar *);
static gchar *ident_dialog_get_text(GtkDialog *, const gchar *);
static gboolean ident_dialog_get_bool(GtkDialog *, const gchar *);
static gboolean ident_dialog_update(GtkDialog *);
static void config_dialog_select_cb(GtkTreeSelection * selection,
                                    gpointer data);

static GtkWidget* libbalsa_identity_display_frame(void);
static void  display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                                     const gchar* name, const gchar* key);
static void display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                                      const gchar* name, const gchar* key);
static void display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident);
static void display_frame_set_field(GtkFrame* frame, const gchar* key, 
                                    const gchar* value);
static void display_frame_set_boolean(GtkFrame* frame, const gchar* key, 
                                      gboolean value);


static void select_dialog_row_cb(GtkTreeSelection * selection,
                                 LibBalsaIdentity ** identity);
static void select_identity(GtkTreeView * tree,
                            LibBalsaIdentity * identity);
static LibBalsaIdentity *get_selected_identity(GtkTreeView * tree);
static GtkWidget *libbalsa_identity_tree(GtkWindow * parent,
                                         GList ** identities,
                                         LibBalsaIdentity ** defid,
                                         GCallback user_function,
                                         gpointer user_data,
                                         GtkSelectionMode mode);


GtkType
libbalsa_identity_get_type()
{
    static GtkType libbalsa_identity_type = 0;
    
    if (!libbalsa_identity_type) {
        static const GtkTypeInfo libbalsa_identity_info = 
            {
                "LibBalsaIdentity",
                sizeof(LibBalsaIdentity),
                sizeof(LibBalsaIdentityClass),
                (GtkClassInitFunc) libbalsa_identity_class_init,
                (GtkObjectInitFunc) libbalsa_identity_init,
                /* reserved_1 */ NULL,
                /* reserved_2 */ NULL,
                (GtkClassInitFunc) NULL
            };
        
        libbalsa_identity_type = 
            gtk_type_unique(gtk_object_get_type(), &libbalsa_identity_info);
    }
    
    return libbalsa_identity_type;
}


static void
libbalsa_identity_class_init(LibBalsaIdentityClass* klass)
{
    GtkObjectClass* object_class;

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class = GTK_OBJECT_CLASS(klass);
    object_class->destroy = libbalsa_identity_destroy;
}


/* 
 * Class inititialization function, set defaults for new objects
 */
static void
libbalsa_identity_init(LibBalsaIdentity* ident)
{
    ident->identity_name = NULL;
    ident->address = libbalsa_address_new();
    gtk_object_ref(GTK_OBJECT(ident->address)); 
    gtk_object_sink(GTK_OBJECT(ident->address));
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
GtkObject* 
libbalsa_identity_new(void) 
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(default_ident_name);
    return GTK_OBJECT(ident);
}


/*
 * Create a new object with the specified identity name.  Does not add
 * it to the list of identities for the application.
 */
GtkObject*
libbalsa_identity_new_with_name(const gchar* ident_name)
{
    LibBalsaIdentity* ident;
    
    ident = LIBBALSA_IDENTITY(gtk_type_new(LIBBALSA_TYPE_IDENTITY));
    ident->identity_name = g_strdup(ident_name);
    return GTK_OBJECT(ident);
}


/* 
 * Destroy the object, freeing all the values in the process.
 */
static void
libbalsa_identity_destroy(GtkObject* object)
{
    LibBalsaIdentity* ident;
    
    g_return_if_fail(object != NULL);
    
    ident = LIBBALSA_IDENTITY(object);

    gtk_object_destroy(GTK_OBJECT(ident->address));
    g_free(ident->identity_name);
    g_free(ident->replyto);
    g_free(ident->domain);
    g_free(ident->bcc);
    g_free(ident->reply_string);
    g_free(ident->forward_string);
    g_free(ident->signature_path);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
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
    
    gtk_object_unref(GTK_OBJECT(ident->address));
    gtk_object_ref(GTK_OBJECT(ad));
    gtk_object_sink(GTK_OBJECT(ad));
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





/*
 * The close callback for the editing/creation dialog.
 * if we create new identity and the dialog gets closed, we need to destroy
 * the half-created object.
 */
static int
ident_dialog_cleanup(GtkDialog* dialog)
{
    gboolean edit;
    gpointer tmpptr;

    edit = (gboolean) gtk_object_get_data(GTK_OBJECT(dialog), 
                                          "identity-edit");
    if (!edit) {
        tmpptr = gtk_object_get_data(GTK_OBJECT(dialog), "identity");
        
        if (tmpptr != NULL) {
            gtk_object_destroy(GTK_OBJECT(tmpptr));
	    g_warning("Object destroyed.\n");
	}
    }

    return FALSE;
}

enum{
    ICON_COLUMN,
    NAME_COLUMN,
    IDENT_COLUMN,
    N_COLUMNS
};

static GtkWidget *
libbalsa_identity_tree(GtkWindow* parent, GList** identities,
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
                               GDK_TYPE_PIXBUF,
                               G_TYPE_STRING,
                               G_TYPE_POINTER);

    tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT (store));
    gtk_object_set_data(GTK_OBJECT(tree), "parent-window", parent);
    gtk_object_set_data(GTK_OBJECT(tree), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(tree), "default-id", defid);

    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes ("Icon", renderer,
                                                       "pixbuf", ICON_COLUMN,
                                                       NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
                                                       "text", NAME_COLUMN,
                                                       NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (tree), FALSE);
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

    tree = libbalsa_identity_tree(parent, identities, defid,
                                  G_CALLBACK(select_dialog_row_cb),
                                  &identity, GTK_SELECTION_SINGLE);
    dialog = gtk_dialog_new_with_buttons(prompt, 
                                         parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_CANCEL,
                                         NULL);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       frame1, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame1), tree);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), padding);
    gtk_widget_show_all(frame1);

    /* default action=Enter is to select the identity, but we'll make
     * the button invisible, to simplify the dialog */
    gtk_widget_hide(gtk_dialog_add_button(GTK_DIALOG(dialog),
                    GTK_STOCK_OK, GTK_RESPONSE_OK));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
        identity = NULL;
    gtk_idle_add((GtkFunction) destroy_dialog, dialog);
    
    return identity;
}

/* callback for the selection "changed" signal:
 * save the selected identity and close the dialog */
static void
select_dialog_row_cb(GtkTreeSelection * selection,
                     LibBalsaIdentity ** identity)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkWidget *widget;
        
        gtk_tree_model_get(model, &iter, IDENT_COLUMN, identity, -1);

        widget = GTK_WIDGET(gtk_tree_selection_get_tree_view(selection));
        widget = gtk_widget_get_ancestor(widget, GTK_TYPE_DIALOG);
        if (widget)
            gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_OK);
    }
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
        libbalsa_identity_tree(NULL, identities, defid,
                               G_CALLBACK(config_frame_button_select_cb),
                               config_frame, GTK_SELECTION_BROWSE);
    gtk_container_add(GTK_CONTAINER(config_frame), tree);

    gtk_object_set_data(GTK_OBJECT(tree), "identities", identities);
    gtk_object_set_data(GTK_OBJECT(tree), "default-id", defid);

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

    identities = gtk_object_get_data(GTK_OBJECT(tree), "identities");
    default_id = gtk_object_get_data(GTK_OBJECT(tree), "default-id");

    current = get_selected_identity(tree);
    if (!current)
        current = *default_id;

    gtk_list_store_clear(store);
    
    sorted = g_list_copy(*identities);
    sorted = g_list_sort(sorted, (GCompareFunc) compare_identities);
    for (list = sorted; list; list = g_list_next(list)) {
        GdkPixbuf *pixbuf;

        ident = LIBBALSA_IDENTITY(list->data);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           NAME_COLUMN, ident->identity_name,
                           IDENT_COLUMN, ident,
                           -1);
        
        pixbuf = gtk_widget_render_icon(GTK_WIDGET(tree),
                                        ident == *default_id ?
                                        GNOME_STOCK_MENU_FORWARD :
                                        GNOME_STOCK_MENU_BLANK,
                                        GTK_ICON_SIZE_BUTTON,
                                        "Balsa");
        gtk_list_store_set(store, &iter, ICON_COLUMN, pixbuf, -1);
    }
    g_list_free(sorted);

    select_identity(tree, current);
}

static void
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
            break;
        }
    }
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
    RESPONSE_NEW,
    RESPONSE_EDIT,
    RESPONSE_SETDEFAULT,
    RESPONSE_DELETE
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
                                      RESPONSE_EDIT, state);
    gtk_dialog_set_response_sensitive((GtkDialog *) widget,
                                      RESPONSE_SETDEFAULT, state);
    gtk_dialog_set_response_sensitive((GtkDialog *) widget,
                                      RESPONSE_DELETE, state);

    config_dialog_select_cb(selection, (GtkDialog *) widget);
}

/*
 * Create a new identity dialog
 */
static void
new_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident;
    GtkWidget* dialog;
    GtkWindow* parent;

    ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
    parent = gtk_object_get_data(GTK_OBJECT(user_data), "parent-window");
    dialog = setup_ident_dialog(parent, TRUE, ident, user_data);
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if(ident_dialog_update(GTK_DIALOG(dialog))) {
            GtkTreeView *tree = GTK_TREE_VIEW(user_data);
            GList** identities = 
                gtk_object_get_data(GTK_OBJECT(tree), "identities");

            *identities = g_list_append(*identities, ident);
            gtk_object_set_data(GTK_OBJECT(dialog), "identity", NULL);
            identity_list_update(tree);
            /* select just added identity */
            select_identity(tree, ident);
        }
    }
    gtk_widget_destroy(dialog);
}

/*
 * Edit the selected identity in a new dialog.
 * there is always exactly one identity selected - _BROWSE selection mode.
 */
static void 
edit_ident_cb(GtkButton* button, gpointer user_data)
{
    GtkWidget* dialog;
    GtkWindow* parent;
    LibBalsaIdentity* ident;
    GtkTreeView* tree = GTK_TREE_VIEW(user_data);

    ident = get_selected_identity(tree);
    g_return_if_fail(ident != NULL);
    
    parent = gtk_object_get_data(GTK_OBJECT(tree), "parent-window");
    dialog = setup_ident_dialog(parent, FALSE, ident, tree);
    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        if(ident_dialog_update(GTK_DIALOG(dialog)))
            identity_list_update(tree);
    }
    gtk_widget_destroy(dialog);
}


/*
 * Put the required GtkEntries, Labels, and Checkbuttons in the dialog
 * for creating/editing identities.
 */
static GtkWidget*
setup_ident_dialog(GtkWindow* parent, gboolean createp, 
                   LibBalsaIdentity* ident, gpointer tree)
{
     
    GtkWidget* frame = gtk_frame_new(NULL);
    GtkWidget *table = gtk_table_new(15, 2, FALSE);
    gint row = 0;
    GtkWidget* main_box;
    GtkDialog* dialog;
    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(createp 
                                                    ? _("Create Identity")  
                                                    : _("Edit Identity"),
                                                    parent,
                                                    GTK_DIALOG_MODAL,
                                                    createp 
                                                    ? GTK_STOCK_OK 
                                                    : GTK_STOCK_APPLY,
                                                    GTK_RESPONSE_OK,
                                                    GTK_STOCK_CANCEL,
                                                    GTK_RESPONSE_CANCEL,
                                                    NULL));
    main_box = dialog->vbox;
    gtk_object_set_data(GTK_OBJECT(dialog), "tree", tree);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity", ident);
    gtk_object_set_data(GTK_OBJECT(dialog), "identity-edit", 
			(gpointer)!createp);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_box_pack_start(GTK_BOX(main_box), frame, TRUE, TRUE, 0);

    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), padding);
    gtk_container_set_border_width(GTK_CONTAINER(table), padding);

    gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_table_set_row_spacings(GTK_TABLE(table), padding);
    gtk_table_set_col_spacings(GTK_TABLE(table), padding);

    g_signal_connect (G_OBJECT (dialog), "close", 
                      G_CALLBACK (ident_dialog_cleanup),
                      G_OBJECT (dialog));

    ident_dialog_add_entry(table, row++, dialog, _("Identity Name:"), 
		           "identity-name",
			   ident->identity_name);

    ident_dialog_add_entry(table, row++, dialog, _("Full Name:"), 
                           "identity-fullname",
                           ident->address->full_name);
    
    if (ident->address->address_list != NULL) {
        ident_dialog_add_entry(table, row++, dialog, _("Mailing Address:"), 
                               "identity-address",
                               (gchar*) ident->address->address_list->data);
    } else {
        ident_dialog_add_entry(table, row++, dialog, _("Mailing Address:"),
                               "identity-address",
                               NULL);
    }
    
    ident_dialog_add_entry(table, row++, dialog, _("Reply To:"), 
                           "identity-replyto",
                           ident->replyto);
    ident_dialog_add_entry(table, row++, dialog, _("Domain:"), 
                           "identity-domain",
                           ident->domain);
    ident_dialog_add_entry(table, row++, dialog, _("Bcc:"), 
                           "identity-bcc",
                           ident->bcc);
    ident_dialog_add_entry(table, row++, dialog, _("Reply String:"), 
                           "identity-replystring",
                           ident->reply_string);
    ident_dialog_add_entry(table, row++, dialog, _("Forward String:"), 
                           "identity-forwardstring",
                           ident->forward_string);
    ident_dialog_add_entry(table, row++, dialog, _("Signature Path:"), 
                           "identity-sigpath",
                           ident->signature_path);
    
    ident_dialog_add_checkbutton(table, row++, dialog,
                                _("Execute Signature"),
				"identity-sigexecutable",
				ident->sig_executable);
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Include Signature"), 
                                 "identity-sigappend", 
                                 ident->sig_sending);
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("Include Signature When Forwarding"),
                                 "identity-whenforward", 
                                 ident->sig_whenforward);
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Include Signature When Replying"),
                                 "identity-whenreply", 
                                 ident->sig_whenreply);
    ident_dialog_add_checkbutton(table, row++, dialog, 
                                 _("Add Signature Separator"),
                                 "identity-sigseparator", 
                                 ident->sig_separator);
    ident_dialog_add_checkbutton(table, row++, dialog,
                                 _("Prepend Signature"),
                                 "identity-sigprepend",
                                 ident->sig_prepend);
    gtk_widget_show_all(main_box);
    return GTK_WIDGET(dialog);
}


/*
 * Create and add a GtkCheckButton to the given dialog with caption
 * and add a pointer to it stored under the given key.  The check
 * button is initialized to the given value.
 */
static void
ident_dialog_add_checkbutton(GtkWidget * table, gint row,
                             GtkDialog * dialog, const gchar * check_label,
                             const gchar * check_key, gboolean init_value)
{
    GtkWidget *check;

    check = gtk_check_button_new_with_label(check_label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), init_value);
    gtk_table_attach_defaults(GTK_TABLE(table), check, 0, 2, row, row + 1);
    gtk_object_set_data(GTK_OBJECT(dialog), check_key, check);
}


/*
 * Add a GtkEntry to the given dialog with a label next to it
 * explaining the contents.  A reference to the entry is stored as
 * object data attached to the dialog with the given key.  The entry
 * is initialized to the init_value given.
 */
static void
ident_dialog_add_entry(GtkWidget * table, gint row, GtkDialog * dialog,
                       const gchar * label_name, const gchar * entry_key,
                       gchar * init_value)
{
    GtkWidget *label;
    GtkWidget *entry;

    label = gtk_label_new(label_name);
    gtk_misc_set_alignment(GTK_MISC(label), 1, 0.5);
    gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, row, row + 1);

    entry = gtk_entry_new();
    if (init_value != NULL)
        gtk_entry_set_text(GTK_ENTRY(entry), init_value);
    gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, row, row + 1);

    gtk_object_set_data(GTK_OBJECT(dialog), entry_key, entry);
    if (row == 0)
        gtk_widget_grab_focus(entry);
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
    
    id = gtk_object_get_data(GTK_OBJECT(dlg), "identity");
    tree = gtk_object_get_data(GTK_OBJECT(dlg), "tree");
    identities = gtk_object_get_data(GTK_OBJECT(tree), "identities");

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
        
        if (g_strcasecmp(exist_ident->identity_name, text) == 0 && 
	    id != exist_ident) {
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
    
    entry = GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(dialog), key));
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
    
    button = GTK_CHECK_BUTTON(gtk_object_get_data(GTK_OBJECT(dialog), key));
    value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    
    return value;
}

/* 
 * Set the default identity to the currently selected.
 */
static void
set_default_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    
    /* should never be true with the _BROWSE selection mode
     * as long as there are any elements on the list */

    default_id = gtk_object_get_data(GTK_OBJECT(tree), "default-id");
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

    ident = get_selected_identity(tree);
    identities = gtk_object_get_data(GTK_OBJECT(tree), "identities");
    *identities = g_list_remove(*identities, ident);
    identity_list_update(tree);
    gtk_object_destroy(GTK_OBJECT(ident));
}

/* 
 * Delete the currently selected identity after confirming. 
 */
static void
delete_ident_cb(GtkButton* button, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkTreeView *tree = GTK_TREE_VIEW(user_data);
    GtkWidget* confirm;
    GtkWindow* parent;
    
    ident = get_selected_identity(tree);
    default_id = gtk_object_get_data(GTK_OBJECT(tree), "default-id");
    g_return_if_fail(ident != *default_id);
    parent = gtk_object_get_data(GTK_OBJECT(tree), "parent-window");
    confirm = gtk_message_dialog_new(parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_OK_CANCEL,
                                     _("Do you really want to delete"
                                       " the selected identity?"));
                      
    if(gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_OK)
        identity_delete_selected(tree);
    gtk_widget_destroy(confirm);
}

/* row_activated_cb:
 * set the activated identity (which is also the currently selected
 * identity) as the default.
 */
static void
row_activated_cb(GtkTreeView * tree, GtkTreePath * path,
                 GtkTreeViewColumn * column, gpointer data)
{
    set_default_ident_cb(NULL, (gpointer) tree);
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
    GtkWidget* display_frame = libbalsa_identity_display_frame();
    GtkWidget* hbox = gtk_hbox_new(FALSE, padding);
    GtkTreeView* tree;
    gint i;
    const static struct {
        const gchar* label; 
        GCallback cb;
        const gchar *pixmap;
    } buttons[] = {
	{ N_("_New"),         G_CALLBACK(new_ident_cb),
          GNOME_STOCK_PIXMAP_NEW        },
	{ N_("_Edit"),        G_CALLBACK(edit_ident_cb),
          GNOME_STOCK_PIXMAP_PROPERTIES },
	{ N_("_Set Default"), G_CALLBACK(set_default_ident_cb),
          GTK_STOCK_APPLY               },
	{ N_("_Delete"),      G_CALLBACK(delete_ident_cb),
          GNOME_STOCK_PIXMAP_REMOVE     } 
    };
    GtkTreeSelection *select;
    GtkTreeModel *model;
    GtkTreeIter iter;

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
                                         GTK_STOCK_CLOSE,
                                         GTK_RESPONSE_CLOSE,
                                         NULL);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), 
                       hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), display_frame, TRUE, TRUE, 0);

    gtk_object_set_data(GTK_OBJECT(tree), "parent-window", parent);
    gtk_object_set_data(GTK_OBJECT(tree), "frame", display_frame);
    gtk_signal_connect(GTK_OBJECT(tree), "row-activated",
                       GTK_SIGNAL_FUNC(row_activated_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));

    gtk_widget_grab_focus(GTK_WIDGET(tree));

    for(i = 0; i < (gint) ELEMENTS(buttons); i++) {
        GtkWidget* butt = balsa_stock_button_with_label(buttons[i].pixmap,
                buttons[i].label);
        gtk_dialog_add_action_widget(GTK_DIALOG(dialog), butt, i);
    }
    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->action_area));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    config_dialog_select_cb(select, dialog);

    while ((i = gtk_dialog_run(GTK_DIALOG(dialog))) >= 0)
        if (i < (gint) ELEMENTS(buttons))
            ((void (*)(gpointer, gpointer)) buttons[i].cb)(NULL, tree);

    gtk_widget_destroy(dialog);

    return i;
}

/* config_dialog_select_cb
 *
 * connected to the "changed" signal of the tree's selection, so it's
 * called both when a selection is made, and when the tree is cleared
 */
static void
config_dialog_select_cb(GtkTreeSelection * selection, gpointer user_data)
{
    LibBalsaIdentity* ident, **default_id;
    GtkFrame *frame;
    GtkTreeView *tree = gtk_tree_selection_get_tree_view(selection);
    GtkDialog* dialog = GTK_DIALOG(user_data);

    ident = get_selected_identity(tree);
    if (!ident)
        return;
    
    /* disable default identity selection and deletion */
    default_id = gtk_object_get_data(GTK_OBJECT(tree), "default-id");
    gtk_dialog_set_response_sensitive(dialog, RESPONSE_SETDEFAULT,
                                      ident != *default_id);
    gtk_dialog_set_response_sensitive(dialog, RESPONSE_DELETE,
                                      ident != *default_id);
    frame = gtk_object_get_data(GTK_OBJECT(tree), "frame");
    g_return_if_fail(frame);
    display_frame_update(frame, ident);
}

static GtkWidget*
libbalsa_identity_display_frame(void)
{
    GtkWidget* frame1 = gtk_frame_new(NULL);
    GtkWidget* vbox1 = gtk_vbox_new(FALSE, padding/2);
    
    gtk_container_add(GTK_CONTAINER(frame1), vbox1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox1), padding);

    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Identity Name:"), "identity-name");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Full Name:"), "identity-fullname");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Mailing Address:"), "identity-address");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply To:"), "identity-replyto");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Domain:"), "identity-domain");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Bcc:"), "identity-bcc");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Reply String:"), "identity-replystring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Forward String:"), "identity-forwardstring");
    display_frame_add_field(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                            _("Signature Path:"), "identity-sigpath");

    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1),
			      _("Execute Signature"), "identity-sigexecutable");

    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature"), "identity-sigappend");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature When Forwarding"), 
                              "identity-whenforward");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Include Signature When Replying"), 
                              "identity-whenreply");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Add Signature Separator"), 
                              "identity-sigseparator");
    display_frame_add_boolean(GTK_FRAME(frame1), GTK_BOX(vbox1), 
                              _("Prepend Signature"), 
                              "identity-sigprepend");
    
    gtk_widget_show_all(frame1);

    return frame1;
}


static void 
display_frame_add_field(GtkFrame* frame, GtkBox* box, 
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(name);
    GtkWidget* label2 = gtk_label_new(NULL);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label2);
}


static void
display_frame_add_boolean(GtkFrame* frame, GtkBox* box,
                        const gchar* name, const gchar* key)
{
    GtkWidget* hbox = gtk_hbox_new(FALSE, 4);
    GtkWidget* label1 = gtk_label_new(NULL);
    GtkWidget* label2 = gtk_label_new(name);
    
    gtk_box_pack_start(box, hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label2, FALSE, FALSE, 0);
    gtk_object_set_data(GTK_OBJECT(frame), key, label1);
}


static void 
display_frame_update(GtkFrame* frame, LibBalsaIdentity* ident)
{
    display_frame_set_field(frame, "identity-name", ident->identity_name);
    display_frame_set_field(frame, "identity-fullname", 
                            ident->address->full_name);
    if (ident->address->address_list)
        display_frame_set_field(frame, "identity-address", 
                                (gchar*)ident->address->address_list->data);
    else
        display_frame_set_field(frame, "identity-address", NULL);
    
    display_frame_set_field(frame, "identity-replyto", ident->replyto);
    display_frame_set_field(frame, "identity-domain", ident->domain);
    display_frame_set_field(frame, "identity-bcc", ident->bcc);
    display_frame_set_field(frame, "identity-replystring", 
                            ident->reply_string);
    display_frame_set_field(frame, "identity-forwardstring", 
                            ident->forward_string);
    display_frame_set_field(frame, "identity-sigpath", ident->signature_path);

    display_frame_set_boolean(frame, "identity-sigexecutable", ident->sig_executable);

    display_frame_set_boolean(frame, "identity-sigappend", ident->sig_sending);
    display_frame_set_boolean(frame, "identity-whenforward", 
                              ident->sig_whenforward);
    display_frame_set_boolean(frame, "identity-whenreply", 
                              ident->sig_whenreply);
    display_frame_set_boolean(frame, "identity-sigseparator", 
                              ident->sig_separator);    
    display_frame_set_boolean(frame, "identity-sigprepend", 
                              ident->sig_prepend);    
}


static void
display_frame_set_field(GtkFrame* frame, const gchar* key, const gchar* value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value);
}

static void
display_frame_set_boolean(GtkFrame* frame, const gchar* key, gboolean value)
{
    GtkLabel* label = GTK_LABEL(gtk_object_get_data(GTK_OBJECT(frame), key));
    
    gtk_label_set_text(label, value ? _("Do") : _("Do Not"));
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

