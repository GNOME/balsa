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

#ifndef __FOLDER_SCANNERS_H__
#define __FOLDER_SCANNERS_H__

typedef GNode* (LocalHandler)(GNode*root, const char*d_name, const char* fn);
typedef gboolean ImapCheck(LibBalsaServer * server, const char *fn,
                           guint depth);
typedef void ImapMark(const char * fn, gpointer data);
typedef void (ImapHandler) (const char *fn, char delim, gpointer data);

/* read_dir used by mailbox-node append-subtree callback */
void libbalsa_scanner_local_dir(GNode *rnode, const gchar * prefix, 
				LocalHandler folder_handler, 
				LocalHandler mailbox_handler);

void libbalsa_scanner_imap_dir(GNode *rnode, LibBalsaServer * server, 
			       const gchar* path, gboolean subscribed, 
			       gboolean list_inbox,
                               ImapCheck check_imap_path,
                               ImapMark mark_imap_path,
			       ImapHandler folder_handler, 
			       ImapHandler mailbox_handler,
			       gpointer cb_data);

#endif				/* __FOLDER_SCANNERS_H__ */
