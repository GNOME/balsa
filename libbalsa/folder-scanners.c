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
#include "mutt.h"
/* FIXME: libbalsa/mailbox.h" name colision */
#include "../libmutt/mailbox.h"
#include "imap/imap.h"
#include "imap/imap_private.h"
#include "browser.h"

static void
libbalsa_scanner_mh_dir(GNode *rnode,
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
	    if (libbalsa_mailbox_type_from_path(filename) == LIBBALSA_TYPE_MAILBOX_MH) {
		parent_node = mailbox_handler(rnode, de->d_name, filename);
		libbalsa_scanner_mh_dir(parent_node, filename, 
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
    gchar * name;
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

	    if (mailbox_type == LIBBALSA_TYPE_MAILBOX_MH) {
		current_node = mailbox_handler(rnode, de->d_name, filename);
		libbalsa_scanner_mh_dir(current_node, filename, 
				        folder_handler, mailbox_handler);
	    } else if (mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR) {
		mailbox_handler(rnode, de->d_name, filename);
	    } else {
		name = g_basename(prefix);
		current_node = folder_handler(rnode, name, filename);
		libbalsa_scanner_local_dir(current_node, filename, 
					   folder_handler, mailbox_handler);
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
void imap_add_folder (char delim, char *folder, int noselect, int noinferiors,
		      struct browser_state *state, short isparent);
void
libbalsa_scanner_imap_dir(GNode *rnode, LibBalsaServer * server, 
			  const gchar* path, gboolean subscribed, int depth,
			  ImapHandler folder_handler, 
			  ImapHandler mailbox_handler)
{
    gchar* imap_path;
    GList* list = NULL, *el;
    struct browser_state state;
    int i;

    printf("imap_dir: reading for %s\n", path);
    init_state (&state);
    state.imap_browse = 1;
    state.rnode = rnode;
    state.mailbox_handler = (void(*)())mailbox_handler;
    state.folder_handler = (void(*)())folder_handler;
    if(!FileMask.rx) {
       FileMask.rx = (regex_t *) safe_malloc (sizeof (regex_t));
       if( (i=REGCOMP(FileMask.rx,"!^\\.[^.]",0)) != 0) {
	   g_warning("FileMask regexp compilation failed with code%i.",
		     i);
	   safe_free((void**)&FileMask.rx);
	   return;
       }
    }
	
    libbalsa_lock_mutt();
    safe_free((void **)&ImapUser);   ImapUser = safe_strdup(server->user);
    safe_free((void **)&ImapPass);   ImapPass = safe_strdup(server->passwd);

    /* subscribed triggers a bug in libmutt, disable it now */
    if(subscribed)
	set_option(OPTIMAPLSUB);
    else
	unset_option(OPTIMAPLSUB);
    state.subfolders = g_list_append(NULL, g_strdup(path));
    state.folder = NULL;

    for(i=0; state.subfolders && i<depth; i++) {
	list = state.subfolders;
	state.subfolders = NULL;
	printf("Deph: %i -------------------------------------------\n", i);
	for(el= g_list_first(list); el; el = g_list_next(el)) {
	    if(*(char*)el->data)
		imap_path = g_strdup_printf("imap://%s:%i/%s/", server->host, 
					    server->port, (char*)el->data);
	    else 
		imap_path = g_strdup_printf("imap://%s:%i/", server->host, 
					    server->port);
	    FREE(&state.folder);
	    imap_browse ((char*)imap_path,  &state);
	    g_free(imap_path);
	}
	g_list_foreach(list, (GFunc)g_free, NULL);
	g_list_free(list); 
    }
    g_list_foreach((GList*)state.subfolders, (GFunc)g_free, NULL);
    g_list_free((GList*)state.subfolders);
    regfree(FileMask.rx);
    libbalsa_unlock_mutt();
    
}

/* this function ovverrides mutt's one. */
void imap_add_folder (char delim, char *folder, int noselect,
  int noinferiors, struct browser_state *state, short isparent)
{
    int isFolder = 0;
    int isMailbox = 0;

    imap_unmunge_mbox_name (folder);
    printf("imap_add_folder. delim: '%c', folder: '%s', noselect: %d\n"
	   "noinferiors: %d, isparent: %d\n", delim, folder, noselect,
	   noinferiors, isparent); 
    if(isparent) return;
    if(!noselect) {
	printf("ADDING MAILBOX %s\n", folder);
	++isMailbox;
    }
    /* this extra check is needed for subscribed folder handling. 
	   Read RFC when iin doubt. */
    if(!g_list_find_custom(state->subfolders, folder,
			   (GCompareFunc)strcmp)) {
	printf("ADDING FOLDER %s\n", folder);
	    
	state->subfolders = g_list_append(state->subfolders,
					  g_strdup(folder));
	++isFolder;
    }
    if (isMailbox)
	state->mailbox_handler(state->rnode, folder, delim);
    else if (isFolder)
	state->folder_handler(state->rnode, folder, delim);
}
