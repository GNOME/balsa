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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>
#include <gtk/gtk.h>

#include "libbalsa.h"
#include "folder-scanners.h"
#include "imap.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap-server.h"

static void
libbalsa_scanner_mdir(GNode *rnode,
	       		const gchar * prefix, 
			LocalHandler folder_handler, 
			LocalHandler mailbox_handler)
{
    DIR *dpc;
    struct dirent *de;
    char filename[PATH_MAX];
    struct stat st;
    GNode* parent_node = NULL;

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
	    if( (foo == LIBBALSA_TYPE_MAILBOX_MH) ||
		(foo == LIBBALSA_TYPE_MAILBOX_MAILDIR ) ) {
		parent_node = mailbox_handler(rnode, de->d_name, filename);
		libbalsa_scanner_mdir(parent_node, filename, 
				      folder_handler, mailbox_handler);
	    }
	} 
	/* ignore regular files */
    }
    closedir(dpc);
}

void
libbalsa_scanner_local_dir(GNode *rnode, const gchar * prefix, 
			   LocalHandler folder_handler, 
			   LocalHandler mailbox_handler)
{
    DIR *dpc;
    struct dirent *de;
    char filename[PATH_MAX];
    struct stat st;
    GtkType mailbox_type;
    GNode* current_node;

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
	    mailbox_type = libbalsa_mailbox_type_from_path(filename);

	    if ( (mailbox_type == LIBBALSA_TYPE_MAILBOX_MH) ||
		 (mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR) ) {
		current_node = mailbox_handler(rnode, de->d_name, filename);
		libbalsa_scanner_mdir(current_node, filename, 
				        folder_handler, mailbox_handler);
	    } else {
                gchar *name = g_path_get_basename(prefix);

		current_node = folder_handler(rnode, name, filename);
		libbalsa_scanner_local_dir(current_node, filename, 
					   folder_handler, mailbox_handler);
                g_free(name);
	    }
	} else {
	    mailbox_type = libbalsa_mailbox_type_from_path(filename);
	    if (mailbox_type != 0)
		mailbox_handler(rnode, de->d_name, filename);
	}
    }
    closedir(dpc);
}

/* ---------------------------------------------------------------------
 * IMAP folder scanner functions 
 * --------------------------------------------------------------------- */
struct browser_state
{
  ImapHandler* mailbox_handler;
  ImapHandler* folder_handler;
  ImapMark* mark_imap_path;
  GList* subfolders;
  gboolean subscribed;
  int delim;
  void* cb_data;       /* data passed to {mailbox,folder}_handlers */
};

static void
libbalsa_imap_add_folder (ImapMboxHandle* handle,
                          int delim, ImapMboxFlags *flags, char *folder,
                          struct browser_state *state)
{
    int isFolder = 0;
    int isMailbox = 0;

    g_return_if_fail(folder && *folder);
    if (folder[strlen(folder)-1] == delim)
	return;

    state->delim = delim;
    if(!IMAP_MBOX_HAS_FLAG(*flags,IMLIST_NOSELECT)) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                             "ADDING MAILBOX %s\n", folder);
	++isMailbox;
    }

    /* this extra check is needed for subscribed folder handling. 
     * Read RFC when in doubt. */
    if(!g_list_find_custom(state->subfolders, folder,
			   (GCompareFunc)strcmp) && 
       !IMAP_MBOX_HAS_FLAG(*flags,IMLIST_NOINFERIORS) &&
       !IMAP_MBOX_HAS_FLAG(*flags,IMLIST_HASNOCHILDREN)) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                             "ADDING FOLDER  %s %x\n", folder, *flags);
	    
        state->subfolders = g_list_append(state->subfolders,
                                          g_strdup(folder));
	++isFolder;
    }

    if (isMailbox)
	state->mailbox_handler(folder, delim, state->cb_data);
    else if (isFolder)
	state->folder_handler(folder, delim, state->cb_data);

    if (IMAP_MBOX_HAS_FLAG(*flags,IMLIST_NOINFERIORS)||
        IMAP_MBOX_HAS_FLAG(*flags,IMLIST_HASNOCHILDREN))
        state->mark_imap_path(folder, state->cb_data);
}

/* executed with GDK lock OFF.
 * see HACKING file for proper locking order description.
 */

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
static void
libbalsa_imap_browse(const gchar * path, struct browser_state *state,
		     ImapMboxHandle* handle, LibBalsaServer * server,
		     ImapCheck check_imap_path, guint * depth)
{
    gchar *imap_path;
    GList *list, *el;
    gboolean browse;

    state->subfolders = NULL;

    if(*path && path[strlen(path) - 1] != state->delim)
        imap_path = g_strdup_printf("%s%c", path, state->delim);
    else 
        imap_path = g_strdup(path);

    if (state->subscribed) 
	imap_mbox_lsub(handle, imap_path);
    else
	imap_mbox_list(handle, imap_path);
    g_free(imap_path);

    list = state->subfolders;
    state->subfolders = NULL;

    ++*depth;
    browse = FALSE;
    for (el = list; el && !browse; el = g_list_next(el))
        browse = check_imap_path(server, el->data, *depth);

    if (browse)
        for (el = list; el; el = g_list_next(el))
            libbalsa_imap_browse(el->data, state, handle, server,
				 check_imap_path, depth);

    --*depth;

    g_list_foreach(list, (GFunc) g_free, NULL);
    g_list_free(list);
}

void
libbalsa_scanner_imap_dir(GNode *rnode, LibBalsaServer * server, 
			  const gchar* path, gboolean subscribed, 
                          gboolean list_inbox,
                          ImapCheck check_imap_path,
                          ImapMark mark_imap_path,
			  ImapHandler folder_handler, 
			  ImapHandler mailbox_handler,
			  gpointer cb_data)
{
    struct browser_state state;
    int i;
    ImapMboxHandle* handle;
    gulong handler_id;

    if (!LIBBALSA_IS_IMAP_SERVER(server))
	    return;
    handle = libbalsa_imap_server_get_handle(LIBBALSA_IMAP_SERVER(server));
    if (!handle)
	return;

    state.mailbox_handler = mailbox_handler;
    state.folder_handler  = folder_handler;
    state.mark_imap_path  = mark_imap_path;
    state.cb_data         = cb_data;
    state.subscribed      = subscribed;
    state.delim           = imap_mbox_handle_get_delim(handle, path);

    handler_id = g_signal_connect(G_OBJECT(handle), "list-response",
                                  G_CALLBACK(libbalsa_imap_add_folder),
                                  (gpointer) &state);

    if (list_inbox) {
        /* force INBOX into the mailbox list
         * delim doesn't matter, so we'll give it '/'
         * and we'll mark it as scanned, because the only reason for
         * using this option is to pickup an INBOX that isn't in the
         * tree specified by the prefix */
        mailbox_handler("INBOX", '/', cb_data);
        mark_imap_path("INBOX", cb_data);
    }

    i = 0;
    libbalsa_imap_browse(path, &state, handle, server, check_imap_path, &i);
    g_signal_handler_disconnect(G_OBJECT(handle), handler_id);
    libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), handle);
}
