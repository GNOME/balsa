/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#include <sys/utsname.h>
#include <errno.h>
#include <ctype.h>

#include <gnome.h>

#include "libbalsa.h"
#include "mailbackend.h"

/* #include "libmutt/imap.h" */

MailboxNode *
mailbox_node_new(const gchar * name, LibBalsaMailbox * mb, gint i)
{
    MailboxNode *mbn;
    mbn = g_new(MailboxNode, 1);
    mbn->name = g_strdup(name);

    if (mb)
	mbn->mailbox = mb;
    else
	mbn->mailbox = NULL;

    mbn->IsDir = i;
    mbn->expanded = FALSE;
    mbn->style = 0;

    return mbn;
}

void
mailbox_node_destroy(MailboxNode * mbn)
{
    g_return_if_fail(mbn != NULL);

    g_free(mbn->name);
    g_free(mbn);
}

/* ------------------------------------------ */
typedef void (*ImapBrowseCb) (const char *path, int isdir, void *);
const char *imap_browse_foreach(const char *imap, const char *path,
				ImapBrowseCb cb, void *data);
#if 1
const char *
imap_browse_foreach(const char *imap, const char *path,
		    ImapBrowseCb cb, void *data)
{
    return "";
}
#else
/* -------------------------------------------------------------------
   Pawel's stuff.
   This should be cleaned up and utilize properly the new IMAP code.
*/

/* this module implements a way to import all the mailboxes from the remote 
   IMAP server.
   Motto: collect scrap - save the earth!
*/
/* Ugly mutt's hack */

enum FolderState {
    NOINFR = 1,
    FDUMMY = 1 << 1,
    FMRKTMP = 1 << 2,
    FFDIR = 1 << 3
};

/* eof - end of flag list */
static const char *
get_flags(char *buf, char *flags, char **eof)
{
    char *param;
    int len;

    param = buf + 7;		/* strlen ("* LIST ") */
    if (*param != '(')
	return "Missing flags in LIST response";

    param++;
    if ((*eof = strchr(param, ')')) == NULL)
	return "Unterminated flag list in LIST response";

    len = *eof - param;
    if (len > 126)
	return "Flag list too long in LIST response";

    strncpy(flags, param, len);
    flags[len] = '\0';

    return NULL;
}

static const char *
process_list_output(CONTEXT * ctx, const char *dir, ImapBrowseCb cb,
		    void *data)
{
    char buf[LONG_STRING];
    char seq[16];
    char flags[127], *param, *p, *fname;
    const char *err_msg = NULL;
    enum FolderState fflags;

    /*
     * Send LIST
     */
    imap_make_sequence(seq, sizeof(seq));
    snprintf(buf, sizeof(buf), "%s LIST \"%s/\" \"*\"\r\n", seq, dir);
    mutt_socket_write(CTX_DATA->conn, buf);

    /* process the LIST output */

    while (1) {
	if (mutt_socket_read_line_d(buf, sizeof(buf), CTX_DATA->conn) ==
	    -1) return "Communication error on LIST";

	if (strncmp(buf, seq, strlen(seq)) == 0)
	    return NULL;

	if (strncmp(buf, "* LIST", 6) != 0)
	    return "Unexpected response from server";

	/* my code */
	if (err_msg = get_flags(buf, flags, &p))
	    return err_msg;

	param = p;

	fflags = 0;

	if ((p = strtok(flags, " ")) != NULL)
	    do {
		if (!strcasecmp(p, "\\Noinferiors")) {
		    fflags |= NOINFR;
		    printf("inf ");
		} else if (!strcasecmp(p, "\\Noselect")) {
		    fflags |= FDUMMY;
		    printf("nosel ");
		} else if (!strcasecmp(p, "\\Marked")) {
		    fflags |= FMRKTMP;
		    printf("mark ");
		}
	    } while ((p = strtok(NULL, " ")) != NULL);

	param++;
	while (*param == ' ')
	    param++;

	if ((p = strchr(param, ' ')) == NULL) {
	    mutt_error("Missing folder name in LIST response");
	    break;
	}

	while (*p == ' ')
	    p++;

	if (*p == '"') {
	    p++;
	}

	fname = p /* + strlen(dir) */ ;
	while (*p != '"' && *p) {
	    p++;
	}
	*p = '\0';
	(*cb) (fname, fflags & FDUMMY, data);

    }
    /* not reached */
}

/* imap_server_load_mboxes:
   loads recursively mailbox list from the IMAP server and appends them
   to the balsa_app.mailbox_nodes structure.
   Corresponds to examine_directory.
*/
const char *
imap_browse_foreach(const char *imap, const char *path,
		    ImapBrowseCb cb, void *data)
{
    CONTEXT ctx;
    CONNECTION *conn;
    IMAP_DATA *idata;
    char host[SHORT_STRING];
    char seq[16];
    char *pc = NULL;
    int port;

    const char *err_msg = NULL;

    /*
     * Open IMAP server
     */
    memset(&ctx, 0, sizeof(ctx));
    ctx.path = (char *) safe_strdup(imap);

    if (imap_parse_path(ctx.path, host, sizeof(host), &port, &pc))
	return "Misformed IMAP path";

    conn = mutt_socket_select_connection(host, port, 0);
    idata = CONN_DATA;

    if (!idata || (idata->state != IMAP_AUTHENTICATED)) {
	if (!idata || (idata->state == IMAP_SELECTED) ||
	    (idata->state == IMAP_CONNECTED)) {
	    /* create a new connection, the current one isn't useful */
	    idata = safe_calloc(1, sizeof(IMAP_DATA));

	    conn = mutt_socket_select_connection(host, port, M_NEW_SOCKET);
	    conn->data = idata;
	    idata->conn = conn;
	}
	if (imap_open_connection(idata, conn))
	    return "Could not connect to the IMAP server";
    }
    ctx.data = (void *) idata;

    err_msg = process_list_output(&ctx, path, cb, data);

    imap_close_connection(&ctx);
    FREE(&(ctx.path));
    return err_msg;
}
#endif

ImapDir *
imapdir_new(void)
{
    ImapDir *id = g_new0(ImapDir, 1);
    id->ignore_hidden = TRUE;
    return id;
}

/* imapdir_destroy:
   destroys the ImapDir structure. For future possible compatibility with 
   GtkObject, leaves the structure in sane state.
*/
void
imapdir_destroy(ImapDir * imap_dir)
{
    g_return_if_fail(imap_dir != NULL);
    g_free(imap_dir->name);
    imap_dir->name = NULL;
    g_free(imap_dir->path);
    imap_dir->path = NULL;
    g_free(imap_dir->user);
    imap_dir->user = NULL;
    g_free(imap_dir->passwd);
    imap_dir->passwd = NULL;
    g_free(imap_dir->host);
    imap_dir->host = NULL;

    if (imap_dir->file_tree) {
	g_node_destroy(imap_dir->file_tree);
	imap_dir->file_tree = NULL;
    }
}

static gboolean
do_traverse(GNode * node, gpointer data)
{
    gpointer *d = data;
    if (!node->data)
	return FALSE;

    if (strcmp(((MailboxNode *) node->data)->name, (gchar *) d[0]) != 0)
	return FALSE;

    d[1] = node;
    return TRUE;
}


static GNode *
find_imap_parent_node(GNode * root, const gchar * path)
{
    gpointer d[2];
    gchar *dirname, *p;

    if (root == NULL)
	return NULL;

    if ((p = strrchr(path, '/')) != NULL)
	dirname = g_strndup(path, p - path);
    else
	dirname = g_strdup(path);

    d[0] = (gpointer) dirname;
    d[1] = NULL;
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1, do_traverse,
		    d);
    g_free(dirname);
    return d[1];
}


static void
add_imap_mbox_cb(const char *file, int isdir, gpointer data)
{
    ImapDir *p = (ImapDir *) data;
    GNode *rnode;
    GNode *node;
    LibBalsaMailboxImap *mailbox = NULL;
    const gchar *basename, *ptr;

    if (strcmp(file, p->path) == 0)
	return;

    if ((ptr = strrchr(file, '/')) != NULL)
	basename = ptr + 1;	/* 1 is for the separator */
    else
	basename = file;

    if (!*basename)
	return;

    if (*basename == '.' && p->ignore_hidden) {
	printf("Ignoring hidden file: %s\n", file);
	return;
    }

    rnode = find_imap_parent_node(p->file_tree, file);

    if (!rnode) {
	g_warning("add_imap_mbox_cb: algorithm failed for %s.\n", file);
	return;
    }

    if (isdir) {
	MailboxNode *mbnode;
	mbnode = mailbox_node_new(file, NULL, TRUE);
	mbnode->expanded = FALSE;	/* FIXME: should come from the IMAPDir conf */
	node = g_node_new(mbnode);
    } else {
	LibBalsaServer *server;
	mailbox = LIBBALSA_MAILBOX_IMAP(libbalsa_mailbox_imap_new());
	LIBBALSA_MAILBOX(mailbox)->name = g_strdup(basename);

	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
	libbalsa_server_set_username(server, p->user);
	libbalsa_server_set_password(server, p->passwd);
	libbalsa_server_set_host(server, p->host, p->port);

	mailbox->path = g_strdup(file);

	node =
	    g_node_new(mailbox_node_new
		       (basename, LIBBALSA_MAILBOX(mailbox), FALSE));
    }

    g_node_append(rnode, node);
}

/* imapdir_scan:
   scans given IMAP tree and creates respective mailboxes, stores the tree
   in id->file_tree.
   FIXME: do error checking.
 */

const gchar *
imapdir_scan(ImapDir * id)
{
    gchar *user, *pass;
    const gchar *res = NULL;
    MailboxNode *mbnode;
    gchar *p = g_strdup_printf("{%s:%i}INBOX", id->host, id->port);

    libbalsa_lock_mutt();

    /* FIXME: Is this needed? All calls which rely on this should
     * be locked by the mutt lock */
    user = ImapUser;
    ImapUser = id->user;
    pass = ImapPass;
    ImapPass = id->passwd;

    mbnode = mailbox_node_new(id->path, NULL, TRUE);
    mbnode->expanded = FALSE;	/* FIXME: should come from the IMAPDir conf */

    if (id->file_tree)
	g_node_destroy(id->file_tree);

    id->file_tree = g_node_new(mbnode);

    res = imap_browse_foreach(p, id->path, add_imap_mbox_cb, id);
    g_free(mbnode->name);
    mbnode->name = g_strdup(id->host);

    ImapUser = user;
    ImapPass = pass;

    libbalsa_unlock_mutt();

    return res;
}

/* ------------------------------------------ */

gchar *
libbalsa_get_hostname(void)
{
    struct utsname utsname;
    uname(&utsname);

    return g_strdup(utsname.nodename);
}

/* FIXME: Move to address.c and change name to
 *   libbalsa_address_list_to_string or something */
gchar *
libbalsa_make_string_from_list(const GList * the_list)
{
    gchar *retc, *str;
    GList *list;
    GString *gs = g_string_new(NULL);
    LibBalsaAddress *addy;

    list = g_list_first((GList *) the_list);

    while (list) {
	addy = list->data;
	str = libbalsa_address_to_gchar(addy);
	if (str)
	    gs = g_string_append(gs, str);

	g_free(str);

	if (list->next)
	    gs = g_string_append(gs, ", ");

	list = list->next;
    }

    retc = g_strdup(gs->str);
    g_string_free(gs, 1);

    return retc;
}


/* readfile allocates enough space for the ending '\0' characeter as well.
   returns the number of read characters.
*/
size_t libbalsa_readfile(FILE * fp, char **buf)
{
    size_t size;
    off_t offset;
    int r;
    int fd;
    struct stat statbuf;

    *buf = NULL;
    if (!fp)
	return 0;

    fd = fileno(fp);
    if (fstat(fd, &statbuf) == -1)
	return -1;

    size = statbuf.st_size;

    if (!size) {
	*buf = NULL;
	return size;
    }

    lseek(fd, 0, SEEK_SET);

    *buf = (char *) g_malloc(size + 1);
    if (*buf == NULL) {
	return -1;
    }

    offset = 0;
    while (offset < size) {
	r = read(fd, *buf + offset, size - offset);
	if (r == 0)
	    return offset;

	if (r > 0) {
	    offset += r;
	} else if ((errno != EAGAIN) && (errno != EINTR)) {
	    perror("Error reading file:");
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
gboolean libbalsa_find_word(const gchar * word, const gchar * str)
{
    const gchar *ptr = str;
    int len = strlen(word);

    while (*ptr) {
	if (g_strncasecmp(word, ptr, len) == 0)
	    return TRUE;
	/* skip one word */
	while (*ptr && !isspace((unsigned char) *ptr))
	    ptr++;
	while (*ptr && isspace((unsigned char) *ptr))
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
libbalsa_wrap_string(gchar * str, int width)
{
    const int minl = width / 2;
    gchar *lnbeg, *sppos, *ptr;
    gint te = 0;

    g_return_if_fail(str != NULL);
    lnbeg = sppos = ptr = str;

    while (*ptr) {
	switch (*ptr) {
	case '\t':
	    te += 7;
	    break;
	case '\n':
	    lnbeg = ptr + 1;
	    te = 0;
	    break;
	case ' ':
	    sppos = ptr;
	    break;
	}
	if (ptr - lnbeg >= width - te && sppos >= lnbeg + minl) {
	    *sppos = '\n';
	    lnbeg = sppos + 1;
	    te = 0;
	}
	ptr++;
    }
}

/* libbalsa_set_charset:
   is a thin wrapper around mutt_set_charset() to get rid of mutt dependices
   in balsa.
*/
void
libbalsa_set_charset(const gchar * charset)
{
    libbalsa_lock_mutt();
    mutt_set_charset((gchar *) charset);
    libbalsa_unlock_mutt();
}

/* libbalsa_marshal_POINTER__OBJECT:
   Marshalling function 
*/

typedef gpointer(*GtkSignal_POINTER__OBJECT) (GtkObject * object,
					      GtkObject * parm,
					      gpointer user_data);

void
libbalsa_marshal_POINTER__OBJECT(GtkObject * object, GtkSignalFunc func,
				 gpointer func_data, GtkArg * args)
{
    GtkSignal_POINTER__OBJECT rfunc;
    gpointer *return_val;

    return_val = GTK_RETLOC_POINTER(args[1]);
    rfunc = (GtkSignal_POINTER__OBJECT) func;
    *return_val = (*rfunc) (object, GTK_VALUE_OBJECT(args[0]), func_data);
}

/* libbalsa_marshall_POINTER__POINTER_POINTER:
   Marshalling function
*/
typedef gpointer(*GtkSignal_POINTER__POINTER_POINTER) (GtkObject *object,
						       gpointer *param1,
						       gpointer *param2,
						       gpointer user_data);
void
libbalsa_marshall_POINTER__POINTER_POINTER(GtkObject *object, GtkSignalFunc func,
					   gpointer func_data, GtkArg *args)
{
    GtkSignal_POINTER__POINTER_POINTER rfunc;
    gpointer *return_val;

    return_val = GTK_RETLOC_POINTER(args[2]);
    rfunc = (GtkSignal_POINTER__POINTER_POINTER) func;
    *return_val = (*rfunc) (object, GTK_VALUE_POINTER(args[0]), GTK_VALUE_POINTER(args[1]), func_data);
}

/*
 * Find  the named mailbox from the balsa_app.mailbox_nodes by it's
 * name
 */

static gint
find_mailbox_func(GNode * g1, gpointer data)
{
    MailboxNode *n1 = (MailboxNode *) g1->data;
    gpointer *d = data;
    LibBalsaMailbox *mb = *(LibBalsaMailbox **) data;

    if (!n1 || n1->mailbox != mb)
	return FALSE;

    *(++d) = g1;
    return TRUE;
}

/* find_gnode_in_mbox_list:
   looks for given mailbox in th GNode tree, usually but not limited to
   balsa_app.mailox_nodes
*/
GNode *
find_gnode_in_mbox_list(GNode * gnode_list, LibBalsaMailbox * mailbox)
{
    gpointer d[2];
    GNode *retval;

    d[0] = mailbox;
    d[1] = NULL;

    g_node_traverse(gnode_list, G_IN_ORDER, G_TRAVERSE_LEAFS, -1,
		    find_mailbox_func, d);
    retval = d[1];
    return retval;
}
