/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
typedef GNode* (ImapHandler)(GNode*root, const char* fn, char delim);

/* read_dir used by mailbox-node append-subtree callback */
void scanner_local_dir(GNode *rnode, const gchar * prefix, 
		       LocalHandler folder_handler, 
		       LocalHandler mailbox_handler);

void scanner_imap_dir(GNode *rnode, LibBalsaServer* server, 
		      const gchar* path, int depth,
		      ImapHandler folder_handler, 
		      ImapHandler mailbox_handler);

#endif				/* __FOLDER_SCANNERS_H__ */
