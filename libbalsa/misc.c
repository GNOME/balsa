/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <gnome.h>

#include "libbalsa.h"
#include "mailbackend.h"

#include "../libmutt/imap.h"

MailboxNode *
mailbox_node_new (const gchar * name, LibBalsaMailbox * mb, gint i)
{
	MailboxNode *mbn;
	mbn = g_new (MailboxNode, 1);
	mbn->name = g_strdup (name);

	if (mb)
		mbn->mailbox = mb;
	else
		mbn->mailbox = NULL;

	mbn->IsDir = i;
	mbn->expanded = FALSE;
	mbn->style = 0;

	return mbn;
}

void mailbox_node_destroy(MailboxNode * mbn)
{
	g_return_if_fail(mbn != NULL);

	g_free(mbn->name);
	g_free(mbn);
}

/* ------------------------------------------ */
ImapDir *imapdir_new(void)
{
	ImapDir *id = g_new0(ImapDir, 1);
	id->ignore_hidden = TRUE;
	return id;
}

/* imapdir_destroy:
   destroys the ImapDir structure. For future possible compatibility with 
   GtkObject, leaves the structure in sane state.
*/
void imapdir_destroy(ImapDir *imap_dir)
{
	g_return_if_fail(imap_dir != NULL);
	g_free(imap_dir->name);               imap_dir->name = NULL; 
	g_free(imap_dir->path);               imap_dir->path = NULL;
	g_free(imap_dir->user);               imap_dir->user = NULL;
	g_free(imap_dir->passwd);             imap_dir->passwd = NULL;
	g_free(imap_dir->host);               imap_dir->host = NULL;

	if(imap_dir->file_tree) {
		g_node_destroy(imap_dir->file_tree);  imap_dir->file_tree = NULL;
	}
}

static gboolean
do_traverse (GNode * node, gpointer data)
{
	gpointer *d = data;
	if (!node->data)
		return FALSE;

	if (strcmp (((MailboxNode *) node->data)->name, (gchar *) d[0]) != 0)
		return FALSE;

	d[1] = node;
	return TRUE;
}


static GNode *
find_imap_parent_node (GNode * root, const gchar *path)
{
	gpointer d[2];
	gchar *dirname, *p;

	if (root == NULL) return NULL;
    
	if( (p= strrchr(path, '/')) != NULL) 
		dirname = g_strndup(path,p-path);
	else  
		dirname = g_strdup(path);

	d[0] = (gpointer)dirname; d[1] = NULL;
	g_node_traverse (root, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1, do_traverse, d);
	g_free(dirname);
	return d[1];
}

static void
add_imap_mbox_cb(const char * file, int isdir, gpointer data)
{
	ImapDir *p = (ImapDir*) data;
	GNode *rnode;
	GNode *node;
	LibBalsaMailboxImap *mailbox = NULL;
	const gchar *basename, *ptr;

	if( strcmp(file, p->path) == 0)
		return;

	if( (ptr=strrchr(file, '/')) !=NULL)
		basename = ptr+1; /* 1 is for the spearator */
	else
		basename = file;

	if( !*basename ) return;

	if( *basename == '.' && p->ignore_hidden) { 
		printf("Ignoring hidden file: %s\n", file); 
		return; 
	}

	rnode = find_imap_parent_node(p->file_tree, file);

	if(!rnode) {
		g_warning("add_imap_mbox_cb: algorithm failed for %s.\n", file);
		return;
	} 

	if (isdir) {
		MailboxNode *mbnode;
		mbnode = mailbox_node_new (file, NULL, TRUE);
		mbnode->expanded = FALSE; /* should come from the IMAPDir conf */
		node = g_node_new (mbnode);
	} else {
		LibBalsaServer *server;
		mailbox = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
		LIBBALSA_MAILBOX(mailbox)->name = g_strdup(basename);

		server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
		libbalsa_server_set_username(server, p->user);
		libbalsa_server_set_password(server, p->passwd);
		libbalsa_server_set_host(server, p->host, p->port);
	
		mailbox->path	= g_strdup(file);

		node = g_node_new (mailbox_node_new (
			basename, LIBBALSA_MAILBOX(mailbox), FALSE));
	}
    
	g_node_append (rnode, node);
}

/* imapdir_scan:
   scans given IMAP tree and creates respective mailboxes, stores the tree
   in id->file_tree.
   FIXME: do error checking.
 */
   
const gchar *
imapdir_scan(ImapDir * id)
{
	gchar * user, *pass;
	const gchar * res;
	MailboxNode *mbnode;
	gchar * p = g_strdup_printf("{%s:%i}INBOX", id->host, id->port);

	user = ImapUser;   ImapUser = id->user; 
	pass = ImapPass;   ImapPass = id->passwd;

	mbnode = mailbox_node_new (id->path, NULL, TRUE);
	mbnode->expanded = FALSE; /* should come from the IMAPDir conf */
	if(id->file_tree) g_node_destroy(id->file_tree);
	id->file_tree = g_node_new (mbnode);

	res = imap_browse_foreach(p, id->path, add_imap_mbox_cb, id);
	g_free(mbnode->name);
	mbnode->name = g_strdup(id->host);
	ImapUser = user;   ImapPass = pass;
	return res;
}

/* ------------------------------------------ */

gchar *
libbalsa_get_hostname (void)
{
	struct utsname utsname;
	uname (&utsname);

	return g_strdup (utsname.nodename);
}

gchar *
make_string_from_list (GList * the_list)
{
	gchar *retc, *str;
	GList *list;
	GString *gs = g_string_new (NULL);
	LibBalsaAddress *addy;

	list = g_list_first (the_list);

	while (list) {
		addy = list->data;
		str = libbalsa_address_to_gchar (addy);
		if(str) 
			gs = g_string_append (gs, str);

		g_free (str);

		if (list->next)
			gs = g_string_append (gs, ", ");

		list = list->next;
	}

	retc = g_strdup (gs->str);
	g_string_free (gs, 1);

	return retc;
}

/* readfile allocates enough space for the ending '\0' characeter as well.
   returns the number of read characters.
*/
size_t
readfile (FILE * fp, char **buf)
{
	size_t size;
	off_t offset;
	int r;
	int fd = fileno (fp);
	struct stat statbuf;

	if (fstat (fd, &statbuf) == -1)
		return -1;

	size = statbuf.st_size;

	if (!size) {
		*buf = NULL;
		return size;
	}

	lseek (fd, 0, SEEK_SET);

	*buf = (char *) g_malloc (size+1);
	if (*buf == NULL) {
		return -1;
	}

	offset = 0;
	while (offset < size) {
		r = read (fd, *buf + offset, size - offset);
		if (r == 0)
			return offset;

		if (r > 0) {
			offset += r;
		} else if ((errno != EAGAIN) && (errno != EINTR)) {
			perror ("Error reading file:");
			return -1;
		}
	}
	(*buf)[size] = '\0';

	return size;
}

/* libbalsa_find_word:
   searches given word delimited by blanks or string boundaries in given
   string. IS NOT case-sensitive.
   Returns TRUE if the word is found.
*/
gboolean
libbalsa_find_word(const gchar * word, const gchar* str) {
	const gchar *ptr = str;
	int  len = strlen(word);
    
	while(*ptr) {
		if(g_strncasecmp(word, ptr, len) == 0)
			return TRUE;
		/* skip one word */
		while(*ptr && !isspace( (unsigned char)*ptr) )
			ptr++;
		while(*ptr && isspace( (unsigned char)*ptr) )
			ptr++;
	}
	return FALSE;
}

/* libbalsa_wrap_string
   wraps given string replacing spaces with '\n'.  do changes in place.
   lnbeg - line beginning position, sppos - space position, 
   te - tab's extra space.
*/
void
libbalsa_wrap_string(gchar* str, int width)
{
	const int minl = width/2;
	gchar *lnbeg, *sppos, *ptr;
	gint te = 0;

	g_return_if_fail(str != NULL);
	lnbeg= sppos = ptr = str;

	while(*ptr) {
		if(*ptr=='\t') te += 7;
		if(*ptr==' ') sppos = ptr;
		if(ptr-lnbeg>width-te && sppos>=lnbeg+minl) {
			*sppos = '\n';
			lnbeg = ptr; te = 0;
		}
		if(*ptr=='\n') {
			lnbeg = ptr; te = 0;
		}
		ptr++;
	}
}

/* libbalsa_set_charset:
   is a thin wrapper around mutt_set_charset() to get rid of mutt dependices
   in balsa.
*/
void libbalsa_set_charset(const gchar* charset)
{
	mutt_set_charset((gchar*)charset);
}



