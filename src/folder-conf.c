/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
#include <string.h>
#include "balsa-app.h"
#include "balsa-icons.h"
#include "folder-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "pref-manager.h"

typedef struct _CommonDialogData CommonDialogData;
typedef struct _FolderDialogData FolderDialogData;
typedef struct _SubfolderDialogData SubfolderDialogData;

typedef void (*CommonDialogFunc)(CommonDialogData * cdd);

#define FOLDER_CONF_COMMON \
    GtkDialog *dialog; \
    BalsaMailboxNode *mbnode; \
    CommonDialogFunc ok

#define BALSA_FOLDER_CONF_IMAP_KEY "balsa-folder-conf-imap"

struct _CommonDialogData {
    FOLDER_CONF_COMMON;
};

struct _FolderDialogData {
    FOLDER_CONF_COMMON;
    GtkWidget *folder_name, *server, *port, *username, *remember,
        *password, *subscribed, *list_inbox, *prefix;
#ifdef USE_SSL
    GtkWidget *use_ssl;
#endif
};

/* FIXME: identity_name will leak on cancelled folder edition */

struct _SubfolderDialogData {
    FOLDER_CONF_COMMON;
    GtkWidget *parent_folder, *folder_name, *identity_label;
    gchar *old_folder, *old_parent, *identity_name;
    BalsaMailboxNode *parent;   /* (new) parent of the mbnode.  */
    /* Used for renaming and creation */
};

/* Destroy notification */
static void
folder_conf_destroy_cdd(CommonDialogData * cdd)
{
    if (cdd->dialog) {
        /* The mailbox node was destroyed. Close the dialog, but don't
         * trigger further calls to folder_conf_destroy_cdd. */
        cdd->mbnode = NULL;
        gtk_dialog_response(cdd->dialog, GTK_RESPONSE_NONE);
    } else
        g_free(cdd);
}

static const gchar folder_config_section[] = "folder-config";

static void
folder_conf_response(GtkDialog * dialog, int response,
                     CommonDialogData * cdd)
{
    GError *err = NULL;

    switch (response) {
    case GTK_RESPONSE_HELP:
        gnome_help_display("balsa", folder_config_section, &err);
        if (err) {
            g_print(_("Error displaying %s: %s\n"), folder_config_section,
                    err->message);
            g_error_free(err);
        }
        return;
    case GTK_RESPONSE_OK:
        cdd->ok(cdd);
        /* Fall over */
    default:
        gtk_widget_destroy(GTK_WIDGET(cdd->dialog));
        cdd->dialog = NULL;
        if (cdd->mbnode)
            /* Clearing the data signifies that the dialog has been
             * destroyed. It also triggers a call to
             * folder_conf_destroy_cdd. */
            g_object_set_data(G_OBJECT(cdd->mbnode),
                              BALSA_FOLDER_CONF_IMAP_KEY, NULL);
        else
            /* Cancelling, without creating a mailbox node. Nobody owns
             * the xDialogData, so we'll free it here. */
            g_free(cdd);
        break;
    }
}

/* folder_conf_imap_node:
   show configuration widget for given mailbox node, allow user to 
   modify it and update mailbox node accordingly.
   Creates the node when mn == NULL.
*/
static void 
validate_folder(GtkWidget *w, FolderDialogData * fcw)
{
    gboolean sensitive = TRUE;
    if (!*gtk_entry_get_text(GTK_ENTRY(fcw->folder_name)))
	sensitive = FALSE;
    else if (!*gtk_entry_get_text(GTK_ENTRY(fcw->server)))
	sensitive = FALSE;

    gtk_dialog_set_response_sensitive(fcw->dialog, GTK_RESPONSE_OK, sensitive);
}

#ifdef USE_SSL
static void imap_use_ssl_cb(GtkToggleButton * button,
                            FolderDialogData * fcw);

/* imap_use_ssl_cb:
 * set default text in the `port' entry box according to the state of
 * the `Use SSL' checkbox
 *
 * callback on toggling fcw->use_ssl
 * */
static void
imap_use_ssl_cb(GtkToggleButton * button, FolderDialogData * fcw)
{
    char *colon, *newhost;
    const gchar* host = gtk_entry_get_text(GTK_ENTRY(fcw->server));
    gchar* port = gtk_toggle_button_get_active(button) ? "993" : "143";

    if( (colon=strchr(host,':')) != NULL) 
        *colon = '\0';
    newhost = g_strconcat(host, ":", port, NULL);
    gtk_entry_set_text(GTK_ENTRY(fcw->server), newhost);
    g_free(newhost);
}
#endif

static void
remember_cb(GtkToggleButton * button, FolderDialogData * fcw)
{
    gtk_widget_set_sensitive(fcw->password,
                             gtk_toggle_button_get_active(button));
}

static void
folder_conf_clicked_ok(FolderDialogData * fcw)
{
    gboolean insert;
    LibBalsaServer *s;
    GNode *gnode;

    if (fcw->mbnode) {
        insert = FALSE;
        s = fcw->mbnode->server;
    } else {
        insert = TRUE;
        s = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
    }

    libbalsa_server_set_host(s, gtk_entry_get_text(GTK_ENTRY(fcw->server))
#ifdef USE_SSL
                             ,
                             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                          (fcw->use_ssl))
#endif
        );
    libbalsa_server_set_username(s,
                                 gtk_entry_get_text(GTK_ENTRY
                                                    (fcw->username)));
    s->remember_passwd =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->remember));
    libbalsa_server_set_password(s,
                                 gtk_entry_get_text(GTK_ENTRY
                                                    (fcw->password)));

    if (!fcw->mbnode) {
        fcw->mbnode = balsa_mailbox_node_new_imap_folder(s, NULL);
        /* The mailbox node takes over ownership of the
         * FolderDialogData. */
        g_object_set_data_full(G_OBJECT(fcw->mbnode),
                               BALSA_FOLDER_CONF_IMAP_KEY, fcw,
                               (GDestroyNotify) folder_conf_destroy_cdd);
    }

    g_free(fcw->mbnode->dir);
    fcw->mbnode->dir = g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw->prefix)));
    g_free(fcw->mbnode->name);
    fcw->mbnode->name =
        g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw->folder_name)));
    fcw->mbnode->subscribed =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->subscribed));
    fcw->mbnode->list_inbox =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->list_inbox));

    if (insert) {
        balsa_mailbox_nodes_lock(TRUE);
        g_node_append(balsa_app.mailbox_nodes,
                      gnode = g_node_new(fcw->mbnode));
        balsa_mailbox_node_append_subtree(fcw->mbnode, gnode);
        balsa_mailbox_nodes_unlock(TRUE);
        balsa_mblist_repopulate(balsa_app.mblist_tree_store);
        config_folder_add(fcw->mbnode, NULL);
        update_mail_servers();
    } else {
        balsa_mailbox_node_rescan(fcw->mbnode);
        config_folder_update(fcw->mbnode);
    }
}


/* folder_conf_imap_node:
   show the IMAP Folder configuration dialog for given mailbox node.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_node(BalsaMailboxNode *mn)
{
    GtkWidget *frame, *table, *label;
    FolderDialogData *fcw;
    static FolderDialogData *fcw_new;
    LibBalsaServer *s;
    gchar *default_server;

    /* Allow only one dialog per mailbox node, and one with mn == NULL
     * for creating a new folder. */
    fcw = mn ? g_object_get_data(G_OBJECT(mn), BALSA_FOLDER_CONF_IMAP_KEY)
             : fcw_new;
    if (fcw) {
        gdk_window_raise(GTK_WIDGET(fcw->dialog)->window);
        return;
    }

    s = mn ? mn->server : NULL;
    default_server = libbalsa_guess_imap_server();

    fcw = g_new(FolderDialogData, 1);
    fcw->ok = (CommonDialogFunc) folder_conf_clicked_ok;
    fcw->mbnode = mn;
    fcw->dialog =
        GTK_DIALOG(gtk_dialog_new_with_buttons
                   (_("Remote IMAP folder"),
                    GTK_WINDOW(balsa_app.main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    mn ? _("_Update") : _("_Create"), GTK_RESPONSE_OK,
                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                    GTK_STOCK_HELP, GTK_RESPONSE_HELP, NULL));
    gtk_window_set_wmclass(GTK_WINDOW(fcw->dialog), 
			   "folder_config_dialog", "Balsa");
    if (mn)
        g_object_set_data_full(G_OBJECT(mn),
                               BALSA_FOLDER_CONF_IMAP_KEY, fcw, 
                               (GDestroyNotify) folder_conf_destroy_cdd);
    else {
        fcw_new = fcw;
        g_object_add_weak_pointer(G_OBJECT(fcw->dialog),
                                  (gpointer) &fcw_new);
    }

    frame = gtk_frame_new(_("Remote IMAP folder set"));
    gtk_box_pack_start(GTK_BOX(fcw->dialog->vbox),
                       frame, TRUE, TRUE, 0);
    table = gtk_table_new(9, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    label = create_label(_("Descriptive _Name:"), table, 0);
    fcw->folder_name = create_entry(fcw->dialog, table,
                                   GTK_SIGNAL_FUNC(validate_folder),
                                   fcw, 0, mn ? mn->name : NULL, 
				   label);

    label = create_label(_("_Server:"), table, 1);
    fcw->server = create_entry(fcw->dialog, table,
                              GTK_SIGNAL_FUNC(validate_folder),
                              fcw, 1, s ? s->host : default_server,
			      label);

    label= create_label(_("Use_r name:"), table, 3);
    fcw->username = create_entry(fcw->dialog, table,
                                GTK_SIGNAL_FUNC(validate_folder),
                                fcw, 3, s ? s->user : g_get_user_name(), 
			        label);

    fcw->remember = create_check(fcw->dialog, _("_Remember password"), 
                                table, 4, s ? s->remember_passwd : TRUE);
    g_signal_connect(G_OBJECT(fcw->remember), "toggled",
                     G_CALLBACK(remember_cb), fcw);

    label = create_label(_("_Password:"), table, 5);
    fcw->password = create_entry(fcw->dialog, table, NULL, NULL, 5,
				s ? s->passwd : NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(fcw->password), FALSE);

    fcw->subscribed = create_check(fcw->dialog, _("Subscribed _folders only"), 
                                  table, 6, mn ? mn->subscribed : FALSE);
    fcw->list_inbox = create_check(fcw->dialog, _("_Always show INBOX"), 
                                  table, 7, mn ? mn->list_inbox : TRUE); 

    label = create_label(_("Pr_efix"), table, 8);
    fcw->prefix = create_entry(fcw->dialog, table, NULL, NULL, 8,
			      mn ? mn->dir : NULL, label);
    
#ifdef USE_SSL
    fcw->use_ssl = create_check(fcw->dialog,
			       _("Use SS_L (IMAPS)"),
			       table, 9, s ? s->use_ssl : FALSE);
    g_signal_connect(G_OBJECT(fcw->use_ssl), "toggled",
                     G_CALLBACK(imap_use_ssl_cb), fcw);
#endif

    gtk_widget_show_all(GTK_WIDGET(fcw->dialog));

    validate_folder(NULL, fcw);
    gtk_widget_grab_focus(fcw->folder_name);

    gtk_dialog_set_default_response(fcw->dialog, 
                                    mn ? GTK_RESPONSE_OK 
                                    : GTK_RESPONSE_CANCEL);

    g_signal_connect(G_OBJECT(fcw->dialog), "response",
                     G_CALLBACK(folder_conf_response), fcw);
    gtk_widget_show_all(GTK_WIDGET(fcw->dialog));
}

/* folder_conf_imap_sub_node:
   Show name and path for an existing subfolder,
   or create a new one.
*/

static void
validate_sub_folder(GtkWidget * w, SubfolderDialogData * sdd)
{
    BalsaMailboxNode *mn = sdd->parent;
    /*
     * Allow typing in the parent_folder entry box only if we already
     * have the server information in mn:
     */
    gboolean have_server = (mn && mn->server
			    && mn->server->type == LIBBALSA_SERVER_IMAP);
    gtk_editable_set_editable(GTK_EDITABLE(sdd->parent_folder),
			      have_server);
    /*
     * We'll allow a null parent name, although some IMAP servers
     * will deny permission:
     */
    gtk_dialog_set_response_sensitive(sdd->dialog, GTK_RESPONSE_OK, 
                                      have_server &&
                                      *gtk_entry_get_text(GTK_ENTRY
                                                          (sdd->folder_name)));
}

/* callbacks for a `Browse...' button: */

typedef struct _BrowseButtonData BrowseButtonData;
struct _BrowseButtonData {
    SubfolderDialogData *sdd;
    GtkDialog *dialog;
    GtkWidget *button;
    BalsaMailboxNode *mbnode;
};

static void
browse_button_select_row_cb(GtkTreeSelection * selection,
                            BrowseButtonData * bbd)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean selected =
        gtk_tree_selection_get_selected(selection, &model, &iter);

    gtk_dialog_set_response_sensitive(bbd->dialog,
                                      GTK_RESPONSE_OK, selected);
    if (selected)
        gtk_tree_model_get(model, &iter, 0, &bbd->mbnode, -1);
}

static void
browse_button_row_activated(GtkTreeView * tree_view, GtkTreePath * path,
                            GtkTreeViewColumn * column,
                            BrowseButtonData * bbd)
{
    gtk_dialog_response(bbd->dialog, GTK_RESPONSE_OK);
}

static void
browse_button_response(GtkDialog * dialog, gint response,
                       BrowseButtonData * bbd)
{
    if (response == GTK_RESPONSE_OK) {
        BalsaMailboxNode *mbnode = bbd->mbnode;
        if (!mbnode)
            return;

        bbd->sdd->parent = mbnode;
        if (mbnode->dir)
            gtk_entry_set_text(GTK_ENTRY(bbd->sdd->parent_folder),
                               mbnode->dir);
    }

    gtk_widget_set_sensitive(bbd->button, TRUE);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static gboolean
folder_selection_func(GtkTreeSelection * selection, GtkTreeModel * model,
                      GtkTreePath * path, gboolean path_currently_selected,
                      SubfolderDialogData * sdd)
{
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, 0, &mbnode, -1);

    return (mbnode->server && mbnode->server->type == LIBBALSA_SERVER_IMAP
            && (sdd->mbnode == NULL || sdd->mbnode->server == mbnode->server));
}

static void
browse_button_cb(GtkWidget * widget, SubfolderDialogData * sdd)
{
    GtkWidget *scroll, *dialog;
    GtkRequisition req;
    GtkWidget *tree_view = balsa_mblist_new();
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    BrowseButtonData *bbd;
    /*
     * Make only IMAP nodes selectable:
     */
    gtk_tree_selection_set_select_function(selection,
                                           (GtkTreeSelectionFunc) 
                                           folder_selection_func, sdd,
                                           NULL);

    dialog =
        gtk_dialog_new_with_buttons(_("Select parent folder"),
                                    GTK_WINDOW(sdd->dialog),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    NULL);
    
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), scroll,
                       TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scroll), tree_view);
    gtk_widget_grab_focus(tree_view);

    bbd = g_new(BrowseButtonData, 1);
    bbd->sdd = sdd;
    bbd->dialog = GTK_DIALOG(dialog);
    bbd->button = widget;
    bbd->mbnode = NULL;
    g_object_weak_ref(G_OBJECT(dialog), (GWeakNotify) g_free, bbd);
    g_signal_connect(G_OBJECT(selection), "changed",
                     G_CALLBACK(browse_button_select_row_cb), bbd);
    g_signal_connect(G_OBJECT(tree_view), "row-activated",
                     G_CALLBACK(browse_button_row_activated), bbd);

    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(tree_view, &req);
    /* don't mess with the width, it gets saved! */
    if (req.height > balsa_app.mw_height)
        req.height = balsa_app.mw_height;
    else if (req.height < balsa_app.mw_height / 2)
        req.height = balsa_app.mw_height / 2;
    gtk_window_set_default_size(GTK_WINDOW(dialog), req.width, req.height);

    /* To prevent multiple dialogs, desensitize the browse button. */
    gtk_widget_set_sensitive(widget, FALSE);
    /* OK button is insensitive until some row is selected. */
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                      GTK_RESPONSE_OK, FALSE);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(browse_button_response), bbd);
    gtk_widget_show_all(GTK_WIDGET(dialog));
}

static void
ident_updated_cb(SubfolderDialogData * sdd, LibBalsaIdentity* identity)
{
    if(identity) {
	g_free(sdd->identity_name);
	sdd->identity_name = g_strdup(identity->identity_name);
	gtk_label_set_text(GTK_LABEL(sdd->identity_label), sdd->identity_name);
    }
}

static void
ident_change_button_cb(GtkWidget * widget, SubfolderDialogData * sdd)
{
    LibBalsaIdentity* ident = balsa_app.current_ident;
    GList *l;
    if(sdd->identity_name) {
	for(l=balsa_app.identities; l; l = l = g_list_next(l))
	    if( strcmp(sdd->identity_name, 
		       LIBBALSA_IDENTITY(l->data)->identity_name)==0) {
		ident = LIBBALSA_IDENTITY(l->data);
		break;
	    }
    }
		
    libbalsa_identity_select_dialog(GTK_WINDOW(sdd->dialog),
				    _("Select Identity"),
				    balsa_app.identities,
				    ident,
				    (LibBalsaIdentityCallback)ident_updated_cb,
				    sdd);
}

static void
subfolder_conf_clicked_ok(SubfolderDialogData * sdd)
{
    gchar *parent, *folder;

    parent =
        gtk_editable_get_chars(GTK_EDITABLE(sdd->parent_folder), 0, -1);
    folder = gtk_editable_get_chars(GTK_EDITABLE(sdd->folder_name), 0, -1);
    if(balsa_app.debug)
	g_print("sdd->old_parent=%s\nsdd->old_folder=%s\n",
		sdd->old_parent, sdd->old_folder);

    if (sdd->mbnode) {
        /* rename */
        if ((sdd->old_parent && strcmp(parent, sdd->old_parent)) ||
            (sdd->old_folder && strcmp(folder, sdd->old_folder))) {
            gint button = GTK_RESPONSE_OK;
            if (sdd->old_folder && !strcmp(sdd->old_folder, "INBOX") &&
                (!sdd->old_parent || !*sdd->old_parent)) {
                gchar *msg =
                    g_strdup_printf(_
                                    ("Renaming INBOX is special!\n"
                                     "You will create a subfolder %s in %s\n"
                                     "containing the messages from INBOX.\n"
                                     "INBOX and its subfolders will remain.\n"
                                     "What would you like to do?"),
folder, parent);
                GtkWidget *ask = gtk_dialog_new_with_buttons(_("Question"),
                                                             GTK_WINDOW
                                                             (sdd->dialog),
                                                             GTK_DIALOG_MODAL,
                                                             _
                                                             ("Rename INBOX"),
                                                             GTK_RESPONSE_OK,
                                                             _("Cancel"),
                                                             GTK_RESPONSE_CANCEL,
                                                             NULL);
                gtk_container_add(GTK_CONTAINER(GTK_DIALOG(ask)->vbox),
                                  gtk_label_new(msg));
                g_free(msg);
                button = gtk_dialog_run(GTK_DIALOG(ask));
                gtk_widget_destroy(ask);
            }
            if (button == GTK_RESPONSE_OK) {
                /* Close the mailbox before renaming,
                 * otherwise the rescan will try to close it
                 * under its old name.
                 */
                balsa_window_close_mbnode(balsa_app.main_window,
                                          sdd->mbnode);
                libbalsa_imap_rename_subfolder
                    (LIBBALSA_MAILBOX_IMAP(sdd->mbnode->mailbox),
                     parent, folder, sdd->mbnode->subscribed);
                g_free(sdd->mbnode->dir);
                sdd->mbnode->dir = g_strdup(parent);

                /*  Rescan as little of the tree as possible. */
                if (sdd->old_parent
                    && !strncmp(parent, sdd->old_parent, strlen(parent))) {
                    /* moved it up the tree */
                    GNode *n = balsa_find_dir(balsa_app.mailbox_nodes,
                                              parent);
                    if (n)
                        balsa_mailbox_node_rescan
                            (BALSA_MAILBOX_NODE(n->data));
                    else
                        printf("Parent not found!?\n");
                } else if (sdd->old_parent
                           && !strncmp(parent, sdd->old_parent,
                                       strlen(sdd->old_parent))) {
                    /* moved it down the tree */
                    GNode *n = balsa_find_dir(balsa_app.mailbox_nodes,
                                              sdd->old_parent);
                    if (n)
                        balsa_mailbox_node_rescan
                            (BALSA_MAILBOX_NODE(n->data));
                } else {
                    /* moved it sideways: a chain of folders might
                     * go away, so we'd better rescan from higher up
                     */
                    BalsaMailboxNode *mb = sdd->mbnode->parent;
                    while (!mb->mailbox && mb->parent)
                        mb = mb->parent;
                    balsa_mailbox_node_rescan(mb);
                    balsa_mailbox_node_rescan(sdd->mbnode);
                }
            }
        } else {
            LibBalsaMailbox *mbx = sdd->mbnode->mailbox;
            g_free(mbx->identity_name);
            mbx->identity_name = sdd->identity_name;
            sdd->identity_name = NULL;
            config_views_save();
        }
    } else {
        /* create and subscribe, if parent was. */
        libbalsa_imap_new_subfolder(parent, folder,
                                    sdd->parent->subscribed,
                                    sdd->parent->server);

        /* see it as server sees it: */
        balsa_mailbox_node_rescan(sdd->parent);
    }
    g_free(parent);
    g_free(folder);
}

/* folder_conf_imap_sub_node:
   show the IMAP Folder configuration dialog for given mailbox node
   representing a sub-folder.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_sub_node(BalsaMailboxNode * mn)
{
    GtkWidget *frame, *table, *subtable, *button, *label, *box;
    SubfolderDialogData *sdd;
    static SubfolderDialogData *sdd_new = NULL;

    /* Allow only one dialog per mailbox node, and one with mn == NULL
     * for creating a new subfolder. */
    sdd = mn ? g_object_get_data(G_OBJECT(mn), BALSA_FOLDER_CONF_IMAP_KEY)
             : sdd_new;
    if (sdd) {
        gdk_window_raise(GTK_WIDGET(sdd->dialog)->window);
        return;
    }

    sdd = g_new(SubfolderDialogData, 1);
    sdd->ok = (CommonDialogFunc) subfolder_conf_clicked_ok;

    if (mn) {
	/* update */
	if (!mn->mailbox) {
            GtkWidget* dlg = 
                gtk_message_dialog_new
                (GTK_WINDOW(balsa_app.main_window),
                 GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                 GTK_BUTTONS_CLOSE,
                 _("An IMAP folder that is not a mailbox\n"
                   "has no properties that can be changed."));
            gtk_dialog_run(GTK_DIALOG(dlg)); gtk_widget_destroy(dlg);
	    return;
	}
	sdd->mbnode = mn;
	sdd->parent = mn->parent;
	sdd->old_folder = mn->mailbox->name;
	sdd->identity_name = g_strdup(mn->mailbox->identity_name);
    } else {
	/* create */
	BalsaMailboxNode *mbnode =
            balsa_mblist_get_selected_node(balsa_app.mblist);
	if (mbnode && mbnode->server &&
	    mbnode->server->type == LIBBALSA_SERVER_IMAP)
	    sdd->mbnode = mbnode;
	else
	    sdd->mbnode = NULL;
        sdd->old_folder = NULL;
        sdd->parent = NULL;
	sdd->identity_name = NULL;
    }
    sdd->old_parent = sdd->mbnode ? sdd->mbnode->parent->dir : NULL;

    sdd->dialog = 
        GTK_DIALOG(gtk_dialog_new_with_buttons
                   (_("Remote IMAP subfolder"), 
                    GTK_WINDOW(balsa_app.main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    mn ? _("_Update") : _("_Create"), GTK_RESPONSE_OK,
                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, 
                    GTK_STOCK_HELP,   GTK_RESPONSE_HELP,
                    NULL));
    /* `Enter' key => Create: */
    gtk_dialog_set_default_response(GTK_DIALOG(sdd->dialog), GTK_RESPONSE_OK);
    gtk_window_set_wmclass(GTK_WINDOW(sdd->dialog), 
			   "subfolder_config_dialog", "Balsa");

    if (sdd->mbnode)
        g_object_set_data_full(G_OBJECT(sdd->mbnode),
                               BALSA_FOLDER_CONF_IMAP_KEY, sdd, 
                               (GDestroyNotify) folder_conf_destroy_cdd);
    else {
        sdd_new = sdd;
        g_object_add_weak_pointer(G_OBJECT(sdd->dialog),
                                  (gpointer) &sdd_new);
    }

    frame = gtk_frame_new(mn ? _("Rename or move subfolder") :
			       _("Create subfolder"));
    gtk_box_pack_start(GTK_BOX(sdd->dialog->vbox),
                       frame, TRUE, TRUE, 0);
    table = gtk_table_new(3, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    label= create_label(_("_Folder name:"), table, 0);
    sdd->folder_name = create_entry(sdd->dialog, table,
                                   GTK_SIGNAL_FUNC(validate_sub_folder),
				   sdd, 0, sdd->old_folder, label);

    subtable = gtk_table_new(1, 3, FALSE);
    label = create_label(_("_Subfolder of:"), table, 1);
    sdd->parent_folder = create_entry(sdd->dialog, subtable,
                                     GTK_SIGNAL_FUNC(validate_sub_folder),
				     sdd, 0, sdd->old_parent, label);

    button = gtk_button_new_with_mnemonic(_("_Browse..."));
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(browse_button_cb), (gpointer) sdd);
    gtk_table_attach(GTK_TABLE(subtable), button, 2, 3, 0, 1,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 5, 5);
    gtk_table_attach(GTK_TABLE(table), subtable, 1, 2, 1, 2,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);

    label = create_label(_("Identity:"), table, 2);
    box = gtk_hbox_new(FALSE, 5);
    sdd->identity_label = gtk_label_new(sdd->identity_name 
					? sdd->identity_name : "");
    gtk_box_pack_start(GTK_BOX(box), sdd->identity_label, TRUE, TRUE, 5);

    button = gtk_button_new_with_mnemonic(_("C_hange..."));
    g_signal_connect(G_OBJECT(button), "clicked", 
		     G_CALLBACK(ident_change_button_cb), sdd);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 5);
    gtk_table_attach(GTK_TABLE(table), box, 1, 2, 2, 3,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);


    gtk_widget_show_all(GTK_WIDGET(sdd->dialog));

    validate_sub_folder(NULL, sdd);
    gtk_widget_grab_focus(sdd->folder_name);

    g_signal_connect(G_OBJECT(sdd->dialog), "response",
                     G_CALLBACK(folder_conf_response), sdd);
    gtk_widget_show_all(GTK_WIDGET(sdd->dialog));
}

void
folder_conf_delete(BalsaMailboxNode* mbnode)
{
    GtkWidget* ask;
    GNode* gnode;
    gint response;

    if(!mbnode->config_prefix) {
	ask = 
            gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE,
	    _("This folder is not stored in configuration."
	      "I do not yet know how to remove it from remote server."));
	gtk_dialog_run(GTK_DIALOG(ask));
        gtk_widget_destroy(ask);
	return;
    }
	
    ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                 GTK_MESSAGE_QUESTION,
                                 GTK_BUTTONS_OK_CANCEL,
                                 _("This will remove the folder "
                                   "\"%s\" from the list.\n"
                                   "You may use \"New IMAP Folder\" "
                                   "later to add this folder again.\n"),
                                 mbnode->name);
    gtk_window_set_title(GTK_WINDOW(ask), _("Confirm"));

    response = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);
    if(response != GTK_RESPONSE_OK)
	return;

    /* Delete it from the config file and internal nodes */
    config_folder_delete(mbnode);

    /* Remove the node from balsa's mailbox list */
    balsa_mailbox_nodes_lock(FALSE);
    gnode = balsa_find_mbnode(balsa_app.mailbox_nodes, mbnode);
    balsa_mailbox_nodes_unlock(FALSE);
    if(gnode) {
        balsa_mailbox_nodes_lock(TRUE);
	balsa_remove_children_mailbox_nodes(gnode);
        balsa_mailbox_nodes_unlock(TRUE);
        balsa_mblist_remove_mailbox_node(balsa_app.mblist_tree_store,
                                         mbnode);
	g_node_unlink(gnode);
	g_node_destroy(gnode);
	g_object_unref(G_OBJECT(mbnode));
	update_mail_servers();
    } else g_warning("folder node %s (%p) not found in hierarchy.\n",
		     mbnode->name, mbnode);
}

void
folder_conf_add_imap_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_imap_node(NULL);
}

void
folder_conf_add_imap_sub_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_imap_sub_node(NULL);
}

void
folder_conf_edit_imap_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_imap_node(BALSA_MAILBOX_NODE(data));
}

void
folder_conf_delete_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_delete(BALSA_MAILBOX_NODE(data));
}
