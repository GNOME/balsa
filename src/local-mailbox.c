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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>

#include "balsa-app.h"
#include "local-mailbox.h"
#include "libbalsa.h"
#include "mailbox-node.h"


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

static gboolean
traverse_find_path(GNode * node, gpointer * d)
{
    const gchar *path;
    LibBalsaMailbox * mailbox;
    if(node->data == NULL) /* true for root node only */
	return FALSE;
    
    mailbox = ((BalsaMailboxNode *) node->data)->mailbox;
    if(!LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) return FALSE;

    path = LIBBALSA_MAILBOX_LOCAL(mailbox)->path;

    if (strcmp(path, (gchar *) d[0]))
	return FALSE;

    d[1] = node;
    return TRUE;
}

static GNode *
find_by_path(GNode * root, GTraverseType order, GTraverseFlags flags,
	     const gchar * path)
{
    gpointer d[2];

    d[0] = (gchar *) path;
    d[1] = NULL;
    g_node_traverse(root, order, flags, -1,
		    (GNodeTraverseFunc) traverse_find_path, d);

    return d[1];
}

/* add_mailbox
   the function scans the local mail directory (LMD) and adds them to the 
   list of mailboxes. Takes care not to duplicate any of the "standard"
   mailboxes (inbox, outbox etc). Avoids also problems with aliasing 
   (someone added a local mailbox - possibly aliased - located in LMD 
   to the configuration).
*/
static GNode*
add_mailbox(GNode *rnode, const gchar * name, const gchar * path, GtkType type)
{
    LibBalsaMailbox *mailbox;
    char *dirname;
    GNode *node;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.inbox)->path) ==
	    0) return;
    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.outbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.outbox)->path) ==
	    0) return;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.sentbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.sentbox)->path)
	    == 0)
	    return;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.draftbox))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.draftbox)->path)
	    == 0)
	    return;

    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.trash))
	if (strcmp(path, LIBBALSA_MAILBOX_LOCAL(balsa_app.trash)->path) ==
	    0) return;

    /* don't add if the mailbox is already in the configuration */
    if (find_by_path(balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
		     G_TRAVERSE_ALL, path))
	return;

    if (type == 0) {
	node = g_node_new(balsa_mailbox_node_new_from_dir(path));
    } else {
	if ( type == LIBBALSA_TYPE_MAILBOX_MH ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mh_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_mbox_new(path, FALSE));
	} else if ( type == LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	    mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_maildir_new(path, FALSE));
	} else {
	    /* type is not a valid local mailbox type. */
	    g_assert_not_reached();
	}
	mailbox->name = g_strdup(name);

	node = g_node_new(balsa_mailbox_node_new_from_mailbox(mailbox));

	if (balsa_app.debug)
	    g_print(_("Local Mailbox Loaded as: %s\n"),
		    gtk_type_name(GTK_OBJECT_TYPE(mailbox)));
    }
    
    /* no type checking, parent is NULL for root */
    BALSA_MAILBOX_NODE(node->data)->parent = (BalsaMailboxNode*)rnode->data;
    g_node_append(rnode, node);
    return node;
}

#if 0
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
read_dir(GNode *rnode, const gchar * prefix)
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
    /* name = g_basename(prefix);
    current_node = add_mailbox(rnode, name, prefix, 0);
    g_free(name); */

    while ((de = readdir(dpc)) != NULL) {
	if (de->d_name[0] == '.')
	    continue;
	snprintf(filename, PATH_MAX, "%s/%s", prefix, de->d_name);

	/* ignore file if it can't be read. */
	if (stat(filename, &st) == -1 || access(filename, R_OK) == -1)
	    continue;
	
	if (S_ISDIR(st.st_mode)) {
	    mailbox_type = libbalsa_mailbox_type_from_path(filename);

	    if (balsa_app.debug)
		fprintf(stderr, "Mailbox name = %s,  mailbox type = %d\n",
			filename, mailbox_type);

	    if (mailbox_type == LIBBALSA_TYPE_MAILBOX_MH || 
		mailbox_type == LIBBALSA_TYPE_MAILBOX_MAILDIR) {
		add_mailbox(rnode, de->d_name, filename, mailbox_type);
	    } else {
		name = g_basename(prefix);
		current_node = add_mailbox(rnode, name, prefix, 0);
		g_free(name);
		read_dir(current_node, filename);
	    }
	} else {
	    mailbox_type = libbalsa_mailbox_type_from_path(filename);
	    if (mailbox_type != 0)
		add_mailbox(rnode, de->d_name, filename, mailbox_type);
	}
    }
    closedir(dpc);
}


void
load_local_mailboxes()
{
    /* read_dir(balsa_app.mailbox_nodes, balsa_app.local_mail_directory);
     */
    g_node_append(balsa_app.mailbox_nodes,
		  g_node_new(balsa_mailbox_node_new_from_dir(balsa_app.local_mail_directory)));
}
