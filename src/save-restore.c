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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "save-restore.h"

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include "balsa-app.h"
#include "server.h"
#include "quote-color.h"
#include "toolbar-prefs.h"

#include "filter.h"
#include "filter-file.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"
#include "libbalsa-conf.h"
#include "net-client-utils.h"
#include "geometry-manager.h"

#include "smtp-server.h"
#include "send.h"

#define FOLDER_SECTION_PREFIX "folder-"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
#define IDENTITY_SECTION_PREFIX "identity-"
#define VIEW_SECTION_PREFIX "view-"
#define SMTP_SERVER_SECTION_PREFIX "smtp-server-"

static gint config_global_load(void);
static gint config_folder_init(const gchar * group);
static gint config_mailbox_init(const gchar * group);
static gchar *config_get_unused_group(const gchar * group);

static void save_color(const gchar * key, GdkRGBA * rgba);
static void load_color(const gchar * name,
                       const gchar * def_val,
                       GdkRGBA     * rgba);
static void save_mru(GList  *mru, const gchar * group);
static void load_mru(GList **mru, const gchar * group);

static void config_address_books_save(void);
static void config_identities_load(void);

static void config_filters_load(void);

static inline gboolean is_special_name(const gchar *name);

static gchar *
mailbox_section_path(LibBalsaMailbox * mailbox)
{
    const gchar *config_prefix =
        libbalsa_mailbox_get_config_prefix(mailbox);

    return config_prefix != NULL ?
        g_strdup(config_prefix) :
        config_get_unused_group(MAILBOX_SECTION_PREFIX);
}

static gchar *
folder_section_path(BalsaMailboxNode * mbnode)
{
    const gchar *config_prefix =
        balsa_mailbox_node_get_config_prefix(mbnode);

    return config_prefix != NULL ?
        g_strdup(config_prefix) :
        config_get_unused_group(FOLDER_SECTION_PREFIX);
}

static gchar *
address_book_section_path(LibBalsaAddressBook *address_book)
{
    const gchar *config_prefix = libbalsa_address_book_get_config_prefix(address_book);

    return config_prefix != NULL ? g_strdup(config_prefix) :
        config_get_unused_group(ADDRESS_BOOK_SECTION_PREFIX);
}

gint config_load(void)
{
    return config_global_load();
}

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */

static gboolean
config_load_section(const gchar * key, const gchar * value, gpointer data)
{
    gint(*cb) (const gchar *) = data;

    cb(key);                    /* FIXME do something about error?? */

    return FALSE;
}

static gboolean
migrate_imap_mailboxes(const gchar *key, const gchar *value, gpointer data)
{
	gchar *type;
	GHashTable *migrated = (GHashTable *) data;

    libbalsa_conf_push_group(key);
	type = libbalsa_conf_get_string("Type");
	if ((type != NULL) && (strcmp(type, "LibBalsaMailboxImap") == 0) && !libbalsa_conf_get_bool("Migrated")) {
		BalsaMailboxNode *mbnode;

		/* try to load the IMAP mailbox as folder */
		libbalsa_conf_pop_group();
		mbnode = balsa_mailbox_node_new_from_config(key);
	    if (mbnode != NULL) {
                LibBalsaServer *server = balsa_mailbox_node_get_server(mbnode);
	    	gchar *folder_key;
	    	gchar *oldname;

	    	/* do not add the same folder multiple times */
	    	folder_key = g_strconcat(libbalsa_server_get_user(server), "@",
                                         libbalsa_server_get_host(server), NULL);
	    	oldname = g_strdup(balsa_mailbox_node_get_name(mbnode));
	    	if (g_hash_table_contains(migrated, folder_key)) {
	    		g_object_unref(mbnode);
	    		g_free(folder_key);
	    	} else {
                        gchar *tmp;

	    		g_hash_table_add(migrated, folder_key);
		    	balsa_mailbox_node_set_name(mbnode, libbalsa_server_get_host(server));
		    	balsa_mblist_mailbox_node_append(NULL, mbnode);

		    	tmp =  config_get_unused_group(FOLDER_SECTION_PREFIX);
		    	balsa_mailbox_node_set_config_prefix(mbnode, tmp);
                        g_free(tmp);

		    	g_signal_connect_swapped(balsa_mailbox_node_get_server(mbnode),
                                                 "config-changed",
                                                 G_CALLBACK(config_folder_update),
                                                 mbnode);
	    	}

	    	if (!is_special_name(value)) {
				libbalsa_conf_remove_group(key);
				libbalsa_conf_private_remove_group(key);
				libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
						"Migrated IMAP mailbox “%s” to IMAP folder. Please review its configuration.", oldname);
			}
	    	g_free(oldname);
	    }
	} else {
		libbalsa_conf_pop_group();
	}
	g_free(type);

    return FALSE;
}

void
config_load_sections(void)
{
	GHashTable *migrated;

	/* hack - migrate all LibBalsaMailboxImap mailboxes to IMAP folders */
	migrated = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    libbalsa_conf_foreach_group(MAILBOX_SECTION_PREFIX, migrate_imap_mailboxes, migrated);
    g_hash_table_destroy(migrated);

    libbalsa_conf_foreach_group(FOLDER_SECTION_PREFIX,
                                config_load_section,
                                config_folder_init);
    libbalsa_conf_foreach_group(MAILBOX_SECTION_PREFIX,
                                config_load_section,
                                config_mailbox_init);
    balsa_app.inbox_input = g_list_reverse(balsa_app.inbox_input);
}

static gint
d_get_gint(const gchar * key, gint def_val)
{
    gint def;
    gint res = libbalsa_conf_get_int_with_default(key, &def);
    return def ? def_val : res;
}

/* save/load_toolbars:
   handle customized toolbars for main/message preview and compose windows.
*/

static void
save_toolbars(void)
{
    libbalsa_conf_remove_group("Toolbars");
    libbalsa_conf_push_group("Toolbars");
    libbalsa_conf_set_int("WrapButtonText",
                         balsa_app.toolbar_wrap_button_text);
    libbalsa_conf_pop_group();
}

static void
load_toolbars(void)
{
    libbalsa_conf_push_group("Toolbars");
    balsa_app.toolbar_wrap_button_text = d_get_gint("WrapButtonText", 1);
    libbalsa_conf_pop_group();
}

/* config_mailbox_set_as_special:
   allows to set given mailboxe as one of the special mailboxes
   PS: I am not sure if I should add outbox to the list.
   specialNames must be in sync with the specialType definition.

   WARNING: may destroy mailbox.
*/
#define INBOX_NAME   "Inbox"
#define SENTBOX_NAME "Sentbox"
#define DRAFTS_NAME  "Draftbox"
#define OUTBOX_NAME  "Outbox"
#define TRASH_NAME   "Trash"

static void
sr_special_notify(gpointer data, GObject * mailbox)
{
    LibBalsaMailbox **special = data;

    if (special == &balsa_app.trash && !balsa_app.mblist_tree_store
        && balsa_app.empty_trash_on_exit)
        empty_trash(balsa_app.main_window);

    *special = NULL;
}

static gchar *specialNames[] = {
    N_(INBOX_NAME),
    N_(SENTBOX_NAME),
    N_(TRASH_NAME),
    N_(DRAFTS_NAME),
    N_(OUTBOX_NAME)
};

static inline gboolean
is_special_name(const gchar *name)
{
	guint n;
	gboolean res = FALSE;

	for (n = 0U; !res && (n < G_N_ELEMENTS(specialNames)); n++) {
		res = strcmp(name, specialNames[n]) == 0;
	}
	return res;
}

void
config_mailbox_set_as_special(LibBalsaMailbox * mailbox, specialType which)
{
    LibBalsaMailbox **special, *old_mailbox;
    BalsaMailboxNode *mbnode;

    g_return_if_fail(mailbox != NULL);

    switch (which) {
    case SPECIAL_INBOX:
	special = &balsa_app.inbox;
	break;
    case SPECIAL_SENT:
	special = &balsa_app.sentbox;
	break;
    case SPECIAL_TRASH:
	special = &balsa_app.trash;
	break;
    case SPECIAL_DRAFT:
	special = &balsa_app.draftbox;
	break;
    default:
	return;
    }
    if ((old_mailbox = *special) != NULL) {
        gchar *basename;

        *special = NULL;
        libbalsa_mailbox_set_config_prefix(old_mailbox, NULL);

        basename = g_path_get_basename(libbalsa_mailbox_get_url(old_mailbox));
        libbalsa_mailbox_set_name(old_mailbox, basename);
        g_free(basename);

        if (!LIBBALSA_IS_MAILBOX_LOCAL(old_mailbox)
            || !libbalsa_path_is_below_dir(libbalsa_mailbox_local_get_path
                                           ((LibBalsaMailboxLocal *) old_mailbox),
                                           balsa_app.local_mail_directory))
            config_mailbox_add(old_mailbox, NULL);

        g_object_weak_unref(G_OBJECT(old_mailbox),
                            (GWeakNotify) sr_special_notify, special);

        mbnode = balsa_find_mailbox(old_mailbox);
	balsa_mblist_mailbox_node_redraw(mbnode);
	g_object_unref(mbnode);
    }
    config_mailbox_delete(mailbox);
    libbalsa_mailbox_set_name(mailbox, _(specialNames[which]));
    config_mailbox_add(mailbox, specialNames[which]);

    *special = mailbox;
    g_object_weak_ref(G_OBJECT(mailbox), (GWeakNotify) sr_special_notify,
                      special);

    mbnode = balsa_find_mailbox(mailbox);
    balsa_mblist_mailbox_node_redraw(mbnode);
    g_object_unref(mbnode);

    switch(which) {
    case SPECIAL_SENT: 
	balsa_mblist_mru_add(&balsa_app.fcc_mru, libbalsa_mailbox_get_url(mailbox));
        break;
    case SPECIAL_TRASH:
        libbalsa_filters_set_trash(balsa_app.trash); break;
    default: break;
    }
}

void
config_address_book_save(LibBalsaAddressBook * ab)
{
    gchar *group;

    group = address_book_section_path(ab);

    libbalsa_address_book_save_config(ab, group);

    g_free(group);

    libbalsa_conf_queue_sync();
}

void
config_address_book_delete(LibBalsaAddressBook * ab)
{
    const gchar *config_prefix = libbalsa_address_book_get_config_prefix(ab);

    if (config_prefix != NULL) {
	libbalsa_conf_remove_group(config_prefix);
	libbalsa_conf_private_remove_group(config_prefix);
	libbalsa_conf_queue_sync();
    }
}

/* config_mailbox_add:
   adds the specifed mailbox to the configuration. If the mailbox does
   not have the unique pkey assigned, find one.
*/
gint config_mailbox_add(LibBalsaMailbox * mailbox, const char *key_arg)
{
    gchar *tmp;
    g_return_val_if_fail(mailbox, FALSE);

    if (key_arg == NULL)
	tmp = mailbox_section_path(mailbox);
    else
	tmp = g_strdup_printf(MAILBOX_SECTION_PREFIX "%s", key_arg);

    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_push_group(tmp);
    libbalsa_mailbox_save_config(mailbox, tmp);
    libbalsa_conf_pop_group();
    g_free(tmp);

    libbalsa_conf_queue_sync();
    return TRUE;
}				/* config_mailbox_add */

/* config_folder_add:
   adds the specifed folder to the configuration. If the folder does
   not have the unique pkey assigned, find one.
*/
gint config_folder_add(BalsaMailboxNode * mbnode, const char *key_arg)
{
    gchar *tmp;
    g_return_val_if_fail(mbnode, FALSE);

    if (key_arg == NULL)
	tmp = folder_section_path(mbnode);
    else
	tmp = g_strdup_printf(FOLDER_SECTION_PREFIX "%s", key_arg);

    libbalsa_conf_push_group(tmp);
    balsa_mailbox_node_save_config(mbnode, tmp);
    libbalsa_conf_pop_group();
    g_free(tmp);

    libbalsa_conf_queue_sync();
    return TRUE;
}				/* config_mailbox_add */

/* removes from the configuration only */
gint
config_mailbox_delete(LibBalsaMailbox * mailbox)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = mailbox_section_path(mailbox);
    res = libbalsa_conf_has_group(tmp);
    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_queue_sync();
    g_free(tmp);
    return res;
}				/* config_mailbox_delete */

gint
config_folder_delete(BalsaMailboxNode * mbnode)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = folder_section_path(mbnode);
    res = libbalsa_conf_has_group(tmp);
    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_queue_sync();
    g_free(tmp);
    return res;
}				/* config_folder_delete */

/* Update the configuration information for the specified mailbox. */
gint config_mailbox_update(LibBalsaMailbox * mailbox)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = mailbox_section_path(mailbox);
    res = libbalsa_conf_has_group(key);
    libbalsa_conf_remove_group(key);
    libbalsa_conf_private_remove_group(key);
    libbalsa_conf_push_group(key);
    libbalsa_mailbox_save_config(mailbox, key);
    g_free(key);
    libbalsa_conf_pop_group();
    libbalsa_conf_queue_sync();
    return res;
}				/* config_mailbox_update */

/* Update the configuration information for the specified folder. */
gint config_folder_update(BalsaMailboxNode * mbnode)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = folder_section_path(mbnode);
    res = libbalsa_conf_has_group(key);
    libbalsa_conf_remove_group(key);
    libbalsa_conf_private_remove_group(key);
    libbalsa_conf_push_group(key);
    balsa_mailbox_node_save_config(mbnode, key);
    g_free(key);
    libbalsa_conf_pop_group();
    libbalsa_conf_queue_sync();
    return res;
}				/* config_folder_update */


/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init(const gchar * prefix)
{
    LibBalsaMailbox *mailbox;
    const gchar *key;

    g_return_val_if_fail(prefix != NULL, FALSE);
    key = prefix + strlen(MAILBOX_SECTION_PREFIX);

    mailbox = libbalsa_mailbox_new_from_config(prefix, is_special_name(key));
    if (mailbox == NULL)
	return FALSE;
    if (LIBBALSA_IS_MAILBOX_REMOTE(mailbox)) {
	LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
        libbalsa_server_connect_signals(server,
                                        G_CALLBACK(ask_password), NULL);
	g_signal_connect_swapped(server, "config-changed",
                                 G_CALLBACK(config_mailbox_update),
				 mailbox);
    }

    if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	balsa_app.inbox_input =
            g_list_prepend(balsa_app.inbox_input,
                           balsa_mailbox_node_new_from_mailbox(mailbox));
    } else {
        LibBalsaMailbox **special = NULL;
	BalsaMailboxNode *mbnode;

	mbnode = balsa_mailbox_node_new_from_mailbox(mailbox);
	if (strcmp(INBOX_NAME, key) == 0)
	    special = &balsa_app.inbox;
	else if (strcmp(OUTBOX_NAME, key) == 0) {
	    special = &balsa_app.outbox;
            libbalsa_mailbox_set_no_reassemble(mailbox, TRUE);
        } else if (strcmp(SENTBOX_NAME, key) == 0)
	    special = &balsa_app.sentbox;
	else if (strcmp(DRAFTS_NAME, key) == 0)
	    special = &balsa_app.draftbox;
	else if (strcmp(TRASH_NAME, key) == 0) {
	    special = &balsa_app.trash;
            libbalsa_filters_set_trash(mailbox);
	}

	if (special) {
	    /* Designate as special before appending to the mblist, so
	     * that view->show gets set correctly for sentbox, draftbox,
	     * and outbox, and view->subscribe gets set correctly for
             * trashbox. */
	    *special = mailbox;
            g_object_weak_ref(G_OBJECT(mailbox),
                              (GWeakNotify) sr_special_notify, special);
            if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
            	/* IMAP folder, used as special mailbox: remember that we migrated it */
            	libbalsa_mailbox_save_config(mailbox, prefix);
            }
	}

        balsa_mblist_mailbox_node_append(NULL, mbnode);
    }
    return TRUE;
}				/* config_mailbox_init */


/* Initialize the specified folder, creating the internal data
   structures which represent the folder. */
static gint
config_folder_init(const gchar * prefix)
{
    BalsaMailboxNode *folder;

    g_return_val_if_fail(prefix != NULL, FALSE);

    if( (folder = balsa_mailbox_node_new_from_config(prefix)) ) {
	g_signal_connect_swapped(balsa_mailbox_node_get_server(folder),
                                 "config-changed",
                                 G_CALLBACK(config_folder_update),
				 folder);
	balsa_mblist_mailbox_node_append(NULL, folder);
    }

    return folder != NULL;
}				/* config_folder_init */

/* Load Balsa's global settings */
static gboolean
config_warning_idle(const gchar * text)
{
    balsa_information(LIBBALSA_INFORMATION_WARNING, "%s", text);
    return FALSE;
}

static gboolean
config_load_smtp_server(const gchar * key, const gchar * value, gpointer data)
{
    GSList **smtp_servers = data;
    LibBalsaSmtpServer *smtp_server;

    libbalsa_conf_push_group(key);
    smtp_server = libbalsa_smtp_server_new_from_config(value);
    libbalsa_server_connect_signals(LIBBALSA_SERVER(smtp_server),
				    G_CALLBACK(ask_password), NULL);
    libbalsa_conf_pop_group();
    libbalsa_smtp_server_add_to_list(smtp_server, smtp_servers);

    return FALSE;
}

static gboolean
load_gtk_print_setting(const gchar * key, const gchar * value, gpointer data)
{
    g_return_val_if_fail(data != NULL, TRUE);
    gtk_print_settings_set((GtkPrintSettings *)data, key, value);
    return FALSE;
}

static GtkPageSetup *
restore_gtk_page_setup()
{
    GtkPageSetup *page_setup;
    GtkPaperSize *paper_size = NULL;
    gdouble width;
    gdouble height;
    gchar *name;
    gchar *ppd_name;
    gchar *display_name;

    width = libbalsa_conf_get_double("Width");
    height = libbalsa_conf_get_double("Height");

    name = libbalsa_conf_get_string("PaperName");
    ppd_name = libbalsa_conf_get_string ("PPDName");
    display_name = libbalsa_conf_get_string("DisplayName");
    page_setup = gtk_page_setup_new();

    if (ppd_name && *ppd_name)
	paper_size = gtk_paper_size_new_from_ppd(ppd_name, display_name,
						 width, height);
    else if (name && *name)
	paper_size = gtk_paper_size_new_custom(name, display_name,
					       width, height, GTK_UNIT_MM);

    /* set defaults if no info is available */
    if (!paper_size) {
	paper_size = gtk_paper_size_new(GTK_PAPER_NAME_A4);
	gtk_page_setup_set_paper_size_and_default_margins(page_setup,
							  paper_size);
    } else {
	gtk_page_setup_set_paper_size(page_setup, paper_size);

	gtk_page_setup_set_top_margin(page_setup,
				      libbalsa_conf_get_double("MarginTop"),
				      GTK_UNIT_MM);
	gtk_page_setup_set_bottom_margin(page_setup,
					 libbalsa_conf_get_double("MarginBottom"),
					 GTK_UNIT_MM);
	gtk_page_setup_set_left_margin(page_setup,
				       libbalsa_conf_get_double("MarginLeft"),
				       GTK_UNIT_MM);
	gtk_page_setup_set_right_margin(page_setup,
					libbalsa_conf_get_double("MarginRight"),
					GTK_UNIT_MM);
	gtk_page_setup_set_orientation(page_setup,
				       (GtkPageOrientation)
				       libbalsa_conf_get_int("Orientation"));
    }
    
    gtk_paper_size_free(paper_size);
    g_free(ppd_name);
    g_free(name);
    g_free(display_name);
    
    return page_setup;
}

static gint
config_global_load(void)
{
    gboolean def_used;
    guint filter_mask;
    static gboolean new_user = FALSE;

    config_address_books_load();

    /* Load SMTP servers before identities. */
    libbalsa_conf_foreach_group(SMTP_SERVER_SECTION_PREFIX,
	                        config_load_smtp_server,
	                        &balsa_app.smtp_servers);

    /* We must load filters before mailboxes, because they refer to the filters list */
    config_filters_load();
    if (filter_errno!=FILTER_NOERR) {
	filter_perror(_("Error during filters loading: "));
 	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Error during filters loading: %s\n"
			       "Filters may not be correct."),
 			     filter_strerror(filter_errno));
    }

    /* Section for the balsa_information() settings... */
    libbalsa_conf_push_group("InformationMessages");

    balsa_app.information_message =
	d_get_gint("ShowInformationMessages", BALSA_INFORMATION_SHOW_BAR);
    balsa_app.warning_message =
	d_get_gint("ShowWarningMessages", BALSA_INFORMATION_SHOW_LIST);
    balsa_app.error_message =
	d_get_gint("ShowErrorMessages", BALSA_INFORMATION_SHOW_DIALOG);
    balsa_app.debug_message =
	d_get_gint("ShowDebugMessages", BALSA_INFORMATION_SHOW_NONE);
    balsa_app.fatal_message = 
	d_get_gint("ShowFatalMessages", BALSA_INFORMATION_SHOW_DIALOG);

    libbalsa_conf_pop_group();

    /* Section for geometry ... */
    libbalsa_conf_push_group("Geometry");

    /* ... column width settings */
    balsa_app.mblist_name_width =
	d_get_gint("MailboxListNameWidth", MBNAME_DEFAULT_WIDTH);
    balsa_app.mblist_newmsg_width =
	d_get_gint("MailboxListNewMsgWidth", NEWMSGCOUNT_DEFAULT_WIDTH);
    balsa_app.mblist_totalmsg_width =
	d_get_gint("MailboxListTotalMsgWidth",
		   TOTALMSGCOUNT_DEFAULT_WIDTH);
    balsa_app.index_num_width =
	d_get_gint("IndexNumWidth", NUM_DEFAULT_WIDTH);
    balsa_app.index_status_width =
	d_get_gint("IndexStatusWidth", STATUS_DEFAULT_WIDTH);
    balsa_app.index_attachment_width =
	d_get_gint("IndexAttachmentWidth", ATTACHMENT_DEFAULT_WIDTH);
    balsa_app.index_from_width =
	d_get_gint("IndexFromWidth", FROM_DEFAULT_WIDTH);
    balsa_app.index_subject_width =
	d_get_gint("IndexSubjectWidth", SUBJECT_DEFAULT_WIDTH);
    balsa_app.index_date_width =
	d_get_gint("IndexDateWidth", DATE_DEFAULT_WIDTH);
    balsa_app.index_size_width =
	d_get_gint("IndexSizeWidth", SIZE_DEFAULT_WIDTH);

    /* ... window sizes */
    geometry_manager_init("MainWindow", 640, 480, FALSE);
    balsa_app.mblist_width = libbalsa_conf_get_int("MailboxListWidth=130");
    geometry_manager_init("SendMsgWindow", 640, 480, FALSE);
    geometry_manager_init("SelectReplyParts", 400, 200, FALSE);
    geometry_manager_init("MessageWindow", 400, 500, FALSE);
    geometry_manager_init("SourceView", 500, 400, FALSE);
    geometry_manager_init("IMAPSubscriptions", 200, 160, FALSE);
    geometry_manager_init("IMAPSelectParent", 200, 160, FALSE);
    geometry_manager_init("KeyDialog", 400, 200, FALSE);
    geometry_manager_init("KeyList", 300, 200, FALSE);
#ifdef HAVE_HTML_WIDGET
    geometry_manager_init("HTMLPrefsDB", 300, 200, FALSE);
#endif
#ifdef ENABLE_AUTOCRYPT
    geometry_manager_init("AutocryptDB", 300, 200, FALSE);
#endif  /* ENABLE_AUTOCRYPT */
    geometry_manager_init("CertChain", 300, 200, FALSE);

    /* FIXME: PKGW: why comment this out? Breaks my Transfer context menu. */
    if (balsa_app.mblist_width < 100)
	balsa_app.mblist_width = 170;

    balsa_app.notebook_height = libbalsa_conf_get_int("NotebookHeight=170");
    /*FIXME: Why is this here?? */
    if (balsa_app.notebook_height < 100)
	balsa_app.notebook_height = 200;

    libbalsa_conf_pop_group();

    /* Message View options ... */
    libbalsa_conf_push_group("MessageDisplay");

    /* ... How we format dates */
    g_free(balsa_app.date_string);
    balsa_app.date_string =
	libbalsa_conf_get_string("DateFormat=" DEFAULT_DATE_FORMAT);

    /* ... Headers to show */
    balsa_app.shown_headers = d_get_gint("ShownHeaders", HEADERS_SELECTED);

    g_free(balsa_app.selected_headers);
    {                           /* scope */
        gchar *selected_headers =
            libbalsa_conf_get_string("SelectedHeaders="
                                     DEFAULT_SELECTED_HDRS);
        balsa_app.selected_headers = g_ascii_strdown(selected_headers, -1);
        g_free(selected_headers);
    }

    balsa_app.expand_tree = libbalsa_conf_get_bool("ExpandTree=false");

    {                             /* scope */
        guint type =
            libbalsa_conf_get_int_with_default("SortField", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_sort_field(NULL, type);
        type =
            libbalsa_conf_get_int_with_default("ThreadingType", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_threading_type(NULL, type);
    }

    /* ... Quote colouring */
    balsa_app.mark_quoted =
        libbalsa_conf_get_bool_with_default("MarkQuoted=true", &def_used);
    g_free(balsa_app.quote_regex);
    balsa_app.quote_regex =
	libbalsa_conf_get_string("QuoteRegex=" DEFAULT_QUOTE_REGEX);

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("RecognizeRFC2646FormatFlowed=true",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option not to recognize “format=flowed” "
                     "text has been removed."));

    {
	int i;
	gchar *default_quoted_color[MAX_QUOTED_COLOR] = {"#088", "#800"};
	for(i=0;i<MAX_QUOTED_COLOR;i++) {
            gchar *key;
            const gchar *def_val;

            key = g_strdup_printf("QuotedColor%d", i);
            def_val = default_quoted_color[i];
            load_color(key, def_val, &balsa_app.quoted_color[i]);
            g_free(key);
	}
    }

    /* URL coloring */
    load_color("UrlColor", DEFAULT_URL_COLOR, &balsa_app.url_color);

    /* ... font used to display messages */
    balsa_app.use_system_fonts =
        libbalsa_conf_get_bool("UseSystemFonts=true");
    g_free(balsa_app.message_font);
    balsa_app.message_font =
	libbalsa_conf_get_string("MessageFont=" DEFAULT_MESSAGE_FONT);
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
	libbalsa_conf_get_string("SubjectFont=" DEFAULT_SUBJECT_FONT);

    /* ... wrap words */
    balsa_app.browse_wrap = libbalsa_conf_get_bool("WordWrap=false");
    balsa_app.browse_wrap_length = libbalsa_conf_get_int("WordWrapLength=79");
    if (balsa_app.browse_wrap_length < 40)
	balsa_app.browse_wrap_length = 40;

    /* ... handling of Multipart/Alternative */
    balsa_app.display_alt_plain = 
	libbalsa_conf_get_bool("DisplayAlternativeAsPlain=true");

    /* ... handling of broken mails with 8-bit chars */
    balsa_app.convert_unknown_8bit = 
	libbalsa_conf_get_bool("ConvertUnknown8Bit=false");
    balsa_app.convert_unknown_8bit_codeset =
	libbalsa_conf_get_int("ConvertUnknown8BitCodeset=" DEFAULT_BROKEN_CODESET);
    libbalsa_set_fallback_codeset(balsa_app.convert_unknown_8bit_codeset);
    libbalsa_conf_pop_group();

    /* Interface Options ... */
    libbalsa_conf_push_group("Interface");

    /* ... interface elements to show */
    balsa_app.previewpane = libbalsa_conf_get_bool("ShowPreviewPane=true");
    balsa_app.show_mblist = libbalsa_conf_get_bool("ShowMailboxList=true");
    balsa_app.show_notebook_tabs = libbalsa_conf_get_bool("ShowTabs=false");

    /* ... alternative layout of main window */
    balsa_app.layout_type = libbalsa_conf_get_int("LayoutType=0");
    if (balsa_app.layout_type != LAYOUT_DEFAULT && 
        balsa_app.layout_type != LAYOUT_WIDE_MSG &&
        balsa_app.layout_type != LAYOUT_WIDE_SCREEN)
        balsa_app.layout_type = LAYOUT_DEFAULT;
    balsa_app.view_message_on_open = libbalsa_conf_get_bool("ViewMessageOnOpen=true");
    balsa_app.ask_before_select = libbalsa_conf_get_bool("AskBeforeSelect=false");
    balsa_app.pgdownmod = libbalsa_conf_get_bool("PageDownMod=false");
    balsa_app.pgdown_percent = libbalsa_conf_get_int("PageDownPercent=50");
    if (balsa_app.pgdown_percent < 10)
	balsa_app.pgdown_percent = 10;

    balsa_app.show_main_toolbar =
        libbalsa_conf_get_bool("ShowMainWindowToolbar=true");
    balsa_app.show_message_toolbar =
        libbalsa_conf_get_bool("ShowMessageWindowToolbar=true");
    balsa_app.show_compose_toolbar =
        libbalsa_conf_get_bool("ShowComposeWindowToolbar=true");
    balsa_app.show_statusbar =
        libbalsa_conf_get_bool("ShowStatusbar=true");
    balsa_app.show_sos_bar =
        libbalsa_conf_get_bool("ShowSOSbar=true");

    /* ... Progress Window Dialogs */
	balsa_app.recv_progress_dialog = libbalsa_conf_get_bool("ShowRecvProgressDlg=true");
    balsa_app.send_progress_dialog = libbalsa_conf_get_bool("ShowSendProgressDlg=true");

    /* ... deleting messages: defaults enshrined here */
    filter_mask = libbalsa_mailbox_get_filter(NULL);
    if (libbalsa_conf_get_bool("HideDeleted=true"))
	filter_mask |= (1 << 0);
    else
	filter_mask &= ~(1 << 0);
    libbalsa_mailbox_set_filter(NULL, filter_mask);

    balsa_app.expunge_on_close =
        libbalsa_conf_get_bool("ExpungeOnClose=true");
    balsa_app.expunge_auto = libbalsa_conf_get_bool("AutoExpunge=true");
    /* timeout in munutes (was hours, so fall back if needed) */
    balsa_app.expunge_timeout =
        libbalsa_conf_get_int("AutoExpungeMinutes") * 60;
    if (!balsa_app.expunge_timeout)
	balsa_app.expunge_timeout = 
	    libbalsa_conf_get_int("AutoExpungeHours=2") * 3600;

    balsa_app.mw_action_after_move = libbalsa_conf_get_int(
        "MessageWindowActionAfterMove");

    libbalsa_conf_pop_group();

    /* Source browsing ... */
    libbalsa_conf_push_group("SourcePreview");
    balsa_app.source_escape_specials = 
        libbalsa_conf_get_bool("EscapeSpecials=true");
    libbalsa_conf_pop_group();

    /* MRU mailbox tree ... */
    libbalsa_conf_push_group("MruTree");
    balsa_app.mru_tree_width  = libbalsa_conf_get_int("Width=150");
    balsa_app.mru_tree_height = libbalsa_conf_get_int("Height=300");
    libbalsa_conf_pop_group();

    /* Printing options ... */
    libbalsa_conf_push_group("Printing");

    /* ... Printing */
    if (balsa_app.page_setup)
	g_object_unref(balsa_app.page_setup);
    balsa_app.page_setup = restore_gtk_page_setup();
    balsa_app.margin_left = libbalsa_conf_get_double("LeftMargin");
    balsa_app.margin_top = libbalsa_conf_get_double("TopMargin");
    balsa_app.margin_right = libbalsa_conf_get_double("RightMargin");
    balsa_app.margin_bottom = libbalsa_conf_get_double("BottomMargin");

    g_free(balsa_app.print_header_font);
    balsa_app.print_header_font =
        libbalsa_conf_get_string("PrintHeaderFont="
                                DEFAULT_PRINT_HEADER_FONT);
    g_free(balsa_app.print_footer_font);
    balsa_app.print_footer_font =
        libbalsa_conf_get_string("PrintFooterFont="
                                DEFAULT_PRINT_FOOTER_FONT);
    g_free(balsa_app.print_body_font);
    balsa_app.print_body_font =
        libbalsa_conf_get_string("PrintBodyFont="
                                DEFAULT_PRINT_BODY_FONT);
    balsa_app.print_highlight_cited =
        libbalsa_conf_get_bool("PrintHighlightCited=false");
    balsa_app.print_highlight_phrases =
        libbalsa_conf_get_bool("PrintHighlightPhrases=false");
    libbalsa_conf_pop_group();

    /* GtkPrint printing */
    libbalsa_conf_push_group("GtkPrint");
    libbalsa_conf_foreach_keys("GtkPrint", load_gtk_print_setting,
			       balsa_app.print_settings);
    libbalsa_conf_pop_group();

    /* Spelling options ... */
    libbalsa_conf_push_group("Spelling");

    balsa_app.spell_check_lang =
        libbalsa_conf_get_string("SpellCheckLanguage");
#if HAVE_GSPELL || HAVE_GTKSPELL
    balsa_app.spell_check_active =
        libbalsa_conf_get_bool_with_default("SpellCheckActive", &def_used);
    if (def_used)
        balsa_app.spell_check_active = balsa_app.spell_check_lang != NULL;
#else                           /* HAVE_GTKSPELL */
    balsa_app.check_sig =
	d_get_gint("SpellCheckSignature", DEFAULT_CHECK_SIG);
    balsa_app.check_quoted =
	d_get_gint("SpellCheckQuoted", DEFAULT_CHECK_QUOTED);
#endif                          /* HAVE_GTKSPELL */

    libbalsa_conf_pop_group();

    /* Mailbox checking ... */
    libbalsa_conf_push_group("MailboxList");

    /* ... show mailbox content info */
    balsa_app.mblist_show_mb_content_info =
	libbalsa_conf_get_bool("ShowMailboxContentInfo=false");

    libbalsa_conf_pop_group();

    /* Maibox checking options ... */
    libbalsa_conf_push_group("MailboxChecking");

    balsa_app.notify_new_mail_dialog =
	d_get_gint("NewMailNotificationDialog", 0);
#ifdef HAVE_CANBERRA
    balsa_app.notify_new_mail_sound =
	d_get_gint("NewMailNotificationSound", 0);
#endif
    balsa_app.check_mail_upon_startup =
	libbalsa_conf_get_bool("OnStartup=false");
    balsa_app.check_mail_auto = libbalsa_conf_get_bool("Auto=false");
    balsa_app.check_mail_timer = libbalsa_conf_get_int("AutoDelay=10");
    if (balsa_app.check_mail_timer < 1)
	balsa_app.check_mail_timer = 10;
    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    balsa_app.check_imap=d_get_gint("CheckIMAP", 1);
    balsa_app.check_imap_inbox=d_get_gint("CheckIMAPInbox", 0);
    balsa_app.quiet_background_check=d_get_gint("QuietBackgroundCheck", 0);
    balsa_app.msg_size_limit=d_get_gint("POPMsgSizeLimit", 20000);
    libbalsa_conf_pop_group();

    /* folder scanning */
    libbalsa_conf_push_group("FolderScanning");
    balsa_app.local_scan_depth = d_get_gint("LocalScanDepth", 1);
    balsa_app.imap_scan_depth = d_get_gint("ImapScanDepth", 1);
    libbalsa_conf_pop_group();

    /* how to react if a message with MDN request is displayed */
    libbalsa_conf_push_group("MDNReply");
    balsa_app.mdn_reply_clean = libbalsa_conf_get_int("Clean=1");
    balsa_app.mdn_reply_notclean = libbalsa_conf_get_int("Suspicious=0");
    libbalsa_conf_pop_group();

    /* Sending options ... */
    libbalsa_conf_push_group("Sending");

    /* ... SMTP servers */
    if (!balsa_app.smtp_servers) {
	/* Transition code */
	LibBalsaSmtpServer *smtp_server;
	LibBalsaServer *server;
	gchar *passphrase, *hostname;

	smtp_server = libbalsa_smtp_server_new();
	libbalsa_smtp_server_set_name(smtp_server,
		libbalsa_smtp_server_get_name(NULL));
	balsa_app.smtp_servers =
	    g_slist_prepend(NULL, smtp_server);
	server = LIBBALSA_SERVER(smtp_server);

        hostname = 
            libbalsa_conf_get_string_with_default("ESMTPServer=localhost:25", 
                                                  &def_used);
	libbalsa_server_set_host(server, hostname, FALSE);
        g_free(hostname);
	libbalsa_server_set_username(server,
		libbalsa_conf_get_string("ESMTPUser"));

        passphrase = libbalsa_conf_private_get_string("ESMTPPassphrase", TRUE);
	if (passphrase) {
            libbalsa_server_set_password(server, passphrase, FALSE);
            net_client_free_authstr(passphrase);
        }

	passphrase =
	    libbalsa_conf_private_get_string("ESMTPCertificatePassphrase", TRUE);
	if (passphrase) {
        libbalsa_server_set_password(server, passphrase, TRUE);
        net_client_free_authstr(passphrase);
	}
    }

    /* ... outgoing mail */
    balsa_app.wordwrap = libbalsa_conf_get_bool("WordWrap=false");
    balsa_app.wraplength = libbalsa_conf_get_int("WrapLength=72");
    if (balsa_app.wraplength < 40)
	balsa_app.wraplength = 40;

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("SendRFC2646FormatFlowed=true",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option not to send “format=flowed” text is now "
                     "on the Options menu of the compose window."));

    balsa_app.autoquote = 
	libbalsa_conf_get_bool("AutoQuote=true");
    balsa_app.send_mail_auto = libbalsa_conf_get_bool("AutoSend=true");
    balsa_app.send_mail_timer = libbalsa_conf_get_int("AutoSendDelay=15");
    if ((balsa_app.send_mail_timer < 1U) || (balsa_app.send_mail_timer > 120U)) {
    	balsa_app.send_mail_timer = 15U;
    }
    libbalsa_auto_send_config(balsa_app.send_mail_auto, balsa_app.send_mail_timer);

    balsa_app.reply_strip_html = 
	libbalsa_conf_get_bool("StripHtmlInReply=true");
    balsa_app.forward_attached = 
	libbalsa_conf_get_bool("ForwardAttached=true");

	balsa_app.always_queue_sent_mail = d_get_gint("AlwaysQueueSentMail", 0);
	balsa_app.copy_to_sentbox = d_get_gint("CopyToSentbox", 1);

    libbalsa_conf_pop_group();

    /* Compose window ... */
    libbalsa_conf_push_group("Compose");

    balsa_app.edit_headers = 
        libbalsa_conf_get_bool("ExternEditorEditHeaders=false");

    g_free(balsa_app.quote_str);
    balsa_app.quote_str = libbalsa_conf_get_string("QuoteString=> ");
    g_free(balsa_app.compose_headers);
    balsa_app.compose_headers =
	libbalsa_conf_get_string("ComposeHeaders=Recipients Subject");
    balsa_app.warn_reply_decrypted =
    	libbalsa_conf_get_bool("WarnReplyDecrypted=true");

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("RequestDispositionNotification=false",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option to request a MDN is now on "
                     "the Options menu of the compose window."));


    libbalsa_conf_pop_group();

    /* Global config options ... */
    libbalsa_conf_push_group("Globals");

    /* directory */
    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory = libbalsa_conf_get_string("MailDir");

    if(!balsa_app.local_mail_directory) {
	libbalsa_conf_pop_group();
        new_user = TRUE;
	return FALSE;
    }
    balsa_app.root_node =
        balsa_mailbox_node_new_from_dir(balsa_app.local_mail_directory);

     balsa_app.open_inbox_upon_startup =
	libbalsa_conf_get_bool("OpenInboxOnStartup=false");

    balsa_app.close_mailbox_auto = libbalsa_conf_get_bool("AutoCloseMailbox=true");
    /* timeouts in minutes in config file for backwards compat */
    balsa_app.close_mailbox_timeout = libbalsa_conf_get_int("AutoCloseMailboxTimeout=10") * 60;
    
    balsa_app.remember_open_mboxes =
	libbalsa_conf_get_bool("RememberOpenMailboxes=false");
    balsa_app.current_mailbox_url =
	libbalsa_conf_get_string("CurrentMailboxURL");

    balsa_app.empty_trash_on_exit =
	libbalsa_conf_get_bool("EmptyTrash=false");

    /* This setting is now per address book */
    libbalsa_conf_clean_key("AddressBookDistMode");

#ifdef ENABLE_SYSTRAY
    balsa_app.enable_systray_icon =
	d_get_gint("EnableSystrayIcon", 0);
#endif

    balsa_app.enable_dkim_checks = d_get_gint("EnableDkimChecks", 0);

    libbalsa_conf_pop_group();
    
    /* Toolbars */
    load_toolbars();

    /* Last used paths options ... */
    libbalsa_conf_push_group("Paths");
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = libbalsa_conf_get_string("AttachDir");
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = libbalsa_conf_get_string("SavePartDir");
    libbalsa_conf_pop_group();

	/* Folder MRU */
    load_mru(&balsa_app.folder_mru, "FolderMRU");

	/* FCC MRU */
    load_mru(&balsa_app.fcc_mru, "FccMRU");

    /* Pipe commands */
    load_mru(&balsa_app.pipe_cmds, "PipeCommands");
    if (!balsa_app.pipe_cmds)
        balsa_app.pipe_cmds = g_list_prepend(NULL, 
                                             g_strdup("sa-learn --spam"));

    /* We load identities at the end because they may refer to SMTP
     * servers. This is also critical when handling damaged config
     * files with no smtp server defined (think switching from
     * --without-esmtp build). */
    config_identities_load();

    if (!new_user) {
        /* Notify user about any changes */
        libbalsa_conf_push_group("Notifications");
        if (!libbalsa_conf_get_bool("GtkUIManager")) {
            g_idle_add((GSourceFunc) config_warning_idle,
                       _("This version of Balsa uses a new user interface; "
                         "if you have changed Balsa’s keyboard accelerators, "
                         "you will need to set them again."));
            libbalsa_conf_set_bool("GtkUIManager", TRUE);
        }
        if (!libbalsa_conf_get_bool("LibBalsaAddressView")) {
            /* No warning */
            if (!libbalsa_find_word("Recipients",
                                    balsa_app.compose_headers)) {
                gchar *compose_headers =
                    g_strconcat(balsa_app.compose_headers, " Recipients",
                                NULL);
                g_free(balsa_app.compose_headers);
                balsa_app.compose_headers = compose_headers;
            }
            libbalsa_conf_set_bool("LibBalsaAddressView", TRUE);
        }
        libbalsa_conf_pop_group();
    }

    return TRUE;
}				/* config_global_load */

static void
save_gtk_print_setting(const gchar *key, const gchar *value, gpointer user_data)
{
    libbalsa_conf_set_string(key, value);
}

static void
save_gtk_page_setup(GtkPageSetup *page_setup)
{
    GtkPaperSize *paper_size;

    if (!page_setup)
	return;

    paper_size = gtk_page_setup_get_paper_size(page_setup);
    g_assert(paper_size != NULL);

    libbalsa_conf_set_string("PaperName",
			     gtk_paper_size_get_name (paper_size));
    libbalsa_conf_set_string("DisplayName",
			     gtk_paper_size_get_display_name (paper_size));
    libbalsa_conf_set_string("PPDName",
			     gtk_paper_size_get_ppd_name (paper_size));

    libbalsa_conf_set_double("Width",
			     gtk_paper_size_get_width(paper_size,
						      GTK_UNIT_MM));
    libbalsa_conf_set_double("Height",
			     gtk_paper_size_get_height(paper_size,
						       GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginTop",
			     gtk_page_setup_get_top_margin(page_setup,
							   GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginBottom",
			     gtk_page_setup_get_bottom_margin(page_setup,
							      GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginLeft",
			     gtk_page_setup_get_left_margin(page_setup,
							    GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginRight",
			     gtk_page_setup_get_right_margin(page_setup,
							     GTK_UNIT_MM));
    libbalsa_conf_set_int("Orientation",
			  gtk_page_setup_get_orientation(page_setup));
}

gint
config_save(void)
{
    gint i;
    GSList *list;

    config_address_books_save();
    config_identities_save();

    /* Section for the balsa_information() settings... */
    libbalsa_conf_push_group("InformationMessages");
    libbalsa_conf_set_int("ShowInformationMessages",
			 balsa_app.information_message);
    libbalsa_conf_set_int("ShowWarningMessages", balsa_app.warning_message);
    libbalsa_conf_set_int("ShowErrorMessages", balsa_app.error_message);
    libbalsa_conf_set_int("ShowDebugMessages", balsa_app.debug_message);
    libbalsa_conf_set_int("ShowFatalMessages", balsa_app.fatal_message);
    libbalsa_conf_pop_group();

    /* Section for geometry ... */
    /* FIXME: Saving window sizes is the WM's job?? */
    libbalsa_conf_push_group("Geometry");

    /* ... column width settings */
    libbalsa_conf_set_int("MailboxListNameWidth",
			 balsa_app.mblist_name_width);
    libbalsa_conf_set_int("MailboxListNewMsgWidth",
			 balsa_app.mblist_newmsg_width);
    libbalsa_conf_set_int("MailboxListTotalMsgWidth",
			 balsa_app.mblist_totalmsg_width);
    libbalsa_conf_set_int("IndexNumWidth", balsa_app.index_num_width);
    libbalsa_conf_set_int("IndexStatusWidth", balsa_app.index_status_width);
    libbalsa_conf_set_int("IndexAttachmentWidth",
			 balsa_app.index_attachment_width);
    libbalsa_conf_set_int("IndexFromWidth", balsa_app.index_from_width);
    libbalsa_conf_set_int("IndexSubjectWidth",
			 balsa_app.index_subject_width);
    libbalsa_conf_set_int("IndexDateWidth", balsa_app.index_date_width);
    libbalsa_conf_set_int("IndexSizeWidth", balsa_app.index_size_width);

    geometry_manager_save();
    libbalsa_conf_set_int("MailboxListWidth", balsa_app.mblist_width);

    libbalsa_conf_set_int("NotebookHeight", balsa_app.notebook_height);

    libbalsa_conf_pop_group();

    /* Message View options ... */
    libbalsa_conf_remove_group("MessageDisplay");
    libbalsa_conf_push_group("MessageDisplay");

    libbalsa_conf_set_string("DateFormat", balsa_app.date_string);
    libbalsa_conf_set_int("ShownHeaders", balsa_app.shown_headers);
    libbalsa_conf_set_string("SelectedHeaders", balsa_app.selected_headers);
    libbalsa_conf_set_bool("ExpandTree", balsa_app.expand_tree);
    libbalsa_conf_set_int("SortField",
			 libbalsa_mailbox_get_sort_field(NULL));
    libbalsa_conf_set_int("ThreadingType",
			 libbalsa_mailbox_get_threading_type(NULL));
    libbalsa_conf_set_bool("MarkQuoted", balsa_app.mark_quoted);
    libbalsa_conf_set_string("QuoteRegex", balsa_app.quote_regex);
    libbalsa_conf_set_bool("UseSystemFonts", balsa_app.use_system_fonts);
    libbalsa_conf_set_string("MessageFont", balsa_app.message_font);
    libbalsa_conf_set_string("SubjectFont", balsa_app.subject_font);
    libbalsa_conf_set_bool("WordWrap", balsa_app.browse_wrap);
    libbalsa_conf_set_int("WordWrapLength", balsa_app.browse_wrap_length);

    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gchar *text = g_strdup_printf("QuotedColor%d", i);
	save_color(text, &balsa_app.quoted_color[i]);
	g_free(text);
    }

    save_color("UrlColor", &balsa_app.url_color);

    /* ... handling of Multipart/Alternative */
    libbalsa_conf_set_bool("DisplayAlternativeAsPlain",
			  balsa_app.display_alt_plain);

    /* ... handling of broken mails with 8-bit chars */
    libbalsa_conf_set_bool("ConvertUnknown8Bit", balsa_app.convert_unknown_8bit);
    libbalsa_conf_set_int("ConvertUnknown8BitCodeset", 
			    balsa_app.convert_unknown_8bit_codeset);

    libbalsa_conf_pop_group();

    /* Interface Options ... */
    libbalsa_conf_push_group("Interface");

    libbalsa_conf_set_bool("ShowPreviewPane", balsa_app.previewpane);
    libbalsa_conf_set_bool("ShowMailboxList", balsa_app.show_mblist);
    libbalsa_conf_set_bool("ShowTabs", balsa_app.show_notebook_tabs);
	libbalsa_conf_set_bool("ShowRecvProgressDlg", balsa_app.recv_progress_dialog);
    libbalsa_conf_set_bool("ShowSendProgressDlg", balsa_app.send_progress_dialog);
    libbalsa_conf_set_int("LayoutType",     balsa_app.layout_type);
    libbalsa_conf_set_bool("ViewMessageOnOpen", balsa_app.view_message_on_open);
    libbalsa_conf_set_bool("AskBeforeSelect", balsa_app.ask_before_select);
    libbalsa_conf_set_bool("PageDownMod", balsa_app.pgdownmod);
    libbalsa_conf_set_int("PageDownPercent", balsa_app.pgdown_percent);

    libbalsa_conf_set_bool("ShowMainWindowToolbar",
                           balsa_app.show_main_toolbar);
    libbalsa_conf_set_bool("ShowMessageWindowToolbar",
                           balsa_app.show_message_toolbar);
    libbalsa_conf_set_bool("ShowComposeWindowToolbar",
                           balsa_app.show_compose_toolbar);
    libbalsa_conf_set_bool("ShowStatusbar",
                           balsa_app.show_statusbar);
    libbalsa_conf_set_bool("ShowSOSbar",
                           balsa_app.show_sos_bar);

    libbalsa_conf_set_bool("HideDeleted", libbalsa_mailbox_get_filter(NULL) & 1);
    libbalsa_conf_set_bool("ExpungeOnClose", balsa_app.expunge_on_close);
    libbalsa_conf_set_bool("AutoExpunge", balsa_app.expunge_auto);
    libbalsa_conf_set_int("AutoExpungeMinutes",
                         balsa_app.expunge_timeout / 60);
    libbalsa_conf_set_int("MessageWindowActionAfterMove",
        balsa_app.mw_action_after_move);

    libbalsa_conf_pop_group();

    /* Source browsing ... */
    libbalsa_conf_push_group("SourcePreview");
    libbalsa_conf_set_bool("EscapeSpecials",
                          balsa_app.source_escape_specials);
    libbalsa_conf_pop_group();

    /* MRU mailbox tree ... */
    libbalsa_conf_push_group("MruTree");
    libbalsa_conf_set_int("Width",  balsa_app.mru_tree_width);
    libbalsa_conf_set_int("Height", balsa_app.mru_tree_height);
    libbalsa_conf_pop_group();

    /* Printing options ... */
    libbalsa_conf_push_group("Printing");
    save_gtk_page_setup(balsa_app.page_setup);
    libbalsa_conf_set_double("LeftMargin", balsa_app.margin_left);
    libbalsa_conf_set_double("TopMargin", balsa_app.margin_top);
    libbalsa_conf_set_double("RightMargin", balsa_app.margin_right);
    libbalsa_conf_set_double("BottomMargin", balsa_app.margin_bottom);

    libbalsa_conf_set_string("PrintHeaderFont",
                            balsa_app.print_header_font);
    libbalsa_conf_set_string("PrintBodyFont", balsa_app.print_body_font);
    libbalsa_conf_set_string("PrintFooterFont",
                            balsa_app.print_footer_font);
    libbalsa_conf_set_bool("PrintHighlightCited",
                          balsa_app.print_highlight_cited);
    libbalsa_conf_set_bool("PrintHighlightPhrases",
                          balsa_app.print_highlight_phrases);
    libbalsa_conf_pop_group();

    /* GtkPrintSettings stuff */
    libbalsa_conf_remove_group("GtkPrint");
    libbalsa_conf_push_group("GtkPrint");
    if (balsa_app.print_settings)
	gtk_print_settings_foreach(balsa_app.print_settings,
				   save_gtk_print_setting, NULL);
    libbalsa_conf_pop_group();

    /* Spelling options ... */
    libbalsa_conf_remove_group("Spelling");
    libbalsa_conf_push_group("Spelling");

    libbalsa_conf_set_string("SpellCheckLanguage",
                             balsa_app.spell_check_lang);
#if HAVE_GSPELL || HAVE_GTKSPELL
    libbalsa_conf_set_bool("SpellCheckActive", 
                           balsa_app.spell_check_active);
#else                           /* HAVE_GTKSPELL */
    libbalsa_conf_set_int("SpellCheckSignature", balsa_app.check_sig);
    libbalsa_conf_set_int("SpellCheckQuoted", balsa_app.check_quoted);
#endif                          /* HAVE_GTKSPELL */

    libbalsa_conf_pop_group();

    /* Mailbox list options */
    libbalsa_conf_push_group("MailboxList");

    libbalsa_conf_set_bool("ShowMailboxContentInfo",
			  balsa_app.mblist_show_mb_content_info);

    libbalsa_conf_pop_group();

    /* Maibox checking options ... */
    libbalsa_conf_push_group("MailboxChecking");

    libbalsa_conf_set_int("NewMailNotificationDialog",
                          balsa_app.notify_new_mail_dialog);
#ifdef HAVE_CANBERRA
    libbalsa_conf_set_int("NewMailNotificationSound",
                          balsa_app.notify_new_mail_sound);
#endif
    libbalsa_conf_set_bool("OnStartup", balsa_app.check_mail_upon_startup);
    libbalsa_conf_set_bool("Auto", balsa_app.check_mail_auto);
    libbalsa_conf_set_int("AutoDelay", balsa_app.check_mail_timer);
    libbalsa_conf_set_int("CheckIMAP", balsa_app.check_imap);
    libbalsa_conf_set_int("CheckIMAPInbox", balsa_app.check_imap_inbox);
    libbalsa_conf_set_int("QuietBackgroundCheck",
			 balsa_app.quiet_background_check);
    libbalsa_conf_set_int("POPMsgSizeLimit", balsa_app.msg_size_limit);

    libbalsa_conf_pop_group();

    /* folder scanning */
    libbalsa_conf_push_group("FolderScanning");
    libbalsa_conf_set_int("LocalScanDepth", balsa_app.local_scan_depth);
    libbalsa_conf_set_int("ImapScanDepth", balsa_app.imap_scan_depth);
    libbalsa_conf_pop_group();

    /* how to react if a message with MDN request is displayed */
    libbalsa_conf_push_group("MDNReply");
    libbalsa_conf_set_int("Clean", balsa_app.mdn_reply_clean);
    libbalsa_conf_set_int("Suspicious", balsa_app.mdn_reply_notclean);
    libbalsa_conf_pop_group();

    /* Sending options ... */
    for (list = balsa_app.smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(list->data);
        gchar *group;

        group =
            g_strconcat(SMTP_SERVER_SECTION_PREFIX,
                        libbalsa_smtp_server_get_name(smtp_server), NULL);
        libbalsa_conf_push_group(group);
	g_free(group);
        libbalsa_smtp_server_save_config(smtp_server);
        libbalsa_conf_pop_group();
    }

    libbalsa_conf_remove_group("Sending");
    libbalsa_conf_private_remove_group("Sending");
    libbalsa_conf_push_group("Sending");
    libbalsa_conf_set_bool("WordWrap", balsa_app.wordwrap);
    libbalsa_conf_set_int("WrapLength", balsa_app.wraplength);
    libbalsa_conf_set_bool("AutoQuote", balsa_app.autoquote);
    libbalsa_conf_set_bool("StripHtmlInReply", balsa_app.reply_strip_html);
    libbalsa_conf_set_bool("ForwardAttached", balsa_app.forward_attached);

	libbalsa_conf_set_int("AlwaysQueueSentMail", balsa_app.always_queue_sent_mail);
    libbalsa_conf_set_bool("AutoSend", balsa_app.send_mail_auto);
    libbalsa_conf_set_int("AutoSendDelay", balsa_app.send_mail_timer);
	libbalsa_conf_set_int("CopyToSentbox", balsa_app.copy_to_sentbox);
    libbalsa_conf_pop_group();

    /* Compose window ... */
    libbalsa_conf_remove_group("Compose");
    libbalsa_conf_push_group("Compose");

    libbalsa_conf_set_string("ComposeHeaders", balsa_app.compose_headers);
    libbalsa_conf_set_bool("ExternEditorEditHeaders", balsa_app.edit_headers);
    libbalsa_conf_set_string("QuoteString", balsa_app.quote_str);
    libbalsa_conf_set_bool("WarnReplyDecrypted", balsa_app.warn_reply_decrypted);

    libbalsa_conf_pop_group();

    /* Global config options ... */
    libbalsa_conf_push_group("Globals");

    libbalsa_conf_set_string("MailDir", balsa_app.local_mail_directory);

    libbalsa_conf_set_bool("OpenInboxOnStartup", 
                          balsa_app.open_inbox_upon_startup);

    libbalsa_conf_set_bool("AutoCloseMailbox", balsa_app.close_mailbox_auto);
    libbalsa_conf_set_int("AutoCloseMailboxTimeout", balsa_app.close_mailbox_timeout/60);

    libbalsa_conf_set_bool("RememberOpenMailboxes",
			  balsa_app.remember_open_mboxes);
    libbalsa_conf_set_string("CurrentMailboxURL",
                             balsa_app.current_mailbox_url);

    libbalsa_conf_set_bool("EmptyTrash", balsa_app.empty_trash_on_exit);

    if (balsa_app.default_address_book != NULL)
        libbalsa_conf_set_string("DefaultAddressBook",
                                 libbalsa_address_book_get_config_prefix
                                 (balsa_app.default_address_book));
    else
	libbalsa_conf_clean_key("DefaultAddressBook");

#ifdef ENABLE_SYSTRAY
    libbalsa_conf_set_int("EnableSystrayIcon", balsa_app.enable_systray_icon);
#endif

    libbalsa_conf_set_int("EnableDkimChecks", balsa_app.enable_dkim_checks);

    libbalsa_conf_pop_group();

    /* Toolbars */
    save_toolbars();

    libbalsa_conf_push_group("Paths");
    if(balsa_app.attach_dir)
	libbalsa_conf_set_string("AttachDir", balsa_app.attach_dir);
    if(balsa_app.save_dir)
	libbalsa_conf_set_string("SavePartDir", balsa_app.save_dir);
    libbalsa_conf_pop_group();

    save_mru(balsa_app.folder_mru, "FolderMRU");
    save_mru(balsa_app.fcc_mru,    "FccMRU");
    save_mru(balsa_app.pipe_cmds,  "PipeCommands");

    /* disabled desktop notifications */
    libbalsa_information_save_cfg();

    libbalsa_conf_sync();
    return TRUE;
}				/* config_global_save */


/* must use a sensible prefix, or this goes weird */

static gboolean
config_get_used_group(const gchar * key, const gchar * value,
                        gpointer data)
{
    gint *max = data;
    gint curr;

    if (*value && (curr = atoi(value)) > *max)
        *max = curr;

    return FALSE;
}

static gchar *
config_get_unused_group(const gchar * prefix)
{
    gint max = 0;
    gchar *name;

    libbalsa_conf_foreach_group(prefix, config_get_used_group, &max);

    name = g_strdup_printf("%s%d", prefix, ++max);
    g_debug("config_mailbox_get_highest_number: name='%s'", name);

    return name;
}

static gboolean
config_list_section(const gchar * key, const gchar * value, gpointer data)
{
    GSList **l = data;

    *l = g_slist_prepend(*l, g_strdup(key));

    return FALSE;
}

static void
config_remove_groups(const gchar * section_prefix)
{
    GSList *sections = NULL, *list;

    libbalsa_conf_foreach_group(section_prefix, config_list_section,
                                &sections);
    for (list = sections; list; list = list->next) {
	libbalsa_conf_remove_group(list->data);
	g_free(list->data);
    }
    g_slist_free(sections);
}

static gboolean
config_address_book_load(const gchar * key, const gchar * value,
                         gpointer data)
{
    const gchar *default_address_book_prefix = data;
    LibBalsaAddressBook *address_book;

    address_book = libbalsa_address_book_new_from_config(key);

    if (address_book) {
        balsa_app.address_book_list =
            g_list_prepend(balsa_app.address_book_list, address_book);

        if (g_strcmp0(key, default_address_book_prefix) == 0) {
            balsa_app.default_address_book = address_book;
        }
    }

    return FALSE;
}

void
config_address_books_load(void)
{
    gchar *default_address_book_prefix;

    libbalsa_conf_push_group("Globals");
    default_address_book_prefix =
        libbalsa_conf_get_string("DefaultAddressBook");
    libbalsa_conf_pop_group();

    /* Free old data in case address books were set by eg. config druid. */
    g_list_free_full(balsa_app.address_book_list, g_object_unref);
    balsa_app.address_book_list = NULL;

    libbalsa_conf_foreach_group(ADDRESS_BOOK_SECTION_PREFIX,
                                config_address_book_load,
                                default_address_book_prefix);

    balsa_app.address_book_list =
        g_list_reverse(balsa_app.address_book_list);

    g_free(default_address_book_prefix);
}

static void
config_address_books_save(void)
{
    g_list_foreach(balsa_app.address_book_list,
                   (GFunc) config_address_book_save, NULL);
}

static LibBalsaSmtpServer *
find_smtp_server_by_name(const gchar * name)
{
    GSList *list;

    if (!name)
        name = libbalsa_smtp_server_get_name(NULL);

    for (list = balsa_app.smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server =
            LIBBALSA_SMTP_SERVER(list->data);
        if (strcmp(name,
                   libbalsa_smtp_server_get_name(smtp_server)) == 0)
            return smtp_server;
    }

    /* Use the first in the list, if any (there really must be one by
     * construction). */
    g_return_val_if_fail(balsa_app.smtp_servers, NULL);
    return LIBBALSA_SMTP_SERVER(balsa_app.smtp_servers->data);
}

static gboolean
config_identity_load(const gchar * key, const gchar * value, gpointer data)
{
    const gchar *default_ident = data;
    LibBalsaIdentity *ident;
    gchar *smtp_server_name;

    libbalsa_conf_push_group(key);
    ident = libbalsa_identity_new_from_config(value);
    smtp_server_name = libbalsa_conf_get_string("SmtpServer");
    libbalsa_identity_set_smtp_server(ident,
                                      find_smtp_server_by_name
                                      (smtp_server_name));
    g_free(smtp_server_name);
    libbalsa_conf_pop_group();
    balsa_app.identities = g_list_prepend(balsa_app.identities, ident);
    if (g_ascii_strcasecmp(libbalsa_identity_get_identity_name(ident),
                           default_ident) == 0)
        balsa_app.current_ident = ident;

    return FALSE;
}

static void
config_identities_load()
{
    gchar *default_ident;

    /* Free old data in case identities were set by eg. config druid. */
    g_list_free_full(balsa_app.identities, g_object_unref);
    balsa_app.identities = NULL;

    libbalsa_conf_push_group("identity");
    default_ident = libbalsa_conf_get_string("CurrentIdentity");
    libbalsa_conf_pop_group();

    libbalsa_conf_foreach_group(IDENTITY_SECTION_PREFIX,
                                config_identity_load,
                                default_ident);
    balsa_app.identities = g_list_reverse(balsa_app.identities);

    if (!balsa_app.identities) {
	libbalsa_conf_push_group("identity-default");
        balsa_app.identities =
            g_list_prepend(NULL,
                           libbalsa_identity_new_from_config("default"));
	libbalsa_conf_pop_group();
    }

    if (balsa_app.current_ident == NULL)
        balsa_app.current_ident =
            LIBBALSA_IDENTITY(balsa_app.identities->data);

    g_free(default_ident);
    if (balsa_app.main_window)
        balsa_identities_changed(balsa_app.main_window);
}

/* config_identities_save:
   saves the balsa_app.identites list.
*/
void
config_identities_save(void)
{
    LibBalsaIdentity* ident;
    GList* list;
    gchar *prefix;

    libbalsa_conf_push_group("identity");
    libbalsa_conf_set_string("CurrentIdentity",
                             libbalsa_identity_get_identity_name
                             (balsa_app.current_ident));
    libbalsa_conf_pop_group();

    config_remove_groups(IDENTITY_SECTION_PREFIX);

    /* save current */
    for (list = balsa_app.identities; list; list = list->next) {
	ident = LIBBALSA_IDENTITY(list->data);
	prefix = g_strconcat(IDENTITY_SECTION_PREFIX, 
			     libbalsa_identity_get_identity_name(ident), NULL);
        libbalsa_identity_save(ident, prefix);
	g_free(prefix);
    }
}

/* Get viewByUrl prefix */
static gchar *
view_by_url_prefix(const gchar * url)
{
    gchar *url_enc;
    gchar *prefix;

    url_enc = libbalsa_urlencode(url);
    prefix = g_strconcat(VIEW_BY_URL_SECTION_PREFIX, url_enc, NULL);
    g_free(url_enc);

    return prefix;
}


LibBalsaMailboxView *
config_load_mailbox_view(const gchar * url)
{
    gchar *prefix;
    LibBalsaMailboxView *view;

    if (!url)
        return NULL;

    prefix = view_by_url_prefix(url);
    if (!libbalsa_conf_has_group(prefix)) {
        g_free(prefix);
        return NULL;
    }

    libbalsa_conf_push_group(prefix);

    view = libbalsa_mailbox_view_new();

    view->identity_name = libbalsa_conf_get_string("Identity");

    if (libbalsa_conf_has_key("Threading"))
        view->threading_type = libbalsa_conf_get_int("Threading");

    if (libbalsa_conf_has_key("GUIFilter"))
        view->filter = libbalsa_conf_get_int("GUIFilter");

    if (libbalsa_conf_has_key("SortType"))
        view->sort_type = libbalsa_conf_get_int("SortType");

    if (libbalsa_conf_has_key("SortField"))
        view->sort_field = libbalsa_conf_get_int("SortField");

    if (libbalsa_conf_has_key("Show"))
        view->show = libbalsa_conf_get_int("Show");

    if (libbalsa_conf_has_key("Subscribe"))
        view->subscribe = libbalsa_conf_get_int("Subscribe");

    if (libbalsa_conf_has_key("Exposed"))
        view->exposed = libbalsa_conf_get_bool("Exposed");

    if (libbalsa_conf_has_key("Open"))
        view->open = libbalsa_conf_get_bool("Open");

    if (libbalsa_conf_has_key("Position"))
        view->position = libbalsa_conf_get_int("Position");

    if (libbalsa_conf_has_key("CryptoMode"))
        view->gpg_chk_mode = libbalsa_conf_get_int("CryptoMode");

    if (libbalsa_conf_has_key("Total"))
        view->total = libbalsa_conf_get_int("Total");

    if (libbalsa_conf_has_key("Unread"))
        view->unread = libbalsa_conf_get_int("Unread");

    if (libbalsa_conf_has_key("ModTime"))
        view->mtime = libbalsa_conf_get_int("ModTime");

    libbalsa_conf_pop_group();
    g_free(prefix);

    return view;
}

void
config_save_mailbox_view(const gchar * url, LibBalsaMailboxView * view)
{
    gchar *prefix;

    if (!view || (view->in_sync && view->used))
	return;
    view->in_sync = TRUE;

    prefix = view_by_url_prefix(url);

    /* Remove the view--it will be recreated if any member needs to be
     * saved. */
    libbalsa_conf_remove_group(prefix);
    libbalsa_conf_push_group(prefix);
    g_free(prefix);

    if (view->identity_name  != libbalsa_mailbox_get_identity_name(NULL))
	libbalsa_conf_set_string("Identity", view->identity_name);
    if (view->threading_type != libbalsa_mailbox_get_threading_type(NULL))
	libbalsa_conf_set_int("Threading",   view->threading_type);
    if (view->filter         != libbalsa_mailbox_get_filter(NULL))
	libbalsa_conf_set_int("GUIFilter",   view->filter);
    if (view->sort_type      != libbalsa_mailbox_get_sort_type(NULL))
	libbalsa_conf_set_int("SortType",    view->sort_type);
    if (view->sort_field     != libbalsa_mailbox_get_sort_field(NULL))
	libbalsa_conf_set_int("SortField",   view->sort_field);
    if (view->show           == LB_MAILBOX_SHOW_TO)
	libbalsa_conf_set_int("Show",        view->show);
    if (view->subscribe      == LB_MAILBOX_SUBSCRIBE_NO)
	libbalsa_conf_set_int("Subscribe",   view->subscribe);
    if (view->exposed        != libbalsa_mailbox_get_exposed(NULL))
	libbalsa_conf_set_bool("Exposed",    view->exposed);
    if (balsa_app.remember_open_mboxes) {
        if (view->open       != libbalsa_mailbox_get_open(NULL))
            libbalsa_conf_set_bool("Open",   view->open);
        if (view->position   != libbalsa_mailbox_get_position(NULL))
            libbalsa_conf_set_int("Position", view->position);
    }
    if (view->gpg_chk_mode   != libbalsa_mailbox_get_crypto_mode(NULL))
	libbalsa_conf_set_int("CryptoMode",  view->gpg_chk_mode);
    /* To avoid accumulation of config entries with only message counts,
     * we save them only if used in this session. */
    if (view->used && view->mtime != 0) {
        gboolean save_mtime = FALSE;
        if (view->unread     != libbalsa_mailbox_get_unread(NULL)) {
            libbalsa_conf_set_int("Unread",  view->unread);
            save_mtime = TRUE;
        }
        if (view->total      != libbalsa_mailbox_get_total(NULL)) {
            libbalsa_conf_set_int("Total",   view->total);
            save_mtime = TRUE;
        }
        if (save_mtime)
            libbalsa_conf_set_int("ModTime", view->mtime);
    }

    libbalsa_conf_pop_group();
}

void
config_view_remove(const gchar * url)
{
    gchar *prefix = view_by_url_prefix(url);
    libbalsa_conf_remove_group(prefix);
    g_free(prefix);
}

static void
save_color(const gchar * key, GdkRGBA * rgba)
{
    gchar *full_key;
    gchar *str;

    full_key = g_strconcat(key, "RGBA", NULL);
    str = gdk_rgba_to_string(rgba);
    libbalsa_conf_set_string(full_key, str);
    g_free(str);
    g_free(full_key);
}

static gboolean
config_filter_load(const gchar * key, const gchar * value, gpointer data)
{
    char *endptr;
    LibBalsaFilter *fil;
    long int dummy;

    dummy = strtol(value, &endptr, 10);
    if (dummy == LONG_MIN || dummy == LONG_MAX) {
        g_warning("%s: Value %ld is too large", __func__, dummy);
        return FALSE;
    }
    if (*endptr) {              /* Bad format. */
        libbalsa_conf_remove_group(key);
        return FALSE;
    }

    libbalsa_conf_push_group(key);

    fil = libbalsa_filter_new_from_config();
    if (!fil->condition) {
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("Filter with no condition was omitted"));
        libbalsa_filter_free(fil, GINT_TO_POINTER(FALSE));
    } else {
        FILTER_SETFLAG(fil, FILTER_VALID);
        FILTER_SETFLAG(fil, FILTER_COMPILED);
        balsa_app.filters = g_slist_prepend(balsa_app.filters, fil);
    }
    libbalsa_conf_pop_group();

    return filter_errno != FILTER_NOERR;
}

static void
config_filters_load(void)
{
    filter_errno = FILTER_NOERR;
    libbalsa_conf_foreach_group(FILTER_SECTION_PREFIX,
                                config_filter_load, NULL);
}

#define FILTER_SECTION_MAX "9999"

void
config_filters_save(void)
{
    GSList *list;
    LibBalsaFilter* fil;
    gchar * buffer,* tmp;
    gint i,nb=0,tmp_len=strlen(FILTER_SECTION_MAX)+2;

    /* We allocate once for all a buffer to store conditions sections names */
    buffer=g_strdup_printf(FILTER_SECTION_PREFIX "%s", FILTER_SECTION_MAX);
    /* section_name points to the beginning of the filter section name */
    /* tmp points to the space where filter number is appended */
    tmp = buffer + strlen(FILTER_SECTION_PREFIX);

    for(list = balsa_app.filters; list; list = list->next) {
	fil = (LibBalsaFilter*)(list->data);
	i=snprintf(tmp,tmp_len,"%d",nb++);
        if (i >= tmp_len)
            g_warning("Group name was truncated");
	libbalsa_conf_push_group(buffer);
	libbalsa_filter_save_config(fil);
	libbalsa_conf_pop_group();
    }
    libbalsa_conf_queue_sync();
    /* This loop takes care of cleaning up old filter sections */
    while (TRUE) {
	i=snprintf(tmp,tmp_len,"%d",nb++);
        if (i >= tmp_len)
            g_warning("Group name was truncated");
	if (libbalsa_conf_has_group(buffer)) {
	    libbalsa_conf_remove_group(buffer);
	}
	else break;
    }
    libbalsa_conf_queue_sync();
    g_free(buffer);
}

void
config_mailbox_filters_save(LibBalsaMailbox * mbox)
{
    const gchar *url;
    gchar * tmp;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbox));

    url = libbalsa_mailbox_get_url(mbox);
    tmp = mailbox_filters_section_lookup(url !=NULL ? url :
                                         libbalsa_mailbox_get_name(mbox));
    if (libbalsa_mailbox_get_filters(mbox) == NULL) {
	if (tmp) {
	    libbalsa_conf_remove_group(tmp);
	    g_free(tmp);
	}
	return;
    }
    if (!tmp) {
	/* If there was no existing filters section for this mailbox we create one */
	tmp=config_get_unused_group(MAILBOX_FILTERS_SECTION_PREFIX);
	libbalsa_conf_push_group(tmp);
	g_free(tmp);
	libbalsa_conf_set_string(MAILBOX_FILTERS_URL_KEY, url);
    }
    else {
	libbalsa_conf_push_group(tmp);
	g_free(tmp);
    }
    libbalsa_mailbox_filters_save_config(mbox);
    libbalsa_conf_pop_group();
    libbalsa_conf_queue_sync();
}

static void
load_color(const gchar * key, const gchar * def_val, GdkRGBA * rgba)
{
    gchar *full_key;
    gchar *str;
    gboolean def_used;

    full_key = g_strdup_printf("%sRGBA", key);
    str = libbalsa_conf_get_string(full_key);
    if (!str) {
        g_free(full_key);
        full_key = g_strdup_printf("%s=%s", key, def_val);
        str = libbalsa_conf_get_string_with_default(full_key, &def_used);
    }
    g_free(full_key);
    gdk_rgba_parse(rgba, str);
    g_free(str);
}

static void
load_mru(GList **mru, const gchar * group)
{
    int count, i;
    char tmpkey[32];
    
    tmpkey[sizeof tmpkey - 1] = '\0';

    libbalsa_conf_push_group(group);
    count=d_get_gint("MRUCount", 0);
    for(i=0;i<count;i++) {
        gchar *val;
	snprintf(tmpkey, sizeof tmpkey - 1, "MRU%d", i + 1);
        if( (val = libbalsa_conf_get_string(tmpkey)) != NULL )
            (*mru)=g_list_prepend((*mru), val);
    }
    *mru = g_list_reverse(*mru);
    libbalsa_conf_pop_group();
}

static void
save_mru(GList * mru, const gchar * group)
{
    int i;
    char tmpkey[32];
    GList *ltmp;

    tmpkey[sizeof tmpkey - 1] = '\0';

    libbalsa_conf_push_group(group);
    for (ltmp = mru, i = 0; ltmp; ltmp = ltmp->next, i++) {
	snprintf(tmpkey, sizeof tmpkey - 1, "MRU%d", i + 1);
        libbalsa_conf_set_string(tmpkey, (gchar *) (ltmp->data));
    }

    libbalsa_conf_set_int("MRUCount", i);
    libbalsa_conf_pop_group();
}

void
config_defclient_save(void)
{
    GDesktopAppInfo *info;
    GError *err;

    if (!balsa_app.default_client)
        return;

    info = g_desktop_app_info_new("balsa.desktop");
    if (!info) {
        g_warning("Failed to create default application for Balsa "
                  "for “mailto”");
        return;
    }

    err = NULL;
    if (!g_app_info_set_as_default_for_type
        (G_APP_INFO(info), "x-scheme-handler/mailto", &err)) {
        g_warning("Failed to set Balsa as the default application "
                  "for “mailto”: %s", err->message);
        g_error_free(err);
    }
    g_object_unref(info);
}

static gboolean
config_mailbox_had_property(const gchar * url, const gchar * key)
{
    gchar *prefix;
    gboolean retval = FALSE;

    prefix = view_by_url_prefix(url);
    if (!libbalsa_conf_has_group(prefix)) {
        g_free(prefix);
        return retval;
    }

    libbalsa_conf_push_group(prefix);

    if (libbalsa_conf_has_key(key))
        retval = libbalsa_conf_get_bool(key);

    libbalsa_conf_pop_group();
    g_free(prefix);

    return retval;
}

gboolean
config_mailbox_was_open(const gchar * url)
{
    return config_mailbox_had_property(url, "Open");
}

gboolean
config_mailbox_was_exposed(const gchar * url)
{
    return config_mailbox_had_property(url, "Exposed");
}

static gint
config_mailbox_get_int_property(const gchar * url, const gchar * key)
{
    gchar *prefix;
    gint retval = -1;

    prefix = view_by_url_prefix(url);
    if (!libbalsa_conf_has_group(prefix)) {
        g_free(prefix);
        return retval;
    }

    libbalsa_conf_push_group(prefix);

    if (libbalsa_conf_has_key(key))
        retval = libbalsa_conf_get_int(key);

    libbalsa_conf_pop_group();
    g_free(prefix);

    return retval;
}

gint
config_mailbox_get_position(const gchar * url)
{
    return config_mailbox_get_int_property(url, "Position");
}
