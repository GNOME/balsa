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

#include <unistd.h>
#include "balsa-app.h"
#include "local-mailbox.h"
#include "mailbox-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"

/* MailboxNode object is a GUI representation of a mailbox, or entire 
   set of them. It can read itself from the configuration, save its data,
   and provide a dialog box for the properties edition.
   Folders can additionally scan associated directory or IMAP server to
   retrieve their tree of mailboxes.
*/
static GtkObjectClass *parent_class = NULL;

static void balsa_mailbox_node_class_init(BalsaMailboxNodeClass *
					     klass);
static void balsa_mailbox_node_init(BalsaMailboxNode * mn);
static void balsa_mailbox_node_destroy(GtkObject * object);

static void balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn,
						const gchar * prefix);
static void balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn,
						const gchar * prefix);

enum {
    SAVE_CONFIG,
    LOAD_CONFIG,
    SHOW_PROP_DIALOG,
    APPEND_SUBTREE,
    LAST_SIGNAL
};

static guint balsa_mailbox_node_signals[LAST_SIGNAL];

GtkType balsa_mailbox_node_get_type(void)
{
    static GtkType mailbox_node_type = 0;

    if (!mailbox_node_type) {
	static const GtkTypeInfo mailbox_node_info = {
	    "BalsaMailboxNode",
	    sizeof(BalsaMailboxNode),
	    sizeof(BalsaMailboxNodeClass),
	    (GtkClassInitFunc)  balsa_mailbox_node_class_init,
	    (GtkObjectInitFunc) balsa_mailbox_node_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_node_type =
	    gtk_type_unique(gtk_object_get_type(), &mailbox_node_info);
    }
    
    return mailbox_node_type;
}

static void
balsa_mailbox_node_class_init(BalsaMailboxNodeClass * klass)
{
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(GTK_TYPE_OBJECT);

    object_class = GTK_OBJECT_CLASS(klass);

    balsa_mailbox_node_signals[SAVE_CONFIG] =
	gtk_signal_new("save-config", GTK_RUN_FIRST, object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMailboxNodeClass, save_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);
    balsa_mailbox_node_signals[LOAD_CONFIG] =
	gtk_signal_new("load-config", GTK_RUN_FIRST, object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMailboxNodeClass, load_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);
    balsa_mailbox_node_signals[SHOW_PROP_DIALOG] =
	gtk_signal_new("show-prop-dialog", GTK_RUN_LAST, object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMailboxNodeClass, 
					 show_prop_dialog),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
    balsa_mailbox_node_signals[APPEND_SUBTREE] =
	gtk_signal_new("append-subtree", GTK_RUN_FIRST, object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaMailboxNodeClass, 
					 append_subtree),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);
    gtk_object_class_add_signals(object_class,
				 balsa_mailbox_node_signals,
				 LAST_SIGNAL);

    klass->save_config = balsa_mailbox_node_real_save_config;
    klass->load_config = balsa_mailbox_node_real_load_config;
    klass->show_prop_dialog = NULL;
    klass->append_subtree   = NULL;

    object_class->destroy = balsa_mailbox_node_destroy;
}

static void
balsa_mailbox_node_init(BalsaMailboxNode * mn)
{
    mn->parent = NULL;
    mn->mailbox = NULL;
    mn->style = 0;
    mn->name = NULL;
}

static void
balsa_mailbox_node_destroy(GtkObject * object)
{
    BalsaMailboxNode *mn;

    mn = BALSA_MAILBOX_NODE(object);

    /* FIXME: should we use references to mailboxes? */
    mn->parent  = NULL; 
    mn->mailbox = NULL;
    g_free(mn->name);
    mn->name = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

static void
balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn, const gchar * prefix)
{}

static void
balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn, const gchar * prefix)
{}


GtkObject *
balsa_mailbox_node_new(void)
{
    BalsaMailboxNode *mn = gtk_type_new(balsa_mailbox_node_get_type());
    return GTK_OBJECT(mn);
}

static void
dir_conf_edit(BalsaMailboxNode* mb)
{
    GtkWidget *err_dialog =
	gnome_error_dialog(_("The folder edition to be written."));
    gnome_dialog_run_and_close(GNOME_DIALOG(err_dialog));
}

static void
read_dir_cb(BalsaMailboxNode* mb, GNode* r)
{
    printf("read_dir_cb: reading from %s\n", mb->name);
    read_dir(r, mb->name);
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox * mb)
{
    BalsaMailboxNode *mbn;
    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    mbn->mailbox = mb;
    gtk_signal_connect(GTK_OBJECT(mbn), "show-prop-dialog", 
		       mailbox_conf_edit, NULL);
    return mbn;
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_dir(const gchar* dir)
{
    BalsaMailboxNode *mbn;
    gchar *tmppath;

    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    tmppath = g_strdup_printf("%s/.expanded", dir);
    mbn->expanded = (access(tmppath, F_OK) != -1);
    g_free(tmppath);
    mbn->name = g_strdup(dir);
    gtk_signal_connect(GTK_OBJECT(mbn), "show-prop-dialog", 
		       dir_conf_edit, NULL);
    gtk_signal_connect(GTK_OBJECT(mbn), "append-subtree", 
		       read_dir_cb, NULL);
    return mbn;
}

void
balsa_mailbox_node_show_prop_dialog(BalsaMailboxNode* mn)
{
    gtk_signal_emit(GTK_OBJECT(mn),
		    balsa_mailbox_node_signals[SHOW_PROP_DIALOG]);
}

void
balsa_mailbox_node_append_subtree(BalsaMailboxNode * mn, GNode *r)
{
    gtk_signal_emit(GTK_OBJECT(mn),
		    balsa_mailbox_node_signals[APPEND_SUBTREE], r);
}

void 
balsa_mailbox_node_show_prop_dialog_cb(GtkWidget * widget, gpointer data)
{
    balsa_mailbox_node_show_prop_dialog((BalsaMailboxNode*)data);
}

static void
add_menu_entry(GtkWidget * menu, const gchar * label, GtkSignalFunc cb,
	       BalsaMailboxNode * mbnode)
{
    GtkWidget *menuitem;

    menuitem = label ? gtk_menu_item_new_with_label(label)
	: gtk_menu_item_new();

    if (cb)
	gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
			   GTK_SIGNAL_FUNC(cb), mbnode);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
}

static void
mb_open_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    mblist_open_mailbox(mbnode->mailbox);
}

static void
mb_close_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    balsa_window_close_mailbox(balsa_app.main_window, mbnode->mailbox);
    balsa_mblist_have_new(balsa_app.mblist);
}

static void
mb_conf_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    balsa_mailbox_node_show_prop_dialog(mbnode);
    /* mailbox_conf_edit (mbnode); */
}

static void
mb_del_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    mailbox_conf_delete(mbnode);
}

/* mb_inbox_cb:
   sets the given mailbox as inbox.
*/
static void
mb_inbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_INBOX);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_sentbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_SENT);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_trash_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_TRASH);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

static void
mb_draftbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_DRAFT);
    balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}


GtkWidget *
balsa_mailbox_node_get_context_menu(BalsaMailboxNode * mbnode)
{
    GtkWidget *menu;
    GtkWidget *submenu;
    GtkWidget *menuitem;

    /*  g_return_val_if_fail(mailbox != NULL, NULL); */

    menu = gtk_menu_new();

    submenu = gtk_menu_new();
    add_menu_entry(submenu, _("Local Mbox Mailbox..."),  
		   mailbox_conf_add_mbox_cb, NULL);
    add_menu_entry(submenu, _("Local Maildir Mailbox..."), 
		   mailbox_conf_add_maildir_cb, NULL);
    add_menu_entry(submenu, _("Local MH Mailbox..."),
		   mailbox_conf_add_mh_cb, NULL);
    add_menu_entry(submenu, _("Remote IMAP Mailbox..."), 
		   mailbox_conf_add_imap_cb, NULL);
    gtk_widget_show(submenu);
    
    menuitem = gtk_menu_item_new_with_label(_("New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_append(GTK_MENU(menu), menuitem);
    
    /* If we didn't click on a mailbox node then there is only one option. */

    add_menu_entry(menu, NULL, NULL, mbnode);

    if (mbnode->mailbox) {
	if (mbnode->mailbox->open_ref == 0)
	    add_menu_entry(menu, _("Open"), mb_open_cb, mbnode);
	else
	    add_menu_entry(menu, _("Close"), mb_close_cb, mbnode);
    }
    add_menu_entry(menu, _("Properties..."), mb_conf_cb, mbnode);
    add_menu_entry(menu, _("Delete"),        mb_del_cb,  mbnode);

    
    if (mbnode->mailbox) {
	add_menu_entry(menu, NULL, NULL, mbnode);
	
	add_menu_entry(menu, _("Mark as Inbox"),    mb_inbox_cb,    mbnode);
	add_menu_entry(menu, _("Mark as Sentbox"),  mb_sentbox_cb,  mbnode);
	add_menu_entry(menu, _("Mark as Trash"),    mb_trash_cb,    mbnode);
	add_menu_entry(menu, _("Mark as Draftbox"), mb_draftbox_cb, mbnode);
    }
    return menu;
}
