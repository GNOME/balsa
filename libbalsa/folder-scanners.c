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
#include "imap/imap.h"
#include "browser.h"
#if 0
static gboolean
traverse_find_dirname(GNode * node, gpointer* d)
{
    BalsaMailboxNode *mn;
    if (!node->data)  /* true for root node only */
	return FALSE;
    mn = (BalsaMailboxNode *) node->data;
    if (mn->name && strcmp(mn->name, (gchar *) d[0]))
	return FALSE;

    ((gpointer*)d)[1] = node;
    return TRUE;
}

static GNode *
find_node_by_dirname(GNode * root, GTraverseType order, 
		     GTraverseFlags flags, gpointer data)
{
    gpointer d[2];

    g_return_val_if_fail(root != NULL, NULL);
    g_return_val_if_fail(order <= G_LEVEL_ORDER, NULL);
    g_return_val_if_fail(flags <= G_TRAVERSE_MASK, NULL);

    d[0] = data;
    d[1] = NULL;

    g_node_traverse(root, order, flags, -1, 
		    (GNodeTraverseFunc) traverse_find_dirname, d);

    return d[1];
}

static int
is_mh_message(gchar * str)
{
    gint i, len;
    len = strlen(str);

    /* check for ,[0-9]+ deleted messages */
    if (len && *str == ',' && is_mh_message(&str[1]))
	return 1;

    for (i = 0; i < len; i++) {
	if (!isdigit((unsigned char) (str[i])))
	    return 0;
    }
    return 1;
}
#endif

void
scanner_local_dir(GNode *rnode, const gchar * prefix, 
		  LocalHandler folder_handler, LocalHandler mailbox_handler)
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

	    if (mailbox_type == LIBBALSA_TYPE_MAILBOX_MH || 
		mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR) {
		mailbox_handler(rnode, de->d_name, filename);
	    } else {
		name = g_basename(prefix);
		current_node = folder_handler(rnode, name, filename);
		scanner_local_dir(current_node, filename, 
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
scanner_imap_dir(GNode *rnode, LibBalsaServer * server, 
		 const gchar* path, int depth,
		 ImapHandler folder_handler, ImapHandler mailbox_handler)
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
	
    unset_option(OPTIMAPLSUB);
    libbalsa_lock_mutt();
    safe_free((void **)&ImapUser);   ImapUser = safe_strdup(server->user);
    safe_free((void **)&ImapPass);   ImapPass = safe_strdup(server->passwd);
    safe_free((void **)&ImapCRAMKey);ImapCRAMKey = safe_strdup(server->passwd);
    
    state.subfolders = g_list_append(NULL, g_strdup(path));
    for(i=0; state.subfolders && i<depth; i++) {
	list = state.subfolders;
	state.subfolders = NULL;
	printf("Deph: %i\n", i);
	for(el= g_list_first(list); el; el = g_list_next(el)) {
	    imap_path = g_strdup_printf("{%s:%i}%s", server->host, 
					server->port, (char*)el->data);
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

void imap_add_folder (char delim, char *folder, int noselect,
  int noinferiors, struct browser_state *state, short isparent)
{
    printf("imap_add_folder. delim: '%c', folder: '%s', noselect: %d\n"
	   "noinferiors: %d, isparent: %d\n", delim, folder, noselect,
	   noinferiors, isparent);
    if(isparent) return;
    if(noinferiors)
	state->mailbox_handler(state->rnode, folder, delim);
    else {
	printf("ADDING FOLDER %s\n", folder);
	state->subfolders = g_list_append(state->subfolders, 
					  g_strdup(folder));
	state->folder_handler(state->rnode, folder, delim);
    }
}
