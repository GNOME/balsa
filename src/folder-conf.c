/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

/* FIXME: create_label and create_entry identical as in mailbox-conf.c */

/* Create a label and add it to a table */
static GtkWidget *
create_label(const gchar* label, GtkWidget* table, gint row, guint* keyval)
{
    guint kv;

    GtkWidget *w = gtk_label_new("");
    kv = gtk_label_parse_uline(GTK_LABEL(w), label);
    if ( keyval ) 
        *keyval = kv;

    gtk_misc_set_alignment(GTK_MISC(w), 1.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
                     GTK_FILL, GTK_FILL, 5, 5);

    gtk_widget_show(w);
    return w;
}

/* Create a text entry and add it to the table */
static GtkWidget *
create_entry(GnomeDialog *fcw, GtkWidget * table, gint row, 
	     const gchar* initval, const guint keyval)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);
    if (initval)
        gtk_entry_append_text(GTK_ENTRY(entry), initval);
    
    gtk_widget_add_accelerator(entry, "grab_focus",
                               fcw->accelerators,
                               keyval, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);

    gnome_dialog_editable_enters(fcw, GTK_EDITABLE(entry));

    /* Watch for changes... */
    /* gtk_signal_connect(GTK_OBJECT(entry), "changed", 
       GTK_SIGNAL_FUNC(check_for_blank_fields), fcw); */

    gtk_widget_show(entry);
    return entry;
}

/* should it be global? */
static void
folder_conf_imap_node(BalsaMailboxNode *mn)
{
    static GnomeHelpMenuEntry help_entry = { NULL, "folder-config" };
    GnomeDialog *dialog;
    GtkWidget *frame, *table;
    GtkWidget * folder_name, *server, *port, *username, *password, *prefix;
    guint keyval;
    gint button, port_no;
    gboolean insert;
    GNode *gnode;
    LibBalsaServer * s = mn ? mn->server : NULL;

    help_entry.name = gnome_app_id;
    dialog = GNOME_DIALOG(gnome_dialog_new(_("Remote IMAP folder"), 
					   mn ? _("Update") : _("Create"), 
					   GNOME_STOCK_BUTTON_CANCEL, 
					   GNOME_STOCK_BUTTON_HELP,
					   NULL));
    gnome_dialog_set_parent(dialog,
                            GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_wmclass(GTK_WINDOW(dialog), 
			   "folder_config_dialog", "Balsa");

    frame = gtk_frame_new(_("Remote IMAP folder set"));
    gtk_box_pack_start(GTK_BOX(dialog->vbox),
                       frame, TRUE, TRUE, 0);
    table = gtk_table_new(6, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
 
    /* INPUT FIELD CREATION */
    create_label(_("Descriptive _Name:"), table, 0, &keyval);
    folder_name = create_entry(dialog, table, 0, 
			       mn ? mn->name : NULL, 
			       keyval);

    create_label(_("_Server:"), table, 1, &keyval);
    server = create_entry(dialog, table, 1, 
			  s ? s->host : "localhost",
			  keyval);

    create_label(_("_Port:"), table, 2, &keyval);
    if(s) {
	gchar* tmp = g_strdup_printf("%d", s->port);
	port = create_entry(dialog, table, 2, tmp, keyval);
	g_free(tmp);
    } else port = create_entry(dialog, table, 2, "143", keyval);


    create_label(_("_User name:"), table, 3, &keyval);
    username = create_entry(dialog, table, 3, 
			    s ? s->user : g_get_user_name(), 
			    keyval);

    create_label(_("_Password:"), table, 4, &keyval);
    password = create_entry(dialog, table, 4, 
			    s ? s->passwd : NULL, 
			    keyval);
    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);

    create_label(_("_Prefix:"), table, 5, &keyval);
    prefix = create_entry(dialog, table, 5, 
			  mn ? mn->dir : NULL, keyval);

    gtk_widget_show_all(GTK_WIDGET(dialog));
    gnome_dialog_close_hides(dialog, TRUE);
    gtk_widget_grab_focus(folder_name);

    /* FIXME: I don't like this loop. */
    while( (button = gnome_dialog_run(dialog)) == 2) 
	gnome_help_display(NULL, &help_entry);
    
    if(button == 0) { /* do create/update */
	if(!mn) { 
	    insert = TRUE; 
	    s = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
	}
	else insert = FALSE;
	
	port_no = atoi(gtk_entry_get_text(GTK_ENTRY(port)));
	libbalsa_server_set_host(s, gtk_entry_get_text(GTK_ENTRY(server)), 
				 port_no);
	libbalsa_server_set_username
	    (s, gtk_entry_get_text(GTK_ENTRY(username)));
	libbalsa_server_set_password
	    (s, gtk_entry_get_text(GTK_ENTRY(password)));
	
	if(!mn)
	    mn = balsa_mailbox_node_new_imap_folder(s, NULL);

	g_free(mn->dir);  
	mn->dir = g_strdup(gtk_entry_get_text(GTK_ENTRY(prefix)));
	g_free(mn->name); 
	mn->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(folder_name)));
	
	if(insert) {
	    g_node_append(balsa_app.mailbox_nodes, gnode = g_node_new(mn));
	    balsa_mailbox_node_append_subtree(mn, gnode);
	    config_folder_add(mn, NULL);
	} else {
	    gnode = find_gnode_of_folder(balsa_app.mailbox_nodes, mn);
	    /* FIXME: remove children here and insert new ones */
	    config_folder_update(mn);
	}
	balsa_mblist_repopulate(balsa_app.mblist);
    }

    gtk_widget_destroy(GTK_WIDGET(dialog));
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
    balsa_information(LIBBALSA_INFORMATION_WARNING,
		      _("Delete this remote IMAP folder requested"));
}
