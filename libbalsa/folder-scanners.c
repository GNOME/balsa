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

#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <limits.h>

#include "libbalsa.h"
#include "folder-scanners.h"
#include "libimap.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap-server.h"

#ifndef PATH_MAX
#define PATH_MAX _POSIX_PATH_MAX
#endif

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
    DIR *dpc;
    struct dirent *de;
    char filename[PATH_MAX];
    struct stat st;
    gpointer parent_node = NULL;

    if (!check_local_path(prefix, *depth))
        return;

    mark_local_path(rnode);

    dpc = opendir(prefix);
    if (!dpc)
        return;

    /*
     * if we don't find any subdirectories inside, we'll go
     * and ignore this one too...
     */
    while ((de = readdir(dpc)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        snprintf(filename, PATH_MAX, "%s/%s", prefix, de->d_name);
        /* ignore file if it can't be read. */
        if (stat(filename, &st) == -1 || access(filename, R_OK) == -1)
            continue;

        if (S_ISDIR(st.st_mode)) {
            /*
             * if we think that this looks like a mailbox, include it as such.
             * otherwise we'll lose the mail in this folder
             */
            GType foo = libbalsa_mailbox_type_from_path(filename);
            if ((foo == LIBBALSA_TYPE_MAILBOX_MH) ||
                (foo == LIBBALSA_TYPE_MAILBOX_MAILDIR)) {
                parent_node =
		    mailbox_handler(rnode, de->d_name, filename, foo);
                ++*depth;
                libbalsa_scanner_mdir(parent_node, filename,
                                      check_local_path, mark_local_path,
                                      folder_handler, mailbox_handler,
                                      depth);
                --*depth;
            }
        }
        /* ignore regular files */
    }
    closedir(dpc);
}

static void
libbalsa_scanner_local_dir_helper(gpointer rnode, const gchar * prefix,
                                  LocalCheck check_local_path,
                                  LocalMark mark_local_path,
                                  LocalHandler folder_handler,
                                  LocalHandler mailbox_handler,
                                  guint * depth)
{
    DIR *dpc;
    struct dirent *de;
    char filename[PATH_MAX];
    struct stat st;
    GType mailbox_type;
    gpointer current_node;

    if (!check_local_path(prefix, *depth))
        return;

    mark_local_path(rnode);

    dpc = opendir(prefix);
    if (!dpc)
        return;

    while ((de = readdir(dpc)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        snprintf(filename, PATH_MAX, "%s/%s", prefix, de->d_name);

        /* ignore file if it can't be read. */
        if (stat(filename, &st) == -1 || access(filename, R_OK) == -1)
            continue;

        if (S_ISDIR(st.st_mode)) {
	    local_scanner_helper helper;
            mailbox_type = libbalsa_mailbox_type_from_path(filename);

            if ((mailbox_type == LIBBALSA_TYPE_MAILBOX_MH) ||
                (mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR)) {
                current_node =
                    mailbox_handler(rnode, de->d_name, filename, mailbox_type);
		helper = libbalsa_scanner_mdir;
            } else {
                gchar *name = g_path_get_basename(prefix);
                current_node = folder_handler(rnode, name, filename, 0);
                g_free(name);
		helper = libbalsa_scanner_local_dir_helper;
            }

            ++*depth;
            helper(current_node, filename, check_local_path,
                   mark_local_path, folder_handler, mailbox_handler,
                   depth);
            --*depth;
        } else {
            mailbox_type = libbalsa_mailbox_type_from_path(filename);
            if (mailbox_type != 0) {
                mark_local_path(mailbox_handler
                                (rnode, de->d_name, filename,
                                 mailbox_type));
            }
        }
    }
    closedir(dpc);
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
  gboolean subscribed;
  int delim;
  void* cb_data;       /* data passed to {mailbox,folder}_handlers */
};

static void
libbalsa_imap_list_cb(ImapMboxHandle * handle, int delim,
                      ImapMboxFlags * flags, char *folder,
                      struct browser_state *state)
{
    gboolean noselect, marked;
    gboolean noscan;
    gchar *f;

    g_return_if_fail(folder && *folder);

    if(delim) state->delim = delim;

    f = folder[strlen(folder) - 1] == delim ?
        g_strdup(folder) : g_strdup_printf("%s%c", folder, delim);

    /* RFC 3501 states that the flags in the LIST response are "more
     * authoritative" than those in the LSUB response, except that a
     * \noselect flag in the LSUB response means that the mailbox isn't
     * subscribed, so we should respect it. */
    noselect = (IMAP_MBOX_HAS_FLAG(*flags, IMLIST_NOSELECT) != 0);
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
    noscan = (IMAP_MBOX_HAS_FLAG(*flags, IMLIST_NOINFERIORS)
              || IMAP_MBOX_HAS_FLAG(*flags, IMLIST_HASNOCHILDREN))
        && !IMAP_MBOX_HAS_FLAG(*flags, IMLIST_HASCHILDREN);
    if (noscan) {
        g_hash_table_remove(state->subfolders, f);
        g_free(f);
    } else
        g_hash_table_insert(state->subfolders, f, NULL);

    marked = IMAP_MBOX_HAS_FLAG(*flags, IMLIST_MARKED);
    state->handle_imap_path(folder, delim, noselect, noscan, marked,
                            state->cb_data);
}

static void
libbalsa_imap_lsub_cb(ImapMboxHandle * handle, int delim,
                      ImapMboxFlags * flags, char *folder,
                      struct browser_state *state)
{
    gchar *f;

    g_return_if_fail(folder && *folder);

    if(delim) state->delim = delim;

    f = folder[strlen(folder) - 1] == delim ?
        g_strdup(folder) : g_strdup_printf("%s%c", folder, delim);

    g_hash_table_insert(state->subfolders, f, GINT_TO_POINTER(*flags));
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
    gchar *imap_path;
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
	    imap_path = g_strdup_printf("%s%c", path, state->delim);
	else
	    imap_path = g_strdup(path);
    } else 
        imap_path = g_strdup(path);

    /* Send LSUB command if we're in subscribed mode, to find paths.
     * Note that flags in the LSUB response aren't authoritative
     * (UW-Imap is the only server thought to give incorrect flags). */
    if (state->subscribed) {
        rc = imap_mbox_lsub(handle, imap_path);
        if (rc != IMR_OK) {
            g_free(imap_path);
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
        rc = imap_mbox_list(handle, imap_path);
    g_free(imap_path);
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

    g_list_foreach(list, (GFunc) g_free, NULL);
    g_list_free(list);
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
        g_signal_connect(G_OBJECT(handle), "list-response",
                         G_CALLBACK(libbalsa_imap_list_cb),
                         (gpointer) & state);
    lsub_handler_id = subscribed ?
        g_signal_connect(G_OBJECT(handle), "lsub-response",
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

    g_signal_handler_disconnect(G_OBJECT(handle), list_handler_id);
    if (lsub_handler_id)
        g_signal_handler_disconnect(G_OBJECT(handle), lsub_handler_id);
    libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), handle);
}
