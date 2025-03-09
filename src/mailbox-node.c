/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "mailbox-node.h"

#include <unistd.h>
#include <string.h>

#include "balsa-app.h"
#include "folder-scanners.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "save-restore.h"
#include "filter.h"
#include "imap-server.h"
#include <glib/gi18n.h>
#include "libbalsa-conf.h"

/* MailboxNode object is a GUI representation of a mailbox, or entire 
   set of them. It can read itself from the configuration, save its data,
   and provide a dialog box for the properties edition.
   Folders can additionally scan associated directory or IMAP server to
   retrieve their tree of mailboxes.
*/

typedef struct imap_scan_item_ imap_scan_item;
struct imap_scan_item_ {
    gchar *fn;
    LibBalsaMailbox **special;
    unsigned scanned:1, selectable:1, marked:1;
};

static void balsa_mailbox_node_dispose(GObject * object);
static void balsa_mailbox_node_finalize(GObject * object);

static void balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn,
						const gchar * group);
static void balsa_mailbox_node_real_load_config(BalsaMailboxNode* mn,
						const gchar * group);

static BalsaMailboxNode *imap_scan_create_mbnode(BalsaMailboxNode * root,
						 imap_scan_item * isi,
						 int delim);
static gboolean imap_scan_attach_mailbox(BalsaMailboxNode * mbnode,
                                         imap_scan_item * isi);
static gboolean bmbn_scan_children_idle(BalsaMailboxNode ** mn);
static gboolean restore_children_from_cache(BalsaMailboxNode *mn);

static BalsaMailboxNode *add_local_mailbox(BalsaMailboxNode * root,
					   const gchar * name,
					   const gchar * path,
					   GType type);
static BalsaMailboxNode *add_local_folder(BalsaMailboxNode * root,
					  const char *d_name,
					  const char *fn);

static void handle_imap_path(const char *fn, int delim, int noselect,
			     int noscan, int marked, void *data);
static gint check_imap_path(const char *fn, LibBalsaServer * server,
			    guint depth);
static void mark_imap_path(const gchar * fn, gpointer data);

enum {
    SHOW_PROP_DIALOG,
    APPEND_SUBTREE,
    LAST_SIGNAL
};

static guint balsa_mailbox_node_signals[LAST_SIGNAL];

struct _BalsaMailboxNode {
    GObject object;
    BalsaMailboxNode *parent; /* NULL for root-level folders & mailboxes */
    LibBalsaMailbox *mailbox; /* != NULL for leaves only */
    gchar *name;       /* used for folders, i.e. when mailbox == NULL */
    time_t last_use;   /* for closing least recently used mailboxes */
    BalsaMailboxNodeStyle style;
    /* folder data */
    gchar* config_prefix;
    gchar* dir;
    LibBalsaServer * server; /* Used only by remote; is referenced */
    int delim; /* IMAP delimiter so that we do not need to check it
		 * too often. */

    unsigned subscribed:1;     /* Used only by remote */
    unsigned list_inbox:1;     /* Used only by remote */
    unsigned scanned:1;        /* IMAP flag */
};

G_DEFINE_TYPE(BalsaMailboxNode, balsa_mailbox_node, G_TYPE_OBJECT)

static void
balsa_mailbox_node_class_init(BalsaMailboxNodeClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    balsa_mailbox_node_signals[SHOW_PROP_DIALOG] =
	g_signal_new("show-prop-dialog",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
		     G_TYPE_NONE, 0);
    balsa_mailbox_node_signals[APPEND_SUBTREE] =
	g_signal_new("append-subtree",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     0, NULL, NULL, NULL,
		     G_TYPE_NONE, 0);

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
    LibBalsaMailbox *mailbox = mn->mailbox;

    if (mailbox != NULL) {
        if (!LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
            libbalsa_mailbox_set_open(mailbox, libbalsa_mailbox_is_open(mailbox));
            config_save_mailbox_view(libbalsa_mailbox_get_url(mailbox),
                                     libbalsa_mailbox_get_view(mailbox));
            if (balsa_app.main_window != NULL)
                balsa_window_close_mbnode(balsa_app.main_window, mn);
        }
	g_object_unref(mailbox);
	mn->mailbox = NULL;
    }

    G_OBJECT_CLASS(balsa_mailbox_node_parent_class)->dispose(object);
}

static void
balsa_mailbox_node_finalize(GObject * object)
{
    BalsaMailboxNode *mn;

    mn = BALSA_MAILBOX_NODE(object);

    balsa_mailbox_node_clear_children_cache(mn);
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

    G_OBJECT_CLASS(balsa_mailbox_node_parent_class)->finalize(object);
}

static void
balsa_mailboxes_append_children(GtkTreeModel *model, GtkTreeIter *parent,
                                GPtrArray *children_names)
{
    GtkTreeIter child;

    if (!gtk_tree_model_iter_children(model, &child, parent))
	return;

    do {
        BalsaMailboxNode *mbnode;
        gchar *str;

        gtk_tree_model_get(model, &child, 0, &mbnode, -1);
        if (mbnode->mailbox != NULL) {
            const char *path =
                libbalsa_mailbox_imap_get_path
                (LIBBALSA_MAILBOX_IMAP(mbnode->mailbox));

            str = g_strconcat("PXS", path, NULL);
        } else {
            str = g_strconcat("PXD", mbnode->dir, NULL);
        }
        str[1] = mbnode->delim;
        g_ptr_array_add(children_names, str);

        g_object_unref(mbnode);

        balsa_mailboxes_append_children(model, &child, children_names);
    } while (gtk_tree_model_iter_next(model, &child));
}

static void
balsa_mailbox_node_real_save_config(BalsaMailboxNode* mn, const gchar * group)
{
    GtkTreeIter iter;

    if(mn->name != NULL)
	g_debug("Saving mailbox node %s with group %s", mn->name, group);
    libbalsa_imap_server_save_config(LIBBALSA_IMAP_SERVER(mn->server));
    libbalsa_conf_set_string("Name",      mn->name);
    libbalsa_conf_set_string("Directory", mn->dir);
    libbalsa_conf_set_bool("Subscribed",  mn->subscribed);
    libbalsa_conf_set_bool("ListInbox",   mn->list_inbox);

    if (balsa_find_iter_by_data(&iter, mn)) {
        GPtrArray *children_names;
        GtkTreeModel *model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);

        children_names = g_ptr_array_new_with_free_func(g_free);
        balsa_mailboxes_append_children(model, &iter,
                                        children_names);
        g_debug("Saving %d children", children_names->len);
        libbalsa_conf_set_vector("Children", children_names->len,
                                 (const char*const*)(children_names->pdata));
        g_ptr_array_free(children_names, TRUE);
    }

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

static gboolean
check_url_func(const gchar * group, const gchar * encoded_url,
               CheckPathInfo * cpi)
{
    gchar *url;

    url = libbalsa_urldecode(encoded_url);

    if (g_str_has_prefix(url, cpi->url)
        && (config_mailbox_was_exposed(url)
            || (balsa_app.remember_open_mboxes
                && config_mailbox_was_open(url)))
        )
        cpi->must_scan = TRUE;

    g_free(url);

    /* stop checking if we already know we must scan deeper */
    return cpi->must_scan;
}

static gboolean
check_local_path(const gchar * path, guint depth)
{
    size_t len;
    CheckPathInfo cpi;

    if (depth < balsa_app.local_scan_depth)
        return TRUE;

    len = strlen(balsa_app.local_mail_directory);
    if (libbalsa_path_is_below_dir(path, balsa_app.local_mail_directory)
	&& !strchr(&path[++len], G_DIR_SEPARATOR))
	/* Top level folder. */
	return TRUE;

    cpi.url = g_strconcat("file://", path, NULL);
    cpi.must_scan = FALSE;
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                (LibBalsaConfForeachFunc) check_url_func,
                                &cpi);
    g_debug("check_local_path: path “%s” must_scan %d.", cpi.url, cpi.must_scan);
    g_free(cpi.url);

    return cpi.must_scan;
}

static gboolean
mark_local_path(BalsaMailboxNode *mbnode)
{
    if (mbnode->scanned)
        return FALSE;
    mbnode->scanned = TRUE;
    return TRUE;
}

/** Read local directory in search for mailboxes. Never does any
    caching - it is not worth it. */
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

    if (!libbalsa_mailbox_get_view(mailbox))
        libbalsa_mailbox_set_view(mailbox,
                                  config_load_mailbox_view
                                  (libbalsa_mailbox_get_url(mailbox)));
}

static gboolean
imap_scan_attach_mailbox(BalsaMailboxNode * mbnode, imap_scan_item * isi)
{
    LibBalsaMailbox *mailbox;
    LibBalsaMailboxImap *m;

    /* If the mailbox was added from the config file, it is already
     * connected to "show-prop-dialog". */
    if (!g_signal_has_handler_pending(mbnode,
                                      balsa_mailbox_node_signals
                                      [SHOW_PROP_DIALOG], 0, FALSE))
	g_signal_connect(mbnode, "show-prop-dialog",
                         G_CALLBACK(folder_conf_imap_sub_node), NULL);
    if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
        /* it already has a mailbox */
        return FALSE;

    mailbox = libbalsa_mailbox_imap_new();
    m = LIBBALSA_MAILBOX_IMAP(mailbox);
    libbalsa_mailbox_remote_set_server(LIBBALSA_MAILBOX_REMOTE(m),
				       mbnode->server);
    libbalsa_mailbox_imap_set_path(m, isi->fn);
    g_debug("imap_scan_attach_mailbox: add mbox of name %s (full path %s)", isi->fn,
            libbalsa_mailbox_get_url(mailbox));
    libbalsa_mailbox_set_name(mailbox, mbnode->name);
    g_free(mbnode->name);
    mbnode->name = NULL;
    mbnode->mailbox = mailbox;
    load_mailbox_view(mbnode);
    if (isi->special) {
	if (*isi->special)
	    g_object_remove_weak_pointer(G_OBJECT(*isi->special),
					 (gpointer *) isi->special);
        *isi->special = mailbox;
	g_object_add_weak_pointer(G_OBJECT(m), (gpointer *) isi->special);
        if (isi->special == &balsa_app.outbox)
            libbalsa_mailbox_set_no_reassemble(mailbox, TRUE);
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
    int delim;
};

static void imap_scan_destroy_tree(imap_scan_tree * tree);

static void
imap_dir_cb(BalsaMailboxNode* mb)
{
    gchar* msg;
    BalsaMailboxNode* mroot=mb;
    BalsaMailboxNode *n;
    GSList *list;
    GError *error = NULL;
    imap_scan_tree imap_tree = { NULL, '.' };
    GtkStatusbar *statusbar;
    guint context_id;

    if (!balsa_app.main_window)
        return;

    statusbar = balsa_window_get_statusbar(balsa_app.main_window);
    context_id = gtk_statusbar_get_context_id(statusbar, "MailboxNode imap");

    if(restore_children_from_cache(mb))
        return;

    while(mroot->parent)
	mroot = mroot->parent;
    msg = g_strdup_printf(_("Scanning %s. Please wait…"), mroot->name);
    gtk_statusbar_push(statusbar, context_id, msg);
    g_free(msg);

    g_return_if_fail(mb->server);
    libbalsa_scanner_imap_dir(mb, mb->server, mb->dir, mb->delim,
			      mb->subscribed, mb->list_inbox, 
                              check_imap_path,
                              mark_imap_path,
                              handle_imap_path,
                              &imap_tree,
                              &error);
    
    if(error) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             error->code == LIBBALSA_MAILBOX_NETWORK_ERROR
                             ? _("Scanning of %s failed: %s\n"
                                 "Check network connectivity.")
                             : _("Scanning of %s failed: %s"),
                             libbalsa_server_get_host(mb->server),
                             error->message);
        g_error_free(error);
        imap_scan_destroy_tree(&imap_tree);
        gtk_statusbar_pop(statusbar, context_id);
        return;
    }

    if (!balsa_app.mblist_tree_store) {
        /* Quitt'n time! */
        imap_scan_destroy_tree(&imap_tree);
        return;
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
    }
    imap_scan_destroy_tree(&imap_tree);

    if (mb->name) {
    	g_debug("imap_dir_cb: main mailbox node %s mailbox is %p", mb->name, mb->mailbox);
    }
    g_debug("%d: Scanning done.", (int)time(NULL));
    gtk_statusbar_pop(statusbar, context_id);

    /* We can save the cache now... */
    config_folder_update(mroot);
}

BalsaMailboxNode *
balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox * mb)
{
    BalsaMailboxNode *mbn;
    mbn = BALSA_MAILBOX_NODE(balsa_mailbox_node_new());
    mbn->mailbox = mb;
    load_mailbox_view(mbn);
    g_signal_connect(mbn, "show-prop-dialog", 
		     G_CALLBACK(mailbox_conf_edit), NULL);
    if (LIBBALSA_IS_MAILBOX_MH(mb) || LIBBALSA_IS_MAILBOX_MAILDIR(mb)) {
	/* Mh and Maildir mailboxes are directories, and may be nested,
	 * so we need to be able to append a subtree. */
	mbn->name =
            g_strdup(libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mb));
	mbn->dir = g_strdup(mbn->name);
	g_signal_connect(mbn, "append-subtree", 
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
    g_signal_connect(mbn, "show-prop-dialog", 
		     G_CALLBACK(dir_conf_edit), NULL);
    g_signal_connect(mbn, "append-subtree", 
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
    gboolean def;
    gint n_children;
    gchar **children;
    BalsaMailboxNode * folder = balsa_mailbox_node_new();

    libbalsa_conf_push_group(group);

    folder->server = LIBBALSA_SERVER(libbalsa_imap_server_new_from_config());

	g_debug("Server loaded, host: %s, security %d.", libbalsa_server_get_host(folder->server),
		libbalsa_server_get_security(folder->server));
    g_signal_connect_swapped(folder->server, "config-changed", 
                             G_CALLBACK(config_folder_update), folder);
    g_signal_connect(folder, "show-prop-dialog", 
		     G_CALLBACK(folder_conf_imap_node), NULL);
    g_signal_connect(folder, "append-subtree", 
		     G_CALLBACK(imap_dir_cb), NULL);
    libbalsa_server_connect_signals(folder->server,
                                    G_CALLBACK(ask_password), NULL);
    balsa_mailbox_node_load_config(folder, group);

    folder->dir = libbalsa_conf_get_string("Directory");
    if (folder->dir == NULL) {
    	folder->dir = g_strdup("");
    }
    folder->subscribed =
	libbalsa_conf_get_bool("Subscribed"); 
    folder->list_inbox =
	libbalsa_conf_get_bool("ListInbox=true"); 

    libbalsa_conf_get_vector_with_default("Children",&n_children,
                                          &children,&def);
    if(!def) {
        g_object_set_data(G_OBJECT(folder), "children-cache", children);
    }

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
    g_signal_connect(folder, "append-subtree", 
		     G_CALLBACK(imap_dir_cb), NULL);

    return folder;
}

BalsaMailboxNode*
balsa_mailbox_node_new_imap_folder(LibBalsaServer* s, const char*p)
{
    BalsaMailboxNode * folder = balsa_mailbox_node_new_imap_node(s, p);
    g_assert(s);

    g_signal_connect(folder, "show-prop-dialog", 
		     G_CALLBACK(folder_conf_imap_node), NULL);
    return folder;
}

void
balsa_mailbox_node_show_prop_dialog(BalsaMailboxNode* mn)
{
    if (mn != NULL)
        g_signal_emit(mn, balsa_mailbox_node_signals[SHOW_PROP_DIALOG], 0);
}

void
balsa_mailbox_node_append_subtree(BalsaMailboxNode * mn)
{
    if (!mn->scanned) {
        g_signal_emit(mn, balsa_mailbox_node_signals[APPEND_SUBTREE], 0);
        mn->scanned = TRUE;
    }
}

/* balsa_mailbox_node_load_config:
   load general configurtion: name ordering, threading option etc
   with some sane defaults.
*/
void
balsa_mailbox_node_load_config(BalsaMailboxNode* mn, const gchar* group)
{
    balsa_mailbox_node_real_load_config(mn, group);
}

void
balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* group)
{
    balsa_mailbox_node_real_save_config(mn, group);
}

/* ---------------------------------------------------------------------
 * Rescanning.
 * --------------------------------------------------------------------- */
static BalsaMailboxNode *
find_dir(const gchar * dir)
{
    if (strcmp(dir, balsa_app.local_mail_directory) == 0)
        return g_object_ref(balsa_app.root_node);
    return balsa_find_dir(NULL, dir);
}

void
balsa_mailbox_local_append(LibBalsaMailbox* mbx)
{
    gchar *dir;
    BalsaMailboxNode *mbnode;
    BalsaMailboxNode *parent = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mbx));

    dir = g_strdup(libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mbx));
    while (dir[1] /* i.e. dir != "/" */
           && !(parent = find_dir(dir))) {
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
    if (!balsa_app.mblist_tree_store)
        return;

    if (mn) {
        balsa_mailbox_node_clear_children_cache(mn);
        balsa_remove_children_mailbox_nodes(mn);
    } else {
        balsa_remove_children_mailbox_nodes(mn);
        mn = balsa_app.root_node;
    }
    mn->scanned = FALSE;
    balsa_mailbox_node_append_subtree(mn);
}

void
balsa_mailbox_node_clear_children_cache(BalsaMailboxNode *mn)
{
    gchar **children_cache = g_object_get_data(G_OBJECT(mn), "children-cache");

    if(children_cache) {
        g_strfreev(children_cache);
        g_object_set_data(G_OBJECT(mn), "children-cache", NULL);
    }
}

static gboolean
restore_children_from_cache(BalsaMailboxNode *mn)
{
    gint i;
    gchar **children = g_object_get_data(G_OBJECT(mn), "children-cache");
    imap_scan_item isi;

    if(!children) {
        if (mn->name) {
        	g_debug("No cache for %s - quitting.", mn->name);
        }
        return FALSE;
    }

    for(i=0; children[i]; i++) {
        if(children[i][0] == 'P' && strlen(children[i])>3) {
            BalsaMailboxNode *n;
            isi.fn = children[i]+3;
            isi.special = NULL;
            isi.scanned = TRUE;
            isi.selectable = children[i][2] == 'S';
            isi.marked = FALSE;
            n = imap_scan_create_mbnode(mn, &isi, children[i][1]);
            if (isi.selectable && imap_scan_attach_mailbox(n, &isi))
                balsa_mblist_mailbox_node_redraw(n);
        }
    }
    
    return TRUE;
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
        g_warning("balsa_mailbox_node_scan_children: didn't find mbnode.");

    if (list && !g_object_get_data(G_OBJECT(mbnode),
                                   BALSA_MAILBOX_NODE_LIST_KEY)) {
        BalsaMailboxNode **mn = g_new(BalsaMailboxNode *, 1);
        *mn = mbnode;
        g_object_add_weak_pointer(G_OBJECT(mbnode), (gpointer *) mn);
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

    if (!*mbnode) {
        g_free(mbnode);
        return FALSE;
    }

    list = g_object_get_data(G_OBJECT(*mbnode), BALSA_MAILBOX_NODE_LIST_KEY);
    for (l = list; l; l = g_slist_next(l)) {
        BalsaMailboxNode *mn;
        
        if (!l->data)
            continue;
        mn = l->data;
        if (!mn->scanned) {
            gboolean has_unread_messages = FALSE;

            if (mn->mailbox)
                has_unread_messages =
                    libbalsa_mailbox_get_has_unread_messages(mn->mailbox);
            balsa_mailbox_node_rescan(mn);
            if (!l->data)
                continue;
            if (mn->mailbox)
                libbalsa_mailbox_set_has_unread_messages
                    (mn->mailbox, has_unread_messages);
            mn->scanned = TRUE;
        } else {
            g_debug("%s: %s “%s” was already scanned", __func__,
                    mn->mailbox ? "mailbox" : "folder",
                    mn->mailbox ? libbalsa_mailbox_get_name(mn->mailbox) : mn->name);
        }
        g_object_remove_weak_pointer(G_OBJECT(mn), & l->data);
    }
    g_slist_free(list);

    if (*mbnode) {
        g_object_set_data(G_OBJECT(*mbnode), BALSA_MAILBOX_NODE_LIST_KEY,
                          NULL);
        g_object_remove_weak_pointer(G_OBJECT(*mbnode), (gpointer *) mbnode);
    }
    g_free(mbnode);

    return FALSE;
}

/* ---------------------------------------------------------------------
 * Context menu, helpers, and callbacks.
 * --------------------------------------------------------------------- */
static void
add_menu_entry(GtkWidget * menu, const gchar * label, GCallback cb,
	       BalsaMailboxNode * mbnode)
{
    GtkWidget *menuitem;

    menuitem = label ? gtk_menu_item_new_with_mnemonic(label)
	: gtk_separator_menu_item_new();

    if (cb)
	g_signal_connect(menuitem, "activate",
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
mb_rescan_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    gchar *current_mailbox_url;
    GPtrArray *url_array;

    current_mailbox_url = g_strdup(balsa_app.current_mailbox_url);
    balsa_mailbox_node_rescan(mbnode);

    /* Reopen mailboxes */
    url_array = g_ptr_array_new();
    if (current_mailbox_url)
        g_ptr_array_add(url_array, current_mailbox_url);
    balsa_add_open_mailbox_urls(url_array);
    g_ptr_array_add(url_array, NULL);
    balsa_open_mailbox_list((gchar **) g_ptr_array_free(url_array, FALSE));
}

static void
mb_filter_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    if (mbnode->mailbox)
        filters_run_dialog(mbnode->mailbox,
                           GTK_WINDOW(balsa_app.main_window));
    else
	/* FIXME : Perhaps should we be able to apply filters on
	   folders (ie recurse on all mailboxes in it), but there are
	   problems of infinite recursion (when one mailbox being
	   filtered is also the destination of the filter action (eg a
	   copy)). So let's see that later :) */
    	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, _("You can apply filters only on mailbox"));
}

static void
mb_empty_trash_cb(GtkWidget * widget, BalsaMailboxNode * mbnode)
{
    empty_trash(balsa_app.main_window);
}

gboolean
balsa_mailbox_node_is_imap(const BalsaMailboxNode *mbnode)
{
	return (mbnode != NULL) &&
		(mbnode->server != NULL) &&
		(strcmp(libbalsa_server_get_protocol(mbnode->server), "imap") == 0);
}

GtkWidget *
balsa_mailbox_node_get_context_menu(BalsaMailboxNode * mbnode)
{
    GtkWidget *menu;
    GtkWidget *submenu;
    GtkWidget *menuitem;
    LibBalsaMailbox *mailbox;
    gboolean special;

    /*  g_return_val_if_fail(mailbox != NULL, NULL); */

    menu = gtk_menu_new();
    /* it's a single-use menu, so we must destroy it when we're done */
    g_signal_connect(menu, "selection-done",
                     G_CALLBACK(gtk_widget_destroy), NULL);

    submenu = gtk_menu_new();
    add_menu_entry(submenu, _("Local _mbox mailbox…"),  
		   G_CALLBACK(mailbox_conf_add_mbox_cb), NULL);
    add_menu_entry(submenu, _("Local Mail_dir mailbox…"), 
		   G_CALLBACK(mailbox_conf_add_maildir_cb), NULL);
    add_menu_entry(submenu, _("Local M_H mailbox…"),
		   G_CALLBACK(mailbox_conf_add_mh_cb), NULL);
    add_menu_entry(submenu, NULL, NULL, mbnode);
    add_menu_entry(submenu, _("Remote IMAP _folder…"), 
		   G_CALLBACK(folder_conf_add_imap_cb), NULL);
    if (balsa_mailbox_node_is_imap(mbnode)) {
    	add_menu_entry(submenu, _("Remote IMAP _subfolder…"),
		   G_CALLBACK(folder_conf_add_imap_sub_cb), mbnode);
    }
    gtk_widget_show(submenu);
    
    /* Translators: popup menu item "New" mailbox or folder */
    menuitem = gtk_menu_item_new_with_mnemonic(C_("mailbox", "_New"));
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_widget_show(menuitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    
    if(mbnode == NULL) {/* clicked on the empty space */
        add_menu_entry(menu, _("_Rescan"), G_CALLBACK(mb_rescan_cb), 
                       NULL);
	return menu;
    }
    /* If we didn't click on a mailbox node then there is only one option. */
    add_menu_entry(menu, NULL, NULL, NULL);

    if (g_signal_has_handler_pending(mbnode,
                                     balsa_mailbox_node_signals
                                     [SHOW_PROP_DIALOG], 0, FALSE))
        add_menu_entry(menu, _("_Properties…"),
                       G_CALLBACK(mb_conf_cb), mbnode);

    if (g_signal_has_handler_pending(mbnode,
		                     balsa_mailbox_node_signals
				     [APPEND_SUBTREE], 0, FALSE))
	add_menu_entry(menu, _("_Rescan"),
		       G_CALLBACK(mb_rescan_cb), mbnode);

    if (mbnode->config_prefix)
	add_menu_entry(menu, _("_Delete"), G_CALLBACK(mb_del_cb),  mbnode);

    if (!(mailbox = mbnode->mailbox))
        return menu;

    if (!MAILBOX_OPEN(mailbox))
        add_menu_entry(menu, _("_Open"),  G_CALLBACK(mb_open_cb),  mbnode);
    else
        add_menu_entry(menu, _("_Close"), G_CALLBACK(mb_close_cb), mbnode);

    special = (   mailbox == balsa_app.inbox
               || mailbox == balsa_app.sentbox
               || mailbox == balsa_app.draftbox
               || mailbox == balsa_app.outbox
               || mailbox == balsa_app.trash);
    if (!special && !mbnode->config_prefix)
	add_menu_entry(menu, _("_Delete"), G_CALLBACK(mb_del_cb),  mbnode);

    if (!special) {
        add_menu_entry(menu, NULL, NULL, NULL);
        add_menu_entry(menu, _("Mark as _Inbox"),    
                       G_CALLBACK(mb_inbox_cb),    mbnode);
        add_menu_entry(menu, _("_Mark as Sentbox"), 
                       G_CALLBACK(mb_sentbox_cb),  mbnode);
        add_menu_entry(menu, _("Mark as _Trash"),    
                       G_CALLBACK(mb_trash_cb),    mbnode);
        add_menu_entry(menu, _("Mark as D_raftbox"),
                       G_CALLBACK(mb_draftbox_cb), mbnode);
    } else if (mailbox == balsa_app.trash)
        add_menu_entry(menu, _("_Empty trash"),    
                       G_CALLBACK(mb_empty_trash_cb), mbnode);

    /* FIXME : No test on mailbox type is made yet, should we ? */
    add_menu_entry(menu, NULL, NULL, NULL);
    add_menu_entry(menu, _("_Edit/Apply filters"), 
                   G_CALLBACK(mb_filter_cb), mbnode);

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

    if (g_strcmp0(url, libbalsa_mailbox_get_url(balsa_app.trash)) == 0)
	mailbox = &balsa_app.trash;
    else if (g_strcmp0(url, libbalsa_mailbox_get_url(balsa_app.inbox)) == 0)
	mailbox = &balsa_app.inbox;
    else if (g_strcmp0(url, libbalsa_mailbox_get_url(balsa_app.outbox)) == 0)
	mailbox = &balsa_app.outbox;
    else if (g_strcmp0(url, libbalsa_mailbox_get_url(balsa_app.sentbox)) == 0)
	mailbox = &balsa_app.sentbox;
    else if (g_strcmp0(url, libbalsa_mailbox_get_url(balsa_app.draftbox)) == 0)
	mailbox = &balsa_app.draftbox;
    else
        mailbox = NULL;

    if (special)
        *special = mailbox;

    return mailbox ? remove_mailbox_from_nodes(*mailbox) : NULL;
}

static gboolean
mailbox_check_idle(LibBalsaMailbox * mailbox)
{
    libbalsa_mailbox_check(mailbox);
    g_object_unref(mailbox);
    return FALSE;
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
	    mailbox = libbalsa_mailbox_mh_new(path, FALSE);
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    mailbox = libbalsa_mailbox_mbox_new(path, FALSE);
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	    mailbox = libbalsa_mailbox_maildir_new(path, FALSE);
	} else {
	    /* type is not a valid local mailbox type. */
	    balsa_information(LIBBALSA_INFORMATION_DEBUG,
			      _("The path “%s” does not lead to a mailbox."),
			      path);
	    mailbox = NULL;
	}
	if(!mailbox) {/* local mailbox could not be created; privileges? */
		g_warning("Not accessible mailbox %s", path);
	    return NULL;
	}
	libbalsa_mailbox_set_name(mailbox, name);
	
	mbnode = balsa_mailbox_node_new_from_mailbox(mailbox);
	
	    g_debug("Local mailbox %s loaded as: %s",
		    libbalsa_mailbox_get_name(mailbox),
		    g_type_name(G_OBJECT_TYPE(mailbox)));
        if (balsa_app.check_mail_upon_startup
            && libbalsa_mailbox_get_subscribe(mailbox) !=
            LB_MAILBOX_SUBSCRIBE_NO) {
            g_object_ref(mailbox);
            g_idle_add((GSourceFunc) mailbox_check_idle, mailbox);
        }
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
	g_debug("Local folder %s", path);

    return mbnode;
}


/* ---------------------------------------------------------------------
 * IMAP folder scanner functions 
 * --------------------------------------------------------------------- */
static gchar*
get_parent_folder_name(const gchar* path, gint delim)
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
			int delim)
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
	LibBalsaMailboxView *view;
	BalsaMailboxNode *special =
	    remove_special_mailbox_by_url(url, &isi->special);
	if (special) {
            mailbox = special->mailbox;
            g_object_ref(mailbox);
            g_object_unref(special);
	} else {
            balsa_mblist_mailbox_node_remove(mbnode);
        }
        /* Unreffing mbnode may finalize it, which would push the
         * mailbox view to the config; to save the open state, we
         * retrieve the view from the config, and restore it after
         * unreffing. */
        view = config_load_mailbox_view(url);
	g_object_unref(mbnode);
        config_save_mailbox_view(url, view);
        libbalsa_mailbox_view_free(view);
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
    mbnode->delim = delim;
    mbnode->parent = parent;
    mbnode->subscribed = parent->subscribed;
    mbnode->scanned = isi->scanned;

    balsa_mblist_mailbox_node_append(mbnode->parent, mbnode);
    g_object_unref(parent);
    
    return mbnode;
}

/* handle_imap_path:
   add given mailbox unless its base name begins on dot.
*/
static void
add_imap_entry(const char *fn, int delim, gboolean noscan,
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
handle_imap_path(const char *fn, int delim, int noselect, int noscan,
		 int marked, void *data)
{
    if (!noselect) {
	const gchar *basename = strrchr(fn, delim);
	if (basename && *++basename == '.' && delim != '.')
	    /* ignore mailboxes that begin with a dot */
	    return;
    }
    g_debug("handle_imap_path: Adding mailbox of path “%s” delim `%c' noselect %d noscan %d",
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

    cpi.url = libbalsa_imap_url(server, fn);
    cpi.must_scan = FALSE;
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                (LibBalsaConfForeachFunc) check_url_func,
                                &cpi);
    g_debug("check_imap_path: path “%s” must_scan %d.", cpi.url, cpi.must_scan);
    g_free(cpi.url);

    return cpi.must_scan;
}

/* mark_imap_path:
 *
 * find the imap_scan_item for fn and set scanned.
 */
static void
mark_imap_path(const gchar * fn, gpointer data)
{
    imap_scan_tree *tree = data;
    GSList *list;

    g_debug("mark_imap_path: find path “%s”.", fn);
    for (list = tree->list; list; list = list->next) {
        imap_scan_item *item = list->data;
        if (!strcmp(item->fn, fn)) {
            item->scanned = TRUE;
            break;
        }
    }
    if (!list) {
    	g_debug("… not found.");
    }
}

/*
 * Setters
 */

void
balsa_mailbox_node_set_dir(BalsaMailboxNode * mbnode, const gchar * dir)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    g_free(mbnode->dir);
    mbnode->dir = g_strdup(dir);
}

void
balsa_mailbox_node_set_name(BalsaMailboxNode * mbnode, const gchar * name)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    g_free(mbnode->name);
    mbnode->name = g_strdup(name);
}

void
balsa_mailbox_node_set_config_prefix(BalsaMailboxNode * mbnode, const gchar * config_prefix)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    g_free(mbnode->config_prefix);
    mbnode->config_prefix = g_strdup(config_prefix);
}

void
balsa_mailbox_node_set_last_use_time(BalsaMailboxNode * mbnode)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    time(&mbnode->last_use);
}

void
balsa_mailbox_node_change_style(BalsaMailboxNode * mbnode,
                                BalsaMailboxNodeStyle set,
                                BalsaMailboxNodeStyle clear)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    mbnode->style |= set;
    mbnode->style &= ~clear;
}

void
balsa_mailbox_node_set_list_inbox(BalsaMailboxNode * mbnode, guint list_inbox)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    mbnode->list_inbox = !!list_inbox;
}

void
balsa_mailbox_node_set_subscribed(BalsaMailboxNode * mbnode, guint subscribed)
{
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(mbnode));

    mbnode->subscribed = !!subscribed;
}

/*
 * Getters
 */

LibBalsaMailbox *
balsa_mailbox_node_get_mailbox(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->mailbox;
}

const gchar *
balsa_mailbox_node_get_name(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->name;
}

const gchar *
balsa_mailbox_node_get_config_prefix(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->config_prefix;
}

time_t
balsa_mailbox_node_get_last_use_time(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), 0);

    return mbnode->last_use;
}

LibBalsaServer *
balsa_mailbox_node_get_server(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->server;
}

BalsaMailboxNodeStyle
balsa_mailbox_node_get_style(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), 0);

    return mbnode->style;
}

guint
balsa_mailbox_node_get_subscribed(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), 0);

    return mbnode->subscribed;
}

guint
balsa_mailbox_node_get_list_inbox(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), 0);

    return mbnode->list_inbox;
}

gint
balsa_mailbox_node_get_delim(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), 0);

    return mbnode->delim;
}

const gchar *
balsa_mailbox_node_get_dir(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->dir;
}

BalsaMailboxNode *
balsa_mailbox_node_get_parent(BalsaMailboxNode * mbnode)
{
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), NULL);

    return mbnode->parent;
}
