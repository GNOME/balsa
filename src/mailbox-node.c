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

#include "config.h"

#if defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED)
#include <pthread.h>
/* for sched_yield() prototype */
#include <sched.h>
#endif

#include <unistd.h>
#include <string.h>
#include "balsa-app.h"
#include "folder-scanners.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "notify.h"
#include "filter.h"

/* MailboxNode object is a GUI representation of a mailbox, or entire 
   set of them. It can read itself from the configuration, save its data,
   and provide a dialog box for the properties edition.
   Folders can additionally scan associated directory or IMAP server to
   retrieve their tree of mailboxes.
*/
static GObjectClass *parent_class = NULL;

static void balsa_mailbox_node_class_init(BalsaMailboxNodeClass *
					     klass);
static void balsa_mailbox_node_init(BalsaMailboxNode * mn);
static void balsa_mailbox_node_finalize(GObject * object);

static void balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn,
						const gchar * prefix);
static void balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn,
						const gchar * prefix);

static GNode* imap_scan_create_mbnode(GNode*root, const char* fn, char delim, 
				      gboolean scanned);
static GNode* add_local_mailbox(GNode*root, const char*d_name, const char* fn);
static GNode* add_local_folder(GNode*root, const char*d_name, const char* fn);

static void add_imap_mailbox(const char *fn, char delim,
			     gboolean scanned, gpointer data);
static void add_imap_folder(const char *fn, char delim,
			    gboolean scanned, gpointer data);

enum {
    SAVE_CONFIG,
    LOAD_CONFIG,
    SHOW_PROP_DIALOG,
    APPEND_SUBTREE,
    LAST_SIGNAL
};

static guint balsa_mailbox_node_signals[LAST_SIGNAL];

GType
balsa_mailbox_node_get_type(void)
{
    static GType mailbox_node_type = 0;

    if (!mailbox_node_type) {
	static const GTypeInfo mailbox_node_info = {
	    sizeof(BalsaMailboxNodeClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc)  balsa_mailbox_node_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(BalsaMailboxNode),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) balsa_mailbox_node_init
	};

	mailbox_node_type =
	    g_type_register_static(G_TYPE_OBJECT, "BalsaMailboxNode",
                                   &mailbox_node_info, 0);
    }
    
    return mailbox_node_type;
}

static void
balsa_mailbox_node_class_init(BalsaMailboxNodeClass * klass)
{
    GObjectClass *object_class;

    parent_class = g_type_class_peek_parent(klass);

    object_class = G_OBJECT_CLASS(klass);

    balsa_mailbox_node_signals[SAVE_CONFIG] =
	g_signal_new("save-config",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaMailboxNodeClass, save_config),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
		     G_TYPE_NONE, 1,
	             G_TYPE_POINTER);
    balsa_mailbox_node_signals[LOAD_CONFIG] =
	g_signal_new("load-config",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaMailboxNodeClass, load_config),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
		     G_TYPE_NONE, 1,
	             G_TYPE_POINTER);
    balsa_mailbox_node_signals[SHOW_PROP_DIALOG] =
	g_signal_new("show-prop-dialog",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(BalsaMailboxNodeClass,
                                     show_prop_dialog),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
		     G_TYPE_NONE, 0);
    balsa_mailbox_node_signals[APPEND_SUBTREE] =
	g_signal_new("append-subtree",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaMailboxNodeClass, append_subtree),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
		     G_TYPE_NONE, 1,
	             G_TYPE_POINTER);

    klass->save_config = balsa_mailbox_node_real_save_config;
    klass->load_config = balsa_mailbox_node_real_load_config;
    klass->show_prop_dialog = NULL;
    klass->append_subtree   = NULL;

    object_class->finalize = balsa_mailbox_node_finalize;
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
    mn->sort_field = BALSA_SORT_DATE;
    mn->subscribed = FALSE;
    mn->scanned = FALSE;
}

static void
balsa_mailbox_node_finalize(GObject * object)
{
    BalsaMailboxNode *mn;

    mn = BALSA_MAILBOX_NODE(object);

    mn->parent  = NULL; 
    if(mn->mailbox) {
	g_object_unref(G_OBJECT(mn->mailbox)); mn->mailbox = NULL;
    }
    g_free(mn->name);          mn->name = NULL;
    g_free(mn->dir);           mn->dir = NULL;
    g_free(mn->config_prefix); mn->config_prefix = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(G_OBJECT(object));
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


BalsaMailboxNode *
balsa_mailbox_node_new(void)
{
    BalsaMailboxNode *mn =
        g_object_new(balsa_mailbox_node_get_type(), NULL);
    return mn;
}

static void
dir_conf_edit(BalsaMailboxNode* mb)
{
    GtkWidget *err_dialog =
	gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_ERROR,
                               GTK_BUTTONS_CLOSE,
                               _("The folder edition to be written."));
    gtk_dialog_run(GTK_DIALOG(err_dialog));
    gtk_widget_destroy(err_dialog);
}

static void
read_dir_cb(BalsaMailboxNode* mb, GNode* r)
{
    libbalsa_scanner_local_dir(r, mb->name, 
			       add_local_folder, add_local_mailbox);
    /* FIXME: we should just redo local subtree starting from root, but... */
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
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
imap_scan_attach_mailbox(GNode* node, const gchar* fn)
{
    LibBalsaMailboxImap *m;
    BalsaMailboxNode* mbnode = BALSA_MAILBOX_NODE(node->data);
    if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
        /* it already has a mailbox */
        return;
    g_signal_connect(G_OBJECT(mbnode), "show-prop-dialog",
                     G_CALLBACK(folder_conf_imap_sub_node), NULL);
    m = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
    libbalsa_mailbox_remote_set_server(
        LIBBALSA_MAILBOX_REMOTE(m),
        BALSA_MAILBOX_NODE(node->data)->server);
    m->path = g_strdup(fn);
    libbalsa_mailbox_imap_update_url(m);
    if(balsa_app.debug)
        printf("add_imap_mailbox: add mbox of name %s (full path %s)\n",
               fn, LIBBALSA_MAILBOX(m)->url);
    /* avoid allocating the name again: */
    LIBBALSA_MAILBOX(m)->name = mbnode->name;
    mbnode->name = NULL;
    mbnode->mailbox = LIBBALSA_MAILBOX(m);
    g_object_ref(G_OBJECT(m));
    /*g_object_sink(G_OBJECT(m)); */
}

/* imap_dir_cb:
   handles append-subtree signal for IMAP folder sets.
   Scanning imap folders may be a time consuming operation and this
   is why it should be done in a thread or in an idle function.
   For that reason, scanning is split into two parts:
   a. actual scanning that can be done in a thread.
   b. GUI update.
   extra care must be taken to avoid situations that the
   mailbox tree is deleted after a is started and before b.
   We do it by remembering url of the root and
   by finding the root again when phase b. is about to start.
*/
typedef struct imap_scan_item_ {
    gchar* fn;
    gboolean scanned, selectable;
    struct imap_scan_item_* next;
} imap_scan_item;

typedef struct {
    imap_scan_item* list;
    char delim;
} imap_scan_tree;
static void imap_scan_destroy_tree(imap_scan_tree* tree);

static void*
imap_dir_cb_real(void* r)
{
    GNode* n, *root=(GNode*)r;
    imap_scan_item *item;
    BalsaMailboxNode*mb = root->data;
    imap_scan_tree imap_tree = { NULL, '.' };

    g_return_val_if_fail(mb->server, NULL);

    balsa_mailbox_nodes_lock(FALSE);
    libbalsa_scanner_imap_dir(root, mb->server, mb->dir, mb->subscribed,
                              mb->list_inbox, 
                              balsa_app.imap_scan_depth,
			      add_imap_folder, add_imap_mailbox,
			      &imap_tree);
    balsa_mailbox_nodes_unlock(FALSE);

    /* phase b. */
    balsa_mailbox_nodes_lock(FALSE);
    for(item=imap_tree.list; item; item = item->next) {
	n = imap_scan_create_mbnode(root, item->fn, 
				    imap_tree.delim, item->scanned);
	if(item->selectable)
	    imap_scan_attach_mailbox(n, item->fn);
    }

    /* register whole tree */
    if(BALSA_MAILBOX_NODE(root->data)->name)
        printf("imap_dir_cb:  main mailbox node %s mailbox is %p\n", 
               BALSA_MAILBOX_NODE(root->data)->name, 
               BALSA_MAILBOX_NODE(root->data)->mailbox);
    g_node_traverse(root, G_IN_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) register_mailbox, NULL);
    /* FIXME: we should just redo local subtree starting from root, but... */
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
    balsa_mailbox_nodes_unlock(FALSE);
    imap_scan_destroy_tree(&imap_tree);
    gnome_appbar_pop(balsa_app.appbar);
    if(balsa_app.debug) printf("%d: Scanning done.\n", (int)time(NULL));
    return NULL;
}

static void
imap_dir_cb(BalsaMailboxNode* mb, GNode* r)
{
    gchar* msg;
    BalsaMailboxNode* mroot=mb;
#if defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED)
    pthread_t scan_th_id;
#endif
    while(mroot->parent)
	mroot = mroot->parent;
    msg = g_strdup_printf(_("Scanning %s. Please wait..."), mroot->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    /* process UI events */
    while(gtk_events_pending()) 
	gtk_main_iteration();
#if defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED)
    pthread_create(&scan_th_id, NULL, imap_dir_cb_real, r);
    pthread_detach(scan_th_id);
    /* give the thread change to start and grab the lock 
     * this is an imperfect way of doing it because it does not
     *  guarantee that the thread will actually manage to grab the lock
     *  but I cannot think of anything better right now. */
    sched_yield();
#else
    imap_dir_cb_real(r);
#endif
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox * mb)
{
    BalsaMailboxNode *mbn;
    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    mbn->mailbox = mb;
    g_signal_connect(G_OBJECT(mbn), "show-prop-dialog", 
		     G_CALLBACK(mailbox_conf_edit), NULL);
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
    g_signal_connect(G_OBJECT(mbn), "show-prop-dialog", 
		     G_CALLBACK(dir_conf_edit), NULL);
    g_signal_connect(G_OBJECT(mbn), "append-subtree", 
		     G_CALLBACK(read_dir_cb), NULL);
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
    libbalsa_server_load_config(folder->server);

#ifdef USE_SSL  
    printf("Server loaded, host: %s, %s.\n", folder->server->host,
	   folder->server->use_ssl ? "SSL" : "no SSL");
#else
    printf("Server loaded, host: %s\n", folder->server->host);
#endif
    g_signal_connect(G_OBJECT(folder), "show-prop-dialog", 
		     G_CALLBACK(folder_conf_imap_node), NULL);
    g_signal_connect(G_OBJECT(folder), "append-subtree", 
		     G_CALLBACK(imap_dir_cb), NULL);
    g_signal_connect(G_OBJECT(folder->server), "get-password",
                     G_CALLBACK(ask_password), NULL);
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
    if(def) folder->sort_field = BALSA_SORT_DATE;
    gnome_config_pop_prefix();

    return folder;
}

static BalsaMailboxNode*
balsa_mailbox_node_new_imap_node(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    g_assert(s);

    folder->server = s;
    folder->dir = g_strdup(p);
    g_signal_connect(G_OBJECT(folder), "append-subtree", 
		     G_CALLBACK(imap_dir_cb), NULL);

    return folder;
}

BalsaMailboxNode*
balsa_mailbox_node_new_imap(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new_imap_node(s, p);
    g_assert(s);

    folder->mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
    g_object_ref(G_OBJECT(folder->mailbox));
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

    g_signal_connect(G_OBJECT(folder), "show-prop-dialog", 
		     G_CALLBACK(folder_conf_imap_node), NULL);
    return folder;
}

void
balsa_mailbox_node_show_prop_dialog(BalsaMailboxNode* mn)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[SHOW_PROP_DIALOG], 0);
}

void
balsa_mailbox_node_append_subtree(BalsaMailboxNode * mn, GNode *r)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[APPEND_SUBTREE], 0, r);
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
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[LOAD_CONFIG], 0, prefix);
}

void
balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* prefix)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[SAVE_CONFIG], 0, prefix);
}

void
balsa_mailbox_local_rescan_parent(LibBalsaMailbox* mbx)
{
    gchar *dir; 
    GNode* parent = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mbx));

    for(dir = g_strdup(libbalsa_mailbox_local_get_path(mbx));
        strlen(dir)>1 /* i.e dir != "/" */ &&
            !(parent = balsa_find_dir(balsa_app.mailbox_nodes,dir));
        ) {
        gchar* tmp =  g_path_get_dirname(dir);
        g_free(dir);
        dir = tmp;
    }
    if(parent)
        balsa_mailbox_node_rescan(BALSA_MAILBOX_NODE(parent->data)); 
    else g_warning("parent for %s not found.\n", mbx->name);
    g_free(dir);
}

/* balsa_mailbox_node_rescan:
   rescans given folders. It can be called on configuration update or just
   to discover mailboxes that did not exist when balsa was initially 
   started.
   NOTE: applicable only to folders (mailbox collections).
   the expansion state preservation is not perfect, only top level is
   preserved.
*/
void
balsa_mailbox_node_rescan(BalsaMailboxNode* mn)
{
    GNode *gnode;

    balsa_mailbox_nodes_lock(FALSE);
    gnode = balsa_find_mbnode(balsa_app.mailbox_nodes, mn);
    balsa_mailbox_nodes_unlock(FALSE);

    if(gnode) {
	/* FIXME: the expanded state needs to be preserved; it would 
	   be reset when all the children are removed */
	gboolean expanded = mn->expanded;
        //balsa_mailbox_nodes_lock(TRUE);
	balsa_remove_children_mailbox_nodes(gnode);
        //balsa_mailbox_nodes_unlock(TRUE);
        balsa_mailbox_node_append_subtree(mn, gnode);
	if (expanded)
            /* if this is an IMAP node, we must scan the children */
	    balsa_mblist_scan_mailbox_node(mn);
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
	g_signal_connect(G_OBJECT(menuitem), "activate",
			 G_CALLBACK(cb), mbnode);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
}

static void
mb_open_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    balsa_mblist_open_mailbox(mbnode->mailbox);
}

static void
mb_close_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    balsa_window_close_mbnode(balsa_app.main_window, mbnode);
    balsa_mblist_have_new(balsa_app.mblist_tree_store);
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
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
}

static void
mb_sentbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_SENT);
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
}

static void
mb_trash_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_TRASH);
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
}

static void
mb_draftbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_DRAFT);
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
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

static void
mb_filter_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    if (mbnode->mailbox) filters_run_dialog(mbnode->mailbox);
    else
	/* FIXME : Perhaps should we be able to apply filters on
	   folders (ie recurse on all mailboxes in it), but there are
	   problems of infinite recursion (when one mailbox being
	   filtered is also the destination of the filter action (eg a
	   copy)). So let's see that later :) */
	g_print("You can apply filters only on mailbox\n");
}

GtkWidget *
balsa_mailbox_node_get_context_menu(BalsaMailboxNode * mbnode)
{
    GtkWidget *menu;
    GtkWidget *submenu;
    GtkWidget *menuitem;

    /*  g_return_val_if_fail(mailbox != NULL, NULL); */

    menu = gtk_menu_new();
    /* it's a single-use menu, so we must destroy it when we're done */
    g_signal_connect(G_OBJECT(menu), "selection-done",
                     G_CALLBACK(gtk_object_destroy), NULL);

    submenu = gtk_menu_new();
    add_menu_entry(submenu, _("Local mbox mailbox..."),  
		   GTK_SIGNAL_FUNC(mailbox_conf_add_mbox_cb), NULL);
    add_menu_entry(submenu, _("Local Maildir mailbox..."), 
		   GTK_SIGNAL_FUNC(mailbox_conf_add_maildir_cb), NULL);
    add_menu_entry(submenu, _("Local MH mailbox..."),
		   GTK_SIGNAL_FUNC(mailbox_conf_add_mh_cb), NULL);
    add_menu_entry(submenu, _("Remote IMAP mailbox..."), 
		   GTK_SIGNAL_FUNC(mailbox_conf_add_imap_cb), NULL);
    add_menu_entry(submenu, NULL, NULL, mbnode);
    add_menu_entry(submenu, _("Remote IMAP folder..."), 
		   GTK_SIGNAL_FUNC(folder_conf_add_imap_cb), NULL);
    add_menu_entry(submenu, _("Remote IMAP subfolder..."), 
		   GTK_SIGNAL_FUNC(folder_conf_add_imap_sub_cb), NULL);
    gtk_widget_show(submenu);
    
    menuitem = gtk_menu_item_new_with_label(_("New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    
    if(mbnode == NULL) {/* clicked on the empty space */
        add_menu_entry(menu, _("Rescan"), GTK_SIGNAL_FUNC(mb_rescan_cb), 
                       BALSA_MAILBOX_NODE(balsa_app.mailbox_nodes->data));
	return menu;
    }
    /* If we didn't click on a mailbox node then there is only one option. */
    add_menu_entry(menu, NULL, NULL, mbnode);

    if (mbnode->mailbox) {
	if (mbnode->mailbox->open_ref == 0)
	    add_menu_entry(menu, _("Open"),
                           G_CALLBACK(mb_open_cb), mbnode);
	else
	    add_menu_entry(menu, _("Close"),
                           G_CALLBACK(mb_close_cb), mbnode);
    }
    if (g_signal_has_handler_pending(G_OBJECT(mbnode),
                                     balsa_mailbox_node_signals
                                     [SHOW_PROP_DIALOG], 0, FALSE))
        add_menu_entry(menu, _("Properties..."),
                       G_CALLBACK(mb_conf_cb), mbnode);
    if (mbnode->mailbox || mbnode->config_prefix)
	add_menu_entry(menu, _("Delete"), 
                       G_CALLBACK(mb_del_cb),  mbnode);

    
    if (mbnode->mailbox) {
	add_menu_entry(menu, NULL, NULL, mbnode);
	if(LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox)) {
	    add_menu_entry(menu, _("Subscribe"),   
                           GTK_SIGNAL_FUNC(mb_subscribe_cb),   mbnode);
	    add_menu_entry(menu, _("Unsubscribe"), 
                           GTK_SIGNAL_FUNC(mb_unsubscribe_cb), mbnode);
	    add_menu_entry(menu, _("Rescan"),	   
                           GTK_SIGNAL_FUNC(mb_rescan_cb),      mbnode);
	    add_menu_entry(menu, NULL, NULL, mbnode);
	}
	
	if(mbnode->mailbox != balsa_app.inbox)
	    add_menu_entry(menu, _("Mark as Inbox"),    
                           GTK_SIGNAL_FUNC(mb_inbox_cb),    mbnode);
	if(mbnode->mailbox != balsa_app.sentbox)
	    add_menu_entry(menu, _("Mark as Sentbox"), 
                           GTK_SIGNAL_FUNC(mb_sentbox_cb),  mbnode);
	if(mbnode->mailbox != balsa_app.trash)
	    add_menu_entry(menu, _("Mark as Trash"),    
                           GTK_SIGNAL_FUNC(mb_trash_cb),    mbnode);
	if(mbnode->mailbox != balsa_app.draftbox)
	    add_menu_entry(menu, _("Mark as Draftbox"),
                           GTK_SIGNAL_FUNC(mb_draftbox_cb), mbnode);
	/* FIXME : No test on mailbox type is made yet, should we ? */
	add_menu_entry(menu, _("Edit/Apply filters"), 
                       GTK_SIGNAL_FUNC(mb_filter_cb), mbnode);
    } else {
	add_menu_entry(menu, _("Rescan"),   
                       GTK_SIGNAL_FUNC(mb_rescan_cb), mbnode);
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

/* remove_mailbox_from_nodes:
   must be called with balsa_mailbox_nodes_lock held.
*/
static GNode*
remove_mailbox_from_nodes(LibBalsaMailbox* mailbox)
{
    GNode* gnode;

    gnode = balsa_find_url(balsa_app.mailbox_nodes, mailbox->url);
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
		    g_type_name(G_OBJECT_TYPE(mailbox)));
    }
    g_free(url);
    /* no type checking, parent is NULL for root */
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
    return node;
}

GNode*
add_local_folder(GNode*root, const char*d_name, const char* path)
{
    GNode *node, *found;

    if(!root)
	return NULL;

    /* don't add if the folder is already in the configuration */
    balsa_mailbox_nodes_lock(FALSE);
    found = balsa_find_path(balsa_app.mailbox_nodes, path);
    balsa_mailbox_nodes_unlock(FALSE);
    if (found)
	return NULL;

    node = g_node_new(balsa_mailbox_node_new_from_dir(path));
    balsa_mailbox_nodes_lock(TRUE);
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)root->data;
    g_node_append(root, node);
    balsa_mailbox_nodes_unlock(TRUE);
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

/* imap_scan_create_mbnode:
 * Returns a node for the path `fn'.
 * Finds the node if it exists, and creates one if it doesn't.
 */
static GNode* 
imap_scan_create_mbnode(GNode*root, const char* fn, char delim, 
			gboolean scanned)
{ 
    GNode* node;
    gchar * parent_name;
    GNode* parent;
    BalsaMailboxNode* mbnode;
    const gchar *basename;
    gchar* url;

    node = balsa_find_dir(root, fn);
    if (node && node != root)
	return node;

    parent_name = get_parent_folder_name(fn, delim);
    parent = balsa_find_dir(root, parent_name);
    if (parent == NULL)
        parent = root;
    g_free(parent_name);

    g_return_val_if_fail(parent, NULL);

    url = g_strdup_printf("imap%s://%s/%s",
#ifdef USE_SSL
			  BALSA_MAILBOX_NODE(root->data)->server->use_ssl 
                          ? "s" : "",
#else
			  "",
#endif
			  BALSA_MAILBOX_NODE(root->data)->server->host,
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
	mbnode->scanned = scanned;
	node = g_node_new(mbnode);
    }

    return g_node_append(parent, node);
}

/* add_imap_mailbox:
   add given mailbox unless its base name begins on dot.
*/
static void
add_imap_entry(const char* fn, char delim, gboolean scanned, 
	       gboolean selectable, void* data)
{
    imap_scan_tree *tree = (imap_scan_tree*)data;
    imap_scan_item* dt = g_new0(imap_scan_item,1);
    dt->fn   = g_strdup(fn);
    dt->scanned = scanned;
    dt->selectable = selectable;
    dt->next = tree->list;
    tree->list = dt;
    tree->delim = delim;
}
static void
imap_scan_destroy_tree(imap_scan_tree* tree)
{
    while(tree->list) {
	imap_scan_item* t = tree->list;
	g_free(t->fn);
	tree->list = t->next;
	g_free(t);
    }
}

static void
add_imap_mailbox(const char* fn, char delim, gboolean scanned, void* data)
{ 
    const gchar *basename = strrchr(fn, delim);
    if(!basename) basename = fn;
    else { 
	if(*++basename == '.' && delim != '.') /* ignore mailboxes
						  that begin with a dot */
	    return; 
    }
    add_imap_entry(fn, delim, scanned, TRUE, data);
}

static void
add_imap_folder(const char* fn, char delim, gboolean scanned, void* data)
{ 
    if(balsa_app.debug) 
	printf("add_imap_folder: Adding folder of path %s\n", fn);
    add_imap_entry(fn, delim, scanned, FALSE, data); 
}

