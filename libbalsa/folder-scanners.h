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

#ifndef __FOLDER_SCANNERS_H__
#define __FOLDER_SCANNERS_H__

#include <glib-object.h>
#include "libbalsa.h"

typedef gboolean LocalCheck(const gchar * fn, guint depth);
typedef gboolean LocalMark(gpointer node);
typedef gpointer LocalHandler(gpointer root, const char *d_name,
			      const char *fn, GType type);

typedef gboolean ImapCheck(const char *fn, LibBalsaServer * server,
                           guint depth);
typedef void ImapMark(const char *fn, gpointer data);
typedef void ImapHandler(const char *fn, gint delim, gint noselect,
			 gint marked, gint noscan, gpointer data);

/* read_dir used by mailbox-node append-subtree callback */
void libbalsa_scanner_local_dir(gpointer rnode, const gchar * prefix, 
				LocalCheck check_local_path,
				LocalMark mark_local_path,
				LocalHandler folder_handler, 
                                LocalHandler mailbox_handler,
                                GType parent_type);

void libbalsa_scanner_imap_dir(gpointer rnode, LibBalsaServer * server, 
                               const gchar* path, int delim,
			       gboolean subscribed, gboolean list_inbox,
                               ImapCheck check_imap_path,
                               ImapMark mark_imap_path,
                               ImapHandler handle_imap_path,
                               gpointer cb_data, 
                               GError **error);


/* Scan the passed IMAP server and return a GtkTreeStore containing all folders and (if requested) their subscription states.  The
 * columns in the returned store are:
 * 0: the folder name (G_TYPE_STRING)
 * 1: the full path of the folder on the server (G_TYPE_STRING)
 * 2 and 3: the current subscription state (G_TYPE_BOOLEAN), doubled as to track state changes and avoid unnecessary (UN)SUBSCRIBE
 *    commands
 * 4: the Pango rendering style for the folder name (PANGO_TYPE_STYLE)
 *
 * If subscriptions is FALSE, only the folder structure is read, columns 2 and 3 are always FALSE, and column 4 is always
 * PANGO_STYLE_NORMAL.
 */
GtkTreeStore *libbalsa_scanner_imap_tree(LibBalsaServer  *server,
										 gboolean		  subscriptions,
										 GError         **error)
	G_GNUC_WARN_UNUSED_RESULT;

typedef enum {
	LB_SCANNER_IMAP_FOLDER = 0,
	LB_SCANNER_IMAP_PATH,
	LB_SCANNER_IMAP_SUBS_NEW,
	LB_SCANNER_IMAP_SUBS_OLD,
	LB_SCANNER_IMAP_STYLE,
	LB_SCANNER_IMAP_N_COLS
} lb_scanner_imap_tree_cols_t;


#endif				/* __FOLDER_SCANNERS_H__ */
