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
#ifdef BALSA_USE_THREADS
/* _XOPEN_SOURCE is needed for rwlocks */
#define _XOPEN_SOURCE 500
#include <pthread.h>
#endif

#include "balsa-app.h"


/* Global application structure */
struct BalsaApplication balsa_app;

const gchar *pspell_modules[] = {
    "ispell",
    "aspell"
};

const gchar *pspell_suggest_modes[] = {
    "fast",
    "normal",
    "bad-spellers"
};

/* ask_password:
   asks the user for the password to the mailbox on given remote server.
*/
static gchar *
ask_password_real(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    GtkWidget *dialog, *entry;
    gchar *prompt, *passwd = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    if (mbox)
	prompt =
	    g_strdup_printf(_("Opening remote mailbox %s.\n"
                              "The password for %s@%s:"),
			    mbox->name, server->user, server->host);
    else
	prompt =
	    g_strdup_printf(_("Mailbox password for %s@%s:"), server->user,
			    server->host);

    dialog = gtk_dialog_new_with_buttons(_("Password needed"),
                                         GTK_WINDOW(balsa_app.main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL); 
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      gtk_label_new(prompt));
    g_free(prompt);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox),
                      entry = gtk_entry_new());
    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 20);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus (entry);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
        passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dialog);
    return passwd;
}

#ifdef BALSA_USE_THREADS
typedef struct {
    pthread_cond_t cond;
    LibBalsaServer* server;
    LibBalsaMailbox* mbox;
    gchar* res;
} AskPasswdData;

/* ask_passwd_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_passwd_idle(gpointer data)
{
    AskPasswdData* apd = (AskPasswdData*)data;
    gdk_threads_enter();
    apd->res = ask_password_real(apd->server, apd->mbox);
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}

static gchar *
ask_password_mt(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    static pthread_mutex_t ask_passwd_lock = PTHREAD_MUTEX_INITIALIZER;
    AskPasswdData apd;

    gdk_threads_leave();
    pthread_mutex_lock(&ask_passwd_lock);
    pthread_cond_init(&apd.cond, NULL);
    apd.server = server;
    apd.mbox   = mbox;
    gtk_idle_add(ask_passwd_idle, &apd);
    pthread_cond_wait(&apd.cond, &ask_passwd_lock);
    
    pthread_cond_destroy(&apd.cond);
    pthread_mutex_unlock(&ask_passwd_lock);
    pthread_mutex_destroy(&ask_passwd_lock);
    gdk_threads_enter();
    return apd.res;
}
#endif

static gboolean
set_passwd_from_matching_server(GNode *nd, gpointer data)
{
    LibBalsaServer *server;
    LibBalsaServer *master;
    LibBalsaMailbox *mbox;
    BalsaMailboxNode *node;

    g_return_val_if_fail(nd != NULL, FALSE);
    node = (BalsaMailboxNode *)nd->data;
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(node), FALSE);
    if(node->server)
        server = node->server;
    else {
        mbox = node->mailbox;
        g_return_val_if_fail(mbox != NULL, FALSE);
        g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mbox), FALSE);

        if (!LIBBALSA_IS_MAILBOX_REMOTE(mbox)) return FALSE;
        server = LIBBALSA_MAILBOX_REMOTE_SERVER(mbox);
        g_return_val_if_fail(server != NULL, FALSE);
    }
    g_return_val_if_fail(server->host != NULL, FALSE);
    g_return_val_if_fail(server->user != NULL, FALSE);
    if (server->passwd == NULL) return FALSE;

    master = (LibBalsaServer *)data;
    g_return_val_if_fail(LIBBALSA_IS_SERVER(master), FALSE);
    if (master == server) return FALSE;

    g_return_val_if_fail(server->host != NULL, FALSE);
    g_return_val_if_fail(server->user != NULL, FALSE);

    if ((strcmp(server->host, master->host) == 0) &&
	(strcmp(server->user, master->user) == 0)) {
	g_free(master->passwd);
	master->passwd = g_strdup(server->passwd);
	return TRUE;
    };
    
    return FALSE;
}

gchar *
ask_password(LibBalsaServer *server, LibBalsaMailbox *mbox)
{
    gchar *password;
    
    g_return_val_if_fail(server != NULL, NULL);

    password = NULL;
    if (mbox) {
        balsa_mailbox_nodes_lock(FALSE);
        g_node_traverse(balsa_app.mailbox_nodes, G_IN_ORDER, G_TRAVERSE_LEAFS,
		-1, set_passwd_from_matching_server, server);
        balsa_mailbox_nodes_unlock(FALSE);
	if (server->passwd != NULL) {
	    password = server->passwd;
	    server->passwd = NULL;
	}
    }
    if (!password)
#ifdef BALSA_USE_THREADS
	return ask_password_mt(server, mbox);
#else
	return ask_password_real(server, mbox);
#endif
    else
	return password;
}

#if ENABLE_ESMTP
static void
authapi_exit (void)
{
    if (balsa_app.smtp_authctx != NULL)
        auth_destroy_context (balsa_app.smtp_authctx);
    auth_client_exit ();
}


/* Callback to get user/password info from SMTP server preferences.
   This is adequate for simple username / password requests but does
   not adequately cope with all SASL mechanisms.  */
static int
authinteract (auth_client_request_t request, char **result, int fields,
              void *arg)
{
    int i;

    for (i = 0; i < fields; i++) {
	if (request[i].flags & AUTH_PASS)
	    result[i] = balsa_app.smtp_passphrase;
	else if (request[i].flags & AUTH_USER)
	    result[i] = balsa_app.smtp_user;

    	/* Fail the AUTH exchange if something was requested
    	   but not supplied. */
    	if (result[i] == NULL)
    	    return 0;
    }
    return 1;
}

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
static int
tlsinteract (char *buf, int buflen, int rwflag, void *arg)
{
  char *pw;
  int len;

  pw = balsa_app.smtp_certificate_passphrase;
  len = strlen (pw);
  if (len + 1 > buflen)
    return 0;
  strcpy (buf, pw);
  return len;
}
#endif
#endif /* ESMTP */

void
balsa_app_init(void)
{
    /* 
     * initalize application structure before ALL ELSE 
     * to some reasonable defaults
     */
    balsa_app.identities = NULL;
    balsa_app.current_ident = NULL;
    balsa_app.local_mail_directory = NULL;

#if ENABLE_ESMTP
    balsa_app.smtp_server = NULL;
    balsa_app.smtp_user = NULL;
    balsa_app.smtp_passphrase = NULL;

    /* Do what's needed at application level to allow libESMTP
       to use authentication.  */
    auth_client_init ();
    atexit (authapi_exit);
    balsa_app.smtp_authctx = auth_create_context ();
    auth_set_mechanism_flags (balsa_app.smtp_authctx, AUTH_PLUGIN_PLAIN, 0);
    auth_set_interact_cb (balsa_app.smtp_authctx, authinteract, NULL);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    /* Use our callback for X.509 certificate passwords.  If STARTTLS is
       not in use or disabled in configure, the following is harmless. */
    balsa_app.smtp_certificate_passphrase = NULL;
    smtp_starttls_set_password_cb (tlsinteract, NULL);
#endif
#endif

    balsa_app.inbox = NULL;
    balsa_app.inbox_input = NULL;
    balsa_app.outbox = NULL;
    balsa_app.sentbox = NULL;
    balsa_app.draftbox = NULL;
    balsa_app.trash = NULL;

    balsa_app.mailbox_nodes = NULL;

    balsa_app.new_messages_timer = 0;
    balsa_app.new_messages = 0;

    balsa_app.check_mail_auto = TRUE;
    balsa_app.check_mail_timer = 10;

    balsa_app.debug = FALSE;
    balsa_app.previewpane = TRUE;
    balsa_app.pgdownmod = FALSE;
    balsa_app.pgdown_percent = 50;

    /* GUI settings */
    balsa_app.mblist = NULL;
    balsa_app.mblist_width = 100;
    balsa_app.mw_width = MW_DEFAULT_WIDTH;
    balsa_app.mw_height = MW_DEFAULT_HEIGHT;
    balsa_app.sw_width = 0;
    balsa_app.sw_height = 0;
    balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;
    balsa_app.pwindow_option = WHILERETR;
    balsa_app.wordwrap = TRUE;
    balsa_app.wraplength = 72;
    balsa_app.browse_wrap = TRUE;
    balsa_app.browse_wrap_length = 79;
    balsa_app.shown_headers = HEADERS_SELECTED;
    balsa_app.selected_headers = g_strdup(DEFAULT_SELECTED_HDRS);
    balsa_app.threading_type = BALSA_INDEX_THREADING_JWZ;
    balsa_app.expand_tree = FALSE;
    balsa_app.show_mblist = TRUE;
    balsa_app.show_notebook_tabs = FALSE;
    balsa_app.alternative_layout = FALSE;
    balsa_app.view_message_on_open = TRUE;
    balsa_app.line_length = FALSE;

    balsa_app.index_num_width = NUM_DEFAULT_WIDTH;
    balsa_app.index_status_width = STATUS_DEFAULT_WIDTH;
    balsa_app.index_attachment_width = ATTACHMENT_DEFAULT_WIDTH;
    balsa_app.index_from_width = FROM_DEFAULT_WIDTH;
    balsa_app.index_subject_width = SUBJECT_DEFAULT_WIDTH;
    balsa_app.index_date_width = DATE_DEFAULT_WIDTH;
    balsa_app.index_size_width = SIZE_DEFAULT_WIDTH;

    /* file paths */
    balsa_app.attach_dir = NULL;
    balsa_app.save_dir = NULL;

    /* Mailbox list column width (not fully implemented) */
    balsa_app.mblist_name_width = MBNAME_DEFAULT_WIDTH;

    balsa_app.mblist_show_mb_content_info = FALSE;
    balsa_app.mblist_newmsg_width = NEWMSGCOUNT_DEFAULT_WIDTH;
    balsa_app.mblist_totalmsg_width = TOTALMSGCOUNT_DEFAULT_WIDTH;

    balsa_app.visual = NULL;
    balsa_app.colormap = NULL;

    gdk_color_parse(MBLIST_UNREAD_COLOR, &balsa_app.mblist_unread_color);

    /* arp */
    balsa_app.quote_str = NULL;

    /* quote regex */
    balsa_app.quote_regex = g_strdup(DEFAULT_QUOTE_REGEX);

    /* font */
    balsa_app.message_font = NULL;
    balsa_app.subject_font = NULL;

    /*encoding */
    balsa_app.encoding_style = 0;

    /* compose: shown headers */
    balsa_app.compose_headers = NULL;

    /* date format */
    balsa_app.date_string = g_strdup(DEFAULT_DATE_FORMAT);

    /* printing */
    balsa_app.paper_size = g_strdup(DEFAULT_PAPER_SIZE);

    /* address book */
    balsa_app.address_book_list = NULL;
    balsa_app.default_address_book = NULL;

    /* Filters */
    balsa_app.filters=NULL;

    /* spell check */
    balsa_app.module = SPELL_CHECK_MODULE_ASPELL;
    balsa_app.suggestion_mode = SPELL_CHECK_SUGGEST_NORMAL;
    balsa_app.ignore_size = 0;
    balsa_app.check_sig = DEFAULT_CHECK_SIG;

    spell_check_modules_name = pspell_modules;
    spell_check_suggest_mode_name = pspell_suggest_modes;

    /* Information messages */
    balsa_app.information_message = 0;
    balsa_app.warning_message = 0;
    balsa_app.error_message = 0;
    balsa_app.debug_message = 0;

    balsa_app.notify_new_mail_sound = 1;
    balsa_app.notify_new_mail_dialog = 0;

    /* Tooltips */
    balsa_app.tooltips = gtk_tooltips_new();

    /* IMAP */
    balsa_app.check_imap = 1;
    balsa_app.check_imap_inbox = 0;
    balsa_app.imap_scan_depth = 1;

    /* RFC2646 format=flowed */
    balsa_app.recognize_rfc2646_format_flowed = TRUE;
    balsa_app.send_rfc2646_format_flowed = TRUE;

    /* Message filing */
    balsa_app.folder_mru=NULL;
    balsa_app.drag_default_is_move=0;
}

gboolean
do_load_mailboxes(void)
{
    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox)) {
	libbalsa_set_spool(libbalsa_mailbox_local_get_path(balsa_app.inbox));
    } else if (LIBBALSA_IS_MAILBOX_IMAP(balsa_app.inbox)
	       || LIBBALSA_IS_MAILBOX_POP3(balsa_app.inbox)) {
	/* Do nothing */
    } else {
	fprintf(stderr, "do_load_mailboxes: Unknown inbox mailbox type\n");
	return FALSE;
    }
    return TRUE;
}

void
update_timer(gboolean update, guint minutes)
{
    guint32 timeout;
    timeout = minutes * 60 * 1000;

    if (update) {
	if (balsa_app.check_mail_timer_id)
	    gtk_timeout_remove(balsa_app.check_mail_timer_id);

	balsa_app.check_mail_timer_id = gtk_timeout_add(timeout,
							(GtkFunction)
							check_new_messages_auto_cb,
							NULL);
    } else {
	if (balsa_app.check_mail_timer_id)
	    gtk_timeout_remove(balsa_app.check_mail_timer_id);
	balsa_app.check_mail_timer_id = 0;
    }
}



/* open_mailboxes_idle_cb:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
   Release the passed argument when done.
 */
gboolean
open_mailboxes_idle_cb(gchar * names[])
{
    LibBalsaMailbox *mbox;
    gint i = 0;

    g_return_val_if_fail(names, FALSE);

    gdk_threads_enter();

    while (names[i]) {
	mbox = mblist_find_mbox_by_name(balsa_app.mblist, names[i]);
	if (balsa_app.debug)
	    fprintf(stderr, "open_mailboxes_idle_cb: opening %s => %p..\n",
		    names[i], mbox);
	if (mbox)
	    mblist_open_mailbox(mbox);
	i++;
    }
    g_strfreev(names);

    if (gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook)) >=
	0) gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), 0);
    gdk_threads_leave();

    return FALSE;
}

GtkWidget *
balsa_stock_button_with_label(const char *icon, const char *text)
{
    GtkWidget *button;
#if BALSA_MAJOR < 2
    GtkWidget *pixmap;

    pixmap = gnome_stock_new_with_icon(icon);
    button = gnome_pixmap_button(pixmap, label);
#else
    GtkWidget *w;

    button = gtk_button_new();
    w = balsa_stock_hbox_with_label(icon, GTK_ICON_SIZE_BUTTON, text);
    gtk_container_add(GTK_CONTAINER(button), w);
    gtk_widget_show_all(button);
#endif                          /* BALSA_MAJOR < 2 */
    return button;
}

GtkWidget *
balsa_stock_hbox_with_label(const char *icon, GtkIconSize size,
                              const char *text)
{
    GtkWidget *pixmap = gtk_image_new_from_stock(icon, size);
    GtkWidget *w = gtk_hbox_new(FALSE, 0);

    gtk_box_pack_start(GTK_BOX(w), pixmap, FALSE, FALSE, 0);
    if (text && *text) {
        GtkWidget *label = gtk_label_new(text);
        gtk_box_pack_start(GTK_BOX(w), label, FALSE, FALSE, 2);
    }

    return w;
}

static gint
find_mailbox(GNode * g1, gpointer data)
{
    BalsaMailboxNode *mbnode = (BalsaMailboxNode *) g1->data;
    gpointer *d = data;
    LibBalsaMailbox *mb = *(LibBalsaMailbox **) data;

    if (!mbnode || mbnode->mailbox != mb)
        return FALSE;

    *(++d) = g1;
    return TRUE;
}

/* find_gnode_in_mbox_list:
   looks for given mailbox in th GNode tree, usually but not limited to
   balsa_app.mailbox_nodes
*/
GNode *
find_gnode_in_mbox_list(GNode * gnode_list, LibBalsaMailbox * mailbox)
{
    gpointer d[2];
    GNode *retval;

    d[0] = mailbox;
    d[1] = NULL;

    g_node_traverse(gnode_list, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                    find_mailbox, d);
    retval = d[1];
    return retval;
}

static gint
find_mbnode(GNode * g1, gpointer data)
{
    BalsaMailboxNode *mbnode = (BalsaMailboxNode *) g1->data;
    gpointer *d = data;
    BalsaMailboxNode *mb = *(BalsaMailboxNode **) data;

    if (mbnode != mb) return FALSE;

    d[1] = g1;
    return TRUE;
}

GNode *
balsa_find_mbnode(GNode* gnode, BalsaMailboxNode* mbnode)
{
    gpointer d[2];

    d[0] = mbnode;
    d[1] = NULL;

    g_node_traverse(gnode, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                    find_mbnode, d);
    return (GNode*)d[1];
}

static gboolean
traverse_find_dir(GNode * node, gpointer * d)
{
    BalsaMailboxNode * mbnode;
    if(node->data == NULL)
	return FALSE;
    
    mbnode = (BalsaMailboxNode *) node->data;

    if (mbnode->dir == NULL || strcmp(mbnode->dir, (gchar *) d[0]))
	return FALSE;

    d[1] = node;
    return TRUE;
}
/* balsa_app_find_by_dir:
   looks for a mailbox node with dir equal to path.
   returns NULL on failure
*/
GNode*
balsa_app_find_by_dir(GNode* root, const gchar* path)
{
    gpointer d[2];

    d[0] = (gchar*) path;
    d[1] = NULL;
    g_node_traverse(root, G_LEVEL_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) traverse_find_dir, d);
    return d[1];
}


/* balsa_remove_children_mailbox_nodes:
   remove all children of given node leaving the node itself intact.
   Applicable to balsa_app.mailbox_nodes and its children.
 */
static gboolean
destroy_mailbox_node(GNode* node, GNode* root)
{ 
    BalsaMailboxNode *mbnode = BALSA_MAILBOX_NODE(node->data);

    g_return_val_if_fail(mbnode, FALSE);
		     
    if (mbnode->mailbox) {
	balsa_window_close_mbnode(balsa_app.main_window, mbnode);
        mbnode->mailbox = NULL;
    }
    mblist_remove_mailbox_node(balsa_app.mblist, 
			       mbnode);
    gtk_object_destroy((GtkObject*)mbnode); 
    return FALSE;
}

#if 0
static void 
destroy_mailbox_tree(GNode* node, GNode* root)
{ 
}
#endif

void
balsa_remove_children_mailbox_nodes(GNode* gnode)
{
    GNode* walk, *next_sibling;
    g_return_if_fail(gnode);

    if(balsa_app.debug)
	printf("Destroying children of %p %s\n",
	       gnode->data, BALSA_MAILBOX_NODE(gnode->data)->name
	       ? BALSA_MAILBOX_NODE(gnode->data)->name : "");
    gtk_clist_freeze(GTK_CLIST(balsa_app.mblist));
    for(walk = g_node_first_child(gnode); walk; walk = next_sibling) {
        BalsaMailboxNode *mbnode = BALSA_MAILBOX_NODE(walk->data);
        next_sibling = g_node_next_sibling(walk);
        if(mbnode==NULL) continue;
        if(mbnode->parent == NULL) {
            printf("sparing %s %s\n", 
                   mbnode->mailbox ? "mailbox" : "folder ",
                   mbnode->mailbox ? mbnode->mailbox->name : mbnode->name);
            continue;
        }
        g_node_traverse(walk, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                        (GNodeTraverseFunc)destroy_mailbox_node, gnode);
	g_node_unlink(walk);
	g_node_destroy(walk);
    }
    gtk_clist_thaw(GTK_CLIST(balsa_app.mblist));
}

/* create_label:
   Create a label and add it to a table in the first column of given row,
   setting the keyval to found accelerator value, that can be later used 
   in create_entry.
*/
GtkWidget *
create_label(const gchar * label, GtkWidget * table, gint row)
{
    GtkWidget *w = gtk_label_new_with_mnemonic(label);

    gtk_misc_set_alignment(GTK_MISC(w), 1.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
		     GTK_FILL, GTK_FILL, 5, 5);
    gtk_widget_show(w);
    return w;
}

/* create_check:
   creates a checkbox with a given label and places them in given array.
*/
GtkWidget *
create_check(GtkDialog *mcw, const gchar *label, GtkWidget *table, gint row,
             gboolean initval)
{
    GtkWidget *cb, *l;
    
    cb = gtk_check_button_new();

    l = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), cb);
    gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
    gtk_widget_show(l);

    gtk_container_add(GTK_CONTAINER(cb), l);

    gtk_table_attach(GTK_TABLE(table), cb, 1, 2, row, row+1,
		     GTK_FILL, GTK_FILL, 5, 5);

    if(initval) 	
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), TRUE);

    return cb;
}

/* Create a text entry and add it to the table */
GtkWidget *
create_entry(GtkDialog *mcw, GtkWidget * table, 
	     GtkSignalFunc changed_func, gpointer data, gint row, 
	     const gchar * initval, GtkWidget* hotlabel)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);
    if (initval)
	gtk_entry_append_text(GTK_ENTRY(entry), initval);

    gtk_label_set_mnemonic_widget(GTK_LABEL(hotlabel), entry);
#if TO_BE_PORTED
    gnome_dialog_editable_enters(mcw, GTK_EDITABLE(entry));
#endif
    /* Watch for changes... */
    if(changed_func)
	gtk_signal_connect(GTK_OBJECT(entry), "changed", 
			   changed_func, data);

    gtk_widget_show(entry);
    return entry;
}

/* balsa_find_index_by_mailbox:
   returns BalsaIndex displaying passed mailbox, or NULL, if mailbox is 
   not displayed.
*/
BalsaIndex*
balsa_find_index_by_mailbox(LibBalsaMailbox * mailbox)
{
    GtkWidget *index;
    guint i;
    g_return_val_if_fail(balsa_app.notebook, NULL);

    for (i = 0;
	 (index =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {
        
	if (BALSA_INDEX(index)->mailbox_node != NULL
            && BALSA_INDEX(index)->mailbox_node->mailbox == mailbox)
	    return BALSA_INDEX(index);
    }

    /* didn't find a matching mailbox */
    return NULL;
}
#ifdef BALSA_USE_THREADS
/* balsa_mailbox_nodes_(un)lock:
   locks/unlocks balsa_app.mailbox_nodes structure so we can modify it
   from a thread.
   exclusive asks for exclusive access.
   NO-OPs in the non-MT build.
*/
static pthread_mutex_t mailbox_nodes_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  mailbox_nodes_cond = PTHREAD_COND_INITIALIZER;

/* a list of outstanding locks */
static GList *nodes_lock_list;

/* what we need to know about a lock */
struct _NodesLockItem {
    gboolean exclusive;
    pthread_t id;
};
typedef struct _NodesLockItem NodesLockItem;

/*
 * balsa_mailbox_nodes_lock
 *
 * requests a lock
 */
void
balsa_mailbox_nodes_lock(gboolean exclusive)
{
    GList *list;
    NodesLockItem *nli;
    pthread_t id = pthread_self();
    gboolean check = TRUE;

    pthread_mutex_lock(&mailbox_nodes_lock);
    while (check) {
        check = FALSE;
        /* if anyone else has a lock, and either we're asking for an
         * exclusive lock or they hold an exclusive lock, we need to
         * wait until someone gives up a lock, and then recheck all the
         * outstanding locks */
        for (list = nodes_lock_list; list; list = g_list_next(list)) {
            nli = list->data;
            if (nli->id != id && (exclusive || nli->exclusive)) {
                pthread_cond_wait(&mailbox_nodes_cond, &mailbox_nodes_lock);
                check = TRUE;
            }
        }
    }
    /* we're the only thread with locks, so we'll just add this one
     * to the list */
    nli = g_new(NodesLockItem, 1);
    nli->id = id;
    nli->exclusive = exclusive;
    nodes_lock_list = g_list_prepend(nodes_lock_list, nli);
    pthread_mutex_unlock(&mailbox_nodes_lock);
}

/*
 * balsa_mailbox_nodes_unlock
 *
 * give up a lock
 */
void
balsa_mailbox_nodes_unlock(gboolean exclusive)
{
    GList *list;
    NodesLockItem *nli = NULL;
    pthread_t id = pthread_self();

    pthread_mutex_lock(&mailbox_nodes_lock);
    for (list = nodes_lock_list; list; list = g_list_next(list)) {
        nli = list->data;
        if (nli->id == id) {
            if (nli->exclusive == exclusive)
                break;
            else
                g_warning("Unlocking an incorrectly nested mailbox_nodes 
lock");
        }
    }

    if (nli) {
        nodes_lock_list = g_list_remove(nodes_lock_list, nli);
        g_free(nli);
        pthread_cond_signal(&mailbox_nodes_cond);
    } else         g_warning("No mailbox_nodes lock to unlock");
    pthread_mutex_unlock(&mailbox_nodes_lock);
}
#else
void
balsa_mailbox_nodes_lock(gboolean exclusive) {}
void
balsa_mailbox_nodes_unlock(gboolean exclusive) {}
#endif
