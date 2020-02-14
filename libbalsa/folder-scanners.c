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
#include "folder-scanners.h"

#include <string.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "libbalsa.h"
#include "libimap.h"
#include "server.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap-server.h"

typedef void (*local_scanner_helper) (gpointer rnode,
                                      const gchar * prefix,
                                      LocalCheck check_local_path,
                                      LocalMark mark_local_path,
                                      LocalHandler folder_handler,
                                      LocalHandler mailbox_handler,
                                      guint * depth);
static void
libbalsa_scanner_mdir(gpointer rnode, const gchar * prefix,
                      LocalCheck check_local_path,
                      LocalMark mark_local_path,
                      LocalHandler folder_handler,
                      LocalHandler mailbox_handler, guint * depth)
{
    GDir *dpc;
    GError *error = NULL;
    const gchar *entry;

    if (!check_local_path(prefix, *depth)
        || !mark_local_path(rnode))
        return;

    dpc = g_dir_open(prefix, 0U, &error);
    if (dpc == NULL) {
    	g_warning("error reading Maildir folder %s: %s", prefix, (error != NULL) ? error->message : "unknown");
    	g_clear_error(&error);
        return;
    }

    /*
     * if we don't find any subdirectories inside, we'll go
     * and ignore this one too...
     */
    while ((entry = g_dir_read_name(dpc)) != NULL) {
    	if (entry[0] != '.') {
    		gchar *filename;

    		filename = g_build_filename(prefix, entry, NULL);

    		/* ignore regular file, or if it can't be read. */
    		if (g_file_test(filename, G_FILE_TEST_IS_DIR) && (g_access(filename, R_OK) == 0)) {
    			/*
    			 * if we think that this looks like a mailbox, include it as such.
    			 * otherwise we'll lose the mail in this folder
    			 */
    			GType foo = libbalsa_mailbox_type_from_path(filename);
    			if ((foo == LIBBALSA_TYPE_MAILBOX_MH) ||
    				(foo == LIBBALSA_TYPE_MAILBOX_MAILDIR)) {
    				gpointer parent_node;

    				parent_node = mailbox_handler(rnode, entry, filename, foo);
    				++*depth;
    				libbalsa_scanner_mdir(parent_node, filename, check_local_path, mark_local_path, folder_handler,
    					mailbox_handler, depth);
    				--*depth;
    			}
    		}
    		g_free(filename);
    	}
    }
    g_dir_close(dpc);
}

static void
libbalsa_scanner_local_dir_helper(gpointer rnode, const gchar * prefix,
                                  LocalCheck check_local_path,
                                  LocalMark mark_local_path,
                                  LocalHandler folder_handler,
                                  LocalHandler mailbox_handler,
                                  guint * depth)
{
    GDir *dpc;
    GError *error = NULL;
    const gchar *entry;

    if (!check_local_path(prefix, *depth)
        || !mark_local_path(rnode))
        return;


    dpc = g_dir_open(prefix, 0U, &error);
    if (dpc == NULL) {
    	g_warning("error reading mail folder %s: %s", prefix, (error != NULL) ? error->message : "unknown");
    	g_clear_error(&error);
        return;
    }

    while ((entry = g_dir_read_name(dpc)) != NULL) {
    	if (entry[0] != '.') {
    		gchar *filename;

    		filename = g_build_filename(prefix, entry, NULL);

    		/* ignore file if it can't be read. */
    		if (g_access(filename, R_OK) == 0) {
    		    GType mailbox_type;

    			if (g_file_test(filename, G_FILE_TEST_IS_DIR)) {
    				local_scanner_helper helper;
    			    gpointer current_node;

    				mailbox_type = libbalsa_mailbox_type_from_path(filename);

    				if ((mailbox_type == LIBBALSA_TYPE_MAILBOX_MH) ||
    					(mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR)) {
    					current_node = mailbox_handler(rnode, entry, filename, mailbox_type);
    					helper = libbalsa_scanner_mdir;
    				} else {
    					gchar *name = g_path_get_basename(prefix);

    					current_node = folder_handler(rnode, name, filename, 0);
    					g_free(name);
    					helper = libbalsa_scanner_local_dir_helper;
    				}

    				++*depth;
    				helper(current_node, filename, check_local_path, mark_local_path, folder_handler, mailbox_handler, depth);
    				--*depth;
    			} else {
    				mailbox_type = libbalsa_mailbox_type_from_path(filename);
    				if (mailbox_type != G_TYPE_OBJECT) {
    					mark_local_path(mailbox_handler(rnode, entry, filename, mailbox_type));
    				}
    			}
    		}
    		g_free(filename);
    	}
    }
    g_dir_close(dpc);
}

void
libbalsa_scanner_local_dir(gpointer rnode, const gchar * prefix,
                           LocalCheck check_local_path,
                           LocalMark mark_local_path,
                           LocalHandler folder_handler,
                           LocalHandler mailbox_handler,
                           GType mailbox_type)
{
    guint depth = 0;
    local_scanner_helper helper =
        (mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR
         || mailbox_type == LIBBALSA_TYPE_MAILBOX_MH) ?
        libbalsa_scanner_mdir : libbalsa_scanner_local_dir_helper;

    helper(rnode, prefix,
           check_local_path,
           mark_local_path, folder_handler, mailbox_handler, &depth);
}

/* ---------------------------------------------------------------------
 * IMAP folder scanner functions 
 * --------------------------------------------------------------------- */
struct browser_state
{
  ImapHandler* handle_imap_path;
  ImapMark* mark_imap_path;
  GHashTable* subfolders;
  gchar *imap_path;
  gboolean subscribed;
  int delim;
  void* cb_data;       /* data passed to {mailbox,folder}_handlers */
};

static void
libbalsa_imap_list_cb(ImapMboxHandle * handle, int delim,
                      ImapMboxFlags flags, char *folder,
                      struct browser_state *state)
{
    gboolean noselect, marked;
    gboolean noscan;
    gchar *f;

    g_return_if_fail(folder && *folder);

    if(delim) state->delim = delim;

    f = folder[strlen(folder) - 1] == delim ?
        g_strdup(folder) : g_strdup_printf("%s%c", folder, delim);

    if(strcmp(f, state->imap_path) == 0) {
        g_free(f);
        return;
    }
    /* RFC 3501 states that the flags in the LIST response are "more
     * authoritative" than those in the LSUB response, except that a
     * \noselect flag in the LSUB response means that the mailbox isn't
     * subscribed, so we should respect it. */
    noselect = (IMAP_MBOX_HAS_FLAG(flags, IMLIST_NOSELECT) != 0);
    if (state->subscribed) {
        gpointer tmp;
        ImapMboxFlags lsub_flags;

        if (!g_hash_table_lookup_extended
            (state->subfolders, f, NULL, &tmp)) {
            /* This folder wasn't listed in the LSUB responses. */
            g_free(f);
            return;
        }
        lsub_flags = GPOINTER_TO_INT(tmp);
        noselect = (IMAP_MBOX_HAS_FLAG(lsub_flags, IMLIST_NOSELECT) != 0);
    }

    /* These flags are different, but both mean that we don't need to
     * scan the folder: */
    noscan = (IMAP_MBOX_HAS_FLAG(flags, IMLIST_NOINFERIORS)
              || IMAP_MBOX_HAS_FLAG(flags, IMLIST_HASNOCHILDREN))
        && !IMAP_MBOX_HAS_FLAG(flags, IMLIST_HASCHILDREN);
    if (noscan) {
        g_hash_table_remove(state->subfolders, f);
        g_free(f);
    } else
        g_hash_table_insert(state->subfolders, f, NULL);

    marked = IMAP_MBOX_HAS_FLAG(flags, IMLIST_MARKED);
    state->handle_imap_path(folder, delim, noselect, noscan, marked,
                            state->cb_data);
}

static void
libbalsa_imap_lsub_cb(ImapMboxHandle * handle, int delim,
                      ImapMboxFlags flags, char *folder,
                      struct browser_state *state)
{
    gchar *f;

    g_return_if_fail(folder && *folder);

    if(delim) state->delim = delim;

    f = folder[strlen(folder) - 1] == delim ?
        g_strdup(folder) : g_strdup_printf("%s%c", folder, delim);

    g_hash_table_insert(state->subfolders, f, GINT_TO_POINTER(flags));
}

/* executed with GDK lock OFF.
 * see HACKING file for proper locking order description.
 */
static gboolean
steal_key(gpointer key, gpointer value, GList **res)
{
    *res = g_list_prepend(*res, key);
    return TRUE;
}
static GList*
steal_keys_to_list(GHashTable *h)
{
    GList *res = NULL;
    g_hash_table_foreach_steal(h, (GHRFunc)steal_key, &res);
    return res;
}
/* libbalsa_imap_browse: recursive helper.
 *
 * path:                the imap path to be browsed;
 * browser_state:       browsing info;
 * handle:              imap server handle
 * server:              the LibBalsa server for the tree;
 * check_imap_path:     a callback for finding out whether a path must
 *                      be scanned;
 * depth:               depth of the recursion.
 */
static gboolean
libbalsa_imap_browse(const gchar * path, struct browser_state *state,
                     ImapMboxHandle* handle, LibBalsaServer * server,
                     ImapCheck check_imap_path, guint * depth,
                     GError **error)
{
    GList *list, *el;
    gboolean browse;
    ImapResponse rc = IMR_OK;
    gboolean ret = TRUE;
    gint i;
    gchar *tmp;
    
    if(*path) {
	if(!state->delim) {
            g_signal_handlers_block_by_func(handle, libbalsa_imap_list_cb,
                                            state);
	    state->delim = imap_mbox_handle_get_delim(handle, path);
            g_signal_handlers_unblock_by_func(handle, libbalsa_imap_list_cb,
                                              state);
        }
	if(path[strlen(path) - 1] != state->delim)
	    state->imap_path = g_strdup_printf("%s%c", path, state->delim);
	else
	    state->imap_path = g_strdup(path);
    } else 
        state->imap_path = g_strdup(path);

    /* Send LSUB command if we're in subscribed mode, to find paths.
     * Note that flags in the LSUB response aren't authoritative
     * (UW-Imap is the only server thought to give incorrect flags). */
    if (state->subscribed) {
        rc = imap_mbox_lsub(handle, state->imap_path);
        if (rc != IMR_OK) {
            g_free(state->imap_path);
            g_set_error(error,
                        LIBBALSA_SCANNER_ERROR,
                        LIBBALSA_SCANNER_ERROR_IMAP,
                        "%d", rc);
            return FALSE;
        }
    }

    /* Send LIST command if either:
     * - we're not in subscribed mode; or
     * - we are, and the LSUB command gave any responses, to get
     *   authoritative flags.
     */
    if (!state->subscribed || g_hash_table_size(state->subfolders) > 0)
        rc = imap_mbox_list(handle, state->imap_path);
    g_free(state->imap_path);
    if(rc != IMR_OK) {
        g_set_error(error,
                    LIBBALSA_SCANNER_ERROR,
                    LIBBALSA_SCANNER_ERROR_IMAP,
                    "%d", rc);
        return FALSE;
    }
    /* Mark this path as scanned, without changing its selectable state. */
    i = strlen(path) - 1;
    tmp = g_strndup(path, i >= 0 && path[i] == state->delim ? i : i + 1);
    state->mark_imap_path(tmp, state->cb_data);
    g_free(tmp);

    list = steal_keys_to_list(state->subfolders);

    ++*depth;
    for (el = list, browse = FALSE; el && !browse; el = el->next) {
        const gchar *fn = el->data;
        i = strlen(fn) - 1;
        tmp = g_strndup(fn, i >= 0 && fn[i] == state->delim ? i : i + 1);
        browse = check_imap_path(tmp, server, *depth);
        g_free(tmp);
    }

    if (browse)
        for (el = list; el; el = el->next) {
            ret = libbalsa_imap_browse(el->data, state, handle, server,
                                       check_imap_path, depth, error);
            if(!ret)
                break;
        }

    --*depth;

    g_list_free_full(list, g_free);
    return ret;
}

void
libbalsa_scanner_imap_dir(gpointer rnode, LibBalsaServer * server, 
                          const gchar* path, int delim,
                          gboolean subscribed, gboolean list_inbox,
                          ImapCheck check_imap_path,
                          ImapMark mark_imap_path,
                          ImapHandler handle_imap_path,
                          gpointer cb_data,
                          GError **error)
{
    struct browser_state state;
    guint i;
    ImapMboxHandle* handle;
    gulong list_handler_id, lsub_handler_id;

    if (!LIBBALSA_IS_IMAP_SERVER(server))
            return;
    handle = 
        libbalsa_imap_server_get_handle(LIBBALSA_IMAP_SERVER(server), error);
    if (!handle) { /* FIXME: propagate error here and translate message */
        if(!*error)
            g_set_error(error,
                        LIBBALSA_SCANNER_ERROR,
                        LIBBALSA_SCANNER_ERROR_IMAP,
                        "Could not connect to the server");
        return;
    }

    state.handle_imap_path = handle_imap_path;
    state.mark_imap_path   = mark_imap_path;
    state.cb_data          = cb_data;
    state.subscribed       = subscribed;
    state.delim            = delim;
    state.subfolders = g_hash_table_new_full(g_str_hash, g_str_equal,
 					     g_free, NULL);

    list_handler_id =
        g_signal_connect(handle, "list-response",
                         G_CALLBACK(libbalsa_imap_list_cb),
                         (gpointer) & state);
    lsub_handler_id = subscribed ?
        g_signal_connect(handle, "lsub-response",
                         G_CALLBACK(libbalsa_imap_lsub_cb),
                         (gpointer) & state) : 0;

    if (list_inbox) {
        /* force INBOX into the mailbox list
         * delim doesn't matter, so we'll give it '/'
         * and we'll mark it as scanned, because the only reason for
         * using this option is to pickup an INBOX that isn't in the
         * tree specified by the prefix */
        handle_imap_path("INBOX", '/', FALSE, TRUE, FALSE, cb_data);
    }

    i = 0;
    libbalsa_imap_browse(path, &state, handle, server,
                         check_imap_path, &i, error);
    g_hash_table_destroy(state.subfolders);

    g_signal_handler_disconnect(handle, list_handler_id);
    if (lsub_handler_id)
        g_signal_handler_disconnect(handle, lsub_handler_id);
    libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), handle);
}


/* ---------------------------------------------------------------------
 * IMAP folder tree functions (parent selection, subscription management)
 * --------------------------------------------------------------------- */
typedef struct {
	gchar delim;
	gboolean subscribed;
	GHashTable *children;
} folder_data_t;

typedef struct {
	GtkTreeStore *store;
	GtkTreeIter *parent;
} imap_tree_t;

typedef struct {
	ImapMboxHandle *handle;
	gboolean subscriptions;
} scan_data_t;

static void imap_tree_scan(scan_data_t *scan_data,
						   const gchar *list_path,
						   GHashTable  *folders);
static void imap_tree_to_store(gchar         *folder,
							   folder_data_t *folder_data,
							   imap_tree_t   *scan_data);

GtkTreeStore *
libbalsa_scanner_imap_tree(LibBalsaServer  *server,
						   gboolean			subscriptions,
						   GError         **error)
{
	scan_data_t scan_data;
	imap_tree_t imap_store = { NULL, NULL };

	g_return_val_if_fail(LIBBALSA_IS_IMAP_SERVER(server) && (error != NULL), NULL);

	scan_data.handle = libbalsa_imap_server_get_handle(LIBBALSA_IMAP_SERVER(server), error);
	if (scan_data.handle != NULL) {
		GHashTable *folders;

		scan_data.subscriptions = subscriptions;

		/* scan the whole IMAP server tree */
		folders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		imap_tree_scan(&scan_data, "", folders);
		libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), scan_data.handle);

		/* create the resulting tree store */
		imap_store.store = gtk_tree_store_new(LB_SCANNER_IMAP_N_COLS,
			G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, PANGO_TYPE_STYLE);
		g_hash_table_foreach(folders, (GHFunc) imap_tree_to_store, &imap_store);
		g_hash_table_unref(folders);
	} else {
		if (*error == NULL) {
			g_set_error(error, LIBBALSA_SCANNER_ERROR, LIBBALSA_SCANNER_ERROR_IMAP, _("Could not connect to “%s”"), libbalsa_server_get_host(server));
		}
	}

	return imap_store.store;
}


static void
imap_tree_scan_list_cb(ImapMboxHandle *handle,
					   int             delim,
					   ImapMboxFlags   flags,
					   gchar          *folder,
					   GHashTable     *folders)
{
	folder_data_t *folder_data;

	folder_data = g_new0(folder_data_t, 1UL);
	folder_data->delim = delim;
	if (IMAP_MBOX_HAS_FLAG(flags, IMLIST_HASCHILDREN) != 0U) {
		folder_data->children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	g_hash_table_insert(folders, g_strdup(folder), folder_data);
}


static void
imap_tree_scan_lsub_cb(ImapMboxHandle *handle,
					   int             delim,
					   ImapMboxFlags   flags,
					   gchar          *folder,
					   GHashTable     *folders)
{
	folder_data_t *folder_data;

	folder_data = g_hash_table_lookup(folders, folder);
	if (folder_data != NULL) {
		folder_data->subscribed = IMAP_MBOX_HAS_FLAG(flags, IMLIST_NOSELECT) == 0U;
	} else {
		g_critical("%s: cannot identify folder %s", __func__, folder);
	}
}


static void
imap_tree_scan_children(gchar         *folder_name,
						folder_data_t *folder_data,
						scan_data_t   *scan_data)
{
	if (folder_data->children != NULL) {
		gchar *scan_name = g_strdup_printf("%s%c", folder_name, folder_data->delim);

		imap_tree_scan(scan_data, scan_name, folder_data->children);
		g_free(scan_name);
	}
}


static void
imap_tree_scan(scan_data_t *scan_data,
			   const gchar *list_path,
			   GHashTable  *folders)
{
	gulong list_handler_id;

	list_handler_id = g_signal_connect(scan_data->handle, "list-response", G_CALLBACK(imap_tree_scan_list_cb), folders);
	imap_mbox_list(scan_data->handle, list_path);
	g_signal_handler_disconnect(scan_data->handle, list_handler_id);

	if (scan_data->subscriptions) {
		list_handler_id =
			g_signal_connect(scan_data->handle, "lsub-response", G_CALLBACK(imap_tree_scan_lsub_cb), folders);
		imap_mbox_lsub(scan_data->handle, list_path);
		g_signal_handler_disconnect(scan_data->handle, list_handler_id);
	}

	g_hash_table_foreach(folders, (GHFunc) imap_tree_scan_children, scan_data);
}


static void
imap_tree_to_store(gchar 		 *folder,
				   folder_data_t *folder_data,
				   imap_tree_t   *tree_data)
{
	GtkTreeIter iter;
	gchar *disp_name;

	gtk_tree_store_append(tree_data->store, &iter, tree_data->parent);
	if (tree_data->parent != NULL) {
		gchar *parent_path;

		gtk_tree_model_get(GTK_TREE_MODEL(tree_data->store), tree_data->parent, LB_SCANNER_IMAP_PATH, &parent_path, -1);
		disp_name = g_strdup(&folder[strlen(parent_path) + 1UL]);
		g_free(parent_path);
	} else {
		disp_name = g_strdup(folder);
	}
	gtk_tree_store_set(tree_data->store, &iter,
		LB_SCANNER_IMAP_FOLDER, disp_name,
		LB_SCANNER_IMAP_PATH, folder,
		LB_SCANNER_IMAP_SUBS_NEW, folder_data->subscribed,
		LB_SCANNER_IMAP_SUBS_OLD, folder_data->subscribed,
		LB_SCANNER_IMAP_STYLE, PANGO_STYLE_NORMAL, -1);
	g_free(disp_name);

	if (folder_data->children != NULL) {
		imap_tree_t child_data;

		child_data.store = tree_data->store;
		child_data.parent = &iter;
		g_hash_table_foreach(folder_data->children, (GHFunc) imap_tree_to_store, &child_data);
		g_hash_table_unref(folder_data->children);
	}
}
