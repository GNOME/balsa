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
static void validate_folder(GtkWidget *w, FolderDialogData * fcw)
{
    gboolean sensitive = TRUE;
    if (!*gtk_entry_get_text(GTK_ENTRY(fcw->folder_name)))
	sensitive = FALSE;
    else if (!*gtk_entry_get_text(GTK_ENTRY(fcw->server)))
	sensitive = FALSE;
    gnome_dialog_set_sensitive(fcw->dialog, 0, sensitive);
}

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
folder_conf_edit_imap_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_imap_node(BALSA_MAILBOX_NODE(data));
}

void
folder_conf_delete_cb(GtkWidget * widget, gpointer data)
{
    folder_conf_delete(BALSA_MAILBOX_NODE(data));
}
