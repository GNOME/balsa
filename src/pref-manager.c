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

/* MAKE SURE YOU USE THE HELPER FUNCTIONS, like create_table(, page), etc. */
#include "config.h"

#include <gnome.h>
#include "balsa-app.h"
#include "pref-manager.h"
#include "mailbox-conf.h"
#include "folder-conf.h"
#include "main-window.h"
#include "save-restore.h"
#include "spell-check.h"
#include "address-book-config.h"
#include "quote-color.h"
#include "misc.h"

#if ENABLE_ESMTP
#include <libesmtp.h>
#endif

#define NUM_TOOLBAR_MODES 3
#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3
#define NUM_THREADING_STYLES 3
#define NUM_CONVERT_8BIT_MODES 2

/* Spacing suggestions from
 * http://developer.gnome.org/projects/gup/hig/1.0/layout.html#window-layout-spacing
 */
#define HIG_PADDING     6
#define BORDER_WIDTH    (2 * HIG_PADDING)
#define GROUP_SPACING   (3 * HIG_PADDING)
#define HEADER_SPACING  (2 * HIG_PADDING)
#define ROW_SPACING     (1 * HIG_PADDING)
#define COL_SPACING     (1 * HIG_PADDING)

#define BALSA_PAGE_SIZE_GROUP_KEY  "balsa-page-size-group"
#define BALSA_TABLE_PAGE_KEY  "balsa-table-page"
typedef struct _PropertyUI {
    GtkWidget *address_books;

    GtkWidget *mail_servers;
#if ENABLE_ESMTP
    GtkWidget *smtp_server, *smtp_user, *smtp_passphrase;
    GtkWidget *smtp_tls_mode_menu;
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    GtkWidget *smtp_certificate_passphrase;
#endif
#endif
    GtkWidget *mail_directory;
    GtkWidget *encoding_menu;
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;
    GtkWidget *quiet_background_check;
    GtkWidget *check_imap;
    GtkWidget *check_imap_inbox;
    GtkWidget *notify_new_mail_dialog;
    GtkWidget *mdn_reply_clean_menu, *mdn_reply_notclean_menu;

    GtkWidget *close_mailbox_auto;
    GtkWidget *close_mailbox_minutes;
    GtkWidget *commit_mailbox_auto;
    GtkWidget *commit_mailbox_minutes;
    GtkWidget *delete_immediately;

    GtkWidget *previewpane;
    GtkWidget *alternative_layout;
    GtkWidget *view_message_on_open;
    GtkWidget *line_length;
    GtkWidget *pgdownmod;
    GtkWidget *pgdown_percent;
    GtkWidget *view_allheaders;
    GtkWidget *debug;		/* enable/disable debugging */
    GtkWidget *empty_trash;
    GtkRadioButton *pwindow_type[NUM_PWINDOW_MODES];
    GtkWidget *wordwrap;
    GtkWidget *wraplength;
    GtkWidget *open_inbox_upon_startup;
    GtkWidget *check_mail_upon_startup;
    GtkWidget *remember_open_mboxes;
    GtkWidget *mblist_show_mb_content_info;
    GtkWidget *always_queue_sent_mail;
    GtkWidget *copy_to_sentbox;
    GtkWidget *autoquote;
    GtkWidget *reply_strip_html_parts;
    GtkWidget *forward_attached;

    /* Information messages */
    GtkWidget *information_message_menu;
    GtkWidget *warning_message_menu;
    GtkWidget *error_message_menu;
    GtkWidget *debug_message_menu;
    GtkWidget *fatal_message_menu;

    /* External editor preferences */
    GtkWidget *edit_headers;

    /* arp */
    GtkWidget *quote_str;

    GtkWidget *message_font;	/* font used to display messages */
    GtkWidget *subject_font;	/* font used to display messages */
    GtkWidget *font_picker;
    GtkWidget *font_picker2;


    GtkWidget *date_format;

    GtkWidget *selected_headers;
    GtkWidget *message_title_format;

    /* colours */
    GtkWidget *quoted_color[MAX_QUOTED_COLOR];
    GtkWidget *url_color;
    GtkWidget *bad_address_color;

    /* threading prefs */
    GtkWidget *tree_expand_check;
    GtkWidget *default_threading_style;
    gint threading_style_index;

    /* quote regex */
    GtkWidget *quote_pattern;

    /* wrap incoming text/plain */
    GtkWidget *browse_wrap;
    GtkWidget *browse_wrap_length;

    /* how to display multipart/alternative */
    GtkWidget *display_alt_plain;

    /* how to handle broken mails with 8-bit chars */
    GtkRadioButton *convert_unknown_8bit[NUM_CONVERT_8BIT_MODES];
    GtkWidget *convert_unknown_8bit_codeset;

    /* spell checking */
    GtkWidget *module;
    gint module_index;
    GtkWidget *suggestion_mode;
    gint suggestion_mode_index;
    GtkWidget *ignore_length;
    GtkWidget *spell_check_sig;
    GtkWidget *spell_check_quoted;

    /* IMAP folder scanning */
    GtkWidget *imap_scan_depth;

} PropertyUI;


static PropertyUI *pui = NULL;
static GtkWidget *property_box;
static gboolean already_open;

/* Mail Servers page */
static GtkWidget *create_mailserver_page(gpointer);
static GtkWidget *remote_mailbox_servers_group(GtkWidget * page);
static GtkWidget *local_mail_group(GtkWidget * page);
#if ENABLE_ESMTP
static GtkWidget *outgoing_mail_group(GtkWidget * page);
#endif

/* Address Books page */
static GtkWidget *create_address_book_page(gpointer);
static GtkWidget *address_books_group(GtkWidget * page);

/* Mail Options page */
static GtkWidget *create_mail_options_page(gpointer);

static GtkWidget *incoming_subpage(gpointer);
static GtkWidget *checking_group(GtkWidget * page);
static GtkWidget *mdn_group(GtkWidget * page);

static GtkWidget *outgoing_subpage(gpointer);
static GtkWidget *word_wrap_group(GtkWidget * page);
static GtkWidget *other_options_group(GtkWidget * page);
static GtkWidget *encoding_group(GtkWidget * page);

/* Display page */
static GtkWidget *create_display_page(gpointer);

static GtkWidget *display_subpage(gpointer data);
static GtkWidget *main_window_group(GtkWidget * page);
static GtkWidget *progress_group(GtkWidget * page);
static GtkWidget *display_formats_group(GtkWidget * page);

static GtkWidget *status_messages_subpage(gpointer data);
static GtkWidget *information_messages_group(GtkWidget * page);

static GtkWidget *colors_subpage(gpointer data);
static GtkWidget *message_colors_group(GtkWidget * page);
static GtkWidget *link_color_group(GtkWidget * page);
static GtkWidget *composition_window_group(GtkWidget * page);

static GtkWidget *message_subpage(gpointer data);
static GtkWidget *preview_font_group(GtkWidget * page);
static GtkWidget *quoted_group(GtkWidget * page);
static GtkWidget *alternative_group(GtkWidget * page);

static GtkWidget *threading_subpage(gpointer data);
static GtkWidget *threading_group(GtkWidget * page);
    
/* Spelling page */
static GtkWidget *create_spelling_page(gpointer);
static GtkWidget *pspell_settings_group(GtkWidget * page);
static GtkWidget *misc_spelling_group(GtkWidget * page);

/* Misc page */
static GtkWidget *create_misc_page(gpointer);
static GtkWidget *misc_group(GtkWidget * page);
static GtkWidget *deleting_messages_group(GtkWidget * page);
    
/* Startup page */
static GtkWidget *create_startup_page(gpointer);
static GtkWidget *options_group(GtkWidget * page);
static GtkWidget *imap_folder_scanning_group(GtkWidget * page);

/* general helpers */
static GtkWidget *create_table(gint rows, gint cols, GtkWidget * page);
static GtkWidget* add_pref_menu(const gchar* label, const gchar* names[], 
                                gint size, gint* index, GtkBox* parent, 
                                gint padding, GtkSignalFunc callback);
static GtkWidget *attach_pref_menu(const gchar * label, gint row,
                                   GtkTable * table, const gchar * names[],
                                   gint size, gint * index,
                                   GCallback callback);
static GtkWidget *attach_entry(const gchar * label, gint row,
                               GtkTable * table);
static GtkWidget *attach_entry_full(const gchar * label, gint row,
                                    GtkTable * table, gint col_left,
                                    gint col_middle, gint col_right);
static GtkWidget* create_pref_option_menu(const gchar* names[], gint size, 
                                          gint* index, GtkSignalFunc callback);

/* page and group object methods */
static GtkWidget *pm_page_new(void);
static void pm_page_add(GtkWidget * page, GtkWidget * child);
static GtkSizeGroup * pm_page_get_size_group(GtkWidget * page);
static void pm_page_add_to_size_group(GtkWidget * page, GtkWidget * child);
static GtkWidget *pm_group_new(const gchar * text);
static void pm_group_add(GtkWidget * group, GtkWidget * child);
static GtkWidget *pm_group_get_vbox(GtkWidget * group);
static GtkWidget *pm_group_add_check(GtkWidget * group,
                                     const gchar * text);

/* special helpers */
static GtkWidget *create_information_message_menu(void);
static GtkWidget *create_encoding_menu(void);
static GtkWidget *create_mdn_reply_menu(void);
#if ENABLE_ESMTP
static GtkWidget *create_tls_mode_menu(void);
#endif
static void balsa_help_pbox_display(gint page_num);
static GtkWidget *create_codeset_menu(void);

/* updaters */
static void set_prefs(void);
static void apply_prefs(GtkDialog * dialog);
void update_mail_servers(void);         /* public; in pref-manager.h */

/* callbacks */
static void response_cb(GtkDialog * dialog, gint response, gpointer data);
static void destroy_pref_window_cb(void);
static void update_address_books(void);
static void properties_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void font_changed_cb(GtkWidget * widget, GtkWidget * pbox);
static void mail_servers_cb(GtkTreeView * tree_view, GtkTreePath * path,
                            GtkTreeViewColumn * column,
                            gpointer user_data);
static void server_edit_cb(GtkWidget * widget, gpointer data);
static void pop3_add_cb(GtkWidget * widget, gpointer data);
static void server_add_cb(GtkWidget * widget, gpointer data);
static void server_del_cb(GtkWidget * widget, gpointer data);
static void address_book_edit_cb(GtkWidget * widget, gpointer data);
static void address_book_add_cb(GtkWidget * widget, gpointer data);
static void address_book_delete_cb(GtkWidget * widget, gpointer data);
static void timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void mailbox_close_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void mailbox_commit_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void browse_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void pgdown_modified_cb(GtkWidget * widget, GtkWidget * pbox);

static void spelling_optionmenu_cb(GtkItem * menuitem, gpointer data);
static void threading_optionmenu_cb(GtkItem* menuitem, gpointer data);
static void set_default_address_book_cb(GtkWidget * button, gpointer data);
static void imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox);

static void convert_8bit_cb(GtkWidget * widget, GtkWidget * pbox);

guint encoding_type[NUM_ENCODING_MODES] = {
    GMIME_PART_ENCODING_7BIT,
    GMIME_PART_ENCODING_8BIT,
    GMIME_PART_ENCODING_QUOTEDPRINTABLE
};

gchar *encoding_type_label[NUM_ENCODING_MODES] = {
    N_("7 Bits"),
    N_("8 Bits"),
    N_("Quoted")
};

guint pwindow_type[NUM_PWINDOW_MODES] = {
    WHILERETR,
    UNTILCLOSED,
    NEVER
};

gchar *pwindow_type_label[NUM_PWINDOW_MODES] = {
    N_("While Retrieving Messages"),
    N_("Until Closed"),
    N_("Never")
};

const gchar *spell_check_suggest_mode_label[NUM_SUGGEST_MODES] = {
    N_("Fast"),
    N_("Normal"),
    N_("Bad Spellers")
};

const gchar* threading_style_label[NUM_THREADING_STYLES] = {
    N_("Flat"),
    N_("Simple"),
    N_("JWZ")
};

const gchar *codeset_label[LIBBALSA_NUM_CODESETS] = {
    N_("west european (traditional)"),
    N_("east european"),
    N_("south european"),
    N_("north european"),
    N_("cyrillic (iso/windows)"),
    N_("arabic"),
    N_("greek"),
    N_("hebrew"),
    N_("turkish"),
    N_("nordic"),
    N_("thai"),
    N_("baltic"),
    N_("celtic"),
    N_("west european (euro)"),
    N_("russian (koi)"),
    N_("ukranian (koi)"),
    N_("japanese"),
    N_("korean")
};

/* and now the important stuff: */
void
open_preferences_manager(GtkWidget * widget, gpointer data)
{
    GtkWidget *notebook;
    GnomeApp *active_win = GNOME_APP(data);
    gint i;
#if BALSA_MAJOR < 2
    static GnomeHelpMenuEntry help_entry = { NULL, "preferences" };
    help_entry.name = gnome_app_id;
#endif

    /* only one preferences manager window */
    if (already_open) {
	gdk_window_raise(GTK_WIDGET(property_box)->window);
	return;
    }

    pui = g_malloc(sizeof(PropertyUI));

    property_box =
        gtk_dialog_new_with_buttons(_("Balsa Preferences"),
                                    GTK_WINDOW(active_win),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK,    GTK_RESPONSE_OK,
                                    GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    GTK_STOCK_HELP,  GTK_RESPONSE_HELP,
                                    NULL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_OK, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(property_box),
                                      GTK_RESPONSE_APPLY, FALSE);
    notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(property_box)->vbox),
                      notebook);
    g_object_set_data(G_OBJECT(property_box), "notebook", notebook);

    already_open = TRUE;

    gtk_window_set_wmclass(GTK_WINDOW(property_box), "preferences", "Balsa");
    gtk_window_set_resizable(GTK_WINDOW(property_box), FALSE);
    g_object_set_data(G_OBJECT(property_box), "balsawindow", active_win);

    /* Create the pages */
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_mailserver_page(property_box),
			     gtk_label_new(_("Mail Servers")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_address_book_page(property_box),
			     gtk_label_new(_("Address Books")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_mail_options_page(property_box),
			     gtk_label_new(_("Mail Options")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_display_page(property_box),
			     gtk_label_new(_("Display")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_spelling_page(property_box),
			     gtk_label_new(_("Spelling")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_misc_page(property_box),
			     gtk_label_new(_("Misc")));

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     create_startup_page(property_box),
			     gtk_label_new(_("Startup")));

    set_prefs();

    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	g_signal_connect(G_OBJECT(pui->pwindow_type[i]), "clicked",
			 G_CALLBACK(properties_modified_cb), property_box);
    }

    g_signal_connect(G_OBJECT(pui->previewpane), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->alternative_layout), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT (pui->view_message_on_open), "toggled",
                     G_CALLBACK (properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT (pui->line_length), "toggled",
                     G_CALLBACK (properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->pgdownmod), "toggled",
		     G_CALLBACK(pgdown_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->pgdown_percent), "changed",
		     G_CALLBACK(pgdown_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->debug), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->mblist_show_mb_content_info), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->spell_check_sig), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->spell_check_quoted), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

#if ENABLE_ESMTP
    g_signal_connect(G_OBJECT(pui->smtp_server), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->smtp_user), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->smtp_passphrase), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    g_signal_connect(G_OBJECT(pui->smtp_certificate_passphrase), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);
#endif
#endif

    g_signal_connect(G_OBJECT(pui->mail_directory), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->check_mail_auto), "toggled",
		     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_mail_minutes), "changed",
		     G_CALLBACK(timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->quiet_background_check), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_imap), "toggled",
		     G_CALLBACK(imap_toggled_cb), property_box);

    g_signal_connect(G_OBJECT(pui->check_imap_inbox), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->notify_new_mail_dialog), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->close_mailbox_auto), "toggled",
		     G_CALLBACK(mailbox_close_timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->close_mailbox_minutes), "changed",
		     G_CALLBACK(mailbox_close_timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->commit_mailbox_auto), "toggled",
		     G_CALLBACK(mailbox_commit_timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->commit_mailbox_minutes), "changed",
		     G_CALLBACK(mailbox_commit_timer_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->delete_immediately), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->browse_wrap), "toggled",
		     G_CALLBACK(browse_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->browse_wrap_length), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->wordwrap), "toggled",
		     G_CALLBACK(wrap_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->wraplength), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->always_queue_sent_mail), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->copy_to_sentbox), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->autoquote), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->reply_strip_html_parts), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->forward_attached), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* external editor */
    g_signal_connect(G_OBJECT(pui->edit_headers), "toggled",
    		     G_CALLBACK(properties_modified_cb), property_box);
		
    /* arp */
    g_signal_connect(G_OBJECT(pui->quote_str), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT
                     (gnome_entry_gtk_entry
                      (GNOME_ENTRY(pui->quote_pattern))), "changed",
                     G_CALLBACK(properties_modified_cb),
                     property_box);

    /* multipart/alternative */
    g_signal_connect(G_OBJECT(pui->display_alt_plain), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* message font */
    g_signal_connect(G_OBJECT(pui->message_font), "changed",
		     G_CALLBACK(font_changed_cb), property_box);
    g_signal_connect(G_OBJECT(pui->font_picker), "font_set",
		     G_CALLBACK(font_changed_cb), property_box);
    g_signal_connect(G_OBJECT(pui->subject_font), "changed",
		     G_CALLBACK(font_changed_cb), property_box);
    g_signal_connect(G_OBJECT(pui->font_picker2), "font_set",
		     G_CALLBACK(font_changed_cb), property_box);


    g_signal_connect(G_OBJECT(pui->open_inbox_upon_startup), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->check_mail_upon_startup), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->remember_open_mboxes), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->imap_scan_depth), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->empty_trash), "toggled",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* threading */
    g_signal_connect(G_OBJECT(pui->tree_expand_check), "toggled",
                     G_CALLBACK(properties_modified_cb), property_box);
    g_signal_connect(G_OBJECT(pui->default_threading_style), "clicked",
                     G_CALLBACK(properties_modified_cb), property_box);

    /* spell checking */
    g_signal_connect(G_OBJECT(pui->module), "clicked",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->suggestion_mode), "clicked",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->ignore_length), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);


    /* Date format */
    g_signal_connect(G_OBJECT(pui->date_format), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* Selected headers */
    g_signal_connect(G_OBJECT(pui->selected_headers), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* Format for the title of the message window */
    g_signal_connect(G_OBJECT(pui->message_title_format), "changed",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* Colour */
    for(i=0;i<MAX_QUOTED_COLOR;i++)
	g_signal_connect(G_OBJECT(pui->quoted_color[i]), "released",
			 G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->url_color), "released",
		     G_CALLBACK(properties_modified_cb), property_box);

    g_signal_connect(G_OBJECT(pui->bad_address_color), "released",
		     G_CALLBACK(properties_modified_cb), property_box);

    /* handling of message parts with 8-bit chars without codeset headers */
    for (i = 0; i < NUM_CONVERT_8BIT_MODES; i++)
	g_signal_connect(G_OBJECT(pui->convert_unknown_8bit[i]), "toggled",
			 G_CALLBACK(convert_8bit_cb), property_box);
	
    /* Gnome Property Box Signals */
    g_signal_connect(G_OBJECT(property_box), "response",
                     G_CALLBACK(response_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(property_box));

}				/* open_preferences_manager */

static void
response_cb(GtkDialog * dialog, gint response, gpointer data)
{
    GtkNotebook *notebook;

    switch (response) {
    case GTK_RESPONSE_APPLY:
        apply_prefs(dialog);
        break;
    case GTK_RESPONSE_HELP:
        notebook = g_object_get_data(G_OBJECT(dialog), "notebook");
        balsa_help_pbox_display(gtk_notebook_get_current_page(notebook));
        break;
    case GTK_RESPONSE_OK:
        apply_prefs(dialog);
        /* and fall through to... */
    default:
        destroy_pref_window_cb();
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

/*
 * update data from the preferences window
 */

static void
destroy_pref_window_cb(void)
{
    g_free(pui);
    pui = NULL; 
    already_open = FALSE;
}

static void
apply_prefs(GtkDialog * pbox)
{
    gint i;
    GtkWidget *balsa_window;
    GtkWidget *entry_widget;
    GtkWidget *menu_item;
    const gchar* tmp;

    /*
     * identity page
     */

#if ENABLE_ESMTP
    g_free(balsa_app.smtp_server);
    balsa_app.smtp_server =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->smtp_server)));

    g_free(balsa_app.smtp_user);
    balsa_app.smtp_user =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->smtp_user)));

    g_free(balsa_app.smtp_passphrase);
    balsa_app.smtp_passphrase =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->smtp_passphrase)));

    menu_item = gtk_menu_get_active(GTK_MENU(pui->smtp_tls_mode_menu));
    balsa_app.smtp_tls_mode =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    g_free(balsa_app.smtp_certificate_passphrase);
    balsa_app.smtp_certificate_passphrase =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->smtp_certificate_passphrase)));
#endif
#endif

    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->mail_directory)));

    /* 
     * display page 
     */
    for (i = 0; i < NUM_PWINDOW_MODES; i++)
	if (GTK_TOGGLE_BUTTON(pui->pwindow_type[i])->active) {
	    balsa_app.pwindow_option = pwindow_type[i];
	    break;
	}
    
    balsa_app.debug = GTK_TOGGLE_BUTTON(pui->debug)->active;
    balsa_app.previewpane = GTK_TOGGLE_BUTTON(pui->previewpane)->active;
    balsa_app.alternative_layout = GTK_TOGGLE_BUTTON(pui->alternative_layout)->active;
    balsa_app.view_message_on_open = GTK_TOGGLE_BUTTON (pui->view_message_on_open)->active;
    balsa_app.line_length = GTK_TOGGLE_BUTTON (pui->line_length)->active;
    balsa_app.pgdownmod = GTK_TOGGLE_BUTTON(pui->pgdownmod)->active;
    balsa_app.pgdown_percent =
      gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->pgdown_percent));

    /* if (balsa_app.alt_layout_is_active != balsa_app.alternative_layout)  */
	balsa_change_window_layout(balsa_app.main_window);

    menu_item = gtk_menu_get_active(GTK_MENU(pui->encoding_menu));
    balsa_app.encoding_style =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));

    if (balsa_app.mblist_show_mb_content_info !=
	GTK_TOGGLE_BUTTON(pui->mblist_show_mb_content_info)->active) {
	balsa_app.mblist_show_mb_content_info =
	    !balsa_app.mblist_show_mb_content_info;
	g_object_set(G_OBJECT(balsa_app.mblist), "show_content_info",
		     balsa_app.mblist_show_mb_content_info, NULL);
    }

    balsa_app.check_mail_auto =
	GTK_TOGGLE_BUTTON(pui->check_mail_auto)->active;
    balsa_app.check_mail_timer =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->check_mail_minutes));
    balsa_app.quiet_background_check =
	GTK_TOGGLE_BUTTON(pui->quiet_background_check)->active;
    balsa_app.check_imap =
	GTK_TOGGLE_BUTTON(pui->check_imap)->active;
    balsa_app.check_imap_inbox =
	GTK_TOGGLE_BUTTON(pui->check_imap_inbox)->active;
    balsa_app.notify_new_mail_dialog =
	GTK_TOGGLE_BUTTON(pui->notify_new_mail_dialog)->active;
    menu_item = gtk_menu_get_active(GTK_MENU(pui->mdn_reply_clean_menu));
    balsa_app.mdn_reply_clean =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->mdn_reply_notclean_menu));
    balsa_app.mdn_reply_notclean =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));

    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    else
	update_timer(FALSE, 0);

    balsa_app.wordwrap = GTK_TOGGLE_BUTTON(pui->wordwrap)->active;
    balsa_app.wraplength =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));
    balsa_app.autoquote =
	GTK_TOGGLE_BUTTON(pui->autoquote)->active;
    balsa_app.reply_strip_html =
	GTK_TOGGLE_BUTTON(pui->reply_strip_html_parts)->active;
    balsa_app.forward_attached =
	GTK_TOGGLE_BUTTON(pui->forward_attached)->active;
    balsa_app.always_queue_sent_mail =
	GTK_TOGGLE_BUTTON(pui->always_queue_sent_mail)->active;
    balsa_app.copy_to_sentbox =
	GTK_TOGGLE_BUTTON(pui->copy_to_sentbox)->active;

    balsa_app.close_mailbox_auto =
	GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->active;
    balsa_app.close_mailbox_timeout =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->close_mailbox_minutes)) * 60;

    balsa_app.commit_mailbox_auto =
	GTK_TOGGLE_BUTTON(pui->commit_mailbox_auto)->active;
    balsa_app.commit_mailbox_timeout =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->commit_mailbox_minutes)) * 60;
    balsa_app.delete_immediately =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->delete_immediately));

    /* external editor */
    balsa_app.edit_headers = GTK_TOGGLE_BUTTON(pui->edit_headers)->active;

    /* arp */
    g_free(balsa_app.quote_str);
    balsa_app.quote_str =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->quote_str)));

    g_free(balsa_app.message_font);
    balsa_app.message_font =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->message_font)));
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->subject_font)));

    g_free(balsa_app.quote_regex);
    entry_widget = gnome_entry_gtk_entry(GNOME_ENTRY(pui->quote_pattern));
    tmp = gtk_entry_get_text(GTK_ENTRY(entry_widget));
    balsa_app.quote_regex = g_strcompress(tmp);

    balsa_app.browse_wrap = GTK_TOGGLE_BUTTON(pui->browse_wrap)->active;
    /* main window view menu can also toggle balsa_app.browse_wrap
     * update_view_menu lets it know we've made a change */
    update_view_menu();
    balsa_app.browse_wrap_length =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->browse_wrap_length));

    balsa_app.display_alt_plain =
	GTK_TOGGLE_BUTTON(pui->display_alt_plain)->active;

    balsa_app.open_inbox_upon_startup =
	GTK_TOGGLE_BUTTON(pui->open_inbox_upon_startup)->active;
    balsa_app.check_mail_upon_startup =
	GTK_TOGGLE_BUTTON(pui->check_mail_upon_startup)->active;
    balsa_app.remember_open_mboxes =
	GTK_TOGGLE_BUTTON(pui->remember_open_mboxes)->active;
    balsa_app.imap_scan_depth =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->imap_scan_depth));
    balsa_app.empty_trash_on_exit =
	GTK_TOGGLE_BUTTON(pui->empty_trash)->active;

    /* spell checking */
    balsa_app.module = pui->module_index;
    balsa_app.suggestion_mode = pui->suggestion_mode_index;
    balsa_app.ignore_size =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->ignore_length));
    balsa_app.check_sig = GTK_TOGGLE_BUTTON(pui->spell_check_sig)->active;
    balsa_app.check_quoted =
	GTK_TOGGLE_BUTTON(pui->spell_check_quoted)->active;

    /* date format */
    g_free(balsa_app.date_string);
    balsa_app.date_string =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->date_format)));

    /* selected headers */
    g_free(balsa_app.selected_headers);
    balsa_app.selected_headers =
	g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(pui->selected_headers)),
                        -1);

    /* message window title format */
    g_free(balsa_app.message_title_format);
    balsa_app.message_title_format =
        gtk_editable_get_chars(GTK_EDITABLE(pui->message_title_format),
                               0, -1);

    /* quoted text color */
    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gdk_colormap_free_colors(gdk_drawable_get_colormap
				 (GTK_WIDGET(pbox)->window),
				 &balsa_app.quoted_color[i], 1);
	gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->quoted_color[i]),
				   &(balsa_app.quoted_color[i].red),
				   &(balsa_app.quoted_color[i].green),
				   &(balsa_app.quoted_color[i].blue),
				   0);
    }

    /* url color */
    gdk_colormap_free_colors(gdk_drawable_get_colormap(GTK_WIDGET(pbox)->window),
			     &balsa_app.url_color, 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->url_color),
			       &(balsa_app.url_color.red),
			       &(balsa_app.url_color.green),
			       &(balsa_app.url_color.blue),
			       0);			       

    /* bad address color */
    gdk_colormap_free_colors(gdk_drawable_get_colormap(GTK_WIDGET(pbox)->window),
			     &balsa_app.bad_address_color, 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->bad_address_color),
			       &(balsa_app.bad_address_color.red),
			       &(balsa_app.bad_address_color.green),
			       &(balsa_app.bad_address_color.blue),
			       0);			       

    /* threading */
    balsa_app.expand_tree = GTK_TOGGLE_BUTTON(pui->tree_expand_check)->active;
    balsa_app.threading_type = pui->threading_style_index;

    /* Information dialogs */
    menu_item =
	gtk_menu_get_active(GTK_MENU(pui->information_message_menu));
    balsa_app.information_message =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->warning_message_menu));
    balsa_app.warning_message =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->error_message_menu));
    balsa_app.error_message =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->debug_message_menu));
    balsa_app.debug_message =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));

    /* handling of 8-bit message parts without codeset header */
    balsa_app.convert_unknown_8bit =
	GTK_TOGGLE_BUTTON(pui->convert_unknown_8bit[1])->active;
    menu_item =
	gtk_menu_get_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(pui->convert_unknown_8bit_codeset))));
    balsa_app.convert_unknown_8bit_codeset = 
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "balsa-data"));
    libbalsa_set_fallback_codeset(balsa_app.convert_unknown_8bit_codeset);

    /*
     * close window and free memory
     */
    config_save();
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
    balsa_window =
	GTK_WIDGET(g_object_get_data(G_OBJECT(pbox), "balsawindow"));
    balsa_window_refresh(BALSA_WINDOW(balsa_window)); 
}


/*
 * refresh data in the preferences window
 */
void
set_prefs(void)
{
    GtkWidget *entry_widget;
    unsigned i;
    gchar* tmp;

    for (i = 0; i < NUM_PWINDOW_MODES; i++)
	if (balsa_app.pwindow_option == pwindow_type[i]) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
					 (pui->pwindow_type[i]), TRUE);
	    break;
	}

#if ENABLE_ESMTP
    if (balsa_app.smtp_server)
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_server),
			   balsa_app.smtp_server);

    if (balsa_app.smtp_user)
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_user),
			   balsa_app.smtp_user);

    if (balsa_app.smtp_passphrase)
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_passphrase),
			   balsa_app.smtp_passphrase);

    gtk_menu_set_active(GTK_MENU(pui->smtp_tls_mode_menu),
			balsa_app.smtp_tls_mode);
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    if (balsa_app.smtp_certificate_passphrase)
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_certificate_passphrase),
			   balsa_app.smtp_certificate_passphrase);
#endif
#endif

    gtk_entry_set_text(GTK_ENTRY(pui->mail_directory),
		       balsa_app.local_mail_directory);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->previewpane),
				 balsa_app.previewpane);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->alternative_layout),
				 balsa_app.alternative_layout);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->view_message_on_open),
                                 balsa_app.view_message_on_open);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->line_length),
                                 balsa_app.line_length);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->pgdownmod),
				 balsa_app.pgdownmod);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->pgdown_percent),
			      (float) balsa_app.pgdown_percent);
    gtk_widget_set_sensitive(pui->pgdown_percent,
			     GTK_TOGGLE_BUTTON(pui->pgdownmod)->active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->debug),
				 balsa_app.debug);

    for (i = 0; i < NUM_ENCODING_MODES; i++)
	if (balsa_app.encoding_style == encoding_type[i]) {
	    gtk_menu_set_active(GTK_MENU(pui->encoding_menu), i);
	    break;
	}

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->mblist_show_mb_content_info),
				 balsa_app.mblist_show_mb_content_info);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto),
				 balsa_app.check_mail_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->check_mail_minutes),
			      (float) balsa_app.check_mail_timer);
    gtk_toggle_button_set_active(
	GTK_TOGGLE_BUTTON(pui->quiet_background_check),
	balsa_app.quiet_background_check);
    gtk_toggle_button_set_active(
	GTK_TOGGLE_BUTTON(pui->check_imap),
	balsa_app.check_imap);
    gtk_toggle_button_set_active(
	GTK_TOGGLE_BUTTON(pui->check_imap_inbox),
	balsa_app.check_imap_inbox);
    gtk_toggle_button_set_active(
	GTK_TOGGLE_BUTTON(pui->notify_new_mail_dialog),
	balsa_app.notify_new_mail_dialog);
    if(!balsa_app.check_imap)
	gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), FALSE);

    gtk_menu_set_active(GTK_MENU(pui->mdn_reply_clean_menu),
			balsa_app.mdn_reply_clean);
    gtk_menu_set_active(GTK_MENU(pui->mdn_reply_notclean_menu),
			balsa_app.mdn_reply_notclean);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->close_mailbox_auto),
				 balsa_app.close_mailbox_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->close_mailbox_minutes),
			      (float) balsa_app.close_mailbox_timeout / 60);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->commit_mailbox_auto),
				 balsa_app.commit_mailbox_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->commit_mailbox_minutes),
			      (float) balsa_app.commit_mailbox_timeout / 60);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->delete_immediately),
                                 balsa_app.delete_immediately);

    gtk_widget_set_sensitive(pui->close_mailbox_minutes,
			     GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->
    		    	    active);

    gtk_widget_set_sensitive(pui->commit_mailbox_minutes,
			     GTK_TOGGLE_BUTTON(pui->commit_mailbox_auto)->
    		    	    active);

    gtk_widget_set_sensitive(pui->check_mail_minutes,
			     GTK_TOGGLE_BUTTON(pui->check_mail_auto)->
			     active);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->wordwrap),
				 balsa_app.wordwrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->wraplength),
			      (float) balsa_app.wraplength);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->always_queue_sent_mail),
				 balsa_app.always_queue_sent_mail);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->copy_to_sentbox),
				 balsa_app.copy_to_sentbox);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->autoquote),
				 balsa_app.autoquote);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->reply_strip_html_parts),
				 balsa_app.reply_strip_html);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->forward_attached),
				 balsa_app.forward_attached);

    gtk_widget_set_sensitive(pui->wraplength,
			     GTK_TOGGLE_BUTTON(pui->wordwrap)->active);

    /* external editor */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->edit_headers),
                                 balsa_app.edit_headers);

    /* arp */
    gtk_entry_set_text(GTK_ENTRY(pui->quote_str), balsa_app.quote_str);
    entry_widget = gnome_entry_gtk_entry(GNOME_ENTRY(pui->quote_pattern));
    tmp = g_strescape(balsa_app.quote_regex, NULL);
    gtk_entry_set_text(GTK_ENTRY(entry_widget), tmp);
    g_free(tmp);

    /* wrap incoming text/plain */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->browse_wrap),
                                 balsa_app.browse_wrap);
    gtk_widget_set_sensitive(pui->browse_wrap_length,
                             balsa_app.browse_wrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->browse_wrap_length),
                              (float) balsa_app.browse_wrap_length);

    /* how to treat multipart/alternative */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->display_alt_plain),
				  balsa_app.display_alt_plain);
    
    /* message font */
    gtk_entry_set_text(GTK_ENTRY(pui->message_font),
		       balsa_app.message_font);
    gtk_editable_set_position(GTK_EDITABLE(pui->message_font), 0);
    gtk_entry_set_text(GTK_ENTRY(pui->subject_font),
		       balsa_app.subject_font);
    gtk_editable_set_position(GTK_EDITABLE(pui->subject_font), 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->open_inbox_upon_startup),
				 balsa_app.open_inbox_upon_startup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->check_mail_upon_startup),
				 balsa_app.check_mail_upon_startup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->remember_open_mboxes),
				 balsa_app.remember_open_mboxes);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->imap_scan_depth),
			      balsa_app.imap_scan_depth);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->empty_trash),
				 balsa_app.empty_trash_on_exit);

    /* threading */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->tree_expand_check), 
                                 balsa_app.expand_tree);
    pui->threading_style_index = balsa_app.threading_type;
    gtk_option_menu_set_history(GTK_OPTION_MENU(pui->default_threading_style),
                                balsa_app.threading_type);

    /* spelling */
    pui->module_index = balsa_app.module;
    gtk_option_menu_set_history(GTK_OPTION_MENU(pui->module),
				balsa_app.module);
    pui->suggestion_mode_index = balsa_app.suggestion_mode;
    gtk_option_menu_set_history(GTK_OPTION_MENU(pui->suggestion_mode),
				balsa_app.suggestion_mode);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->ignore_length),
			      balsa_app.ignore_size);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->spell_check_sig),
				 balsa_app.check_sig);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->spell_check_quoted),
				 balsa_app.check_quoted);


    /* date format */
    if (balsa_app.date_string)
	gtk_entry_set_text(GTK_ENTRY(pui->date_format),
			   balsa_app.date_string);

    /* selected headers */
    if (balsa_app.selected_headers)
	gtk_entry_set_text(GTK_ENTRY(pui->selected_headers),
			   balsa_app.selected_headers);

    /* message window title format */
    if (balsa_app.message_title_format)
	gtk_entry_set_text(GTK_ENTRY(pui->message_title_format),
			   balsa_app.message_title_format);

    /* Colour */
    for(i=0;i<MAX_QUOTED_COLOR;i++)
	gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->quoted_color[i]),
				   balsa_app.quoted_color[i].red,
				   balsa_app.quoted_color[i].green,
				   balsa_app.quoted_color[i].blue, 0);

    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->url_color),
			       balsa_app.url_color.red,
			       balsa_app.url_color.green,
			       balsa_app.url_color.blue, 0);

    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->bad_address_color),
			       balsa_app.bad_address_color.red,
			       balsa_app.bad_address_color.green,
			       balsa_app.bad_address_color.blue, 0);

    /* Information Message */
    gtk_menu_set_active(GTK_MENU(pui->information_message_menu),
			balsa_app.information_message);
    gtk_menu_set_active(GTK_MENU(pui->warning_message_menu),
			balsa_app.warning_message);
    gtk_menu_set_active(GTK_MENU(pui->error_message_menu),
			balsa_app.error_message);
    gtk_menu_set_active(GTK_MENU(pui->debug_message_menu),
			balsa_app.debug_message);

    /* handling of 8-bit message parts without codeset header */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->convert_unknown_8bit[1]),
				 balsa_app.convert_unknown_8bit);
    gtk_widget_set_sensitive(pui->convert_unknown_8bit_codeset,
			     balsa_app.convert_unknown_8bit);
}

enum {
    AB_TYPE_COLUMN,
    AB_NAME_COLUMN,
    AB_XPND_COLUMN,
    AB_DATA_COLUMN,
    AB_N_COLUMNS
};

static void
update_address_books(void)
{
    gchar *type, *name;
    GList *list = balsa_app.address_book_list;
    LibBalsaAddressBook *address_book;
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->address_books);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;

    gtk_list_store_clear(GTK_LIST_STORE(model));

    while (list) {
	address_book = LIBBALSA_ADDRESS_BOOK(list->data);

	g_assert(address_book != NULL);

	if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book))
	    type = "VCARD";
	else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book))
	    type = "LDIF";
#if ENABLE_LDAP
	else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
	    type = "LDAP";
#endif
	else
	    type = _("Unknown");

	if (address_book == balsa_app.default_address_book) {
	    name = g_strdup_printf(_("%s (default)"), address_book->name);
	} else {
	    name = g_strdup(address_book->name);
	}
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           AB_TYPE_COLUMN, type,
                           AB_NAME_COLUMN, name,
                           AB_XPND_COLUMN, address_book->expand_aliases,
                           AB_DATA_COLUMN, address_book,
                           -1);

	g_free(name);
	list = g_list_next(list);
    }

    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection
                                       (tree_view), &iter);
}

enum {
    MS_PROT_COLUMN,
    MS_NAME_COLUMN,
    MS_DATA_COLUMN,
    MS_N_COLUMNS
};

static void
add_other_server(GNode * node, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    gchar *protocol = NULL;
    gchar *name = NULL;
    gboolean append = FALSE;
    BalsaMailboxNode *mbnode = node->data;

    if (mbnode) {
	LibBalsaMailbox *mailbox = mbnode->mailbox;
	if (mailbox) {
	    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
		protocol = "IMAP";
		name = mailbox->name;
		append = TRUE;
	    }
	} else
	    if (mbnode->server
		&& mbnode->server->type == LIBBALSA_SERVER_IMAP) {
	    protocol = "IMAP";
	    name = mbnode->name;
	    append = TRUE;
	}
	if (append) {
            GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
            GtkTreeIter iter;

            gtk_list_store_append(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               MS_PROT_COLUMN, protocol,
                               MS_NAME_COLUMN, name,
                               MS_DATA_COLUMN, mbnode,
                               -1);
	}
    }
}

/* update_mail_servers:
   update mail server list in the preferences window.
   NOTE: it can be called even when the preferences window is closed (via
   mailbox context menu) - and it should check for it.
*/
void
update_mail_servers(void)
{
    GtkTreeView *tree_view;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *list = balsa_app.inbox_input;
    gchar *protocol;

    BalsaMailboxNode *mbnode;

    if(pui == NULL) return;

    tree_view = GTK_TREE_VIEW(pui->mail_servers);
    model = gtk_tree_view_get_model(tree_view);

    gtk_list_store_clear(GTK_LIST_STORE(model));
    while (list) {
	mbnode = list->data;
	if (mbnode) {
	    if (LIBBALSA_IS_MAILBOX_POP3(mbnode->mailbox))
		protocol = "POP3";
	    else if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
		protocol = "IMAP";
	    else 
		protocol = _("Unknown");

            gtk_list_store_append(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               MS_PROT_COLUMN, protocol,
                               MS_NAME_COLUMN, mbnode->mailbox->name,
                               MS_DATA_COLUMN, mbnode,
                               -1);
	}
	list = list->next;
    }
    /*
     * add other remote servers
     *
     * we'll check everything at the top level in the mailbox_nodes
     * list:
     */
    balsa_mailbox_nodes_lock(FALSE);
    g_node_children_foreach(balsa_app.mailbox_nodes, G_TRAVERSE_ALL,
			    (GNodeForeachFunc) add_other_server, tree_view);
    balsa_mailbox_nodes_unlock(FALSE);
    if (gtk_tree_model_get_iter_first(model, &iter))
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(tree_view),
                                       &iter);
}

/* helper functions that simplify often performed actions */
static GtkWidget*
attach_entry(const gchar* label,gint row, GtkTable *table)
{
    return attach_entry_full(label, row, table, 0, 1, 2);
}

static GtkWidget *
attach_entry_full(const gchar * label, gint row, GtkTable * table,
                  gint col_left, gint col_middle, gint col_right)
{
    GtkWidget * res, *lw;
    GtkWidget *page;

    res = gtk_entry_new();
    lw = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(lw), 0, 0.5);

    page = g_object_get_data(G_OBJECT(table), BALSA_TABLE_PAGE_KEY);
    pm_page_add_to_size_group(page, lw);

    gtk_table_attach(GTK_TABLE(table), lw,
                     col_left, col_middle,
                     row, row + 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify(GTK_LABEL(lw), GTK_JUSTIFY_RIGHT);
    
    gtk_table_attach(GTK_TABLE(table), res,
                     col_middle, col_right,
                     row, row + 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    return res;
}

static GtkWidget*
attach_information_menu(const gchar* label,gint row, GtkTable *table,
			gint defval)
{
    GtkWidget* w, *option_menu, *menu;
    w = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row+1,
		     GTK_FILL, 0, 0, 0);

    menu = create_information_message_menu();
    option_menu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu), defval);
    gtk_table_attach(GTK_TABLE(table), option_menu, 1, 2, row, row+1,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    return menu;
}

static GtkWidget*
box_start_check(const gchar* label, GtkWidget* box)
{
    GtkWidget *res = gtk_check_button_new_with_mnemonic(label);
    gtk_box_pack_start(GTK_BOX(box), res, FALSE, TRUE, 0);
    return res;
}

static void
add_button_to_box(const gchar*label, GCallback cb, GtkWidget* box)
{
    GtkWidget *button = gtk_button_new_with_mnemonic(label);
    g_signal_connect_swapped(G_OBJECT(button), "clicked", cb, NULL);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
}

static GtkWidget*
vbox_in_container(GtkWidget* container)
{
    GtkWidget* res = gtk_vbox_new(FALSE, ROW_SPACING);
    gtk_container_add(GTK_CONTAINER(container), res);
    return res;
}

static GtkWidget*
color_box(GtkBox* parent, const gchar* title)
{
    GtkWidget* box, *picker;
    box = gtk_hbox_new(FALSE, COL_SPACING);
    gtk_box_pack_start(parent, box, FALSE, FALSE, 0);

    picker = gnome_color_picker_new();
    gnome_color_picker_set_title(GNOME_COLOR_PICKER(picker), (gchar*)title);
    gnome_color_picker_set_dither(GNOME_COLOR_PICKER(picker),TRUE);
    gtk_box_pack_start(GTK_BOX(box), picker,  FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(title), FALSE, FALSE, 0);
    return picker;
}

static void
mail_servers_cb(GtkTreeView * tree_view, GtkTreePath * path,
                GtkTreeViewColumn * column, gpointer user_data)
{
    server_edit_cb(NULL, NULL);
}

static GtkWidget *
create_mailserver_page(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, remote_mailbox_servers_group(page));
    pm_page_add(page, local_mail_group(page));
#if ENABLE_ESMTP
    pm_page_add(page, outgoing_mail_group(page));
#endif

    return page;
}

static GtkWidget *
remote_mailbox_servers_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *scrolledwindow;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    group = pm_group_new(_("Remote Mailbox Servers"));
    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledwindow, -1, 150);
    pm_page_add_to_size_group(page, scrolledwindow);

    store = gtk_list_store_new(MS_N_COLUMNS, 
                               G_TYPE_STRING,   /* MS_PROT_COLUMN */
                               G_TYPE_STRING,   /* MS_NAME_COLUMN */
                               G_TYPE_POINTER); /* MS_DATA_COLUMN */
    pui->mail_servers = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Type"),
                                                 renderer,
                                                 "text", MS_PROT_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Mailbox Name"),
                                                 renderer,
                                                 "text", MS_NAME_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    gtk_container_add(GTK_CONTAINER(scrolledwindow), tree_view);

    g_signal_connect(G_OBJECT(pui->mail_servers), "row-activated",
                     G_CALLBACK(mail_servers_cb), NULL);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("_Add"),    G_CALLBACK(server_add_cb),  vbox);
    add_button_to_box(_("_Modify"), G_CALLBACK(server_edit_cb), vbox);
    add_button_to_box(_("_Delete"), G_CALLBACK(server_del_cb),  vbox);

    /* fill in data */
    update_mail_servers();

    return group;
}

static GtkWidget *
local_mail_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *fileentry;

    group = pm_group_new(_("Local Mail"));
    fileentry = gnome_file_entry_new("MAIL-DIR",
				      _("Select your local mail directory"));
    pm_group_add(group, fileentry);

    gnome_file_entry_set_directory_entry(GNOME_FILE_ENTRY(fileentry), TRUE);
    gnome_file_entry_set_modal(GNOME_FILE_ENTRY(fileentry), TRUE);

    pui->mail_directory =
	gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fileentry));

    return group;
}

#if ENABLE_ESMTP
static GtkWidget *
outgoing_mail_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table, *label;
    GtkWidget *optionmenu;

    group = pm_group_new(_("Outgoing Mail"));
    table = create_table(5, 2, page);
    pm_group_add(group, table);

    pui->smtp_server =
        attach_entry_full(_("Remote SMTP Server"), 0, GTK_TABLE(table),
                          0, 1, 2);
    pui->smtp_user = attach_entry_full(_("User"), 1, GTK_TABLE(table),
                                       0, 1, 2);
    pui->smtp_passphrase =
        attach_entry_full(_("Pass Phrase"), 2, GTK_TABLE(table), 0, 1, 2);
    gtk_entry_set_visibility (GTK_ENTRY(pui->smtp_passphrase), FALSE);

    /* STARTTLS */
    label = gtk_label_new(_("Use TLS"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 3, 4,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    optionmenu = gtk_option_menu_new ();
    pui->smtp_tls_mode_menu = create_tls_mode_menu();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (optionmenu),
			      pui->smtp_tls_mode_menu);
    gtk_option_menu_set_history (GTK_OPTION_MENU (optionmenu),
				 balsa_app.smtp_tls_mode);
    gtk_table_attach(GTK_TABLE(table), optionmenu, 1, 2, 3, 4,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    pui->smtp_certificate_passphrase =
        attach_entry_full(_("Certificate Pass Phrase"), 4,
                          GTK_TABLE(table), 0, 1, 2);
    gtk_entry_set_visibility (GTK_ENTRY(pui->smtp_certificate_passphrase),
                              FALSE);
#endif
    return group;
}
#endif

static GtkWidget *
create_mail_options_page(gpointer data)
{
    GtkWidget *note;

    note = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(note), HIG_PADDING);

    gtk_notebook_append_page(GTK_NOTEBOOK(note), incoming_subpage(data),
			     gtk_label_new(_("Incoming")));
    gtk_notebook_append_page(GTK_NOTEBOOK(note), outgoing_subpage(data),
			     gtk_label_new(_("Outgoing")));

    return note;
}

static GtkWidget *
incoming_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, checking_group(page));
    pm_page_add(page, mdn_group(page));

    return page;
}

static GtkWidget *
checking_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *spinbutton_adj;
    GtkWidget *label;

    group = pm_group_new(_("Checking"));
    table = create_table(4, 4, page);
    pm_group_add(group, table);

    pui->check_mail_auto = gtk_check_button_new_with_mnemonic(
	_("_Check mail automatically every:"));
    gtk_table_attach(GTK_TABLE(table), pui->check_mail_auto, 0, 1, 0, 1,
                     GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->check_mail_auto);

    spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->check_mail_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->check_mail_minutes, 1, 2, 0, 1,
                     0, 0, 0, 0);

    label = gtk_label_new(_("minutes"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    pui->check_imap = gtk_check_button_new_with_mnemonic(
	_("Check _IMAP mailboxes"));
    gtk_table_attach(GTK_TABLE(table), pui->check_imap, 0, 1, 1, 2,
                     GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->check_imap);
    
    pui->check_imap_inbox = gtk_check_button_new_with_mnemonic(
	_("Check INBOX _only"));
    gtk_table_attach(GTK_TABLE(table), pui->check_imap_inbox, 1, 3, 1, 2,
                     GTK_FILL, 0, 0, 0);
    
    pui->notify_new_mail_dialog = gtk_check_button_new_with_label(
	_("Display message if new mail has arrived in an open mailbox"));
    gtk_table_attach(GTK_TABLE(table), pui->notify_new_mail_dialog,
                     0, 4, 2, 3, GTK_FILL, 0, 0, 0);
    
    pui->quiet_background_check = gtk_check_button_new_with_label(
	_("Do background check quietly (no messages in status bar)"));
    gtk_table_attach(GTK_TABLE(table), pui->quiet_background_check,
                     0, 4, 3, 4, GTK_FILL, 0, 0, 0);
    
    return group;
}

static GtkWidget *
quoted_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *spinbutton_adj;
    GtkWidget *label;

    /* Quoted text regular expression */
    /* and RFC2646-style flowed text  */

    group = pm_group_new(_("Quoted and Flowed Text"));
    table = create_table(3, 3, page);
    pm_group_add(group, table);

    label = gtk_label_new(_("Quoted Text Regular Expression"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, label);

    pui->quote_pattern = gnome_entry_new("quote-regex-history");
    gtk_table_attach(GTK_TABLE(table), pui->quote_pattern,
                     1, 3, 0, 1, GTK_FILL, 0, 0, 0);

    pui->browse_wrap =
	gtk_check_button_new_with_label(_("Wrap Incoming Text at:"));
    gtk_table_attach(GTK_TABLE(table), pui->browse_wrap,
                     0, 1, 1, 2, GTK_FILL, 0, 0, 0);
    pm_page_add_to_size_group(page, pui->browse_wrap);

    spinbutton_adj = gtk_adjustment_new(1.0, 40.0, 200.0, 1.0, 5.0, 0.0);
    pui->browse_wrap_length =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->browse_wrap_length,
                     1, 2, 1, 2, 0, 0, 0, 0);
    gtk_widget_set_sensitive(pui->browse_wrap_length, FALSE);
    label = gtk_label_new(_("characters"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 1, 2,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return group;
}

static GtkWidget *
alternative_group(GtkWidget * page)
{
    GtkWidget *group;

    /* handling of multipart/alternative */

    group = pm_group_new(_("Display of Multipart/Alternative Parts"));

    pui->display_alt_plain =
	gtk_check_button_new_with_label(_("prefer text/plain over html"));
    pm_group_add(group, pui->display_alt_plain);
    pm_page_add_to_size_group(page, pui->display_alt_plain);

    return group;
}

static GtkWidget *
broken_8bit_codeset_group(GtkWidget * page)
{
    GtkSizeGroup *size_group = pm_page_get_size_group(page);
    GtkWidget *group;
    GtkWidget *table;
    GSList *radio_group = NULL;
    GtkWidget *menu;
    
    /* treatment of messages with 8-bit chars, but without proper MIME encoding */

    group = pm_group_new(_("National (8-bit) characters in broken messages without codeset header"));
    table = create_table(2, 2, page);
    pm_group_add(group, table);
    
    pui->convert_unknown_8bit[0] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display as \"?\"")));
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(pui->convert_unknown_8bit[0]),
		     0, 2, 0, 1,
		     (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
    radio_group = 
	gtk_radio_button_get_group(GTK_RADIO_BUTTON(pui->convert_unknown_8bit[0]));
    
    pui->convert_unknown_8bit[1] =
	GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(radio_group,
							 _("display using codeset")));
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(pui->convert_unknown_8bit[1]),
		     0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
    
    pui->convert_unknown_8bit_codeset = gtk_option_menu_new ();
    gtk_table_attach(GTK_TABLE(table), pui->convert_unknown_8bit_codeset,
		     1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    
    menu = create_codeset_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(pui->convert_unknown_8bit_codeset),
			     menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(pui->convert_unknown_8bit_codeset),
				balsa_app.convert_unknown_8bit_codeset);

    gtk_size_group_add_widget(size_group, table);
    
    return group;
}


static GtkWidget *
mdn_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label;
    GtkWidget *table;
    GtkWidget *optionmenu;

    /* How to handle received MDN requests */

    group = pm_group_new(_("Message Disposition Notification Requests"));

    label = gtk_label_new(_("When I receive a message and its sender "
                            "requested to return a\n"
                            "Message Disposition Notification (MDN), "
                            "send it in the following cases:"));
    pm_group_add(group, label);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    table = create_table(2, 2, page);
    pm_group_add(group, table);

    label = gtk_label_new(_("The message header looks clean\n"
                            "(the notify-to address is equal to the "
                            "return path,\n"
                            "I am in the \"To:\" or \"Cc:\" list)."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
                     GTK_FILL, 0, 0, 0);

    optionmenu = gtk_option_menu_new();
    pui->mdn_reply_clean_menu = create_mdn_reply_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(optionmenu),
                             pui->mdn_reply_clean_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(optionmenu),
                                balsa_app.mdn_reply_clean);
    gtk_table_attach(GTK_TABLE(table), optionmenu, 1, 2, 0, 1,
                     GTK_FILL | GTK_EXPAND, 0, 0, 0);

    label = gtk_label_new(_("The message header looks suspicious."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2,
                     GTK_FILL, (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    optionmenu = gtk_option_menu_new();
    pui->mdn_reply_notclean_menu = create_mdn_reply_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(optionmenu),
                             pui->mdn_reply_notclean_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(optionmenu),
                                balsa_app.mdn_reply_notclean);
    gtk_table_attach(GTK_TABLE(table), optionmenu, 1, 2, 1, 2,
                     GTK_FILL | GTK_EXPAND, 0, 0, 0);

    return group;
}

static GtkWidget *
outgoing_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, word_wrap_group(page));
    pm_page_add(page, other_options_group(page));
    pm_page_add(page, encoding_group(page));

    return page;
}

static GtkWidget *
word_wrap_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *spinbutton_adj;
    GtkWidget *label;

    group = pm_group_new(_("Word Wrap"));
    table = create_table(2, 3, page);
    pm_group_add(group, table);

    pui->wordwrap =
	gtk_check_button_new_with_label(_("Wrap Outgoing Text at:"));
    gtk_table_attach(GTK_TABLE(table), pui->wordwrap, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pm_page_add_to_size_group(page, pui->wordwrap);

    spinbutton_adj =
        gtk_adjustment_new(1.0, 40.0, 79.0, 1.0, 5.0, 0.0);
    pui->wraplength =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->wraplength, 1, 2, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->wraplength, FALSE);

    label = gtk_label_new(_("characters"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     GTK_EXPAND | GTK_FILL, (GtkAttachOptions) (0), 0, 0);

    return group;
}

static GtkWidget *
other_options_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Other Options"));

    table = create_table(1, 2, page);
    pm_group_add(group, table);

    pui->quote_str = attach_entry(_("Reply Prefix:"), 1, GTK_TABLE(table));

    pui->edit_headers =
        pm_group_add_check(group, _("Edit headers in external editor"));
    pui->autoquote =
        pm_group_add_check(group, _("Automatically quote original "
                                    "when replying"));
    pui->reply_strip_html_parts =
        pm_group_add_check(group, _("Don't include HTML parts as text "
                                    "when replying or forwarding mail"));
    pui->forward_attached =
        pm_group_add_check(group, _("Forward a mail as attachment "
                                    "instead of quoting it"));
    pui->always_queue_sent_mail =
        pm_group_add_check(group, _("Send button always queues "
                                    "outgoing mail in outbox"));
    pui->copy_to_sentbox =
        pm_group_add_check(group, _("Copy outgoing messages to sentbox"));

    return group;
}

static GtkWidget *
encoding_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *hbox;
    GtkWidget *optionmenu;
    gint i;

    group = pm_group_new(_("Encoding"));
    hbox = gtk_hbox_new(FALSE, 0);
    pm_group_add(group, hbox);

    optionmenu = gtk_option_menu_new ();
    gtk_box_pack_start(GTK_BOX(hbox), optionmenu, FALSE, FALSE, 0);

    pui->encoding_menu = create_encoding_menu();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (optionmenu), pui->encoding_menu);
    for (i = 0; i < NUM_ENCODING_MODES; i++)
	if (balsa_app.encoding_style == encoding_type[i]) {
	    gtk_option_menu_set_history (GTK_OPTION_MENU (optionmenu), i);
	    break;
	}

    return group;
}

static GtkWidget *
create_display_page(gpointer data)
{
    GtkWidget *note;

    note = gtk_notebook_new ();
    gtk_container_set_border_width (GTK_CONTAINER (note), HIG_PADDING);

    gtk_notebook_append_page (GTK_NOTEBOOK (note), display_subpage(data),
                              gtk_label_new (_("Display")));
    gtk_notebook_append_page (GTK_NOTEBOOK (note),
                              status_messages_subpage(data),
                              gtk_label_new (_("Status Messages")));
    gtk_notebook_append_page(GTK_NOTEBOOK(note), colors_subpage(data),
                             gtk_label_new(_("Colors")));
    gtk_notebook_append_page(GTK_NOTEBOOK(note), message_subpage(data),
                             gtk_label_new(_("Message")));
    gtk_notebook_append_page(GTK_NOTEBOOK(note), threading_subpage(data),
                             gtk_label_new(_("Threading")));

    return note;
}

static GtkWidget *
display_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, main_window_group(page));
    pm_page_add(page, progress_group(page));
    pm_page_add(page, display_formats_group(page));

    return page;
}

static GtkWidget *
main_window_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkObject *scroll_adj;
    GtkWidget *label;

    group = pm_group_new(_("Main Window"));

    pui->previewpane =
        pm_group_add_check(group, _("Use preview pane"));
    pui->mblist_show_mb_content_info =
        pm_group_add_check(group, _("Show mailbox statistics in left pane"));
    pui->alternative_layout =
        pm_group_add_check(group, _("Use alternative main window layout"));
    pui->view_message_on_open =
        pm_group_add_check(group, _("Automatically view message "
                                    "when mailbox opened"));
    pui->line_length =
        pm_group_add_check(group, _("Display message size as number of lines"));

    table = create_table(1, 3, page);
    pm_group_add(group, table);
    pui->pgdownmod =
        gtk_check_button_new_with_label(_("PageUp/PageDown keys "
                                          "scroll message by:"));
    gtk_table_attach(GTK_TABLE(table), pui->pgdownmod, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    scroll_adj = gtk_adjustment_new(50.0, 10.0, 100.0, 5.0, 10.0, 0.0);
    pui->pgdown_percent =
	 gtk_spin_button_new(GTK_ADJUSTMENT(scroll_adj), 1, 0);
    gtk_widget_set_sensitive(pui->pgdown_percent, FALSE);
    gtk_table_attach(GTK_TABLE(table), pui->pgdown_percent, 1, 2, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    label = gtk_label_new(_("percent"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     GTK_FILL, (GtkAttachOptions) (0), 0, 0);

    return group;
}

static GtkWidget *
progress_group(GtkWidget * page)
{
    GtkWidget *group;
    GSList *radio_group;
    gint i;

    group = pm_group_new(_("Display Progress Dialog"));

    radio_group = NULL;
    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	pui->pwindow_type[i] =
	    GTK_RADIO_BUTTON(gtk_radio_button_new_with_label
			     (radio_group, _(pwindow_type_label[i])));
	pm_group_add(group, GTK_WIDGET(pui->pwindow_type[i]));
	radio_group = gtk_radio_button_get_group(pui->pwindow_type[i]);
    }

    return group;
}

static GtkWidget *
display_formats_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Encoding"));
    table = create_table(2, 2, page);
    pm_group_add(group, table);
    
    pui->date_format =
        attach_entry(_("Date encoding (for strftime):"), 0, GTK_TABLE(table));
    pui->selected_headers =
        attach_entry(_("Selected headers:"), 1, GTK_TABLE(table));
    pui->message_title_format =
        attach_entry(_("Message window title format:"), 2, GTK_TABLE(table));

    return group;
}

static GtkWidget *
status_messages_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, information_messages_group(page));

    return page;
}

static GtkWidget *
information_messages_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;

    group = pm_group_new(_("Information Messages"));
    table = create_table(5, 2, page);
    pm_group_add(group, table);
    
    pui->information_message_menu = 
	attach_information_menu(_("Information Messages"), 0, 
				GTK_TABLE(table),
				balsa_app.information_message);
    pui->warning_message_menu =
	attach_information_menu(_("Warning Messages"), 1,
				GTK_TABLE(table),
				balsa_app.warning_message);
    pui->error_message_menu = 
	attach_information_menu(_("Error Messages"), 2,
				GTK_TABLE(table),
				balsa_app.error_message);
    pui->fatal_message_menu = 
	attach_information_menu(_("Fatal Error Messages"), 3,
				GTK_TABLE(table), 
				balsa_app.fatal_message);
    pui->debug_message_menu = 
	attach_information_menu(_("Debug Messages"), 4,
				GTK_TABLE(table),
				balsa_app.debug_message);

    return group;
}

static GtkWidget *
colors_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, message_colors_group(page));
    pm_page_add(page, link_color_group(page));
    pm_page_add(page, composition_window_group(page));

    return page;
}

static GtkWidget *
message_colors_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;
    gint i;
    
    group = pm_group_new(_("Message Colors"));
    vbox = gtk_vbox_new(TRUE, HIG_PADDING);
    pm_group_add(group, vbox);

    for(i = 0; i < MAX_QUOTED_COLOR; i++) {
        gchar *text = g_strdup_printf(_("Quote level %d color"), i+1);
        pui->quoted_color[i] = color_box( GTK_BOX(vbox), text);
        g_free(text);
    }

    return group;
}

static GtkWidget *
link_color_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Link Color"));
    pui->url_color =
        color_box(GTK_BOX(pm_group_get_vbox(group)), _("Hyperlink color"));

    return group;
}

static GtkWidget *
composition_window_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;

    group = pm_group_new(_("Composition Window"));
    vbox = pm_group_get_vbox(group);
    pui->bad_address_color =
        color_box(GTK_BOX(vbox),
                  _("Invalid or incomplete address label color"));

    return group;
}

static GtkWidget *
message_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, preview_font_group(page));
    pm_page_add(page, quoted_group(page));
    pm_page_add(page, alternative_group(page));
    pm_page_add(page, broken_8bit_codeset_group(page));

    return page;
}

static GtkWidget *
preview_font_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *table;
    GtkWidget *label;

    group = pm_group_new(_("Fonts"));
    table = create_table(4, 2, page);
    pm_group_add(group, table);

    pui->message_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), pui->message_font, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    pui->font_picker = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table), pui->font_picker, 1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);

    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker),
				    balsa_app.message_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER(pui->font_picker),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);
    label = gtk_label_new(_("Select..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker),
				    GTK_WIDGET(label));
    g_object_set_data(G_OBJECT(pui->font_picker), "balsa-data",
		      GTK_OBJECT(pui->message_font));
    g_object_set_data(G_OBJECT(pui->message_font), "balsa-data",
		      GTK_OBJECT(pui->font_picker));
    label = gtk_label_new(_("Message Font"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL), 0, 0, 0);

    pui->subject_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), pui->subject_font, 0, 1, 3, 4,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    pui->font_picker2 = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table), pui->font_picker2, 1, 2, 3, 4,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker2),
				    balsa_app.subject_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER
				       (pui->font_picker2),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker2),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);
    label = gtk_label_new(_("Select..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker2),
				    GTK_WIDGET(label));
    g_object_set_data(G_OBJECT(pui->font_picker2), "balsa-data",
			     GTK_OBJECT(pui->subject_font));
    g_object_set_data(G_OBJECT(pui->subject_font), "balsa-data",
			     GTK_OBJECT(pui->font_picker2));

    label = gtk_label_new(_("Message Subject Font"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
		     (GtkAttachOptions) (GTK_FILL), 0, 0, 0);

    return group;
}

static GtkWidget *
threading_subpage(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, threading_group(page));

    return page;
}

static GtkWidget *
threading_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *vbox;

    group = pm_group_new(_("Threading"));

    pui->tree_expand_check =
        pm_group_add_check(group, _("Expand mailbox tree on open"));
    
    vbox = pm_group_get_vbox(group);
    pui->default_threading_style = 
        add_pref_menu(_("Default threading style"), threading_style_label, 
                      NUM_THREADING_STYLES, &pui->threading_style_index, 
                      GTK_BOX(vbox), 2 * HIG_PADDING, 
                      G_CALLBACK(threading_optionmenu_cb));
    
    return group;
}

static GtkWidget*
add_pref_menu(const gchar* label, const gchar *names[], gint size, 
	       gint *index, GtkBox* parent, gint padding, 
               GtkSignalFunc callback)
{
    GtkWidget *omenu;
    GtkWidget *hbox, *lbw;

    omenu = create_pref_option_menu(names, size, index, callback);

    hbox = gtk_hbox_new(FALSE, padding);
    lbw = gtk_label_new(label);
    gtk_box_pack_start(GTK_BOX(hbox), lbw,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), omenu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_hbox_new(FALSE, 0), TRUE, TRUE, 0);
    gtk_box_pack_start(parent,        hbox,  FALSE, FALSE, 0);
    return omenu;
}

static GtkWidget *
create_table(gint rows, gint cols, GtkWidget * page)
{
    GtkWidget *table;

    table = gtk_table_new(rows, cols, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), ROW_SPACING);
    gtk_table_set_col_spacings(GTK_TABLE(table), COL_SPACING);
    g_object_set_data(G_OBJECT(table), BALSA_TABLE_PAGE_KEY, page);

    return table;
}

static GtkWidget *
attach_pref_menu(const gchar * label, gint row, GtkTable * table,
                 const gchar * names[], gint size, gint * index,
                 GCallback callback)
{
    GtkWidget *w, *omenu;

    w = gtk_label_new(label);
    gtk_misc_set_alignment(GTK_MISC(w), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
                     GTK_FILL, 0, 0, 0);

    omenu = create_pref_option_menu(names, size, index, callback);
    gtk_table_attach(GTK_TABLE(table), omenu, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return omenu;
}

static GtkWidget *
create_spelling_page(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, pspell_settings_group(page));
    pm_page_add(page, misc_spelling_group(page));

    return page;
}

static GtkWidget *
pspell_settings_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkObject *ignore_adj;
    GtkWidget *table;
    GtkWidget *label;
    GtkWidget *hbox;

    group = pm_group_new(_("Pspell Settings"));
    table = create_table(3, 2, page);
    pm_group_add(group, table);

    /* do the module menu */
    pui->module =
        attach_pref_menu(_("Spell Check Module"), 0, GTK_TABLE(table),
                         spell_check_modules_name, NUM_PSPELL_MODULES,
                         &pui->module_index,
                         G_CALLBACK(spelling_optionmenu_cb));

    /* do the suggestion modes menu */
    pui->suggestion_mode =
        attach_pref_menu(_("Suggestion Level"), 1, GTK_TABLE(table),
                         spell_check_suggest_mode_label, NUM_SUGGEST_MODES,
                         &pui->suggestion_mode_index,
                         G_CALLBACK(spelling_optionmenu_cb));

    /* do the ignore length */
    label = gtk_label_new(_("Ignore words shorter than"));
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 2, 3,
                     GTK_FILL, 0, 0, 0);
    ignore_adj = gtk_adjustment_new(0.0, 0.0, 99.0, 1.0, 5.0, 0.0);
    pui->ignore_length =
        gtk_spin_button_new(GTK_ADJUSTMENT(ignore_adj), 1, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->ignore_length, FALSE, FALSE, 0);
    gtk_table_attach(GTK_TABLE(table), hbox, 1, 2, 2, 3,
                     GTK_FILL, 0, 0, 0);

    return group;
}

static GtkWidget *
misc_spelling_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Miscellaneous Spelling Settings"));

    pui->spell_check_sig = pm_group_add_check(group, _("Check signature"));
    pui->spell_check_quoted = pm_group_add_check(group, _("Check quoted"));

    return group;
}

static GtkWidget *
create_misc_page(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, misc_group(page));
    pm_page_add(page, deleting_messages_group(page));

    return page;
}

static GtkWidget *
misc_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label1, *label2;
    GtkWidget *hbox1,*hbox2;
    GtkObject *close_spinbutton_adj;
    GtkObject *commit_spinbutton_adj;

    group = pm_group_new(_("Miscellaneous"));

    pui->debug = pm_group_add_check(group, _("Debug"));
    pui->empty_trash = pm_group_add_check(group, _("Empty Trash on exit"));

    hbox1 = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox1);

    pui->close_mailbox_auto =
	gtk_check_button_new_with_label(_("Automatically close mailbox "
                                          "if unused more than"));
    gtk_box_pack_start(GTK_BOX(hbox1), pui->close_mailbox_auto,
                       FALSE, FALSE, 0);
    pm_page_add_to_size_group(page, pui->close_mailbox_auto);

    close_spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->close_mailbox_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(close_spinbutton_adj), 1, 0);
    gtk_widget_show(pui->close_mailbox_minutes);
    gtk_widget_set_sensitive(pui->close_mailbox_minutes, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox1), pui->close_mailbox_minutes,
                       FALSE, FALSE, 0);

    label1 = gtk_label_new(_("minutes"));
    gtk_box_pack_start(GTK_BOX(hbox1), label1, FALSE, TRUE, 0);


    hbox2 = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox2);

    pui->commit_mailbox_auto =
	gtk_check_button_new_with_label(_("Automatically commit mailbox "
                                          "if unused more than"));
    gtk_box_pack_start(GTK_BOX(hbox2), pui->commit_mailbox_auto,
                       FALSE, FALSE, 0);
    pm_page_add_to_size_group(page, pui->commit_mailbox_auto);

    commit_spinbutton_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->commit_mailbox_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(commit_spinbutton_adj), 1, 0);
    gtk_widget_show(pui->commit_mailbox_minutes);
    gtk_widget_set_sensitive(pui->commit_mailbox_minutes, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox2), pui->commit_mailbox_minutes,
                       FALSE, FALSE, 0);
    label2 = gtk_label_new(_("minutes"));
    gtk_box_pack_start(GTK_BOX(hbox2), label2, FALSE, TRUE, 0);

    return group;
}

static GtkWidget *
deleting_messages_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Deleting Messages"));

    pui->delete_immediately =
        pm_group_add_check(group, _("Delete immediately "
                                    "and irretrievably"));
    return group;
}

static GtkWidget *
create_startup_page(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, options_group(page));
    pm_page_add(page, imap_folder_scanning_group(page));

    return page;
}

static GtkWidget *
options_group(GtkWidget * page)
{
    GtkWidget *group;

    group = pm_group_new(_("Options"));

    pui->open_inbox_upon_startup =
        pm_group_add_check(group, _("Open Inbox upon startup"));
    pui->check_mail_upon_startup =
        pm_group_add_check(group, _("Check mail upon startup"));
    pui->remember_open_mboxes =
        pm_group_add_check(group, _("Remember open mailboxes "
                                    "between sessions"));

    return group;
}

static GtkWidget *
imap_folder_scanning_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *label;
    GtkWidget *hbox;
    GtkObject *scan_adj;

    group = pm_group_new(_("IMAP Folder Scanning"));

    label = gtk_label_new(_("Choose depth 1 for fast startup; "
                            "this defers scanning some folders.\n"
                            "To see more of the tree at startup, "
                            "choose a greater depth."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    pm_group_add(group, label);

    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox);
    gtk_box_pack_start(GTK_BOX(hbox),
                       gtk_label_new(_("Scan tree to depth")),
                       FALSE, FALSE, 0);
    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->imap_scan_depth =
        gtk_spin_button_new(GTK_ADJUSTMENT(scan_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->imap_scan_depth,
                       FALSE, FALSE, 0);

    return group;
}

static GtkWidget *
create_address_book_page(gpointer data)
{
    GtkWidget *page = pm_page_new();

    pm_page_add(page, address_books_group(page));

    return page;
}

static GtkWidget *
address_books_group(GtkWidget * page)
{
    GtkWidget *group;
    GtkWidget *tree_view;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkWidget *hbox;
    GtkWidget *scrolledwindow;
    GtkWidget *vbox;

    group = pm_group_new(_("Address Books"));
    hbox = gtk_hbox_new(FALSE, COL_SPACING);
    pm_group_add(group, hbox);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolledwindow, -1, 150);

    store = gtk_list_store_new(AB_N_COLUMNS,
                               G_TYPE_STRING,   /* AB_TYPE_COLUMN */
                               G_TYPE_STRING,   /* AB_NAME_COLUMN */
                               G_TYPE_BOOLEAN,  /* AB_XPND_COLUMN */
                               G_TYPE_POINTER); /* AB_DATA_COLUMN */
    pui->address_books = tree_view =
        gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Type"),
                                                 renderer,
                                                 "text", AB_TYPE_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Address Book Name"),
                                                 renderer,
                                                 "text", AB_NAME_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    renderer = gtk_cell_renderer_toggle_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Expand aliases"),
                                                 renderer,
                                                 "active",
                                                 AB_XPND_COLUMN,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection
                                (GTK_TREE_VIEW(tree_view)),
                                GTK_SELECTION_BROWSE);

    gtk_container_add(GTK_CONTAINER(scrolledwindow), tree_view);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("_Add"),
                      G_CALLBACK(address_book_add_cb),         vbox);
    add_button_to_box(_("_Modify"),
                      G_CALLBACK(address_book_edit_cb),        vbox);
    add_button_to_box(_("_Delete"),         
                      G_CALLBACK(address_book_delete_cb),      vbox);
    add_button_to_box(_("_Set as default"), 
                      G_CALLBACK(set_default_address_book_cb), vbox);

    update_address_books();

    return group;
}


/*
 * callbacks
 */
static void
properties_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_OK,
                                      TRUE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(pbox), GTK_RESPONSE_APPLY,
                                      TRUE);
}


static void
font_changed_cb(GtkWidget * widget, GtkWidget * pbox)
{
    const gchar *font;
    GtkWidget *peer;
    if (GNOME_IS_FONT_PICKER(widget)) {
	font = gnome_font_picker_get_font_name(GNOME_FONT_PICKER(widget));
	peer = g_object_get_data(G_OBJECT(widget), "balsa-data");
	gtk_entry_set_text(GTK_ENTRY(peer), font);
    } else {
	font = gtk_entry_get_text(GTK_ENTRY(widget));
        /* called once on deleting the text, and again on inserting the
         * new text--we must just return the first time */
        if (font[0] == '\0')
            return;
	peer = g_object_get_data(G_OBJECT(widget), "balsa-data");
	gnome_font_picker_set_font_name(GNOME_FONT_PICKER(peer), font);
	properties_modified_cb(widget, pbox);
    }
}

static void
server_edit_cb(GtkWidget * widget, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->mail_servers);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, MS_DATA_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode);
    balsa_mailbox_node_show_prop_dialog(mbnode);
}

static void
address_book_change(LibBalsaAddressBook * address_book, gboolean append)
{
    if (append)
        balsa_app.address_book_list =
            g_list_append(balsa_app.address_book_list, address_book);
    config_address_book_save(address_book);
    update_address_books();
}

static void
address_book_edit_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->address_books);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);

    balsa_address_book_config_new(address_book, address_book_change,
                                  GTK_WINDOW(property_box));
}

static void
set_default_address_book_cb(GtkWidget * button, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->address_books);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);
    balsa_app.default_address_book = address_book;
    update_address_books();
}

static void
address_book_add_cb(GtkWidget * widget, gpointer data)
{
    balsa_address_book_config_new(NULL, address_book_change,
                                  GTK_WINDOW(property_box));
}

static void
address_book_delete_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->address_books);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, AB_DATA_COLUMN, &address_book, -1);

    g_assert(address_book != NULL);

    config_address_book_delete(address_book);
    balsa_app.address_book_list =
	g_list_remove(balsa_app.address_book_list, address_book);
    if (balsa_app.default_address_book == address_book)
	balsa_app.default_address_book = NULL;

    g_object_unref(address_book);

    update_address_books();
}

static void
pop3_add_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_POP3);
}

static void
server_add_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *menu;
    GtkWidget *menuitem;

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Remote POP3 mailbox..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(pop3_add_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP mailbox..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(mailbox_conf_add_imap_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP folder..."));
    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(folder_conf_add_imap_cb), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show(menuitem);
    gtk_widget_show(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
}

static void
server_del_cb(GtkWidget * widget, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(pui->mail_servers);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model;
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, MS_DATA_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode);

    if (mbnode->mailbox)
	mailbox_conf_delete(mbnode);
    else
	folder_conf_delete(mbnode);
}

void
timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate = 
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->check_mail_minutes), newstate);
    properties_modified_cb(widget, pbox);
}

static void
browse_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->browse_wrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->browse_wrap_length), newstate);
    properties_modified_cb(widget, pbox);
}

static void
wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->wraplength), newstate);
    properties_modified_cb(widget, pbox);
}

static void
pgdown_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->pgdownmod));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->pgdown_percent), newstate);
    properties_modified_cb(widget, pbox);
}

static void
spelling_optionmenu_cb(GtkItem * menuitem, gpointer data)
{
    /* update the index number */
    gint *index = (gint *) data;
    *index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem),
                                               "menu_index"));

}


static void
threading_optionmenu_cb(GtkItem* menuitem, gpointer data)
{
    /* update the index number */
    gint *index = (gint*) data;
    *index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menuitem),
                                               "menu_index"));
}


static GtkWidget *
create_pref_option_menu(const gchar * names[], gint size, gint * index, GtkSignalFunc callback)
{
    GtkWidget *omenu;
    GtkWidget *gmenu;
    GtkWidget *menuitem;
    gint i;

    omenu = gtk_option_menu_new();
    gmenu = gtk_menu_new();

    for (i = 0; i < size; i++) {
	menuitem = gtk_menu_item_new_with_label(names[i]);
	g_object_set_data(G_OBJECT(menuitem), "menu_index",
			    GINT_TO_POINTER(i));
	g_signal_connect(G_OBJECT(menuitem), "select",
                         G_CALLBACK(callback), (gpointer) index);
	g_signal_connect(G_OBJECT(menuitem), "select",
			 G_CALLBACK(properties_modified_cb), property_box);

	gtk_menu_shell_append(GTK_MENU_SHELL(gmenu), menuitem);
    }

    gtk_option_menu_set_menu(GTK_OPTION_MENU(omenu), gmenu);

    return omenu;
}


static void
add_show_menu(const char* label, gint level, GtkWidget* menu)
{
    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
    gtk_widget_show(menu_item);
    g_object_set_data(G_OBJECT(menu_item), "balsa-data",
                      GINT_TO_POINTER(level));
    g_signal_connect(G_OBJECT(menu_item), "activate",
		     G_CALLBACK(properties_modified_cb), property_box);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
}

static GtkWidget *
create_information_message_menu(void)
{
    GtkWidget *menu = gtk_menu_new();
    add_show_menu(_("Show nothing"),     BALSA_INFORMATION_SHOW_NONE,   menu);
    add_show_menu(_("Show dialog"),      BALSA_INFORMATION_SHOW_DIALOG, menu);
    add_show_menu(_("Show in list"),     BALSA_INFORMATION_SHOW_LIST,   menu);
    add_show_menu(_("Print to console"), BALSA_INFORMATION_SHOW_STDERR, menu);
    return menu;
}

static GtkWidget *
create_encoding_menu(void)
{
    gint i;

    GtkWidget *menu = gtk_menu_new();
    for (i = 0; i < NUM_ENCODING_MODES; i++)
	add_show_menu(_(encoding_type_label[i]), encoding_type[i], menu );
    return menu;
}

static GtkWidget *
create_mdn_reply_menu(void)
{
    GtkWidget *menu = gtk_menu_new();
    add_show_menu(_("Never"),  BALSA_MDN_REPLY_NEVER,  menu);
    add_show_menu(_("Ask me"), BALSA_MDN_REPLY_ASKME,  menu);
    add_show_menu(_("Always"), BALSA_MDN_REPLY_ALWAYS, menu);
    return menu;
}

static GtkWidget *
create_codeset_menu(void)
{
    LibBalsaCodeset n;
    GtkWidget *menu = gtk_menu_new();
    
    for (n = WEST_EUROPE; n <= KOREA; n++)
	add_show_menu(_(codeset_label[n]), n, menu);
    return menu;
}

void
mailbox_close_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	    pui->close_mailbox_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->close_mailbox_minutes),
			     newstate);

    properties_modified_cb(widget, pbox);
}

void
mailbox_commit_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	    pui->commit_mailbox_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->commit_mailbox_minutes),
			     newstate);

    properties_modified_cb(widget, pbox);
}

static void imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox)
{
    properties_modified_cb(widget, pbox);

    if(GTK_TOGGLE_BUTTON(pui->check_imap)->active) 
	gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), TRUE);
    else {
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(pui->check_imap_inbox), FALSE);
	gtk_widget_set_sensitive(GTK_WIDGET(pui->check_imap_inbox), FALSE);
    }
}

static void 
convert_8bit_cb(GtkWidget * widget, GtkWidget * pbox)
{
    properties_modified_cb(widget, pbox);

    gtk_widget_set_sensitive(pui->convert_unknown_8bit_codeset,
			     GTK_TOGGLE_BUTTON(pui->convert_unknown_8bit[1])->active);
}

#if ENABLE_ESMTP
static GtkWidget *
create_tls_mode_menu(void)
{
    GtkWidget *menu = gtk_menu_new();
    add_show_menu(_("Never"),       Starttls_DISABLED, menu);
    add_show_menu(_("If Possible"), Starttls_ENABLED,  menu);
    add_show_menu(_("Required"),    Starttls_REQUIRED, menu);
    return menu;
}
#endif

/* refresh any data displayed in the preferences manager
 * window in case it has changed */
void
refresh_preferences_manager(void)
{
    if (pui == NULL)
        return;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->browse_wrap),
                                 balsa_app.browse_wrap);
}

static void
balsa_help_pbox_display(gint page_num)
{
    gchar *link_id = g_strdup_printf("preferences-%d", page_num);
    GError *err = NULL;

    gnome_help_display("balsa", link_id, &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
		_("Error displaying link_id %s: %s\n"),
		link_id, err->message);
        g_error_free(err);
    }

    g_free(link_id);
}

/* pm_page: methods for making the contents of a notebook page
 *
 * pm_page_new:            creates a vbox with the desired border width
 *                         and inter-group spacing
 *
 * pm_page_add:            adds a child to the contents
 *
 * pm_page_get_size_group: get the GtkSizeGroup for the page; this is
 *                         used to make all left-most widgets the same
 *                         size, to line up the second widget in each row
 *
 * because we use size-groups to align widgets, we could pack each row
 * in an hbox instead of using tables, but the tables are convenient
 */
static GtkWidget *
pm_page_new(void)
{
    GtkWidget *page;
    GtkSizeGroup *size_group;

    page = gtk_vbox_new(FALSE, GROUP_SPACING);
    gtk_container_set_border_width(GTK_CONTAINER(page), BORDER_WIDTH);

    size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    g_object_set_data_full(G_OBJECT(page), BALSA_PAGE_SIZE_GROUP_KEY,
                           size_group, g_object_unref);

    return page;
}

static void
pm_page_add(GtkWidget * page, GtkWidget * child)
{
    gtk_box_pack_start(GTK_BOX(page), child, FALSE, FALSE, 0);
}

static GtkSizeGroup *
pm_page_get_size_group(GtkWidget * page)
{
    return (GtkSizeGroup *) g_object_get_data(G_OBJECT(page),
                                              BALSA_PAGE_SIZE_GROUP_KEY);
}

static void
pm_page_add_to_size_group(GtkWidget * page, GtkWidget * child)
{
    GtkSizeGroup *size_group;

    size_group = pm_page_get_size_group(page);
    gtk_size_group_add_widget(size_group, child);
}

/* pm_group: methods for making groups of controls, to be added to a
 * page
 *
 * pm_group_new:       creates a box containing a bold title,
 *                     and an inner vbox that indents its contents
 *
 * pm_group_get_vbox:  returns the inner vbox
 *
 * pm_group_add:       adds a child to the inner vbox
 *
 * pm_group_add_check: uses box_start_check to create a check-box
 *                     with the given title, and adds it
 *                     to the group's vbox
 */
#define BALSA_GROUP_VBOX_KEY "balsa-group-vbox"
static GtkWidget *
pm_group_new(const gchar * text)
{
    GtkWidget *group;
    GtkWidget *label;
    gchar *markup;
    GtkWidget *hbox;
    GtkWidget *vbox;

    group = gtk_vbox_new(FALSE, HEADER_SPACING);

    label = gtk_label_new(NULL);
    markup = g_strdup_printf("<b>%s</b>", text);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(group), label, FALSE, FALSE, 0);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(group), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("    "),
                       FALSE, FALSE, 0);
    vbox = gtk_vbox_new(FALSE, ROW_SPACING);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(group), BALSA_GROUP_VBOX_KEY, vbox);

    return group;
}

static GtkWidget *
pm_group_get_vbox(GtkWidget * group)
{
    return GTK_WIDGET(g_object_get_data(G_OBJECT(group),
                                        BALSA_GROUP_VBOX_KEY));
}

static void
pm_group_add(GtkWidget * group, GtkWidget * child)
{
    gtk_box_pack_start(GTK_BOX(pm_group_get_vbox(group)), child,
                       FALSE, FALSE, 0);
}

static GtkWidget *
pm_group_add_check(GtkWidget * group, const gchar * text)
{
    return box_start_check(text, pm_group_get_vbox(group));
}
