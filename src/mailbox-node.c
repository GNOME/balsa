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

#include <unistd.h>
#include "balsa-app.h"
#include "folder-scanners.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "notify.h"

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

static GNode* add_local_mailbox(GNode*root, const char*d_name, const char* fn);
static GNode* add_local_folder(GNode*root, const char*d_name, const char* fn);

static GNode* add_imap_mailbox(GNode*root, const char* fn, char delim);
static GNode* add_imap_folder(GNode*root, const char* fn, char delim);

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
    mn->dir = NULL;
    mn->config_prefix = NULL;
    mn->threading_type = BALSA_INDEX_THREADING_SIMPLE;
    mn->sort_type = GTK_SORT_DESCENDING;
    mn->sort_field = BALSA_SORT_NO;
    mn->subscribed = FALSE;
}

static void
balsa_mailbox_node_destroy(GtkObject * object)
{
    BalsaMailboxNode *mn;

    mn = BALSA_MAILBOX_NODE(object);

    mn->parent  = NULL; 
    if(mn->mailbox) {
	gtk_object_unref(GTK_OBJECT(mn->mailbox)); mn->mailbox = NULL;
    }
    g_free(mn->name);          mn->name = NULL;
    g_free(mn->dir);           mn->dir = NULL;
    g_free(mn->config_prefix); mn->config_prefix = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

static void
balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn, const gchar * prefix)
{
    if(mn->name)
	printf("Saving mailbox node %s with prefix %s\n", mn->name, prefix);
    libbalsa_server_save_config(mn->server);
    gnome_config_set_string("Name",      mn->name);
    gnome_config_set_string("Directory", mn->dir);
    gnome_config_set_bool("Subscribed",  mn->subscribed);
    gnome_config_set_bool("ListInbox",   mn->list_inbox);
    gnome_config_set_int("Threading",    mn->threading_type);
    gnome_config_set_int("SortType",     mn->sort_type);
    gnome_config_set_int("SortField",    mn->sort_field);
    
    g_free(mn->config_prefix);
    mn->config_prefix = g_strdup(prefix);
}

static void
balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn, const gchar * prefix)
{
    g_free(mn->config_prefix);
    mn->config_prefix = g_strdup(prefix);
    g_free(mn->name);
    mn->name = gnome_config_get_string("Name");
}


BalsaMailboxNode*
balsa_mailbox_node_new(void)
{
    BalsaMailboxNode *mn = gtk_type_new(balsa_mailbox_node_get_type());
    return mn;
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
    libbalsa_scanner_local_dir(r, mb->name, 
			       add_local_folder, add_local_mailbox);
}

static gboolean
register_mailbox(GNode* node, gpointer data)
{
    g_return_val_if_fail(node->data, FALSE);
    if(!BALSA_MAILBOX_NODE(node->data)->mailbox) 
	/* this happens only when there is no mailboxes in the dir */
	return  FALSE;
    
    libbalsa_notify_register_mailbox(BALSA_MAILBOX_NODE(node->data)->mailbox);
    return FALSE;
}

static void
imap_dir_cb(BalsaMailboxNode* mb, GNode* r)
{
    g_return_if_fail(mb->server);
    libbalsa_scanner_imap_dir(r, mb->server, mb->dir, mb->subscribed,
                              mb->list_inbox, 7,
			      add_imap_folder, add_imap_mailbox);
    /* register whole tree */
    printf("imap_dir_cb:  main mailbox node %s mailbox is %p\n", 
	   BALSA_MAILBOX_NODE(r->data)->name, 
	   BALSA_MAILBOX_NODE(r->data)->mailbox);
    g_node_traverse(r, G_IN_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) register_mailbox, NULL);
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox * mb)
{
    BalsaMailboxNode *mbn;
    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    mbn->mailbox = mb;
    gtk_object_ref(GTK_OBJECT(mb));
    gtk_object_sink(GTK_OBJECT(mb));
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
    mbn->dir  = g_strdup(dir);
    gtk_signal_connect(GTK_OBJECT(mbn), "show-prop-dialog", 
		       dir_conf_edit, NULL);
    gtk_signal_connect(GTK_OBJECT(mbn), "append-subtree", 
		       read_dir_cb, NULL);
    return mbn;
}

/* balsa_mailbox_node_new_from_config:
   creates the mailbox node from given configuration data.
   Because local folders are not very useful, we assume that folders created
   in this way are IMAP folders. Otherwise, we should follow a procedure 
   similiar to mailbox creation from configuration data.
*/
BalsaMailboxNode*
balsa_mailbox_node_new_from_config(const gchar* prefix)
{
    gint def;
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    gnome_config_push_prefix(prefix);

    folder->server = LIBBALSA_SERVER(
	libbalsa_server_new(LIBBALSA_SERVER_IMAP));
    /* take over the ownership */
    gtk_object_ref(GTK_OBJECT(folder->server)); 
    gtk_object_sink(GTK_OBJECT(folder->server)); 
    printf("Loading its configuration...\n");
    libbalsa_server_load_config(folder->server, 143);

#ifdef USE_SSL  
    printf("Server loaded, host: %s, port %d %s\n", folder->server->host,
	   folder->server->port,
	   folder->server->use_ssl ? "SSL" : "no SSL");
#else
    printf("Server loaded, host: %s, port %d\n", folder->server->host,
	   folder->server->port);
#endif
    gtk_signal_connect(GTK_OBJECT(folder), "show-prop-dialog", 
		       folder_conf_imap_node, NULL);
    gtk_signal_connect(GTK_OBJECT(folder), "append-subtree", 
		       imap_dir_cb, NULL);
    balsa_mailbox_node_load_config(folder, prefix);

    folder->dir = gnome_config_get_string("Directory");
    folder->subscribed =
	gnome_config_get_bool("Subscribed"); 
    folder->list_inbox =
	gnome_config_get_bool("ListInbox=true"); 
    folder->threading_type = gnome_config_get_int_with_default("Threading", &def);
    if(def) folder->threading_type = BALSA_INDEX_THREADING_SIMPLE;
    folder->sort_type = gnome_config_get_int_with_default("SortType", &def);
    if(def) folder->sort_type = GTK_SORT_ASCENDING;
    folder->sort_field = gnome_config_get_int_with_default("SortField", &def);
    if(def) folder->sort_field = BALSA_SORT_NO;
    gnome_config_pop_prefix();

    return folder;
}

static BalsaMailboxNode*
balsa_mailbox_node_new_imap_node(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    g_assert(s);

    folder->server = s;
    gtk_object_ref(GTK_OBJECT(s));
    gtk_object_sink(GTK_OBJECT(s));
    folder->dir = g_strdup(p);
    gtk_signal_connect(GTK_OBJECT(folder), "append-subtree", 
		       imap_dir_cb, NULL);

    return folder;
}

BalsaMailboxNode*
balsa_mailbox_node_new_imap(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new_imap_node(s, p);
    g_assert(s);

    folder->mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
    gtk_object_ref(GTK_OBJECT(folder->mailbox));
    libbalsa_mailbox_remote_set_server(
	LIBBALSA_MAILBOX_REMOTE(folder->mailbox), s);
    libbalsa_mailbox_imap_set_path(LIBBALSA_MAILBOX_IMAP(folder->mailbox), p);

    return folder;
}

BalsaMailboxNode*
balsa_mailbox_node_new_imap_folder(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new_imap_node(s, p);
    g_assert(s);

    gtk_signal_connect(GTK_OBJECT(folder), "show-prop-dialog", 
		        folder_conf_imap_node, NULL);
    return folder;
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

/* balsa_mailbox_node_load_config:
   load general configurtion: name ordering, threading option etc
   with some sane defaults.
*/
void
balsa_mailbox_node_load_config(BalsaMailboxNode* mn, const gchar* prefix)
{
    gtk_signal_emit(GTK_OBJECT(mn),
		    balsa_mailbox_node_signals[LOAD_CONFIG], prefix);
}

void
balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* prefix)
{
    gtk_signal_emit(GTK_OBJECT(mn),
		    balsa_mailbox_node_signals[SAVE_CONFIG], prefix);
}

/* balsa_mailbox_node_rescan:
   rescans given folders. It can be called on configuration update or just
   to discover mailboxes that did not exist when balsa was initially 
   started.
   NOTE: applicable only to folders (mailbox collections).
   the expansion state preservation is not perfect, only top level is
   preserved.
*/
void balsa_mailbox_node_rescan(BalsaMailboxNode* mn)
{
    GNode *gnode;

    g_return_if_fail(mn->mailbox == NULL);

    gnode = balsa_find_mbnode(balsa_app.mailbox_nodes, mn);

    if(gnode) {
	/* the expanded state needs to be preserved; it would 
	   be reset when all the children are removed */
	gboolean expanded = mn->expanded;
	balsa_remove_children_mailbox_nodes(gnode);
	balsa_mailbox_node_append_subtree(mn, gnode);
	mn->expanded = expanded;
	balsa_mblist_repopulate(balsa_app.mblist);
    } else g_warning("folder node %s (%p) not found in hierarchy.\n",
		     mn->name, mn);
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
    balsa_window_close_mbnode(balsa_app.main_window, mbnode);
    balsa_mblist_have_new(balsa_app.mblist);
}

static void
mb_conf_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    balsa_mailbox_node_show_prop_dialog(mbnode);
}

static void
mb_del_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    if(mbnode->mailbox)
	mailbox_conf_delete(mbnode);
    else folder_conf_delete(mbnode);
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

static void
mb_subscribe_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox));
    libbalsa_mailbox_imap_subscribe(LIBBALSA_MAILBOX_IMAP(mbnode->mailbox), 
				    TRUE);
}

static void
mb_unsubscribe_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox));
    libbalsa_mailbox_imap_subscribe(LIBBALSA_MAILBOX_IMAP(mbnode->mailbox),
				     FALSE);
}

static void
mb_rescan_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    /* no need for this:
    g_return_if_fail(mbnode->mailbox == NULL);
     * balsa_mailbox_node_rescan() does a more refined test.
     */
    balsa_mailbox_node_rescan(mbnode);
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
    add_menu_entry(submenu, _("Local mbox mailbox..."),  
		   mailbox_conf_add_mbox_cb, NULL);
    add_menu_entry(submenu, _("Local Maildir mailbox..."), 
		   mailbox_conf_add_maildir_cb, NULL);
    add_menu_entry(submenu, _("Local MH mailbox..."),
		   mailbox_conf_add_mh_cb, NULL);
    add_menu_entry(submenu, _("Remote IMAP mailbox..."), 
		   mailbox_conf_add_imap_cb, NULL);
    add_menu_entry(submenu, NULL, NULL, mbnode);
    add_menu_entry(submenu, _("Remote IMAP folder..."), 
		   folder_conf_add_imap_cb, NULL);
    add_menu_entry(submenu, _("Remote IMAP subfolder..."), 
		   folder_conf_add_imap_sub_cb, NULL);
    gtk_widget_show(submenu);
    
    menuitem = gtk_menu_item_new_with_label(_("New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_append(GTK_MENU(menu), menuitem);
    
    if(mbnode == NULL) /* clicked on the empty space */
	return menu;

    /* If we didn't click on a mailbox node then there is only one option. */
    add_menu_entry(menu, NULL, NULL, mbnode);

    if (mbnode->mailbox) {
	if (mbnode->mailbox->open_ref == 0)
	    add_menu_entry(menu, _("Open"), mb_open_cb, mbnode);
	else
	    add_menu_entry(menu, _("Close"), mb_close_cb, mbnode);
    }
    if (gtk_signal_handler_pending(GTK_OBJECT(mbnode),
				   balsa_mailbox_node_signals[SHOW_PROP_DIALOG],
				   FALSE))
	add_menu_entry(menu, _("Properties..."), mb_conf_cb, mbnode);
    if (mbnode->mailbox || mbnode->config_prefix)
	add_menu_entry(menu, _("Delete"),        mb_del_cb,  mbnode);

    
    if (mbnode->mailbox) {
	add_menu_entry(menu, NULL, NULL, mbnode);
	if(LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox)) {
	    add_menu_entry(menu, _("Subscribe"),   mb_subscribe_cb,   mbnode);
	    add_menu_entry(menu, _("Unsubscribe"), mb_unsubscribe_cb, mbnode);
	    add_menu_entry(menu, _("Rescan"),	   mb_rescan_cb,      mbnode);
	    add_menu_entry(menu, NULL, NULL, mbnode);
	}
	
	if(mbnode->mailbox != balsa_app.inbox)
	    add_menu_entry(menu, _("Mark as Inbox"),    mb_inbox_cb,    mbnode);
	if(mbnode->mailbox != balsa_app.sentbox)
	    add_menu_entry(menu, _("Mark as Sentbox"),  mb_sentbox_cb,  mbnode);
	if(mbnode->mailbox != balsa_app.trash)
	    add_menu_entry(menu, _("Mark as Trash"),    mb_trash_cb,    mbnode);
	if(mbnode->mailbox != balsa_app.draftbox)
	    add_menu_entry(menu, _("Mark as Draftbox"), mb_draftbox_cb, mbnode);
    } else {
	add_menu_entry(menu, _("Rescan"),   mb_rescan_cb,   mbnode);
    }

    return menu;
}

/* ---------------------------------------------------------------------
 * folder scanner related functions 
 * --------------------------------------------------------------------- */

/* add_local_mailbox
   the function scans the local mail directory (LMD) and adds them to the 
   list of mailboxes. Takes care not to duplicate any of the "standard"
   mailboxes (inbox, outbox etc). Avoids also problems with aliasing 
   (someone added a local mailbox - possibly aliased - located in LMD 
   to the configuration).
*/

static gboolean
traverse_find_path(GNode * node, gpointer * d)
{
    const gchar *path;
    LibBalsaMailbox * mailbox;
    if(node->data == NULL) /* true for root node only */
	return FALSE;
    
    mailbox = ((BalsaMailboxNode *) node->data)->mailbox;

    if(LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) 
	path = libbalsa_mailbox_local_get_path(mailbox);
    else if(!mailbox)
	path = ((BalsaMailboxNode *) node->data)->name;
    else 
	return FALSE;

    if (strcmp(path, (gchar *) d[0]))
	return FALSE;

    d[1] = node;
    return TRUE;
}

static GNode *
find_by_path(GNode * root, GTraverseType order, GTraverseFlags flags,
	     const gchar * path)
{
    gpointer d[2];

    d[0] = (gchar *) path;
    d[1] = NULL;
    g_node_traverse(root, order, flags, -1,
		    (GNodeTraverseFunc) traverse_find_path, d);

    return d[1];
}

static gboolean
traverse_find_url(GNode * node, gpointer * d)
{
    LibBalsaMailbox * mailbox;
    if(node->data == NULL) /* true for root node only */
	return FALSE;
    
    mailbox = ((BalsaMailboxNode *) node->data)->mailbox;
    if (mailbox == NULL || strcmp(mailbox->url, (gchar *) d[0]))
	return FALSE;

    d[1] = node;
    return TRUE;
}

static GNode *
find_by_url(GNode * root, GTraverseType order, GTraverseFlags flags,
	     const gchar * path)
{
    gpointer d[2];

    d[0] = (gchar *) path;
    d[1] = NULL;
    g_node_traverse(root, order, flags, -1,
		    (GNodeTraverseFunc) traverse_find_url, d);

    return d[1];
}


static GNode*
remove_mailbox_from_nodes(LibBalsaMailbox* mailbox)
{
    GNode* gnode = find_by_url(balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
			       G_TRAVERSE_ALL, mailbox->url);
    if (gnode)
	g_node_unlink(gnode);
    else
	gnode = g_node_new(balsa_mailbox_node_new_from_mailbox(mailbox));
    return gnode;
}
    
static GNode* 
remove_special_mailbox_by_url(const gchar* url)
{
    if (strcmp(url, balsa_app.trash->url) == 0)
	return remove_mailbox_from_nodes(balsa_app.trash);
    else if (strcmp(url, balsa_app.inbox->url) == 0)
	return remove_mailbox_from_nodes(balsa_app.inbox);
    else if (strcmp(url, balsa_app.outbox->url) == 0)
	return remove_mailbox_from_nodes(balsa_app.outbox);
    if (strcmp(url, balsa_app.sentbox->url) == 0)
	return remove_mailbox_from_nodes(balsa_app.sentbox);
    else if (strcmp(url, balsa_app.draftbox->url) == 0)
	return remove_mailbox_from_nodes(balsa_app.draftbox);
    else return NULL;
	
}

static GNode*
add_local_mailbox(GNode *root, const gchar * name, const gchar * path)
{
    LibBalsaMailbox *mailbox;
    GNode *node;
    GtkType type;
    gchar* url;

    if(root == NULL) return NULL;
    url = g_strconcat("file://", path, NULL);
    
    if( (node = remove_special_mailbox_by_url(url)) == NULL) {
	type = libbalsa_mailbox_type_from_path(path);
	
	if ( type == LIBBALSA_TYPE_MAILBOX_MH ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mh_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mbox_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	    mailbox =
		LIBBALSA_MAILBOX(libbalsa_mailbox_maildir_new(path, FALSE));
	} else {
	    /* type is not a valid local mailbox type. */
	    g_assert_not_reached(); mailbox = NULL;
	}
	if(!mailbox) {/* local mailbox could not be created; privileges? */
	    printf("Not accessible mailbox %s\n", path);
	    return NULL;
	}
	mailbox->name = g_strdup(name);
	
	node = g_node_new(balsa_mailbox_node_new_from_mailbox(mailbox));
	
	if (balsa_app.debug)
	    g_print(_("Local mailbox %s loaded as: %s\n"),
		    mailbox->name,
		    gtk_type_name(GTK_OBJECT_TYPE(mailbox)));
    }
    g_free(url);
    /* no type checking, parent is NULL for root */
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
    return node;
}

GNode* add_local_folder(GNode*root, const char*d_name, const char* path)
{
    GNode *node = g_node_new(balsa_mailbox_node_new_from_dir(path));

    if(!root)
	return NULL;

    /* don't add if the folder is already in the configuration */
    if (find_by_path(balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
		     G_TRAVERSE_ALL, path))
	return NULL;

    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
    if (balsa_app.debug)
	g_print(_("Local folder %s\n"), path );
		
    return node;
}	


/* ---------------------------------------------------------------------
 * IMAP folder scanner functions 
 * --------------------------------------------------------------------- */
static gchar*
get_parent_folder_name(const gchar* path, char delim)
{
    const gchar* last_delim = strrchr(path, delim);
    return last_delim ? g_strndup(path, last_delim-path)
	: g_strdup("");
}

/* add_imap_entry:
 * Returns a node for the path `fn'.
 * Finds the node if it exists, and creates one if it doesn't.
 */
static GNode* 
add_imap_entry(GNode*root, const char* fn, char delim)
{ 
    GNode* node;
    gchar * parent_name;
    GNode* parent;
    BalsaMailboxNode* mbnode;
    const gchar *basename;
    gchar* url;

    node = balsa_app_find_by_dir(root, fn);
    if (node && node != root)
	return node;

    parent_name = get_parent_folder_name(fn, delim);
    parent = balsa_app_find_by_dir(root, parent_name);
    if (parent == NULL)
        parent = root;
    g_free(parent_name);

    g_return_val_if_fail(parent, NULL);

    url = g_strdup_printf("imap%s://%s:%i/%s",
#ifdef USE_SSL
			  BALSA_MAILBOX_NODE(root->data)->server->use_ssl ? "s" : "",
#else
			  "",
#endif
			  BALSA_MAILBOX_NODE(root->data)->server->host,
			  BALSA_MAILBOX_NODE(root->data)->server->port,
			  fn);
    node = remove_special_mailbox_by_url(url);
    g_free(url);
    if(node != NULL) {
	g_free(BALSA_MAILBOX_NODE(node->data)->dir);
	BALSA_MAILBOX_NODE(node->data)->dir = g_strdup(fn);
    } else {
	mbnode = balsa_mailbox_node_new_imap_node(BALSA_MAILBOX_NODE
						  (root->data)->server, fn);
	basename = strrchr(fn, delim);
	if(!basename) basename = fn;
	else basename++;
	mbnode->name = g_strdup(basename);
	mbnode->parent = BALSA_MAILBOX_NODE(parent->data);
	mbnode->subscribed = mbnode->parent->subscribed;
	node = g_node_new(mbnode);
    }

    return g_node_append(parent, node);
}

/* add_imap_mailbox:
   add given mailbox unless its base name begins on dot.
   It is called as a callback from libmutt so it has to take measures not
   to call libmutt again. In particular, it CANNOT call
   libbalsa_mailbox_imap_set_path because ut would call in turn 
   mailbox_notify_register, which calls in turn libmutt code.

   FIXME: this needs some cleanup. Moving existing node (as found by
   remove_special_mailbox) to another branch of the tree should be
   easier/cleaner. A function like: add_gnode_to_its_parent() would do 
   the job.
   
*/
static GNode*
add_imap_mailbox(GNode*root, const char* fn, char delim)
{ 
    LibBalsaMailboxImap* m;
    const gchar *basename;
    GNode *node;
    BalsaMailboxNode* mbnode;

    basename = strrchr(fn, delim);
    if(!basename) basename = fn;
    else { 
	if(*++basename == '.' && delim != '.') /* ignore mailboxes
						  that begin with a dot */
	    return NULL; 
    }

    node = add_imap_entry(root, fn, delim);
    mbnode = BALSA_MAILBOX_NODE(node->data);
    if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
	/* it already has a mailbox */
	return node;
    gtk_signal_connect(GTK_OBJECT(mbnode), "show-prop-dialog",
		       folder_conf_imap_sub_node, NULL);
    m = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
    libbalsa_mailbox_remote_set_server(
	LIBBALSA_MAILBOX_REMOTE(m), 
	BALSA_MAILBOX_NODE(root->data)->server);
    m->path = g_strdup(fn);
    libbalsa_mailbox_imap_update_url(m);
    if(balsa_app.debug) 
	printf("add_imap_mailbox: add mbox of name %s (full path %s)\n", 
	       basename, fn);
    /* avoid allocating the name again: */
    LIBBALSA_MAILBOX(m)->name = mbnode->name;
    mbnode->name = NULL;
    mbnode->mailbox = LIBBALSA_MAILBOX(m);
    gtk_object_ref(GTK_OBJECT(m));
    gtk_object_sink(GTK_OBJECT(m));

    return node;
}

static GNode*
add_imap_folder(GNode*root, const char* fn, char delim)
{ 
    if(balsa_app.debug) 
	printf("add_imap_folder: Adding folder of path %s\n", fn);
    return add_imap_entry(root, fn, delim);
}

