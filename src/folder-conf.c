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

typedef struct {
    GnomeDialog *dialog;
    GtkWidget * folder_name, *server, *port, *username, *password, 
	*subscribed, *prefix;
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

/* folder_conf_imap_node:
   show the IMAP Folder configuration dialog for given mailbox node.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_node(BalsaMailboxNode *mn)
{
    static GnomeHelpMenuEntry help_entry = { NULL, "folder-config.html" };
    GtkWidget *frame, *table;
    FolderDialogData fcw;
    guint keyval;
    gint button, port_no;
    gboolean insert;
    GNode *gnode;
    LibBalsaServer * s = mn ? mn->server : NULL;

    help_entry.name = gnome_app_id;
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
    table = gtk_table_new(7, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    create_label(_("Descriptive _Name:"), table, 0, &keyval);
    fcw.folder_name = create_entry(fcw.dialog, table, validate_folder, &fcw, 
				   0, mn ? mn->name : NULL, 
				   keyval);

    create_label(_("_Server:"), table, 1, &keyval);
    fcw.server = create_entry(fcw.dialog, table, validate_folder, &fcw, 1, 
			  s ? s->host : "localhost",
			  keyval);

    create_label(_("_Port:"), table, 2, &keyval);
    if(s) {
	gchar* tmp = g_strdup_printf("%d", s->port);
	fcw.port = create_entry(fcw.dialog, table, NULL, NULL, 2, tmp, keyval);
	g_free(tmp);
    } else fcw.port = 
	       create_entry(fcw.dialog, table, NULL, NULL, 2, "143", keyval);


    create_label(_("_User name:"), table, 3, &keyval);
    fcw.username = create_entry(fcw.dialog, table, validate_folder, &fcw, 3, 
			    s ? s->user : g_get_user_name(), 
			    keyval);

    create_label(_("_Password:"), table, 4, &keyval);
    fcw.password = create_entry(fcw.dialog, table, NULL, NULL, 4,
				s ? s->passwd : NULL, 
				keyval);
    gtk_entry_set_visibility(GTK_ENTRY(fcw.password), FALSE);

    fcw.subscribed = 
	create_check(fcw.dialog, _("_Subscribed folders only"), table, 5);
    create_label(_("_Prefix"), table, 6, &keyval);
    fcw.prefix = create_entry(fcw.dialog, table, NULL, NULL, 6, 
			      mn ? mn->dir : NULL, keyval);

    gtk_widget_show_all(GTK_WIDGET(fcw.dialog));
    gnome_dialog_close_hides(fcw.dialog, TRUE);

    /* all the widgets are ready, set the values */
    if(mn && mn->subscribed)
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fcw.subscribed), TRUE);
    validate_folder(NULL, &fcw);
    gtk_widget_grab_focus(fcw.folder_name);

    /* FIXME: I don't like this loop. */
    while( (button = gnome_dialog_run(fcw.dialog)) == 2) 
	gnome_help_display(NULL, &help_entry);
    
    if(button == 0) { /* do create/update */
	if(!mn) { 
	    insert = TRUE; 
	    s = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
	}
	else insert = FALSE;
	
	port_no = atoi(gtk_entry_get_text(GTK_ENTRY(fcw.port)));
	libbalsa_server_set_host(s, gtk_entry_get_text(GTK_ENTRY(fcw.server)), 
				 port_no);
	libbalsa_server_set_username
	    (s, gtk_entry_get_text(GTK_ENTRY(fcw.username)));
	libbalsa_server_set_password
	    (s, gtk_entry_get_text(GTK_ENTRY(fcw.password)));
	
	if(!mn)
	    mn = balsa_mailbox_node_new_imap_folder(s, NULL);

	g_free(mn->dir);  
	mn->dir = g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw.prefix)));
	g_free(mn->name); 
	mn->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(fcw.folder_name)));
	mn->subscribed = 
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fcw.subscribed));
	
	if(insert) {
	    g_node_append(balsa_app.mailbox_nodes, gnode = g_node_new(mn));
	    balsa_mailbox_node_append_subtree(mn, gnode);
	    balsa_mblist_repopulate(balsa_app.mblist);
	    config_folder_add(mn, NULL);
	} else {
	    balsa_mailbox_node_rescan(mn);
	    config_folder_update(mn);
	}
    }

    gtk_widget_destroy(GTK_WIDGET(fcw.dialog));
}

/* folder_conf_imap_sub_node:
   Show name and path for an existing subfolder,
   or create a new one.
*/

/* data to pass around: */
typedef struct {
    GnomeDialog *dialog;
    GtkWidget *parent_folder, *folder_name;
    BalsaMailboxNode *mbnode;
} SubfolderDialogData;

static void
validate_sub_folder(GtkWidget *w, SubfolderDialogData * fcw)
{
    gboolean sensitive = TRUE;
    BalsaMailboxNode *mn = fcw->mbnode;

    /* We'll allow a null parent name, although some IMAP servers
     * will deny permission:
     */
    /* if (!*gtk_entry_get_text(GTK_ENTRY(fcw->parent_folder)))	   */
    /*     sensitive = FALSE;					   */
    /* else if (!*gtk_entry_get_text(GTK_ENTRY(fcw->folder_name))) */
    if (!*gtk_entry_get_text(GTK_ENTRY(fcw->folder_name)))
	sensitive = FALSE;
    else if (!mn || !mn->server || mn->server->type != LIBBALSA_SERVER_IMAP)
	sensitive = FALSE;
    gnome_dialog_set_sensitive(fcw->dialog, 0, sensitive);
}

/* callbacks for a `Browse...' button: */
static void
browse_button_select_row_cb(GtkCTree *ctree, GList *node, gint column,
			    gpointer data)
{
    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree,
							   GTK_CTREE_NODE
							   (node));
    if (mbnode) {
	SubfolderDialogData *fcw = (SubfolderDialogData *)data;
	gchar *path = mbnode->dir;
	fcw->mbnode = mbnode;
	if (path)
	    gtk_entry_set_text(GTK_ENTRY(fcw->parent_folder), path);
    }
}

/* slightly different from the function in mblist.c */
static gboolean
mailbox_nodes_to_ctree(GtkCTree * ctree, guint depth, GNode * gnode,
                       GtkCTreeNode * cnode, gpointer data)
{
    SubfolderDialogData *fcw = (SubfolderDialogData *)data;
    BalsaMailboxNode *mbnode;
    g_return_val_if_fail(gnode, FALSE);

    if ( (mbnode = gnode->data) == NULL) return FALSE;
    if (!mbnode->server || mbnode->server->type !=  LIBBALSA_SERVER_IMAP)
	/* not an IMAP folder */
	return FALSE;
    if (fcw->mbnode && fcw->mbnode->server != mbnode->server)
	/* folder in a different tree from the one
	 * we're modifying/creating in
	 */
	return FALSE;

    if (mbnode->mailbox) {
        if (LIBBALSA_IS_MAILBOX_POP3(mbnode->mailbox))
            g_assert_not_reached();
        else {
            BalsaIconName in;
            if(mbnode->mailbox == balsa_app.inbox)
                in = BALSA_ICON_INBOX;
            else if(mbnode->mailbox == balsa_app.outbox)
                in = BALSA_ICON_OUTBOX;
            else if(mbnode->mailbox == balsa_app.sentbox)
                in = BALSA_ICON_TRAY_EMPTY;
            else if(mbnode->mailbox == balsa_app.trash)
                in = BALSA_ICON_TRASH;
            else
                in = (mbnode->mailbox->new_messages > 0)
                ? BALSA_ICON_TRAY_FULL : BALSA_ICON_TRAY_EMPTY;

            gtk_ctree_set_node_info(ctree, cnode,
                                    mbnode->mailbox->name, 5,
                                    balsa_icon_get_pixmap(in),
                                    balsa_icon_get_bitmap(in),
                                    /* same icon when expanded: */
                                    balsa_icon_get_pixmap(in),
                                    balsa_icon_get_bitmap(in),
                                    FALSE, mbnode->expanded);
        }
    } else {
        /* new directory, but not a mailbox */
        gtk_ctree_set_node_info(ctree, cnode, g_basename(mbnode->name), 5,
                                balsa_icon_get_pixmap
                                (BALSA_ICON_DIR_CLOSED),
                                balsa_icon_get_bitmap
                                (BALSA_ICON_DIR_CLOSED),
                                balsa_icon_get_pixmap(BALSA_ICON_DIR_OPEN),
                                balsa_icon_get_bitmap(BALSA_ICON_DIR_OPEN),
                                FALSE, mbnode->expanded);
    }
    gtk_ctree_node_set_row_data(ctree, cnode, mbnode);
    return TRUE;
}

static void
browse_button_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *scroll, *dialog;
    GtkRequisition req;
    GtkWidget *ctree = GTK_WIDGET(gtk_ctree_new(1, 0));

    dialog = gnome_dialog_new(_("Select parent folder"),
			      GNOME_STOCK_BUTTON_OK, NULL);
    gnome_dialog_set_parent(GNOME_DIALOG(dialog),
			    GTK_WINDOW(balsa_app.main_window));
    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), scroll, 
                    TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll),
				    GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

    if (balsa_app.mailbox_nodes) {
        GNode *walk;
        GtkCTreeNode *node;

        for (walk = g_node_last_child(balsa_app.mailbox_nodes); walk;
	     walk = walk->prev) {
            node = gtk_ctree_insert_gnode(GTK_CTREE(ctree), NULL, NULL, walk,
                                       mailbox_nodes_to_ctree, data);
        }
    }
    gtk_ctree_sort_recursive(GTK_CTREE(ctree), NULL);
    gtk_signal_connect(GTK_OBJECT(ctree), "tree-select-row",
		       (GtkSignalFunc) browse_button_select_row_cb, data);
   
    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(ctree, &req);
    if ( req.height > balsa_app.mw_height/2 )
	req.height = balsa_app.mw_height/2;
    else if ( req.height < balsa_app.mw_height/4)
	req.height = balsa_app.mw_height/4;
    if ( req.width > gdk_screen_width() )
	req.width = gdk_screen_width() - 2*GTK_CONTAINER(scroll)->border_width;
    else if ( req.width < gdk_screen_width()/6)
	req.width = gdk_screen_width()/6;
    gtk_widget_set_usize(GTK_WIDGET(ctree), req.width, req.height);

    gtk_container_add(GTK_CONTAINER(scroll), ctree);

    gtk_widget_show(ctree);
    gtk_widget_show(scroll);
    gnome_dialog_run_and_close(GNOME_DIALOG(dialog));
}

/* folder_conf_imap_sub_node:
   show the IMAP Folder configuration dialog for given mailbox node
   representing a sub-folder.
   If mn is NULL, setup it with default values for folder creation.
*/
void
folder_conf_imap_sub_node(BalsaMailboxNode *mn)
{
    static GnomeHelpMenuEntry help_entry = { NULL, "subfolder-config.html" };
    GtkWidget *frame, *table, *subtable, *browse_button;
    SubfolderDialogData fcw;
    guint keyval;
    gint button;
    gchar *old_folder;
    gchar *old_parent;

    help_entry.name = gnome_app_id;
    if (mn) {
	/* update */
	if (!mn->mailbox) {
	    gnome_dialog_run_and_close(GNOME_DIALOG(gnome_ok_dialog(_(
		"An IMAP folder that is not a mailbox\n"
		"has no properties that can be changed."))));
	    return;
	}
	fcw.mbnode = mn->parent;
	old_folder = mn->mailbox->name;
    } else {
	/* create */
	BalsaMailboxNode *mbnode = mblist_get_selected_node(balsa_app.mblist);
	if (mbnode && mbnode->server &&
	    mbnode->server->type == LIBBALSA_SERVER_IMAP)
	    fcw.mbnode = mbnode;
	else
	    fcw.mbnode = NULL;
	old_folder = NULL;
    }
    old_parent = fcw.mbnode ? fcw.mbnode->dir : NULL;

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
    fcw.folder_name = create_entry(fcw.dialog, table, validate_sub_folder,
				   &fcw, 0, old_folder, keyval);

    subtable = gtk_table_new(1, 3, FALSE);
    create_label(_("_Subfolder of:"), table, 1, &keyval);
    fcw.parent_folder = create_entry(fcw.dialog, subtable, validate_sub_folder,
				     &fcw, 0, old_parent, keyval);

    browse_button = gtk_button_new_with_label(_("Browse..."));
    gtk_signal_connect(GTK_OBJECT(browse_button), "clicked",
		       (GtkSignalFunc) browse_button_cb,
		       (gpointer) &fcw);
    gtk_table_attach(GTK_TABLE(subtable), browse_button, 2, 3, 0, 1,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);
    gtk_table_attach(GTK_TABLE(table), subtable, 1, 2, 1, 2,
	GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);

    gtk_widget_show_all(GTK_WIDGET(fcw.dialog));
    gnome_dialog_close_hides(fcw.dialog, TRUE);

    validate_sub_folder(NULL, &fcw);
    gtk_widget_grab_focus(fcw.folder_name);

    /* FIXME: I don't like this loop. */
    while( (button = gnome_dialog_run(fcw.dialog)) == 2)
        gnome_help_display(NULL, &help_entry);

    if(button == 0) { /* do create/update */
	gchar *parent = gtk_editable_get_chars(GTK_EDITABLE(fcw.parent_folder),
					       0, -1);
	gchar *folder = gtk_editable_get_chars(GTK_EDITABLE(fcw.folder_name),
					       0, -1);

	if (mn) {
	    /* rename */
	    if (strcmp(parent, old_parent) || strcmp(folder, old_folder)) {
		gint button = 0;
		if (!strcmp(old_folder, "INBOX") &&
		    (!old_parent || !*old_parent)) {
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
		    balsa_window_close_mbnode(balsa_app.main_window, mn);
		    libbalsa_imap_rename_subfolder(mn->dir, parent, folder,
						   fcw.mbnode->subscribed,
						   fcw.mbnode->server);

		    /*	Rescan as little of the tree as possible.
		     *	We'll assume that `parent' is consistent
		     *	with `fcw.mbnode' (that is, the parent path
		     *	was found by browsing, not typing in the entry.
		     */
		    if (!strncmp(parent, old_parent, strlen(parent)))
			/* moved it up the tree */
			balsa_mailbox_node_rescan(fcw.mbnode);
		    else if (!strncmp(parent, old_parent, strlen(old_parent)))
			/* moved it down the tree */
			balsa_mailbox_node_rescan(mn->parent);
		    else {
			/* moved it sideways: a chain of folders might
			 * go away, so we'd better rescan from higher up
			 */
			BalsaMailboxNode *mb = mn->parent;
			while (!mb->mailbox && mb->parent)
			   mb = mb->parent;
			balsa_mailbox_node_rescan(mb);
			balsa_mailbox_node_rescan(fcw.mbnode);
		    }
		}
	    }
	} else {
	    /* create */
	    libbalsa_imap_new_subfolder(parent, folder,
					fcw.mbnode->subscribed,
					fcw.mbnode->server);

	    /* see it as server sees it: */
	    balsa_mailbox_node_rescan(fcw.mbnode);
	}
	g_free(parent);
	g_free(folder);
    }
    gtk_widget_destroy(GTK_WIDGET(fcw.dialog));
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
