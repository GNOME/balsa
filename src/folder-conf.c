/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
#include "balsa-app.h"
#include "balsa-icons.h"
#include "folder-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "pref-manager.h"

typedef struct {
    GnomeDialog *dialog;
    GtkWidget * folder_name, *server, *port, *username, *remember,
        *password, *subscribed, *list_inbox, *prefix;
#ifdef USE_SSL
    GtkWidget *use_ssl;
#endif
    BalsaMailboxNode* mn;
} FolderDialogData;

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
    gnome_dialog_set_sensitive(fcw->dialog, 0, sensitive);
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
folder_conf_clicked_cb(GtkObject* dialog, int buttonno, gpointer data)
{
    FolderDialogData *fcw = (FolderDialogData*)data;
    LibBalsaServer * s = fcw->mn ? fcw->mn->server : NULL;
#if BALSA_MAJOR < 2
    static GnomeHelpMenuEntry help_entry = { NULL, "folder-config.html" };
#else
    static const gchar help_path[] = "folder-config.html";
    GError *err = NULL;
#endif                          /* BALSA_MAJOR < 2 */
    gboolean insert;
    GNode *gnode;

#if BALSA_MAJOR < 2
    help_entry.name = gnome_app_id;

#endif                          /* BALSA_MAJOR < 2 */
    switch(buttonno) {
    case 0: /* OK */
	if(!fcw->mn) { 
	    insert = TRUE; 
	    s = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
	}
	else insert = FALSE;
	
	libbalsa_server_set_host(s, 
                                 gtk_entry_get_text(GTK_ENTRY(fcw->server))
#ifdef USE_SSL
				 , gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->use_ssl))
#endif
                                 );
	libbalsa_server_set_username
	    (s, gtk_entry_get_text(GTK_ENTRY(fcw->username)));
        s->remember_passwd = 
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->remember));
	libbalsa_server_set_password
	    (s, gtk_entry_get_text(GTK_ENTRY(fcw->password)));
	
	if(!fcw->mn)
	    fcw->mn = balsa_mailbox_node_new_imap_folder(s, NULL);
        
	g_free(fcw->mn->dir);  
	fcw->mn->dir = g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw->prefix)));
	g_free(fcw->mn->name); 
	fcw->mn->name = 
            g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw->folder_name)));
	fcw->mn->subscribed = 
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->subscribed));
	fcw->mn->list_inbox = 
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw->list_inbox));
	
	if(insert) {
	    g_node_append(balsa_app.mailbox_nodes, 
                          gnode = g_node_new(fcw->mn));
	    balsa_mailbox_node_append_subtree(fcw->mn, gnode);
	    balsa_mblist_repopulate(balsa_app.mblist);
	    config_folder_add(fcw->mn, NULL);
	    update_mail_servers();
	} else {
	    balsa_mailbox_node_rescan(fcw->mn);
	    config_folder_update(fcw->mn);
	}
        /* Fall over */
    default: 
        gnome_dialog_close(GNOME_DIALOG(dialog));
        break;
    case 2:
#if BALSA_MAJOR < 2
        gnome_help_display(NULL, &help_entry);
#else
        gnome_help_display_uri(help_path, &err);
        if (err) {
            g_print(_("Error displaying %s: %s\n"), help_path,
                    err->message);
            g_error_free(err);
        }
#endif                          /* BALSA_MAJOR < 2 */
        break;
    }
}

/* folder_conf_imap_node:
   show the IMAP Folder configuration dialog for given mailbox node.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_node(BalsaMailboxNode *mn)
{
    GtkWidget *frame, *table;
    FolderDialogData fcw;
    guint keyval;
    LibBalsaServer * s = mn ? mn->server : NULL;
    gchar *default_server = libbalsa_guess_imap_server();

    fcw.mn = mn;
    fcw.dialog = GNOME_DIALOG(gnome_dialog_new(_("Remote IMAP folder"), 
					       mn ? _("Update") : _("Create"), 
					       GNOME_STOCK_BUTTON_CANCEL, 
					       GNOME_STOCK_BUTTON_HELP,
					       NULL));
    gnome_dialog_set_parent(fcw.dialog,
                            GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_wmclass(GTK_WINDOW(fcw.dialog), 
			   "folder_config_dialog", "Balsa");

    frame = gtk_frame_new(_("Remote IMAP folder set"));
    gtk_box_pack_start(GTK_BOX(fcw.dialog->vbox),
                       frame, TRUE, TRUE, 0);
    table = gtk_table_new(9, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    create_label(_("Descriptive _Name:"), table, 0, &keyval);
    fcw.folder_name = create_entry(fcw.dialog, table,
                                   GTK_SIGNAL_FUNC(validate_folder),
                                   &fcw, 0, mn ? mn->name : NULL, 
				   keyval);

    create_label(_("_Server:"), table, 1, &keyval);
    fcw.server = create_entry(fcw.dialog, table,
                              GTK_SIGNAL_FUNC(validate_folder),
                              &fcw, 1, s ? s->host : default_server,
			      keyval);

    create_label(_("_User name:"), table, 3, &keyval);
    fcw.username = create_entry(fcw.dialog, table,
                                GTK_SIGNAL_FUNC(validate_folder),
                                &fcw, 3, s ? s->user : g_get_user_name(), 
			        keyval);

    fcw.remember = create_check(fcw.dialog, _("_Remember password"), 
                                table, 4, s ? s->remember_passwd : TRUE);
    gtk_signal_connect(GTK_OBJECT(fcw.remember), "toggled",
                       GTK_SIGNAL_FUNC(remember_cb), 
                       &fcw);

    create_label(_("_Password:"), table, 5, &keyval);
    fcw.password = create_entry(fcw.dialog, table, NULL, NULL, 5,
				s ? s->passwd : NULL, keyval);
    gtk_entry_set_visibility(GTK_ENTRY(fcw.password), FALSE);

    fcw.subscribed = create_check(fcw.dialog, _("_Subscribed folders only"), 
                                  table, 6, mn ? mn->subscribed : FALSE);
    fcw.list_inbox = create_check(fcw.dialog, _("_Always show INBOX"), 
                                  table, 7, mn ? mn->list_inbox : TRUE); 

    create_label(_("_Prefix"), table, 8, &keyval);
    fcw.prefix = create_entry(fcw.dialog, table, NULL, NULL, 8,
			      mn ? mn->dir : NULL, keyval);
    
#ifdef USE_SSL
    fcw.use_ssl = create_check(fcw.dialog,
			       _("Use SSL (IMAPS)"),
			       table, 9, s ? s->use_ssl : FALSE);
    gtk_signal_connect(GTK_OBJECT(fcw.use_ssl), "toggled",
                       GTK_SIGNAL_FUNC(imap_use_ssl_cb), &fcw);
#endif

    gtk_widget_show_all(GTK_WIDGET(fcw.dialog));

    validate_folder(NULL, &fcw);
    gtk_widget_grab_focus(fcw.folder_name);

    gtk_signal_connect(GTK_OBJECT(fcw.dialog), "clicked", 
                       GTK_SIGNAL_FUNC(folder_conf_clicked_cb), &fcw);
    gnome_dialog_run(fcw.dialog);
}

/* folder_conf_imap_sub_node:
   Show name and path for an existing subfolder,
   or create a new one.
*/

/* data to pass around: */
typedef struct {
    GnomeDialog *dialog;
    GtkWidget *parent_folder, *folder_name;
    gchar *old_folder, *old_parent;
    BalsaMailboxNode *mbnode;
} SubfolderDialogData;

static void
validate_sub_folder(GtkWidget * w, SubfolderDialogData * fcw)
{
    BalsaMailboxNode *mn = fcw->mbnode;
    /*
     * Allow typing in the parent_folder entry box only if we already
     * have the server information in mn:
     */
    gboolean have_server = (mn && mn->server
			    && mn->server->type == LIBBALSA_SERVER_IMAP);
    gtk_editable_set_editable(GTK_EDITABLE(fcw->parent_folder),
			      have_server);
    /*
     * We'll allow a null parent name, although some IMAP servers
     * will deny permission:
     */
    gnome_dialog_set_sensitive(fcw->dialog, 0, have_server &&
			       *gtk_entry_get_text(GTK_ENTRY
						   (fcw->folder_name)));
}

/* callbacks for a `Browse...' button: */
static void
browse_button_select_row_cb(GtkCTree * ctree, GList * node, gint column,
			    gpointer data)
{
    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree,
							   GTK_CTREE_NODE
							   (node));
    if (mbnode) {
	SubfolderDialogData *fcw = (SubfolderDialogData *) data;
	gchar *path = mbnode->dir;
	if (path)
	    gtk_entry_set_text(GTK_ENTRY(fcw->parent_folder), path);
	gnome_dialog_close(GNOME_DIALOG
			   (gtk_widget_get_ancestor
			    (GTK_WIDGET(ctree), GTK_TYPE_WINDOW)));
    }
}

static void
fix_ctree(GtkCTree * ctree, GtkCTreeNode * cnode, gpointer data)
{
    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree, cnode);
    if (mbnode) {
	SubfolderDialogData *fcw = (SubfolderDialogData *) data;
	if (!mbnode->server || mbnode->server->type != LIBBALSA_SERVER_IMAP
	    || (fcw->mbnode && fcw->mbnode->server != mbnode->server))
	    mblist_remove_mblist_node(BALSA_MBLIST(ctree), mbnode, cnode);
	else
	    gtk_ctree_node_set_selectable(ctree, cnode, TRUE);
    }
}

static void
browse_button_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *scroll, *dialog;
    GtkRequisition req;
    GtkWidget *ctree = balsa_mblist_new();
    /*
     * Customize the ctree:
     * */
    gtk_ctree_post_recursive(GTK_CTREE(ctree), NULL, fix_ctree, data);

    dialog = gnome_dialog_new(_("Select parent folder"),
			      GNOME_STOCK_BUTTON_CANCEL, NULL);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog),
			    GTK_WINDOW(balsa_app.main_window));
    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), scroll, 
                    TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll),
				    GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

    gtk_signal_connect(GTK_OBJECT(ctree), "tree-select-row",
		       (GtkSignalFunc) browse_button_select_row_cb, data);
   
    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(ctree, &req);
    /* don't mess with the width, it gets saved! */
    if ( req.height > balsa_app.mw_height )
	req.height = balsa_app.mw_height;
    else if ( req.height < balsa_app.mw_height/4)
	req.height = balsa_app.mw_height/4;
    gtk_widget_set_usize(GTK_WIDGET(ctree), req.width, req.height);

    gtk_container_add(GTK_CONTAINER(scroll), ctree);

    gtk_widget_show(ctree);
    gtk_widget_show(scroll);
    gnome_dialog_run_and_close(GNOME_DIALOG(dialog));
}

static void
subfolder_conf_clicked_cb(GtkObject* dialog, int buttonno, gpointer data)
{
#if BALSA_MAJOR < 2
    static GnomeHelpMenuEntry help_entry =
	{ NULL, "folder-config.html#SUBFOLDER-CONFIG" };
#else
    static const gchar help_path[] = "folder-config.html#SUBFOLDER-CONFIG";
    GError *err = NULL;
#endif                          /* BALSA_MAJOR < 2 */
    SubfolderDialogData *fcw = (SubfolderDialogData*)data;
    gchar* parent, *folder;

#if BALSA_MAJOR < 2
    help_entry.name = gnome_app_id;

#endif                          /* BALSA_MAJOR < 2 */
    switch(buttonno) {
    case 0: /* OK */
	parent = 
            gtk_editable_get_chars(GTK_EDITABLE(fcw->parent_folder), 0, -1);
	folder = 
            gtk_editable_get_chars(GTK_EDITABLE(fcw->folder_name), 0, -1);
        
	if (fcw->old_folder) {
	    /* rename */
	    if (strcmp(parent, fcw->old_parent) || 
                strcmp(folder, fcw->old_folder)) {
		gint button = 0;
		if (!strcmp(fcw->old_folder, "INBOX") &&
		    (!fcw->old_parent || !*fcw->old_parent)) {
		    gchar *msg = g_strdup_printf(
			_("Renaming INBOX is special!\n"
			  "You will create a subfolder %s in %s\n"
			  "containing the messages from INBOX.\n"
			  "INBOX and its subfolders will remain.\n"
			  "What would you like to do?"),
			folder, parent);
		    GtkWidget *ask = gnome_message_box_new(msg,
			GNOME_MESSAGE_BOX_QUESTION,
			_("Rename INBOX"), _("Cancel"), NULL);
		    g_free(msg);
		    gnome_dialog_set_parent(GNOME_DIALOG(ask),
                            GTK_WINDOW(balsa_app.main_window));
		    gtk_window_set_modal(GTK_WINDOW(ask), TRUE);
		    button = gnome_dialog_run_and_close(GNOME_DIALOG(ask));
		}
		if (button == 0) {
		    /* Close the mailbox before renaming,
		     * otherwise the rescan will try to close it
		     * under its old name.
		     */
		    balsa_window_close_mbnode(balsa_app.main_window, 
                                              fcw->mbnode);
		    libbalsa_imap_rename_subfolder
                        (LIBBALSA_MAILBOX_IMAP(fcw->mbnode->mailbox),
                         parent, folder, fcw->mbnode->subscribed);
                    g_free(fcw->mbnode->dir);
                    fcw->mbnode->dir = g_strdup(parent);

		    /*	Rescan as little of the tree as possible. */
		    if (!strncmp(parent, fcw->old_parent, strlen(parent))){
			/* moved it up the tree */
                        GNode* n = balsa_find_dir(balsa_app.mailbox_nodes,
                                                  parent);
                        if(n)
                            balsa_mailbox_node_rescan
                                (BALSA_MAILBOX_NODE(n->data));
                        else printf("Parent not found!?\n");
                    } else if (!strncmp(parent, fcw->old_parent, 
                                        strlen(fcw->old_parent))) {
			/* moved it down the tree */
                        GNode* n = balsa_find_dir(balsa_app.mailbox_nodes,
                                                  fcw->old_parent);
                        if(n)
                            balsa_mailbox_node_rescan
                                (BALSA_MAILBOX_NODE(n->data));
                    } else {
			/* moved it sideways: a chain of folders might
			 * go away, so we'd better rescan from higher up
			 */
			BalsaMailboxNode *mb = fcw->mbnode->parent;
			while (!mb->mailbox && mb->parent)
			   mb = mb->parent;
			balsa_mailbox_node_rescan(mb);
			balsa_mailbox_node_rescan(fcw->mbnode);
		    }
		}
	    }
	} else {
	    /* create */
	    libbalsa_imap_new_subfolder(parent, folder,
					fcw->mbnode->subscribed,
					fcw->mbnode->server);

	    /* see it as server sees it: */
	    balsa_mailbox_node_rescan(fcw->mbnode);
	}
	g_free(parent);
	g_free(folder);
        /* fall over */
    default:
        gnome_dialog_close(GNOME_DIALOG(dialog));
        break;
    case 2:
#if BALSA_MAJOR < 2
        gnome_help_display(NULL, &help_entry);
#else
        gnome_help_display_uri(help_path, &err);
        if (err) {
            g_print(_("Error displaying %s: %s\n"), help_path,
                    err->message);
            g_error_free(err);
        }
#endif                          /* BALSA_MAJOR < 2 */
        break;
    }
}

/* folder_conf_imap_sub_node:
   show the IMAP Folder configuration dialog for given mailbox node
   representing a sub-folder.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_sub_node(BalsaMailboxNode * mn)
{
    GtkWidget *frame, *table, *subtable, *browse_button;
    SubfolderDialogData fcw;
    guint keyval;

    if (mn) {
	/* update */
	if (!mn->mailbox) {
	    gnome_dialog_run_and_close(GNOME_DIALOG(gnome_ok_dialog(_(
		"An IMAP folder that is not a mailbox\n"
		"has no properties that can be changed."))));
	    return;
	}
	fcw.mbnode = mn;
	fcw.old_folder = mn->mailbox->name;
    } else {
	/* create */
	BalsaMailboxNode *mbnode = mblist_get_selected_node(balsa_app.mblist);
	if (mbnode && mbnode->server &&
	    mbnode->server->type == LIBBALSA_SERVER_IMAP)
	    fcw.mbnode = mbnode;
	else
	    fcw.mbnode = NULL;
	fcw.old_folder = NULL;
    }
    fcw.old_parent = fcw.mbnode ? fcw.mbnode->parent->dir : NULL;

    fcw.dialog = GNOME_DIALOG(gnome_dialog_new(_("Remote IMAP subfolder"), 
					       mn ? _("Update") : _("Create"), 
					       GNOME_STOCK_BUTTON_CANCEL, 
					       GNOME_STOCK_BUTTON_HELP,
					       NULL));
    /* `Enter' key => Create: */
    gnome_dialog_set_default(GNOME_DIALOG(fcw.dialog), 0);
    gnome_dialog_set_parent(fcw.dialog,
                            GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_wmclass(GTK_WINDOW(fcw.dialog), 
			   "subfolder_config_dialog", "Balsa");

    frame = gtk_frame_new(mn ? _("Rename or move subfolder") :
			       _("Create subfolder"));
    gtk_box_pack_start(GTK_BOX(fcw.dialog->vbox),
                       frame, TRUE, TRUE, 0);
    table = gtk_table_new(2, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    create_label(_("_Folder name:"), table, 0, &keyval);
    fcw.folder_name = create_entry(fcw.dialog, table,
                                   GTK_SIGNAL_FUNC(validate_sub_folder),
				   &fcw, 0, fcw.old_folder, keyval);

    subtable = gtk_table_new(1, 3, FALSE);
    create_label(_("_Subfolder of:"), table, 1, &keyval);
    fcw.parent_folder = create_entry(fcw.dialog, subtable,
                                     GTK_SIGNAL_FUNC(validate_sub_folder),
				     &fcw, 0, fcw.old_parent, keyval);

    browse_button = gtk_button_new_with_label(_("Browse..."));
    gtk_signal_connect(GTK_OBJECT(browse_button), "clicked",
		       (GtkSignalFunc) browse_button_cb,
		       (gpointer) &fcw);
    gtk_table_attach(GTK_TABLE(subtable), browse_button, 2, 3, 0, 1,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);
    gtk_table_attach(GTK_TABLE(table), subtable, 1, 2, 1, 2,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);

    gtk_widget_show_all(GTK_WIDGET(fcw.dialog));

    validate_sub_folder(NULL, &fcw);
    gtk_widget_grab_focus(fcw.folder_name);

    gtk_signal_connect(GTK_OBJECT(fcw.dialog), "clicked", 
                       GTK_SIGNAL_FUNC(subfolder_conf_clicked_cb), &fcw);
    gnome_dialog_run(fcw.dialog);
}

void
folder_conf_delete(BalsaMailboxNode* mbnode)
{
    gchar* msg;
    GtkWidget* ask;
    GNode* gnode;

    if(!mbnode->config_prefix) {
	ask = gnome_warning_dialog_parented(
	    _("This folder is not stored in configuration."
	      "I do not yet know how to remove it from remote server."),
	    GTK_WINDOW(balsa_app.main_window));
	gnome_dialog_run_and_close(GNOME_DIALOG(ask));
	return;
    }
	
    msg = g_strdup_printf
	(_("This will remove the folder %s from the list.\n"
	   "You may use \"New IMAP Folder\" later to add this folder again.\n"
	   "What would you like to do?"),
	 mbnode->name);
    ask = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_QUESTION,
				_("Remove from list"),
				_("Cancel"),
				NULL);
    g_free(msg);
    
    gnome_dialog_set_parent(GNOME_DIALOG(ask), 
			    GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_modal(GTK_WINDOW(ask), TRUE);

    if(1 == gnome_dialog_run_and_close(GNOME_DIALOG(ask)))
	return;

    /* Delete it from the config file and internal nodes */
    config_folder_delete(mbnode);

    /* Remove the node from balsa's mailbox list */
    gnode = balsa_find_mbnode(balsa_app.mailbox_nodes, mbnode);
    if(gnode) {
	balsa_remove_children_mailbox_nodes(gnode);
	mblist_remove_mailbox_node(balsa_app.mblist, mbnode);
	g_node_unlink(gnode);
	g_node_destroy(gnode);
	gtk_object_destroy(GTK_OBJECT(mbnode));
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
