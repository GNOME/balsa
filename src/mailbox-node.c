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
#include "filter.h"
#include "imap-server.h"
#include "i18n.h"
#include "libbalsa-conf.h"

/* MailboxNode object is a GUI representation of a mailbox, or entire 
   set of them. It can read itself from the configuration, save its data,
   and provide a dialog box for the properties edition.
   Folders can additionally scan associated directory or IMAP server to
   retrieve their tree of mailboxes.
*/
static GObjectClass *parent_class = NULL;

typedef struct imap_scan_item_ imap_scan_item;
struct imap_scan_item_ {
    gchar *fn;
    LibBalsaMailbox **special;
    unsigned scanned:1, selectable:1, marked:1;
};

static void balsa_mailbox_node_class_init(BalsaMailboxNodeClass *
					     klass);
static void balsa_mailbox_node_init(BalsaMailboxNode * mn);
static void balsa_mailbox_node_dispose(GObject * object);
static void balsa_mailbox_node_finalize(GObject * object);

static void balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn,
						const gchar * group);
static void balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn,
						const gchar * group);

static BalsaMailboxNode *imap_scan_create_mbnode(BalsaMailboxNode * root,
						 imap_scan_item * isi,
						 char delim);
static gboolean imap_scan_attach_mailbox(BalsaMailboxNode * mbnode,
                                         imap_scan_item * isi);
static gboolean bmbn_scan_children_idle(BalsaMailboxNode ** mn);

static BalsaMailboxNode *add_local_mailbox(BalsaMailboxNode * root,
					   const gchar * name,
					   const gchar * path,
					   GType type);
static BalsaMailboxNode *add_local_folder(BalsaMailboxNode * root,
					  const char *d_name,
					  const char *fn);

static void handle_imap_path(const char *fn, char delim, int noselect,
			     int noscan, int marked, void *data);
static gint check_imap_path(const char *fn, LibBalsaServer * server,
			    guint depth);
static void mark_imap_path(const gchar * fn, gint noselect, gint noscan,
			   gpointer data);

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
                     g_cclosure_marshal_VOID__VOID,
		     G_TYPE_NONE, 0);

    klass->save_config = balsa_mailbox_node_real_save_config;
    klass->load_config = balsa_mailbox_node_real_load_config;
    klass->show_prop_dialog = NULL;
    klass->append_subtree   = NULL;

    object_class->dispose = balsa_mailbox_node_dispose;
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
    mn->subscribed = FALSE;
    mn->scanned = FALSE;
}

static void
balsa_mailbox_node_dispose(GObject * object)
{
    BalsaMailboxNode *mn = BALSA_MAILBOX_NODE(object);

    if(mn->mailbox) {
	libbalsa_mailbox_set_frozen(mn->mailbox, TRUE);
	if (balsa_app.main_window)
	    balsa_window_close_mbnode(balsa_app.main_window, mn);
	g_object_unref(mn->mailbox);
	mn->mailbox = NULL;
    }

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
balsa_mailbox_node_finalize(GObject * object)
{
    BalsaMailboxNode *mn;

    mn = BALSA_MAILBOX_NODE(object);

    mn->parent  = NULL; 
    g_free(mn->name);          mn->name = NULL;
    g_free(mn->dir);           mn->dir = NULL;
    g_free(mn->config_prefix); mn->config_prefix = NULL;

    if (mn->server) {
	g_signal_handlers_disconnect_matched(mn->server,
                                             G_SIGNAL_MATCH_DATA, 0,
					     (GQuark) 0, NULL, NULL, mn);
	mn->server = NULL;
    }

    G_OBJECT_CLASS(parent_class)->finalize(G_OBJECT(object));
}

static void
balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn, const gchar * group)
{
    if(mn->name)
	printf("Saving mailbox node %s with group %s\n", mn->name, group);
    libbalsa_imap_server_save_config(LIBBALSA_IMAP_SERVER(mn->server));
    libbalsa_conf_set_string("Name",      mn->name);
    libbalsa_conf_set_string("Directory", mn->dir);
    libbalsa_conf_set_bool("Subscribed",  mn->subscribed);
    libbalsa_conf_set_bool("ListInbox",   mn->list_inbox);
    
    g_free(mn->config_prefix);
    mn->config_prefix = g_strdup(group);
}

static void
balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn, const gchar * group)
{
    g_free(mn->config_prefix);
    mn->config_prefix = g_strdup(group);
    g_free(mn->name);
    mn->name = libbalsa_conf_get_string("Name");
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
    balsa_information(LIBBALSA_INFORMATION_ERROR,
                      _("The folder edition to be written."));
}

/* read_dir_cb and helpers */

typedef struct _CheckPathInfo CheckPathInfo;
struct _CheckPathInfo {
    gchar *url;
    gboolean must_scan;
};

static void
check_url_func(const gchar * url, LibBalsaMailboxView * view,
	       CheckPathInfo * cpi)
{
    if ((view->exposed || view->open) && !cpi->must_scan
	&& strncmp(url, cpi->url, strlen(cpi->url)) == 0)
	cpi->must_scan = TRUE;
}

static gboolean
check_local_path(const gchar * path, guint depth)
{
    size_t len;
    CheckPathInfo cpi;

    if (depth < balsa_app.local_scan_depth)
        return TRUE;

    len = strlen(balsa_app.local_mail_directory);
    if (!strncmp(path, balsa_app.local_mail_directory, len)
	&& !strchr(&path[++len], G_DIR_SEPARATOR))
	/* Top level folder. */
	return TRUE;

    if (!libbalsa_mailbox_view_table)
        return FALSE;

    cpi.url = g_strconcat("file://", path, NULL);
    cpi.must_scan = FALSE;
    g_hash_table_foreach(libbalsa_mailbox_view_table,
                         (GHFunc) check_url_func, &cpi);
    if(balsa_app.debug) 
	printf("check_local_path: path \"%s\" must_scan %d.\n",
               cpi.url, cpi.must_scan);
    g_free(cpi.url);

    return cpi.must_scan;
}

static void
mark_local_path(BalsaMailboxNode *mbnode)
{
    mbnode->scanned = TRUE;
}

static void
read_dir_cb(BalsaMailboxNode* mb)
{
    libbalsa_scanner_local_dir(mb, mb->name, 
			       (LocalCheck *) check_local_path,
			       (LocalMark *) mark_local_path,
			       (LocalHandler *) add_local_folder,
			       (LocalHandler *) add_local_mailbox,
                               (mb->mailbox ?
                                G_TYPE_FROM_INSTANCE(mb->mailbox) :
                                (GType) 0));
}

static void
load_mailbox_view(BalsaMailboxNode * mbnode)
{
    LibBalsaMailbox *mailbox = mbnode->mailbox;

    if (!mailbox->url)
	return;

    mailbox->view =
	g_hash_table_lookup(libbalsa_mailbox_view_table, mailbox->url);
    if (libbalsa_mailbox_get_frozen(mailbox)
	&& libbalsa_mailbox_get_open(mailbox))
	/* We are rescanning. */
	balsa_window_open_mbnode(balsa_app.main_window, mbnode);
    libbalsa_mailbox_set_frozen(mailbox, FALSE);
}

static gboolean
imap_scan_attach_mailbox(BalsaMailboxNode * mbnode, imap_scan_item * isi)
{
    LibBalsaMailboxImap *m;

    /* If the mailbox was added from the config file, it is already
     * connected to "show-prop-dialog". */
    if (!g_signal_has_handler_pending(G_OBJECT(mbnode),
                                      balsa_mailbox_node_signals
                                      [SHOW_PROP_DIALOG], 0, FALSE))
	g_signal_connect(G_OBJECT(mbnode), "show-prop-dialog",
                         G_CALLBACK(folder_conf_imap_sub_node), NULL);
    if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
        /* it already has a mailbox */
        return FALSE;
    m = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
    libbalsa_mailbox_remote_set_server(LIBBALSA_MAILBOX_REMOTE(m),
				       mbnode->server);
    libbalsa_mailbox_imap_set_path(m, isi->fn);
    if(balsa_app.debug)
        printf("imap_scan_attach_mailbox: add mbox of name %s "
	       "(full path %s)\n", isi->fn, LIBBALSA_MAILBOX(m)->url);
    /* avoid allocating the name again: */
    LIBBALSA_MAILBOX(m)->name = mbnode->name;
    mbnode->name = NULL;
    mbnode->mailbox = LIBBALSA_MAILBOX(m);
    load_mailbox_view(mbnode);
    if (isi->special) {
	if (*isi->special)
	    g_object_remove_weak_pointer(G_OBJECT(*isi->special),
					 (gpointer) isi->special);
        *isi->special = LIBBALSA_MAILBOX(m);
	g_object_add_weak_pointer(G_OBJECT(m), (gpointer) isi->special);
    }

    return TRUE;
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

typedef struct imap_scan_tree_ imap_scan_tree;
struct imap_scan_tree_ {
    GSList *list;               /* a list of imap_scan_items */
    char delim;
};

static void imap_scan_destroy_tree(imap_scan_tree * tree);

static void*
imap_dir_cb_real(void* r)
{
    BalsaMailboxNode *n;
    GSList *list;
    BalsaMailboxNode*mb = r;
    GError *error = NULL;
    imap_scan_tree imap_tree = { NULL, '.' };

    g_return_val_if_fail(mb->server, NULL);

    libbalsa_scanner_imap_dir(mb, mb->server, mb->dir, mb->subscribed,
                              mb->list_inbox, 
                              check_imap_path,
                              mark_imap_path,
                              handle_imap_path,
                              &imap_tree,
                              &error);
    
    if(error) {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             error->code == LIBBALSA_MAILBOX_NETWORK_ERROR
                             ? _("Scanning of %s failed: %s\n"
                                 "Check network connectivity.")
                             : _("Scanning of %s failed: %s"),
                             mb->server->host,
                             error->message);
        g_error_free(error);
        imap_scan_destroy_tree(&imap_tree);
        gnome_appbar_pop(balsa_app.appbar);
        return NULL;
    }

    if (!balsa_app.mblist_tree_store) {
        /* Quitt'n time! */
        imap_scan_destroy_tree(&imap_tree);
        return NULL;
    }

    /* phase b. */

    imap_tree.list = g_slist_reverse(imap_tree.list);
    for (list = imap_tree.list; list; list = g_slist_next(list)) {
        imap_scan_item *item = list->data;
	
	n = imap_scan_create_mbnode(mb, item, imap_tree.delim);
	if (item->selectable && imap_scan_attach_mailbox(n, item))
	    balsa_mblist_mailbox_node_redraw(n);
        if(item->marked)
            libbalsa_mailbox_set_unread_messages_flag(n->mailbox, TRUE);
	g_object_unref(n);
        
    }
    imap_scan_destroy_tree(&imap_tree);

    if(balsa_app.debug && mb->name)
        printf("imap_dir_cb:  main mailbox node %s mailbox is %p\n", 
               mb->name, mb->mailbox);
    gnome_appbar_pop(balsa_app.appbar);
    if(balsa_app.debug) printf("%d: Scanning done.\n", (int)time(NULL));
    return NULL;
}

static void
imap_dir_cb(BalsaMailboxNode* mb)
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
#if defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED)
    pthread_create(&scan_th_id, NULL, imap_dir_cb_real, mb);
    pthread_detach(scan_th_id);
#else
    imap_dir_cb_real(mb);
#endif
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox * mb)
{
    BalsaMailboxNode *mbn;
    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    mbn->mailbox = mb;
    load_mailbox_view(mbn);
    g_signal_connect(G_OBJECT(mbn), "show-prop-dialog", 
		     G_CALLBACK(mailbox_conf_edit), NULL);
    if (LIBBALSA_IS_MAILBOX_MH(mb) || LIBBALSA_IS_MAILBOX_MAILDIR(mb)) {
	/* Mh and Maildir mailboxes are directories, and may be nested,
	 * so we need to be able to append a subtree. */
	mbn->name = g_strdup(libbalsa_mailbox_local_get_path(mb));
	mbn->dir = g_strdup(mbn->name);
	g_signal_connect(G_OBJECT(mbn), "append-subtree", 
                         G_CALLBACK(read_dir_cb), NULL);
    }
    return mbn;
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_dir(const gchar* dir)
{
    BalsaMailboxNode *mbn;

    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());

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
balsa_mailbox_node_new_from_config(const gchar* group)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new();
    libbalsa_conf_push_group(group);

    folder->server = LIBBALSA_SERVER(libbalsa_imap_server_new_from_config());

    if(balsa_app.debug)
	printf("Server loaded, host: %s, %s.\n", folder->server->host,
	       folder->server->use_ssl ? "SSL" : "no SSL");
    g_signal_connect(G_OBJECT(folder), "show-prop-dialog", 
		     G_CALLBACK(folder_conf_imap_node), NULL);
    g_signal_connect(G_OBJECT(folder), "append-subtree", 
		     G_CALLBACK(imap_dir_cb), NULL);
    libbalsa_server_connect_signals(folder->server,
                                    G_CALLBACK(ask_password), NULL);
    balsa_mailbox_node_load_config(folder, group);

    folder->dir = libbalsa_conf_get_string("Directory");
    folder->subscribed =
	libbalsa_conf_get_bool("Subscribed"); 
    folder->list_inbox =
	libbalsa_conf_get_bool("ListInbox=true"); 
    libbalsa_conf_pop_group();

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
    load_mailbox_view(folder);

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
    if (mn)
        g_signal_emit(G_OBJECT(mn),
                      balsa_mailbox_node_signals[SHOW_PROP_DIALOG], 0);
}

void
balsa_mailbox_node_append_subtree(BalsaMailboxNode * mn)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[APPEND_SUBTREE], 0);
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
balsa_mailbox_node_load_config(BalsaMailboxNode* mn, const gchar* group)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[LOAD_CONFIG], 0, group);
}

void
balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* group)
{
    g_signal_emit(G_OBJECT(mn),
		  balsa_mailbox_node_signals[SAVE_CONFIG], 0, group);
}

/* ---------------------------------------------------------------------
 * Rescanning.
 * --------------------------------------------------------------------- */
void
balsa_mailbox_local_append(LibBalsaMailbox* mbx)
{
    gchar *dir; 
    BalsaMailboxNode *mbnode;
    BalsaMailboxNode *parent = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mbx));

    for(dir = g_strdup(libbalsa_mailbox_local_get_path(mbx));
        strlen(dir)>1 /* i.e dir != "/" */ &&
            !(parent = balsa_find_dir(NULL, dir));
        ) {
        gchar* tmp =  g_path_get_dirname(dir);
        g_free(dir);
        dir = tmp;
    }
    mbnode = balsa_mailbox_node_new_from_mailbox(mbx);
    mbnode->parent = parent;
    balsa_mblist_mailbox_node_append(parent, mbnode);
    if(parent)
	g_object_unref(parent);
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
balsa_mailbox_node_rescan(BalsaMailboxNode * mn)
{
#if defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED)
    GNode *gnode;

    if (!balsa_app.mailbox_nodes)
        return;

    gnode = balsa_find_mbnode(balsa_app.mailbox_nodes, mn);

    if(gnode) {
	balsa_remove_children_mailbox_nodes(gnode);
	balsa_mailbox_node_append_subtree(mn, gnode);
        mn->scanned = TRUE;
    } else {
        g_warning("folder node %s (%p) not found in hierarchy.\n",
		     mn->name, mn);
    }
#else

    if (!balsa_app.mblist_tree_store)
        return;

    balsa_remove_children_mailbox_nodes(mn);
    balsa_mailbox_node_append_subtree(mn ? mn : balsa_app.root_node);

#endif  /* defined(BALSA_USE_THREADS) && defined(THREADED_IMAP_SCAN_FIXED) */
}

/* balsa_mailbox_node_scan_children:
 * checks whether a mailbox node's children need scanning. 
 * Note that rescanning local_mail_directory will *not* trigger rescanning
 * eventual IMAP servers.
 */
#define BALSA_MAILBOX_NODE_LIST_KEY "balsa-mailbox-node-list"

void
balsa_mailbox_node_scan_children(BalsaMailboxNode * mbnode)
{
    GtkTreeIter parent;
    GtkTreeModel *model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);
    GSList *list = NULL;

    if (balsa_find_iter_by_data(&parent, mbnode)) {
	gboolean valid;
	GtkTreeIter iter;

	for (valid = gtk_tree_model_iter_children(model, &iter, &parent);
	     valid; valid = gtk_tree_model_iter_next(model, &iter)) {
            BalsaMailboxNode *mn;
	    gtk_tree_model_get(model, &iter, 0, &mn, -1);
	    if (!mn->scanned) {
                list = g_slist_prepend(list, mn);
                g_object_add_weak_pointer(G_OBJECT(mn), & list->data);
            }
	    g_object_unref(mn);
        }
    } else
        g_print("balsa_mailbox_node_scan_children: didn't find mbnode.\n");

    if (list && !g_object_get_data(G_OBJECT(mbnode),
                                   BALSA_MAILBOX_NODE_LIST_KEY)) {
        BalsaMailboxNode **mn = g_new(BalsaMailboxNode *, 1);
        *mn = mbnode;
        g_object_add_weak_pointer(G_OBJECT(mbnode), (gpointer) mn);
        g_object_set_data(G_OBJECT(mbnode), BALSA_MAILBOX_NODE_LIST_KEY,
                          g_slist_reverse(list));
        g_idle_add((GSourceFunc) bmbn_scan_children_idle, mn);
    }
}

static gboolean
bmbn_scan_children_idle(BalsaMailboxNode ** mbnode)
{
    GSList *list;
    GSList *l;

    gdk_threads_enter();

    if (!*mbnode) {
        g_free(mbnode);
        gdk_threads_leave();
        return FALSE;
    }

    list = g_object_get_data(G_OBJECT(*mbnode), BALSA_MAILBOX_NODE_LIST_KEY);
    for (l = list; l; l = g_slist_next(l)) {
        BalsaMailboxNode *mn;
        gboolean has_unread_messages = FALSE;
        
        if (!l->data)
            continue;
        mn = l->data;
        if (mn->mailbox)
            has_unread_messages = mn->mailbox->has_unread_messages;
        balsa_mailbox_node_rescan(mn);
        if (!l->data)
            continue;
        if (mn->mailbox)
            mn->mailbox->has_unread_messages = has_unread_messages;
        mn->scanned = TRUE;
        g_object_remove_weak_pointer(G_OBJECT(mn), & l->data);
    }
    g_slist_free(list);

    if (*mbnode) {
        g_object_set_data(G_OBJECT(*mbnode), BALSA_MAILBOX_NODE_LIST_KEY,
                          NULL);
        g_object_remove_weak_pointer(G_OBJECT(*mbnode), (gpointer) mbnode);
    }
    g_free(mbnode);

    gdk_threads_leave();

    return FALSE;
}

/* ---------------------------------------------------------------------
 * Context menu, helpers, and callbacks.
 * --------------------------------------------------------------------- */
static void
add_menu_entry(GtkWidget * menu, const gchar * label, GtkSignalFunc cb,
	       BalsaMailboxNode * mbnode)
{
    GtkWidget *menuitem;

    menuitem = label ? gtk_menu_item_new_with_mnemonic(label)
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

static void
mb_inbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_INBOX);
}

static void
mb_sentbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_SENT);
}

static void
mb_trash_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_TRASH);
}

static void
mb_draftbox_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    config_mailbox_set_as_special(mbnode->mailbox, SPECIAL_DRAFT);
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
    add_menu_entry(submenu, _("Local _mbox mailbox..."),  
		   GTK_SIGNAL_FUNC(mailbox_conf_add_mbox_cb), NULL);
    add_menu_entry(submenu, _("Local Mail_dir mailbox..."), 
		   GTK_SIGNAL_FUNC(mailbox_conf_add_maildir_cb), NULL);
    add_menu_entry(submenu, _("Local M_H mailbox..."),
		   GTK_SIGNAL_FUNC(mailbox_conf_add_mh_cb), NULL);
    add_menu_entry(submenu, _("Remote _IMAP mailbox..."), 
		   GTK_SIGNAL_FUNC(mailbox_conf_add_imap_cb), NULL);
    add_menu_entry(submenu, NULL, NULL, mbnode);
    add_menu_entry(submenu, _("Remote IMAP _folder..."), 
		   GTK_SIGNAL_FUNC(folder_conf_add_imap_cb), NULL);
    add_menu_entry(submenu, _("Remote IMAP _subfolder..."), 
		   GTK_SIGNAL_FUNC(folder_conf_add_imap_sub_cb), NULL);
    gtk_widget_show(submenu);
    
    menuitem = gtk_menu_item_new_with_mnemonic(_("_New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    
    if(mbnode == NULL) {/* clicked on the empty space */
        add_menu_entry(menu, _("_Rescan"), GTK_SIGNAL_FUNC(mb_rescan_cb), 
                       NULL);
	return menu;
    }
    /* If we didn't click on a mailbox node then there is only one option. */
    add_menu_entry(menu, NULL, NULL, mbnode);

    if (mbnode->mailbox) {
	if (!MAILBOX_OPEN(mbnode->mailbox))
	    add_menu_entry(menu, _("_Open"),
                           G_CALLBACK(mb_open_cb), mbnode);
	else
	    add_menu_entry(menu, _("_Close"),
                           G_CALLBACK(mb_close_cb), mbnode);
    }
    if (g_signal_has_handler_pending(G_OBJECT(mbnode),
                                     balsa_mailbox_node_signals
                                     [SHOW_PROP_DIALOG], 0, FALSE))
        add_menu_entry(menu, _("_Properties..."),
                       G_CALLBACK(mb_conf_cb), mbnode);
    if (mbnode->mailbox || mbnode->config_prefix)
	add_menu_entry(menu, _("_Delete"), 
                       G_CALLBACK(mb_del_cb),  mbnode);
    if (g_signal_has_handler_pending(G_OBJECT(mbnode),
		                     balsa_mailbox_node_signals
				     [APPEND_SUBTREE], 0, FALSE))
	add_menu_entry(menu, _("_Rescan"),
		       GTK_SIGNAL_FUNC(mb_rescan_cb), mbnode);

    
    if (mbnode->mailbox) {
	add_menu_entry(menu, NULL, NULL, mbnode);
	if(LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox)) {
	    add_menu_entry(menu, _("_Subscribe"),   
                           GTK_SIGNAL_FUNC(mb_subscribe_cb),   mbnode);
	    add_menu_entry(menu, _("_Unsubscribe"), 
                           GTK_SIGNAL_FUNC(mb_unsubscribe_cb), mbnode);
	}
	
	if(mbnode->mailbox != balsa_app.inbox)
	    add_menu_entry(menu, _("Mark as _Inbox"),    
                           GTK_SIGNAL_FUNC(mb_inbox_cb),    mbnode);
	if(mbnode->mailbox != balsa_app.sentbox)
	    add_menu_entry(menu, _("_Mark as Sentbox"), 
                           GTK_SIGNAL_FUNC(mb_sentbox_cb),  mbnode);
	if(mbnode->mailbox != balsa_app.trash)
	    add_menu_entry(menu, _("Mark as _Trash"),    
                           GTK_SIGNAL_FUNC(mb_trash_cb),    mbnode);
	if(mbnode->mailbox != balsa_app.draftbox)
	    add_menu_entry(menu, _("Mark as D_raftbox"),
                           GTK_SIGNAL_FUNC(mb_draftbox_cb), mbnode);
	/* FIXME : No test on mailbox type is made yet, should we ? */
	add_menu_entry(menu, _("_Edit/Apply filters"), 
                       GTK_SIGNAL_FUNC(mb_filter_cb), mbnode);
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
*/
static BalsaMailboxNode *
remove_mailbox_from_nodes(LibBalsaMailbox* mailbox)
{
    BalsaMailboxNode *mbnode;

    mbnode = balsa_find_mailbox(mailbox);
    if (mbnode)
	balsa_mblist_mailbox_node_remove(mbnode);
    else
	mbnode = balsa_mailbox_node_new_from_mailbox(mailbox);
    return mbnode;
}
    
static BalsaMailboxNode * 
remove_special_mailbox_by_url(const gchar* url, LibBalsaMailbox *** special)
{
    LibBalsaMailbox **mailbox;

    if (balsa_app.trash && strcmp(url, balsa_app.trash->url) == 0)
	mailbox = &balsa_app.trash;
    else if (balsa_app.inbox && strcmp(url, balsa_app.inbox->url) == 0)
	mailbox = &balsa_app.inbox;
    else if (balsa_app.outbox && strcmp(url, balsa_app.outbox->url) == 0)
	mailbox = &balsa_app.outbox;
    else if (balsa_app.sentbox && strcmp(url, balsa_app.sentbox->url) == 0)
	mailbox = &balsa_app.sentbox;
    else if (balsa_app.draftbox && strcmp(url, balsa_app.draftbox->url) == 0)
	mailbox = &balsa_app.draftbox;
    else
        mailbox = NULL;

    if (special)
        *special = mailbox;
	
    return mailbox ? remove_mailbox_from_nodes(*mailbox) : NULL;
}

static BalsaMailboxNode *
add_local_mailbox(BalsaMailboxNode *root, const gchar * name,
		  const gchar * path, GType type)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;
    gchar* url;

    if(root == NULL) return NULL;
    url = g_strconcat("file://", path, NULL);
    
    mbnode = remove_special_mailbox_by_url(url, NULL);
    if (!mbnode) {
	if ( type == LIBBALSA_TYPE_MAILBOX_MH ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mh_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mbox_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	    mailbox =
		LIBBALSA_MAILBOX(libbalsa_mailbox_maildir_new(path, FALSE));
	} else {
	    /* type is not a valid local mailbox type. */
	    balsa_information(LIBBALSA_INFORMATION_DEBUG,
			      _("The path \"%s\" does not lead to a mailbox."),
			      path);
	    mailbox = NULL;
	}
	if(!mailbox) {/* local mailbox could not be created; privileges? */
	    printf("Not accessible mailbox %s\n", path);
	    return NULL;
	}
	mailbox->name = g_strdup(name);
	
	mbnode = balsa_mailbox_node_new_from_mailbox(mailbox);
	
	if (balsa_app.debug)
	    g_print(_("Local mailbox %s loaded as: %s\n"),
		    mailbox->name,
		    g_type_name(G_OBJECT_TYPE(mailbox)));
	if (balsa_app.check_mail_upon_startup)
	    libbalsa_mailbox_check(mailbox);
    }
    g_free(url);
    /* no type checking, parent is NULL for root */
    mbnode->parent = root;
    load_mailbox_view(mbnode);
    balsa_mblist_mailbox_node_append(root, mbnode);
    return mbnode;
}

static BalsaMailboxNode *
add_local_folder(BalsaMailboxNode * root, const char *d_name,
		 const char *path)
{
    BalsaMailboxNode *mbnode;

    if (!root)
	return NULL;

    mbnode = balsa_mailbox_node_new_from_dir(path);
    mbnode->parent = root;
    balsa_mblist_mailbox_node_append(root, mbnode);
    if (balsa_app.debug)
	g_print(_("Local folder %s\n"), path);

    return mbnode;
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
 * Returns a node for the path isi->fn.
 * Finds the node if it exists, and creates one if it doesn't.
 * If this is a special mailbox and should replace a place-holder, we
 * save a pointer to its address in isi.
 * Caller must unref mbnode.
 */
static BalsaMailboxNode *
imap_scan_create_mbnode(BalsaMailboxNode * root, imap_scan_item * isi,
			char delim)
{
    gchar *parent_name;
    BalsaMailboxNode *mbnode;
    BalsaMailboxNode *parent;
    const gchar *basename;
    gchar *url = libbalsa_imap_url(root->server, isi->fn);
    LibBalsaMailbox *mailbox = NULL;

    mbnode = balsa_find_url(url);
    if (mbnode) {
	/* A mailbox with this url is already in the tree... */
	BalsaMailboxNode *special =
	    remove_special_mailbox_by_url(url, &isi->special);
	if (!special) {
	    /* ...and it's not special, so we'll return this mbnode. */
	    g_free(url);
	    return mbnode;
	}
	g_object_unref(mbnode);
	mailbox = special->mailbox;
	g_object_ref(mailbox);
	g_object_unref(special);
    }
    g_free(url);

    parent_name = get_parent_folder_name(isi->fn, delim);
    parent = balsa_find_dir(root->server, parent_name);
    if (parent == NULL) {
        parent = root;
	g_object_ref(parent);
    }
    g_free(parent_name);

    mbnode = balsa_mailbox_node_new_imap_node(root->server, isi->fn);
    mbnode->mailbox = mailbox;
    basename = strrchr(isi->fn, delim);
    if (!basename)
        basename = isi->fn;
    else
        basename++;
    mbnode->name = g_strdup(basename);
    mbnode->parent = parent;
    mbnode->subscribed = parent->subscribed;
    mbnode->scanned = isi->scanned;

    g_object_ref(mbnode);
    balsa_mblist_mailbox_node_append(mbnode->parent, mbnode);
    g_object_unref(parent);
    
    return mbnode;
}

/* handle_imap_path:
   add given mailbox unless its base name begins on dot.
*/
static void
add_imap_entry(const char *fn, char delim, gboolean noscan,
	       gboolean selectable, gboolean marked, void *data)
{
    imap_scan_tree *tree = (imap_scan_tree *) data;
    imap_scan_item *item = g_new0(imap_scan_item, 1);
    item->fn = g_strdup(fn);
    item->scanned = noscan;
    item->selectable = selectable;
    item->marked = marked;

    tree->list = g_slist_prepend(tree->list, item);
    if(delim)  /* some servers may set delim to NIL for some mailboxes */
        tree->delim = delim;
}

static void
imap_scan_destroy_tree(imap_scan_tree * tree)
{
    GSList *list;

    for (list = tree->list; list; list = g_slist_next(list)) {
        imap_scan_item *item = list->data;

        g_free(item->fn);
        g_free(item);
    }

    g_slist_free(tree->list);
}

static void
handle_imap_path(const char *fn, char delim, int noselect, int noscan,
		 int marked, void *data)
{
    if (!noselect) {
	const gchar *basename = strrchr(fn, delim);
	if (basename && *++basename == '.' && delim != '.')
	    /* ignore mailboxes that begin with a dot */
	    return;
    }
    if (balsa_app.debug)
	printf("handle_imap_path: Adding mailbox of path \"%s\" "
	       "delim `%c' noselect %d noscan %d\n",
	       fn, delim, noselect, noscan);
    add_imap_entry(fn, delim, noscan, !noselect, marked, data);
}

/*
 * check_imap_path: Check an imap path to see whether we need to scan it.
 * 
 * server:      LibBalsaServer for the folders;
 * fn:          folder name;
 * depth:       depth of the current scan;
 *
 * returns TRUE if the path must be scanned.
 */

static gint
check_imap_path(const gchar *fn, LibBalsaServer * server, guint depth)
{
    CheckPathInfo cpi;

    if (depth < balsa_app.imap_scan_depth)
        return TRUE;

    if (!libbalsa_mailbox_view_table)
        return FALSE;

    cpi.url = libbalsa_imap_url(server, fn);
    cpi.must_scan = FALSE;
    g_hash_table_foreach(libbalsa_mailbox_view_table,
                         (GHFunc) check_url_func, &cpi);
    if(balsa_app.debug) 
	printf("check_imap_path: path \"%s\" must_scan %d.\n",
               cpi.url, cpi.must_scan);
    g_free(cpi.url);

    return cpi.must_scan;
}

/* mark_imap_path:
 *
 * find the imap_scan_item for fn and set flags.
 *
 * noselect may be -1, 0, or 1; -1 means no change, 0 or 1
 * mean set accordingly.
 *
 * noscan is always set
 */
static void
mark_imap_path(const gchar * fn, gint noselect, gint noscan, gpointer data)
{
    imap_scan_tree *tree = data;
    GSList *list;

    if(balsa_app.debug) 
	printf("mark_imap_path: find path \"%s\".\n", fn);
    for (list = tree->list; list; list = list->next) {
        imap_scan_item *item = list->data;
        if (!strcmp(item->fn, fn)) {
            if (noselect >= 0)
        	item->selectable = !noselect;
            item->scanned = noscan;
            if (balsa_app.debug)
        	printf(" noselect %d, noscan %d.\n", noselect, noscan);
            break;
        }
    }
    if (!list && balsa_app.debug)
	printf(" not found.\n");
}
