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
#include "folder-scanners.h"
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
    mn->threading_type = BALSA_INDEX_THREADING_JWZ;
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
    printf("read_dir_cb: reading from %s\n", mb->name);
    scanner_local_dir(r, mb->name, add_local_folder, add_local_mailbox);
}

static void
imap_dir_cb(BalsaMailboxNode* mb, GNode* r)
{
    g_return_if_fail(mb->server);
    scanner_imap_dir(r, mb->server, mb->dir, 3,
	     add_imap_folder, add_imap_mailbox);
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

/* balsa_mailbox_node_new_from_config:
   creates the mailbox node from given configuration data.
   Because local folders are not very useful, we assume that folders created
   in this way are IMAP folders. Otherwise, we should follow a procedure 
   similiar to mailbox creation from configuration data.
*/
BalsaMailboxNode*
balsa_mailbox_node_new_from_config(const gchar* prefix)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    gnome_config_push_prefix(prefix);

    printf("Creating remote server %s\n", prefix);
    folder->server = LIBBALSA_SERVER(
	libbalsa_server_new(LIBBALSA_SERVER_IMAP));
    /* take over the ownership */
    gtk_object_ref(GTK_OBJECT(folder->server)); 
    gtk_object_sink(GTK_OBJECT(folder->server)); 
    printf("Loading its configuration...\n");
    libbalsa_server_load_config(folder->server, 143);
  
    printf("Server loaded, host: %s, port %d\n", folder->server->host,
	   folder->server->port);
    gtk_signal_connect(GTK_OBJECT(folder), "append-subtree", 
		       imap_dir_cb, NULL);
    folder->name = gnome_config_get_string("Name");
    folder->dir = gnome_config_get_string("Directory");
    gnome_config_pop_prefix();

    return folder;
}

BalsaMailboxNode*
balsa_mailbox_node_new_imap(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    g_assert(s);

    folder->server = s;
    gtk_object_ref(GTK_OBJECT(s));
    folder->dir = g_strdup(p);
    folder->mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
    libbalsa_mailbox_remote_set_server(
	LIBBALSA_MAILBOX_REMOTE(folder->mailbox), s);
    libbalsa_mailbox_imap_set_path(LIBBALSA_MAILBOX_IMAP(folder->mailbox), p);

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
    add_menu_entry(submenu, _("Local mbox mailbox..."),  
		   mailbox_conf_add_mbox_cb, NULL);
    add_menu_entry(submenu, _("Local Maildir mailbox..."), 
		   mailbox_conf_add_maildir_cb, NULL);
    add_menu_entry(submenu, _("Local MH mailbox..."),
		   mailbox_conf_add_mh_cb, NULL);
    add_menu_entry(submenu, _("Remote IMAP mailbox..."), 
		   mailbox_conf_add_imap_cb, NULL);
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
    add_menu_entry(menu, _("Properties..."), mb_conf_cb, mbnode);
    add_menu_entry(menu, _("Delete"),        mb_del_cb,  mbnode);

    
    if (mbnode->mailbox) {
	add_menu_entry(menu, NULL, NULL, mbnode);
	
	if(mbnode->mailbox != balsa_app.inbox)
	    add_menu_entry(menu, _("Mark as Inbox"),    mb_inbox_cb,    mbnode);
	if(mbnode->mailbox != balsa_app.sentbox)
	    add_menu_entry(menu, _("Mark as Sentbox"),  mb_sentbox_cb,  mbnode);
	if(mbnode->mailbox != balsa_app.trash)
	    add_menu_entry(menu, _("Mark as Trash"),    mb_trash_cb,    mbnode);
	if(mbnode->mailbox != balsa_app.draftbox)
	    add_menu_entry(menu, _("Mark as Draftbox"), mb_draftbox_cb, mbnode);
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
    if(!LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) return FALSE;

    path = LIBBALSA_MAILBOX_LOCAL(mailbox)->path;

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

static GNode*
add_local_mailbox(GNode *root, const gchar * name, const gchar * path)
{
    LibBalsaMailbox *mailbox;
    GNode *node;
    GtkType type;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.inbox)->path) ==
	    0) return NULL;
    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.outbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.outbox)->path) ==
	    0) return NULL;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.sentbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.sentbox)->path)
	    == 0)
	    return NULL;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.draftbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.draftbox)->path)
	    == 0)
	    return NULL;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.trash))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.trash)->path) ==
	    0) return NULL;

    /* don't add if the mailbox is already in the configuration */
    if (find_by_path(balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
		     G_TRAVERSE_ALL, path))
	return NULL;

    type = libbalsa_mailbox_type_from_path(path);

    if ( type == LIBBALSA_TYPE_MAILBOX_MH ) {
	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mh_new(path, FALSE));
    } else if ( type == LIBBALSA_TYPE_MAILBOX_MBOX ) {
	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mbox_new(path, FALSE));
    } else if ( type == LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_maildir_new(path, FALSE));
    } else {
	/* type is not a valid local mailbox type. */
	g_assert_not_reached();
    }
    mailbox->name = g_strdup(name);
    
    node = g_node_new(balsa_mailbox_node_new_from_mailbox(mailbox));

    if (balsa_app.debug)
	g_print(_("Local mailbox loaded as: %s\n"),
		gtk_type_name(GTK_OBJECT_TYPE(mailbox)));
    
    /* no type checking, parent is NULL for root */
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
    return node;
}

GNode* add_local_folder(GNode*root, const char*d_name, const char* path)
{
    GNode *node = g_node_new(balsa_mailbox_node_new_from_dir(path));
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
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

static gboolean
traverse_find_parent(GNode * node, gpointer * d)
{
    BalsaMailboxNode * mbnode;
    if(node->data == NULL)
	return FALSE;
    
    mbnode = (BalsaMailboxNode *) node->data;

    g_return_val_if_fail(mbnode->dir, FALSE);
    if (strcmp(mbnode->dir, (gchar *) d[0]))
	return FALSE;

    d[1] = node;
    return TRUE;
}

static GNode*
get_parent_by_name(GNode* root, const gchar* path)
{
    gpointer d[2];

    if(strcmp(BALSA_MAILBOX_NODE(root->data)->dir, path) == 0)
	return root;

    d[0] = (gchar*) path;
    d[1] = NULL;
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) traverse_find_parent, d);

    return d[1];
}
static GNode* add_imap_entry(GNode*root, const char* fn, 
			     LibBalsaMailboxImap* mailbox, char delim)
{ 
    GNode* parent;
    BalsaMailboxNode* mbnode;
    gchar * parent_name = get_parent_folder_name(fn, delim);

    printf("Looking for parent of name %s\n", parent_name);
    parent = get_parent_by_name(root, parent_name);
    g_return_val_if_fail(parent, NULL);
    if(mailbox)
	mbnode = 
	    balsa_mailbox_node_new_from_mailbox(LIBBALSA_MAILBOX(mailbox));
    else {
	mbnode = balsa_mailbox_node_new();
	mbnode->name = g_strdup(fn);
    }
    mbnode->dir = g_strdup(fn);
    return g_node_append(parent, g_node_new(mbnode));
}

/* add_imap_mailbox:
   add given mailbox unless its base name begins on dot.
*/
static GNode*
add_imap_mailbox(GNode*root, const char* fn, char delim)
{ 
    LibBalsaMailboxImap* m;
    const gchar *basename;

    basename = strrchr(fn, delim);
    if(!basename) basename = fn;
    else { if(*++basename == '.') return NULL; }

    m = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
    libbalsa_mailbox_remote_set_server(
	LIBBALSA_MAILBOX_REMOTE(m), BALSA_MAILBOX_NODE(root->data)->server);
    libbalsa_mailbox_imap_set_path(m, fn);
    printf("Adding mailbox of name %s\n", basename);
    LIBBALSA_MAILBOX(m)->name = g_strdup(basename);
    return add_imap_entry(root, fn, m, delim);
}

static GNode*
add_imap_folder(GNode*root, const char* fn, char delim)
{ 
    return add_imap_entry(root, fn, NULL, delim);
}

