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

/* MAKE SURE YOU USE THE HELPER FUNCTIONS, like create_table(), etc. */
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

/* FIXME: Mutt dependency for ENC7BIT ENC8BIT ENCQUOTEDPRINTABLE consts*/
#include "../libmutt/mime.h"

#if ENABLE_ESMTP
#include <libesmtp.h>
#endif

#define NUM_TOOLBAR_MODES 3
#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3
#define NUM_THREADING_STYLES 3

typedef struct _PropertyUI {
    GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];

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
    GtkRadioButton *encoding_type[NUM_ENCODING_MODES];
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;
    GtkWidget *quiet_background_check;
    GtkWidget *check_imap;
    GtkWidget *check_imap_inbox;
    GtkWidget *notify_new_mail_dialog;
    GtkWidget *mdn_reply_clean_menu, *mdn_reply_notclean_menu;

    GtkWidget *close_mailbox_auto;
    GtkWidget *close_mailbox_minutes;
    GtkWidget *drag_default_is_move;
    GtkWidget *delete_immediately;
    GtkWidget *hide_deleted;

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
    GtkWidget *send_rfc2646_format_flowed;
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
    GtkWidget *extern_editor_command;
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
    GtkWidget *unread_color;
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
    GtkWidget *recognize_rfc2646_format_flowed;

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

static GtkWidget *create_mailserver_page(gpointer);
static GtkWidget *create_mailoptions_page(gpointer);
static GtkWidget *create_display_page(gpointer);
static GtkWidget *create_misc_page(gpointer);
static GtkWidget *create_startup_page(gpointer);
static GtkWidget *create_spelling_page(gpointer);
static GtkWidget *create_address_book_page(gpointer);

static GtkWidget *create_information_message_menu(void);
static GtkWidget *create_mdn_reply_menu(void);
#if ENABLE_ESMTP
static GtkWidget *create_tls_mode_menu(void);
#endif

static GtkWidget *incoming_page(gpointer);
static GtkWidget *outgoing_page(gpointer);
static void destroy_pref_window_cb(GtkWidget * pbox, gpointer data);
static void set_prefs(void);
static void apply_prefs(GnomePropertyBox * pbox, gint page_num);
void update_mail_servers(void);
static void update_address_books(void);
static void properties_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void font_changed(GtkWidget * widget, GtkWidget * pbox);
static void mail_servers_cb(GtkCList * clist, gint row, gint column,
                            GdkEventButton * event, gpointer data);
static void server_edit_cb(GtkWidget * widget, gpointer data);
static void pop3_add(GtkWidget * widget, gpointer data);
static void server_add_cb(GtkWidget * widget, gpointer data);
static void server_del_cb(GtkWidget * widget, gpointer data);
static void address_book_edit_cb(GtkWidget * widget, gpointer data);
static void address_book_add_cb(GtkWidget * widget, gpointer data);
static void address_book_delete_cb(GtkWidget * widget, gpointer data);
static void timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void mailbox_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void browse_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void pgdown_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static GtkWidget* add_pref_menu(const gchar* label, const gchar* names[], 
                                gint size, gint* index, GtkBox* parent, 
                                gint padding, GtkSignalFunc callback);
static GtkWidget* create_pref_option_menu(const gchar* names[], gint size, 
                                          gint* index, GtkSignalFunc callback);
static void spelling_optionmenu_cb(GtkItem * menuitem, gpointer data);
static void threading_optionmenu_cb(GtkItem* menuitem, gpointer data);
static void set_default_address_book_cb(GtkWidget * button, gpointer data);
static void imap_toggled_cb(GtkWidget * widget, GtkWidget * pbox);
#if BALSA_MAJOR < 2
#else
static void balsa_help_pbox_display(GnomePropertyBox * property_box,
                                    gint page_num);
#endif                          /* BALSA_MAJOR < 2 */

guint toolbar_type[NUM_TOOLBAR_MODES] = {
    GTK_TOOLBAR_TEXT,
    GTK_TOOLBAR_ICONS,
    GTK_TOOLBAR_BOTH
};

gchar *toolbar_type_label[NUM_TOOLBAR_MODES] = {
    N_("Text"),
    N_("Icons"),
    N_("Both"),
};

guint encoding_type[NUM_ENCODING_MODES] = {
    ENC7BIT,
    ENC8BIT,
    ENCQUOTEDPRINTABLE
};

gchar *encoding_type_label[NUM_ENCODING_MODES] = {
    N_("7bits"),
    N_("8bits"),
    N_("quoted")
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


/* and now the important stuff: */
void
open_preferences_manager(GtkWidget * widget, gpointer data)
{
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

    property_box = gnome_property_box_new();

    already_open = TRUE;

    gtk_window_set_title(GTK_WINDOW(property_box), _("Balsa Preferences"));
    gtk_window_set_policy(GTK_WINDOW(property_box), FALSE, FALSE, FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(property_box), "preferences", "Balsa");
    gnome_dialog_set_parent(GNOME_DIALOG(property_box),
			    GTK_WINDOW(active_win));
    gtk_object_set_data(GTK_OBJECT(property_box), "balsawindow",
			(gpointer) active_win);

    /* Create the pages */
    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_mailserver_page(property_box),
				   gtk_label_new(_("Mail Servers")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_address_book_page(property_box),
				   gtk_label_new(_("Address Books")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_mailoptions_page(property_box),
				   gtk_label_new(_("Mail Options")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_display_page(property_box),
				   gtk_label_new(_("Display")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_spelling_page(property_box),
				   gtk_label_new(_("Spelling")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_misc_page(property_box),
				   gtk_label_new(_("Misc")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_startup_page(property_box),
				   gtk_label_new(_("Startup")));

    set_prefs();

    for (i = 0; i < NUM_TOOLBAR_MODES; i++) {
	gtk_signal_connect(GTK_OBJECT(pui->toolbar_type[i]), "clicked",
			   GTK_SIGNAL_FUNC(properties_modified_cb),
                           property_box);
    }

    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	gtk_signal_connect(GTK_OBJECT(pui->pwindow_type[i]), "clicked",
			   GTK_SIGNAL_FUNC(properties_modified_cb),
                           property_box);
    }

    gtk_signal_connect(GTK_OBJECT(pui->previewpane), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->alternative_layout), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect (GTK_OBJECT (pui->view_message_on_open), "toggled",
                        GTK_SIGNAL_FUNC (properties_modified_cb),
                        property_box);
    gtk_signal_connect (GTK_OBJECT (pui->line_length), "toggled",
                        GTK_SIGNAL_FUNC (properties_modified_cb),
                        property_box);
    gtk_signal_connect(GTK_OBJECT(pui->pgdownmod), "toggled",
		       GTK_SIGNAL_FUNC(pgdown_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->pgdown_percent), "changed",
		       GTK_SIGNAL_FUNC(pgdown_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->debug), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->mblist_show_mb_content_info),
		       "toggled", GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->spell_check_sig), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->spell_check_quoted), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

#if ENABLE_ESMTP
    gtk_signal_connect(GTK_OBJECT(pui->smtp_server), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->smtp_user), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->smtp_passphrase), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    gtk_signal_connect(GTK_OBJECT(pui->smtp_certificate_passphrase), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
#endif
#endif

    for (i = 0; i < NUM_ENCODING_MODES; i++) {
	gtk_signal_connect(GTK_OBJECT(pui->encoding_type[i]), "clicked",
			   GTK_SIGNAL_FUNC(properties_modified_cb),
                           property_box);
    }
    gtk_signal_connect(GTK_OBJECT(pui->mail_directory), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->check_mail_auto), "toggled",
		       GTK_SIGNAL_FUNC(timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->check_mail_minutes), "changed",
		       GTK_SIGNAL_FUNC(timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->quiet_background_check), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->check_imap), "toggled",
		       GTK_SIGNAL_FUNC(imap_toggled_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->check_imap_inbox), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->notify_new_mail_dialog), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->close_mailbox_auto), "toggled",
		       GTK_SIGNAL_FUNC(mailbox_timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->close_mailbox_minutes), "changed",
		       GTK_SIGNAL_FUNC(mailbox_timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->drag_default_is_move), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->delete_immediately), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->hide_deleted), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->browse_wrap), "toggled",
		       GTK_SIGNAL_FUNC(browse_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->browse_wrap_length), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->wordwrap), "toggled",
		       GTK_SIGNAL_FUNC(wrap_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->wraplength), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->send_rfc2646_format_flowed), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->always_queue_sent_mail), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->copy_to_sentbox), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->autoquote), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->reply_strip_html_parts), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->forward_attached), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb), property_box);

    /* external editor */
    gtk_signal_connect(GTK_OBJECT(pui->extern_editor_command), "changed",
                       GTK_SIGNAL_FUNC(properties_modified_cb),
    		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->edit_headers), "toggled",
    		       GTK_SIGNAL_FUNC(properties_modified_cb),
    		       property_box);
		
    /* arp */
    gtk_signal_connect(GTK_OBJECT(pui->quote_str), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT
		       (gnome_entry_gtk_entry
			(GNOME_ENTRY(pui->quote_pattern))), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->recognize_rfc2646_format_flowed), 
		       "toggled", GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* message font */
    gtk_signal_connect(GTK_OBJECT(pui->message_font), "changed",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->font_picker), "font_set",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->subject_font), "changed",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->font_picker2), "font_set",
		       GTK_SIGNAL_FUNC(font_changed), property_box);


    gtk_signal_connect(GTK_OBJECT(pui->open_inbox_upon_startup), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->check_mail_upon_startup), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->remember_open_mboxes), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->imap_scan_depth), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->empty_trash), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* threading */
    gtk_signal_connect(GTK_OBJECT(pui->tree_expand_check), "toggled",
                       GTK_SIGNAL_FUNC(properties_modified_cb),
                       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->default_threading_style), "clicked",
                       GTK_SIGNAL_FUNC(properties_modified_cb),
                       property_box);

    /* spell checking */
    gtk_signal_connect(GTK_OBJECT(pui->module), "clicked",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->suggestion_mode), "clicked",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->ignore_length), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);


    /* Date format */
    gtk_signal_connect(GTK_OBJECT(pui->date_format), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Selected headers */
    gtk_signal_connect(GTK_OBJECT(pui->selected_headers), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Format for the title of the message window */
    gtk_signal_connect(GTK_OBJECT(pui->message_title_format), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Colour */
    gtk_signal_connect(GTK_OBJECT(pui->unread_color), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    for(i=0;i<MAX_QUOTED_COLOR;i++)
	gtk_signal_connect(GTK_OBJECT(pui->quoted_color[i]), "released",
			   GTK_SIGNAL_FUNC(properties_modified_cb),
			   property_box);

    gtk_signal_connect(GTK_OBJECT(pui->url_color), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->bad_address_color), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Gnome Property Box Signals */
    gtk_signal_connect(GTK_OBJECT(property_box), "destroy",
		       GTK_SIGNAL_FUNC(destroy_pref_window_cb), pui);

    gtk_signal_connect(GTK_OBJECT(property_box), "apply",
		       GTK_SIGNAL_FUNC(apply_prefs), pui);

#if BALSA_MAJOR < 2
    gtk_signal_connect(GTK_OBJECT(property_box), "help",
		       GTK_SIGNAL_FUNC(gnome_help_pbox_display),
		       &help_entry);
#else
    gtk_signal_connect(GTK_OBJECT(property_box), "help",
		       GTK_SIGNAL_FUNC(balsa_help_pbox_display),
                       NULL);
#endif                          /* BALSA_MAJOR < 2 */

    gtk_widget_show_all(GTK_WIDGET(property_box));

}				/* open_preferences_manager */


/*
 * update data from the preferences window
 */

static void
destroy_pref_window_cb(GtkWidget * pbox, gpointer data)
{
    g_free(pui);
    pui = NULL; 
    already_open = FALSE;
}

static void
apply_prefs(GnomePropertyBox * pbox, gint page_num)
{
    gint i;
    GtkWidget *balsa_window;
    GtkWidget *entry_widget;
    GtkWidget *menu_item;
    const gchar* tmp;

    if (page_num != -1)
	return;

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
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));

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
    for (i = 0; i < NUM_TOOLBAR_MODES; i++)
	if (GTK_TOGGLE_BUTTON(pui->toolbar_type[i])->active) {
	    balsa_app.toolbar_style = toolbar_type[i];
	    balsa_window_refresh(balsa_app.main_window);
	    break;
	}
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
    
    for (i = 0; i < NUM_ENCODING_MODES; i++)
	if (GTK_TOGGLE_BUTTON(pui->encoding_type[i])->active) {
	    balsa_app.encoding_style = encoding_type[i];
	    break;
	}

    if (balsa_app.mblist_show_mb_content_info !=
	GTK_TOGGLE_BUTTON(pui->mblist_show_mb_content_info)->active) {
	balsa_app.mblist_show_mb_content_info =
	    !balsa_app.mblist_show_mb_content_info;
	gtk_object_set(GTK_OBJECT(balsa_app.mblist), "show_content_info",
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
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->mdn_reply_notclean_menu));
    balsa_app.mdn_reply_notclean =
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));

    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    else
	update_timer(FALSE, 0);

    balsa_app.wordwrap = GTK_TOGGLE_BUTTON(pui->wordwrap)->active;
    balsa_app.wraplength =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));
    balsa_app.send_rfc2646_format_flowed =
	GTK_TOGGLE_BUTTON(pui->send_rfc2646_format_flowed)->active;
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
					 (pui->close_mailbox_minutes));
    balsa_app.drag_default_is_move =
	GTK_TOGGLE_BUTTON(pui->drag_default_is_move)->active;
    balsa_app.delete_immediately =
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                     (pui->delete_immediately));
    {
        gboolean hide =
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                         (pui->hide_deleted));
        if (balsa_app.hide_deleted != hide) {
            balsa_app.hide_deleted = hide;
            balsa_index_hide_deleted(hide);
        }
    }

    /* external editor */
    g_free(balsa_app.extern_editor_command);
    balsa_app.extern_editor_command = 
    	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->extern_editor_command)));
    	
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
    balsa_app.quote_regex = libbalsa_deescape_specials(tmp);

    balsa_app.browse_wrap = GTK_TOGGLE_BUTTON(pui->browse_wrap)->active;
    /* main window view menu can also toggle balsa_app.browse_wrap
     * update_view_menu lets it know we've made a change */
    update_view_menu();
    balsa_app.browse_wrap_length =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->browse_wrap_length));
    balsa_app.recognize_rfc2646_format_flowed =
	GTK_TOGGLE_BUTTON(pui->recognize_rfc2646_format_flowed)->active;

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
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->selected_headers)));
    g_strdown(balsa_app.selected_headers);

    /* message window title format */
    g_free(balsa_app.message_title_format);
    balsa_app.message_title_format =
        gtk_editable_get_chars(GTK_EDITABLE(pui->message_title_format),
                               0, -1);

    /* unread mailbox color */
    gdk_colormap_free_colors(gdk_window_get_colormap
			     (GTK_WIDGET(pbox)->window),
			     &balsa_app.mblist_unread_color, 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->unread_color),
			       &(balsa_app.mblist_unread_color.red),
			       &(balsa_app.mblist_unread_color.green),
			       &(balsa_app.mblist_unread_color.blue), 0);

    /* quoted text color */
    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gdk_colormap_free_colors(gdk_window_get_colormap
				 (GTK_WIDGET(pbox)->window),
				 &balsa_app.quoted_color[i], 1);
	gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->quoted_color[i]),
				   &(balsa_app.quoted_color[i].red),
				   &(balsa_app.quoted_color[i].green),
				   &(balsa_app.quoted_color[i].blue),
				   0);
    }

    /* url color */
    gdk_colormap_free_colors(gdk_window_get_colormap(GTK_WIDGET(pbox)->window),
			     &balsa_app.url_color, 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->url_color),
			       &(balsa_app.url_color.red),
			       &(balsa_app.url_color.green),
			       &(balsa_app.url_color.blue),
			       0);			       

    /* bad address color */
    gdk_colormap_free_colors(gdk_window_get_colormap(GTK_WIDGET(pbox)->window),
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
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->warning_message_menu));
    balsa_app.warning_message =
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->error_message_menu));
    balsa_app.error_message =
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));
    menu_item = gtk_menu_get_active(GTK_MENU(pui->debug_message_menu));
    balsa_app.debug_message =
	GPOINTER_TO_INT(gtk_object_get_user_data(GTK_OBJECT(menu_item)));

    /*
     * close window and free memory
     */
    config_save();
    balsa_mblist_repopulate(balsa_app.mblist_tree_store);
    balsa_window =
	GTK_WIDGET(gtk_object_get_data(GTK_OBJECT(pbox), "balsawindow"));
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

    for (i = 0; i < NUM_TOOLBAR_MODES; i++)
	if (balsa_app.toolbar_style == toolbar_type[i]) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
					 (pui->toolbar_type[i]), TRUE);
	    break;
	}

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
    for (i = 0; i<NUM_ENCODING_MODES; i++)
	if (balsa_app.encoding_style == encoding_type[i]) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
					 (pui->encoding_type[i]), TRUE);
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
			      (float) balsa_app.close_mailbox_timeout);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->drag_default_is_move),
				 balsa_app.drag_default_is_move);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->delete_immediately),
                                 balsa_app.delete_immediately);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
                                 (pui->hide_deleted),
                                 balsa_app.hide_deleted);

    gtk_widget_set_sensitive(pui->close_mailbox_minutes,
			     GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->
    		    	    active);

    gtk_widget_set_sensitive(pui->check_mail_minutes,
			     GTK_TOGGLE_BUTTON(pui->check_mail_auto)->
			     active);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->wordwrap),
				 balsa_app.wordwrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->wraplength),
			      (float) balsa_app.wraplength);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
		    		 (pui->send_rfc2646_format_flowed),
				 balsa_app.send_rfc2646_format_flowed);
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
    gtk_widget_set_sensitive(pui->send_rfc2646_format_flowed,
			     GTK_TOGGLE_BUTTON(pui->wordwrap)->active);

    /* external editor */
    gtk_entry_set_text(GTK_ENTRY(pui->extern_editor_command), 
                       balsa_app.extern_editor_command);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->edit_headers),
                                 balsa_app.edit_headers);

    /* arp */
    gtk_entry_set_text(GTK_ENTRY(pui->quote_str), balsa_app.quote_str);
    entry_widget = gnome_entry_gtk_entry(GNOME_ENTRY(pui->quote_pattern));
    tmp = libbalsa_escape_specials(balsa_app.quote_regex);
    gtk_entry_set_text(GTK_ENTRY(entry_widget), tmp);
    g_free(tmp);

    /* wrap incoming text/plain */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->browse_wrap),
                                 balsa_app.browse_wrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->browse_wrap_length),
		    (float) balsa_app.browse_wrap_length);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->recognize_rfc2646_format_flowed),
				  balsa_app.recognize_rfc2646_format_flowed);

    gtk_widget_set_sensitive(pui->browse_wrap_length,
			     balsa_app.browse_wrap);
    gtk_widget_set_sensitive(pui->recognize_rfc2646_format_flowed,
			     balsa_app.browse_wrap);

    /* message font */
    gtk_entry_set_text(GTK_ENTRY(pui->message_font),
		       balsa_app.message_font);
    gtk_entry_set_position(GTK_ENTRY(pui->message_font), 0);
    gtk_entry_set_text(GTK_ENTRY(pui->subject_font),
		       balsa_app.subject_font);
    gtk_entry_set_position(GTK_ENTRY(pui->subject_font), 0);

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
    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->unread_color),
			       balsa_app.mblist_unread_color.red,
			       balsa_app.mblist_unread_color.green,
			       balsa_app.mblist_unread_color.blue, 0);

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

}

static void
update_address_books(void)
{
    gchar *text[3];
    GList *list = balsa_app.address_book_list;
    LibBalsaAddressBook *address_book;
    gint row;
    GtkCList *clist;

    clist = GTK_CLIST(pui->address_books);

    gtk_clist_clear(clist);
    gtk_clist_freeze(clist);

    while (list) {
	address_book = LIBBALSA_ADDRESS_BOOK(list->data);

	g_assert(address_book != NULL);

	if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book))
	    text[0] = "VCARD";
	else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book))
	    text[0] = "LDIF";
#if ENABLE_LDAP
	else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
	    text[0] = "LDAP";
#endif
	else
	    text[0] = "UNKNOWN";

	if (address_book == balsa_app.default_address_book) {
	    text[1] = g_strdup_printf("%s (default)", address_book->name);
	} else {
	    text[1] = g_strdup(address_book->name);
	}
	text[2] = address_book->expand_aliases ? _("Yes") : "";
	row = gtk_clist_append(clist, text);

	g_free(text[1]);

	gtk_clist_set_row_data(clist, row, address_book);
	list = g_list_next(list);
    }
    gtk_clist_select_row(clist, 0, 0);
    gtk_clist_thaw(clist);
}

static void
add_other_server(GNode * node, gpointer data)
{
    GtkCList *clist = GTK_CLIST(data);
    gchar *text[2];
    gboolean append = FALSE;
    BalsaMailboxNode *mbnode = node->data;

    if (mbnode) {
	LibBalsaMailbox *mailbox = mbnode->mailbox;
	if (mailbox) {
	    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
		text[0] = "IMAP";
		text[1] = mailbox->name;
		append = TRUE;
	    }
	} else
	    if (mbnode->server
		&& mbnode->server->type == LIBBALSA_SERVER_IMAP) {
	    text[0] = "IMAP";
	    text[1] = mbnode->name;
	    append = TRUE;
	}
	if (append) {
	    gint row = gtk_clist_append(clist, text);
	    gtk_clist_set_row_data(clist, row, mbnode);
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
    GtkCList *clist;
    GList *list = balsa_app.inbox_input;
    gchar *text[2];
    gint row;

    BalsaMailboxNode *mbnode;

    if(pui == NULL) return;

    clist = GTK_CLIST(pui->mail_servers);

    gtk_clist_clear(clist);
    gtk_clist_freeze(clist);
    while (list) {
	mbnode = list->data;
	if (mbnode) {
	    if (LIBBALSA_IS_MAILBOX_POP3(mbnode->mailbox))
		text[0] = "POP3";
	    else if (LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox))
		text[0] = "IMAP";
	    else 
		text[0] = "????";

	    text[1] = mbnode->mailbox->name;
	    row = gtk_clist_append(clist, text);
	    gtk_clist_set_row_data(clist, row, mbnode);
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
			    (GNodeForeachFunc) add_other_server, clist);
    balsa_mailbox_nodes_unlock(FALSE);
    gtk_clist_select_row(clist, 0, 0);
    gtk_clist_thaw(clist);
}

/* helper functions that simplify often performed actions */
static GtkWidget *
create_table(gint rows, gint cols, GtkContainer* parent)
{
    GtkTable * table = GTK_TABLE(gtk_table_new(rows, cols, FALSE));
    gtk_container_add(parent, GTK_WIDGET(table));
    gtk_container_set_border_width(GTK_CONTAINER(table), 10);
    gtk_table_set_row_spacings(table, 5);
    gtk_table_set_col_spacings(table, 10);
    gtk_container_set_border_width(parent, 5);
    return GTK_WIDGET(table);
}

static GtkWidget*
attach_entry(const gchar* label,gint row, GtkTable *table)
{
    GtkWidget * res, *lw;
    
    res = gtk_entry_new();
    lw = gtk_label_new(label);
    gtk_table_attach(GTK_TABLE(table), lw, 0, 1, row, row+1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify(GTK_LABEL(lw), GTK_JUSTIFY_RIGHT);
    
    gtk_table_attach(GTK_TABLE(table), res, 1, 2, row, row+1,
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
    GtkWidget *res = gtk_check_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(box), res, FALSE, TRUE, 0);
    return res;
}

static void
add_button_to_box(const gchar*label, GtkSignalFunc cb, GtkWidget* box)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_signal_connect_object(GTK_OBJECT(button), "clicked", cb, NULL);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
}

static GtkWidget*
vbox_in_container(GtkWidget* container)
{
    GtkWidget* res = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(container), res);
    gtk_container_set_border_width(GTK_CONTAINER(res), 5);
    return res;
}

static GtkWidget*
color_box(GtkBox* parent, const gchar* title)
{
    GtkWidget* box, *picker, *label;
    box = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start_defaults(parent, box);

    picker = gnome_color_picker_new();
    gnome_color_picker_set_title(GNOME_COLOR_PICKER(picker), (gchar*)title);
    gnome_color_picker_set_dither(GNOME_COLOR_PICKER(picker),TRUE);
    gtk_box_pack_start(GTK_BOX(box), picker,  FALSE, FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(picker), 5);

    label = gtk_label_new(title);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    return picker;
}

static void
mail_servers_cb(GtkCList * clist, gint row, gint column,
                GdkEventButton * event, gpointer data)
{
    if (event && event->type == GDK_2BUTTON_PRESS)
        server_edit_cb(NULL, NULL);
}

static GtkWidget *
create_mailserver_page(gpointer data)
{
    GtkWidget *table3;
    GtkWidget *frame3;
    GtkWidget *hbox1;
    GtkWidget *scrolledwindow3;
    GtkWidget *label14;
    GtkWidget *label15;
    GtkWidget *vbox1;
    GtkWidget *frame4;
    GtkWidget *box2;
    GtkWidget *fileentry2;
#if ENABLE_ESMTP
    GtkWidget *frame5, *table4, *label16, *label17, *label18, *label19;
    GtkWidget *optionmenu;
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    GtkWidget *label20;
#endif
#endif

    table3 = gtk_table_new(3, 1, FALSE);

    frame3 = gtk_frame_new(_("Remote Mailbox Servers"));
    gtk_table_attach(GTK_TABLE(table3), frame3, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    gtk_widget_set_usize(frame3, -2, 115);
    gtk_container_set_border_width(GTK_CONTAINER(frame3), 5);

    hbox1 = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame3), hbox1);

    scrolledwindow3 = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox1), scrolledwindow3, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(scrolledwindow3), 5);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow3),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    pui->mail_servers = gtk_clist_new(2);
    gtk_container_add(GTK_CONTAINER(scrolledwindow3), pui->mail_servers);
    gtk_clist_set_column_width(GTK_CLIST(pui->mail_servers), 0, 40);
    gtk_clist_set_column_width(GTK_CLIST(pui->mail_servers), 1, 80);
    gtk_clist_column_titles_show(GTK_CLIST(pui->mail_servers));

    label14 = gtk_label_new(_("Type"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->mail_servers), 0, label14);

    label15 = gtk_label_new(_("Mailbox Name"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->mail_servers), 1, label15);
    gtk_label_set_justify(GTK_LABEL(label15), GTK_JUSTIFY_LEFT);
    gtk_signal_connect(GTK_OBJECT(pui->mail_servers), "select-row",
                       GTK_SIGNAL_FUNC(mail_servers_cb), NULL);

    vbox1 = vbox_in_container(hbox1);
    add_button_to_box(_("Add"),    GTK_SIGNAL_FUNC(server_add_cb),  vbox1);
    add_button_to_box(_("Modify"), GTK_SIGNAL_FUNC(server_edit_cb), vbox1);
    add_button_to_box(_("Delete"), GTK_SIGNAL_FUNC(server_del_cb),  vbox1);

    frame4 = gtk_frame_new(_("Local mail"));
    gtk_table_attach(GTK_TABLE(table3), frame4, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame4), 5);

    box2 = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame4), box2);

    fileentry2 = gnome_file_entry_new("MAIL-DIR",
				      _("Select your local mail directory"));
    gtk_box_pack_start(GTK_BOX(box2), fileentry2, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(fileentry2), 10);
    gnome_file_entry_set_directory_entry(GNOME_FILE_ENTRY(fileentry2), TRUE);
    gnome_file_entry_set_modal(GNOME_FILE_ENTRY(fileentry2), TRUE);

    pui->mail_directory =
	gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fileentry2));

#if ENABLE_ESMTP
    frame5 = gtk_frame_new(_("Outgoing mail"));
    gtk_table_attach(GTK_TABLE(table3), frame5, 0, 1, 2, 3,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame5), 5);

    table4 = gtk_table_new(3, 4, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table4), 3);
    gtk_table_set_col_spacings(GTK_TABLE(table4), 3);
    gtk_container_add(GTK_CONTAINER(frame5), table4);
    gtk_container_set_border_width(GTK_CONTAINER(table4), 10);

    label16 = gtk_label_new(_("Remote SMTP Server"));
    gtk_table_attach(GTK_TABLE(table4), label16, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pui->smtp_server = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table4), pui->smtp_server, 1, 4, 0, 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    label17 = gtk_label_new(_("User"));
    gtk_table_attach(GTK_TABLE(table4), label17, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pui->smtp_user = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table4), pui->smtp_user, 1, 2, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    label18 = gtk_label_new(_("Pass Phrase"));
    gtk_table_attach(GTK_TABLE(table4), label18, 2, 3, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pui->smtp_passphrase = gtk_entry_new();
    gtk_entry_set_visibility (GTK_ENTRY(pui->smtp_passphrase), FALSE);

    gtk_table_attach(GTK_TABLE(table4), pui->smtp_passphrase, 3, 4, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    /* STARTTLS */
    label19 = gtk_label_new(_("Use TLS"));
    gtk_table_attach(GTK_TABLE(table4), label19, 0, 1, 2, 3,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    optionmenu = gtk_option_menu_new ();
    pui->smtp_tls_mode_menu = create_tls_mode_menu();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (optionmenu),
			      pui->smtp_tls_mode_menu);
    gtk_option_menu_set_history (GTK_OPTION_MENU (optionmenu),
				 balsa_app.smtp_tls_mode);
    gtk_table_attach(GTK_TABLE(table4), optionmenu, 1, 2, 2, 3,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    label20 = gtk_label_new(_("Certificate Pass Phrase"));
    gtk_table_attach(GTK_TABLE(table4), label20, 2, 3, 2, 3,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pui->smtp_certificate_passphrase = gtk_entry_new();
    gtk_entry_set_visibility (GTK_ENTRY(pui->smtp_certificate_passphrase),
                              FALSE);

    gtk_table_attach(GTK_TABLE(table4),
                     pui->smtp_certificate_passphrase, 3, 4, 2, 3,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
#endif
#endif
    /* fill in data */
    update_mail_servers();

    return table3;
}

static GtkWidget *
create_mailoptions_page(gpointer data)
{
    GtkWidget *note;

    note = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(note), 5);

    gtk_notebook_append_page(GTK_NOTEBOOK(note), incoming_page(data), 
			     gtk_label_new(_("Incoming")));
    gtk_notebook_append_page(GTK_NOTEBOOK(note), outgoing_page(data), 
			     gtk_label_new(_("Outgoing")));

    return note;
}

static GtkWidget *
incoming_page(gpointer data)
{
    GtkWidget *vbox1;
    GtkWidget *vbox2;
    GtkWidget *hbox1;
    GtkWidget *frame15;
    GtkWidget *table7;
    GtkWidget *label33;
    GtkObject *spinbutton4_adj;
    GtkWidget *regex_frame;
    GtkWidget *regex_hbox;
    GtkWidget *regex_label;
    GtkWidget *mdn_frame;
    GtkWidget *mdn_vbox;
    GtkWidget *mdn_label;
    GtkWidget *mdn_table;
    GtkWidget *mdn_optionmenu;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame15 = gtk_frame_new(_("Checking"));
    gtk_container_set_border_width(GTK_CONTAINER(frame15), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame15, FALSE, FALSE, 0);

	vbox2 = vbox_in_container(frame15);

    table7 = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(vbox2), table7);
    gtk_container_set_border_width(GTK_CONTAINER(table7), 0);

    label33 = gtk_label_new(_("minutes"));
    gtk_table_attach(GTK_TABLE(table7), label33, 2, 3, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);

    spinbutton4_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->check_mail_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton4_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table7), pui->check_mail_minutes, 1, 2, 0,
		     1, (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0,
		     0);

    pui->check_mail_auto = gtk_check_button_new_with_label(
	_("Check mail automatically every:"));
    gtk_table_attach(GTK_TABLE(table7), pui->check_mail_auto, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    hbox1 = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox1,
		       TRUE, FALSE, 0);
    pui->check_imap = gtk_check_button_new_with_label(
	_("Check IMAP mailboxes"));
    gtk_box_pack_start(GTK_BOX(hbox1), pui->check_imap,
		       FALSE, FALSE, 0);
    
    pui->check_imap_inbox = gtk_check_button_new_with_label(
	_("Check INBOX only"));
    gtk_box_pack_start(GTK_BOX(hbox1), pui->check_imap_inbox,
		       FALSE, FALSE, 0);
    
    pui->notify_new_mail_dialog = gtk_check_button_new_with_label(
	_("Display message if new mail has arrived"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->notify_new_mail_dialog,
		       FALSE, FALSE, 0);
    
    pui->quiet_background_check = gtk_check_button_new_with_label(
	_("Do background check quietly (no messages in status bar)"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->quiet_background_check,
		       TRUE, FALSE, 0);
    
    /* Quoted text regular expression */
    /* and RFC2646-style flowed text  */
    regex_frame = gtk_frame_new(_("Quoted and Flowed Text"));
    gtk_container_set_border_width(GTK_CONTAINER(regex_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), regex_frame, FALSE, FALSE, 0);

    vbox2 = vbox_in_container(regex_frame);

    regex_hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox2), regex_hbox, FALSE, FALSE, 0);
    /*
    gtk_container_add(GTK_CONTAINER(regex_frame), regex_hbox);
    */
    gtk_container_set_border_width(GTK_CONTAINER(regex_hbox), 5);

    regex_label = gtk_label_new(_("Quoted Text Regular Expression"));
    gtk_box_pack_start(GTK_BOX(regex_hbox), regex_label, FALSE, FALSE, 5);

    pui->quote_pattern = gnome_entry_new("quote-regex-history");
    gtk_box_pack_start(GTK_BOX(regex_hbox),pui->quote_pattern, FALSE,FALSE,0);

    hbox1 = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox1, FALSE, FALSE, 0);
    pui->browse_wrap =
	gtk_check_button_new_with_label(_("Wrap Incoming Text at:"));
    gtk_box_pack_start(GTK_BOX(hbox1), pui->browse_wrap,
		       FALSE, FALSE, 0);
    spinbutton4_adj = gtk_adjustment_new(1.0, 40.0, 200.0, 1.0, 5.0, 0.0);
    pui->browse_wrap_length =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton4_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox1), pui->browse_wrap_length,
		       FALSE, FALSE, 0);
    gtk_widget_set_sensitive(pui->browse_wrap_length, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox1), gtk_label_new(_("Characters")),
		       FALSE, FALSE, 0);

    pui->recognize_rfc2646_format_flowed =
	gtk_check_button_new_with_label(
					_("Reflow messages of type "
					  "`text/plain; format=flowed'"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->recognize_rfc2646_format_flowed,
			                       TRUE, FALSE, 0);

	

    /* How to handle received MDN requests */
    mdn_frame = gtk_frame_new (_("Message Disposition Notification requests"));
    gtk_container_set_border_width (GTK_CONTAINER(mdn_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), mdn_frame, FALSE, FALSE, 0);

    mdn_vbox = gtk_vbox_new (FALSE, 0);
    gtk_container_add (GTK_CONTAINER(mdn_frame), mdn_vbox);
    gtk_container_set_border_width (GTK_CONTAINER(mdn_vbox), 5);

    mdn_label = gtk_label_new (_("When I receive a message and its sender requested to return a\nMessage Disposition Notification (MDN), send it in the following cases:"));
    gtk_box_pack_start (GTK_BOX (mdn_vbox), mdn_label, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (mdn_label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (mdn_label), 0, 0.5);

    mdn_table = gtk_table_new(2, 2, FALSE);
    gtk_box_pack_start (GTK_BOX (mdn_vbox), mdn_table, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(mdn_table), 10);
    gtk_table_set_row_spacings(GTK_TABLE(mdn_table), 5);
    gtk_table_set_col_spacings(GTK_TABLE(mdn_table), 10);

    mdn_label = gtk_label_new (_("the message header looks clean\n(the notify-to address is equal to the return path, I am in the to or cc list)"));
    gtk_label_set_justify (GTK_LABEL (mdn_label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (mdn_label), 0, 0.5);
    gtk_table_attach (GTK_TABLE (mdn_table), mdn_label, 0, 1, 0, 1,
		      GTK_FILL, 0, 0, 0);

    mdn_optionmenu = gtk_option_menu_new ();
    pui->mdn_reply_clean_menu = create_mdn_reply_menu();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (mdn_optionmenu),
			      pui->mdn_reply_clean_menu);
    gtk_option_menu_set_history (GTK_OPTION_MENU (mdn_optionmenu),
				 balsa_app.mdn_reply_clean);
    gtk_table_attach (GTK_TABLE (mdn_table), mdn_optionmenu, 1, 2, 0, 1,
		      GTK_FILL | GTK_EXPAND, 0, 0, 0);
 
    mdn_label = gtk_label_new (_("the message header looks suspicious"));
    gtk_label_set_justify (GTK_LABEL (mdn_label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (mdn_label), 0, 0.5);
    gtk_table_attach (GTK_TABLE (mdn_table), mdn_label, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (mdn_label), 0, 0.5);

    mdn_optionmenu = gtk_option_menu_new ();
    pui->mdn_reply_notclean_menu = create_mdn_reply_menu();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (mdn_optionmenu), 
			      pui->mdn_reply_notclean_menu);
    gtk_option_menu_set_history (GTK_OPTION_MENU (mdn_optionmenu),
				 balsa_app.mdn_reply_notclean);
    gtk_table_attach (GTK_TABLE (mdn_table), mdn_optionmenu, 1, 2, 1, 2,
		      GTK_FILL | GTK_EXPAND, 0, 0, 0);

    return vbox1;
}

static GtkWidget *
outgoing_page(gpointer data)
{
    GtkWidget *frame1;
    GtkWidget *frame2;
    GtkWidget *table;
    GtkTable  *table2;
    GtkObject *spinbutton_adj;
    GtkWidget *label;
    GtkWidget *vbox1, *vbox2;
    GSList *group;
    gint i;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame1 = gtk_frame_new(_("Word Wrap"));
    gtk_container_set_border_width(GTK_CONTAINER(frame1), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame1, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), 5);

    table = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame1), table);
    gtk_container_set_border_width(GTK_CONTAINER(table), 5);

    pui->wordwrap =
	gtk_check_button_new_with_label(_("Wrap Outgoing Text at:"));
    gtk_table_attach(GTK_TABLE(table), pui->wordwrap, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    spinbutton_adj =
        gtk_adjustment_new(1.0, 40.0,
                           balsa_app.
                           send_rfc2646_format_flowed ? 79.0 : 200.0, 1.0,
                           5.0, 0.0);
    pui->wraplength =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->wraplength, 1, 2, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->wraplength, FALSE);

    label = gtk_label_new(_("Characters"));
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);

    pui->send_rfc2646_format_flowed =
	gtk_check_button_new_with_label(_("Send message as type "
					  "`text/plain; format=flowed'"));
    gtk_table_attach(GTK_TABLE(table), pui->send_rfc2646_format_flowed,
		     0, 3, 1, 2,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    frame2 = gtk_frame_new(_("Other options"));
    gtk_box_pack_start(GTK_BOX(vbox1), frame2, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame2), 5);

	vbox2 = vbox_in_container(frame2);

    table2 = GTK_TABLE(gtk_table_new(5, 2, FALSE));
    gtk_container_add(GTK_CONTAINER(vbox2), GTK_WIDGET(table2));
    gtk_container_set_border_width(GTK_CONTAINER(table2), 2);
    pui->extern_editor_command = 
        attach_entry(_("External editor command:"), 4, table2);
    pui->edit_headers = 
    	gtk_check_button_new_with_label(_("Edit headers in external editor"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->edit_headers, FALSE, TRUE, 0);
    pui->quote_str = attach_entry(_("Reply Prefix:"), 5, table2);

    pui->autoquote =
        gtk_check_button_new_with_label(_("Automatically quote original "
                                          "when replying"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->autoquote, FALSE, TRUE, 0);

    pui->reply_strip_html_parts =
	gtk_check_button_new_with_label(_("Don't include HTML parts as text "
                                          "when replying or forwarding mail"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->reply_strip_html_parts,
		       FALSE, TRUE, 0);

    pui->forward_attached =
	gtk_check_button_new_with_label(_("Forward a mail as attachment "
                                          "instead of quoting it"));
    gtk_box_pack_start(GTK_BOX(vbox2), pui->forward_attached,
		       FALSE, TRUE, 0);

	pui->always_queue_sent_mail =
	gtk_check_button_new_with_label(_("Send button always queues outgoing mail in outbox"));
	gtk_box_pack_start(GTK_BOX(vbox2), pui->always_queue_sent_mail,
				FALSE, TRUE, 0);

	pui->copy_to_sentbox =
	gtk_check_button_new_with_label(_("Copy outgoing messages to sentbox"));
	gtk_box_pack_start(GTK_BOX(vbox2), pui->copy_to_sentbox,
				FALSE, TRUE, 0);

    frame2 = gtk_frame_new(_("Encoding"));
    gtk_box_pack_start(GTK_BOX(vbox1), frame2, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame2), 5);

    vbox2 = vbox_in_container(frame2);

    group = NULL;
    for (i = 0; i < NUM_ENCODING_MODES; i++) {
	pui->encoding_type[i] =
	    GTK_RADIO_BUTTON(gtk_radio_button_new_with_label
			     (group, _(encoding_type_label[i])));
	gtk_box_pack_start(GTK_BOX(vbox2),
			   GTK_WIDGET(pui->encoding_type[i]), FALSE, TRUE,
			   0);
	group = gtk_radio_button_group(pui->encoding_type[i]);
    }

    return vbox1;

}

static GtkWidget *
create_display_page(gpointer data)
{
    /*
     * finished mail options, starting on display
     * PKGW: This naming scheme is, uh, unclear.
     */
    gint i;
    GtkWidget *vbox1;
    GtkWidget *frame7;
    GtkWidget *vbox7;
    GtkWidget *hbox4;
    GtkWidget *frame8;
    GtkWidget *vbox3;
    GtkWidget *frame9;
    GtkWidget *vbox4;
    GSList *group;
    GtkWidget *vbox2;
    GtkWidget *format_frame;
    GtkTable  *ftbl;
    GtkWidget *information_frame, *information_table;
    GtkWidget *subnb;
    GtkWidget *vbox6;
    GtkWidget *vbox8;
    GtkWidget *color_frame;
    GtkWidget *vbox12;
    GtkWidget *vbox13;
    GtkWidget *hbox5;
    GtkWidget *mbcolor_frame;
    GtkWidget *vbox9;
    GtkWidget *font_frame;
    GtkWidget *header_frame;
    GtkWidget *table1;
    GtkWidget *table2;
    GtkWidget *table3;
    GtkWidget *label1;
    GtkWidget *label2;
    GtkWidget *label3;
    GtkWidget *label4;
    GtkWidget *label5;
    GtkObject *scroll_adj;

    /* Threading page */
    GtkWidget* vbox10;
    GtkWidget* threading_frame;
    GtkWidget* threading_vbox;

    GtkWidget *current_vbox;
    const gint padding = 5;

    vbox1 = gtk_vbox_new (FALSE, 0);
    subnb = gtk_notebook_new ();
    gtk_container_set_border_width (GTK_CONTAINER (subnb), 5);
    gtk_box_pack_start (GTK_BOX (vbox1), subnb, FALSE, FALSE, 0);

    vbox2 = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page (GTK_NOTEBOOK (subnb), vbox2,
                              gtk_label_new (_("Display")));
    frame7 = gtk_frame_new(_("Main window"));
    gtk_box_pack_start(GTK_BOX(vbox2), frame7, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame7), 5);

    vbox7 = vbox_in_container(frame7);

    pui->previewpane = box_start_check(_("Use preview pane"), vbox7);
    pui->mblist_show_mb_content_info =	box_start_check(
	_("Show mailbox statistics in left pane"), vbox7);
    pui->alternative_layout =	box_start_check(
	_("Use alternative main window layout"), vbox7);
    pui->view_message_on_open = box_start_check(
        _("Automatically view message when mailbox opened"), vbox7);
    pui->line_length = box_start_check(
	_("Display message size as number of lines"), vbox7);

    table3 = gtk_table_new(1, 3, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox7), table3, FALSE, FALSE, 0);
    pui->pgdownmod = gtk_check_button_new_with_label(
	_("PageUp/PageDown keys scroll message by:"));
    gtk_table_attach(GTK_TABLE(table3), pui->pgdownmod, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    scroll_adj = gtk_adjustment_new(50.0, 10.0, 100.0, 5.0, 10.0, 0.0);
    pui->pgdown_percent =
	 gtk_spin_button_new(GTK_ADJUSTMENT(scroll_adj), 1, 0);
    gtk_widget_set_sensitive(pui->pgdown_percent, FALSE);
    gtk_table_attach(GTK_TABLE(table3), pui->pgdown_percent, 1, 2, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    label5 = gtk_label_new(_("percent"));
    gtk_table_attach(GTK_TABLE(table3), label5, 2, 3, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);

    hbox4 = gtk_hbox_new(TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox2), hbox4, FALSE, FALSE, 0);

    frame8 = gtk_frame_new(_("Toolbars"));
    gtk_box_pack_start(GTK_BOX(hbox4), frame8, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame8), 5);

    
    vbox3 = vbox_in_container(frame8);
    group = NULL;
    for (i = 0; i < NUM_TOOLBAR_MODES; i++) {
	pui->toolbar_type[i] =
	    GTK_RADIO_BUTTON(gtk_radio_button_new_with_label
			     (group, _(toolbar_type_label[i])));
	gtk_box_pack_start(GTK_BOX(vbox3),
			   GTK_WIDGET(pui->toolbar_type[i]), FALSE, TRUE,
			   0);
	group = gtk_radio_button_group(pui->toolbar_type[i]);
    }

    frame9 = gtk_frame_new(_("Display progress dialog"));
    gtk_box_pack_start(GTK_BOX(hbox4), frame9, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame9), 5);

    vbox4 = vbox_in_container(frame9);

    format_frame = gtk_frame_new(_("Display Formats"));
    gtk_box_pack_start(GTK_BOX(vbox2), format_frame, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(format_frame), 5);

    ftbl = GTK_TABLE(gtk_table_new(2, 2, FALSE));
    gtk_container_add(GTK_CONTAINER(format_frame), GTK_WIDGET(ftbl));
    gtk_container_set_border_width(GTK_CONTAINER(ftbl), 5);
    
    pui->date_format = attach_entry(_("Date encoding (for strftime):"),0,ftbl);
    pui->selected_headers = attach_entry(_("Selected headers:"), 1, ftbl);
    pui->message_title_format =
        attach_entry(_("Message window title format:"), 2, ftbl);

    group = NULL;
    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	pui->pwindow_type[i] =
	    GTK_RADIO_BUTTON(gtk_radio_button_new_with_label
			     (group, _(pwindow_type_label[i])));
	gtk_box_pack_start(GTK_BOX(vbox4),
			   GTK_WIDGET(pui->pwindow_type[i]), FALSE, TRUE,
			   0);
	group = gtk_radio_button_group(pui->pwindow_type[i]);
    }

    /* Information messages frame... */
    vbox6 = gtk_vbox_new (FALSE, 0);
    gtk_notebook_append_page (GTK_NOTEBOOK (subnb), vbox6,
                              gtk_label_new (_("Status Messages")));
    information_frame = gtk_frame_new(_("Information Messages"));
    gtk_box_pack_start(GTK_BOX(vbox6), information_frame, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(information_frame), 5);

    information_table = create_table(5, 2, GTK_CONTAINER(information_frame));
    /* gtk_table_set_row_spacings(GTK_TABLE(information_table), 1);
       gtk_table_set_col_spacings(GTK_TABLE(information_table), 5); */

    
    pui->information_message_menu = 
	attach_information_menu(_("Information Messages"), 0, 
				GTK_TABLE(information_table),
				balsa_app.information_message);
    pui->warning_message_menu =
	attach_information_menu(_("Warning Messages"), 1,
				GTK_TABLE(information_table),
				balsa_app.warning_message);
    pui->error_message_menu = 
	attach_information_menu(_("Error Messages"), 2,
				GTK_TABLE(information_table),
				balsa_app.error_message);
    pui->fatal_message_menu = 
	attach_information_menu(_("Fatal Error Messages"), 3,
				GTK_TABLE(information_table), 
				balsa_app.fatal_message);
    pui->debug_message_menu = 
	attach_information_menu(_("Debug Messages"), 4,
				GTK_TABLE(information_table),
				balsa_app.debug_message);

    /* Color Preferences Page */
    vbox8 = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(subnb), vbox8,
                             gtk_label_new(_("Colors")));

    mbcolor_frame = gtk_frame_new(_("Mailbox Colors"));
    gtk_container_set_border_width(GTK_CONTAINER(mbcolor_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox8), mbcolor_frame, FALSE, FALSE, 0);
    vbox9 = vbox_in_container(mbcolor_frame);

    pui->unread_color = color_box(
	GTK_BOX(vbox9), _("Mailbox with unread messages color"));

    color_frame = gtk_frame_new(_("Message Colors"));
    gtk_container_set_border_width(GTK_CONTAINER(color_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox8), color_frame, FALSE, FALSE, 0);
    hbox5 = gtk_hbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(color_frame), hbox5);
    vbox12 = gtk_vbox_new(FALSE, 5);
    vbox13 = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox5), vbox12, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox5), vbox13, TRUE, TRUE, 0);

    for(i = 0; i < MAX_QUOTED_COLOR; i++) {
	gchar *text = g_strdup_printf(_("Quote level %d color"), i+1);

        if (i < MAX_QUOTED_COLOR/2) {
            current_vbox = vbox12;
        } else {
            current_vbox = vbox13;
        }
        
	pui->quoted_color[i] = color_box( GTK_BOX(current_vbox), text);
	g_free(text);
    }

    color_frame = gtk_frame_new(_("Link Color"));
    gtk_container_set_border_width(GTK_CONTAINER(color_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox8), color_frame, FALSE, FALSE, 0);
    vbox9 = vbox_in_container(color_frame);

    pui->url_color = color_box(GTK_BOX(vbox9), _("Hyperlink color"));
    
    {
        GtkWidget *frame = gtk_frame_new(_("Composition Window"));
        GtkWidget *vbox = vbox_in_container(frame);
        gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
        gtk_box_pack_start(GTK_BOX(vbox8), frame, FALSE, FALSE, 0);
        pui->bad_address_color =
            color_box(GTK_BOX(vbox),
                      _("Invalid or incomplete address label color"));
    }

    /* Fonts Preferences Page */
    vbox9 = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(subnb), vbox9,
                             gtk_label_new(_("Fonts")));

    font_frame = gtk_frame_new(_("Preview Font"));
    gtk_container_set_border_width(GTK_CONTAINER(font_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox9), font_frame, FALSE, FALSE, 0);

    table1 = create_table(10, 3, GTK_CONTAINER(font_frame));
    pui->message_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table1), pui->message_font, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    pui->font_picker = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table1), pui->font_picker, 1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(pui->font_picker), 5);

    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker),
				    balsa_app.message_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER(pui->font_picker),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);
    label1 = gtk_label_new(_("Select..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker),
				    GTK_WIDGET(label1));
    gtk_object_set_user_data(GTK_OBJECT(pui->font_picker),
			     GTK_OBJECT(pui->message_font));
    gtk_object_set_user_data(GTK_OBJECT(pui->message_font),
			     GTK_OBJECT(pui->font_picker));
    label2 = gtk_label_new(_("Preview pane"));
    gtk_table_attach(GTK_TABLE(table1), label2, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_JUSTIFY_RIGHT), 0, 0);
    gtk_label_set_justify(GTK_LABEL(label2), GTK_JUSTIFY_RIGHT);


    /* Subject Font */    
    header_frame = gtk_frame_new(_("Subject Header Font"));
    gtk_container_set_border_width(GTK_CONTAINER(header_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox9), header_frame, FALSE, FALSE, 0);
    table2 = create_table(10, 3, GTK_CONTAINER(header_frame));
    pui->subject_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table2), pui->subject_font, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    pui->font_picker2 = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table2), pui->font_picker2, 1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(pui->font_picker2), 5);
    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker2),
				    balsa_app.subject_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER
				       (pui->font_picker2),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker2),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);
    label3 = gtk_label_new(_("Select..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker2),
				    GTK_WIDGET(label3));
    gtk_object_set_user_data(GTK_OBJECT(pui->font_picker2),
			     GTK_OBJECT(pui->subject_font));
    gtk_object_set_user_data(GTK_OBJECT(pui->subject_font),
			     GTK_OBJECT(pui->font_picker2));

    label4 = gtk_label_new(_("Preview pane"));
    gtk_table_attach(GTK_TABLE(table2), label4, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_JUSTIFY_RIGHT), 0, 0);
    gtk_label_set_justify(GTK_LABEL(label4), GTK_JUSTIFY_RIGHT);


    /* Threading notebook page */
    vbox10 = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(subnb), vbox10,
                             gtk_label_new(_("Threading")));
    threading_frame = gtk_frame_new(_("Threading"));
    gtk_container_set_border_width(GTK_CONTAINER(threading_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox10), threading_frame, FALSE, FALSE, 0);
    threading_vbox = vbox_in_container(threading_frame);
    pui->tree_expand_check = box_start_check(_("Expand mailbox tree on open"), threading_vbox);
    
    pui->default_threading_style = 
        add_pref_menu(_("Default threading style"), threading_style_label, 
                      NUM_THREADING_STYLES, &pui->threading_style_index, 
                      GTK_BOX(threading_vbox), padding, 
                      GTK_SIGNAL_FUNC(threading_optionmenu_cb));
    
    return vbox1;
}


static GtkWidget*
add_pref_menu(const gchar* label, const gchar *names[], gint size, 
	       gint *index, GtkBox* parent, gint padding, 
               GtkSignalFunc callback)
{
    GtkWidget *omenu;
    GtkWidget* table, *hbox, *lbw;

    omenu = create_pref_option_menu(names, size, index, callback);

    table = gtk_table_new(1, 2, TRUE);
    gtk_table_attach_defaults(GTK_TABLE(table), omenu, 0, 1, 0, 1);

    hbox = gtk_hbox_new(FALSE, 0);
    lbw = gtk_label_new(label);
    gtk_misc_set_padding(GTK_MISC(lbw), padding, padding);
    gtk_box_pack_start(GTK_BOX(hbox), lbw,   FALSE, FALSE, padding);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE,  TRUE,  padding);
    gtk_box_pack_start(parent,        hbox,  FALSE, FALSE, 0);
    return omenu;
}

static GtkWidget *
create_spelling_page(gpointer data)
{
    GtkWidget *frame0;
    GtkWidget *vbox0;
    GtkWidget *vbox1;
    GtkWidget *hbox2;

    GtkObject *ignore_adj;
    GtkWidget *ignore_spin;

    GtkWidget *frame1;
    GtkWidget *vbox2;
    GtkWidget *sig_check_toggle;
    GtkWidget *check_quoted_toggle;

    const guint padding = 5;

    vbox0 = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox0), padding);

    /* pspell frame */
    frame0 = gtk_frame_new(_("Pspell Settings"));
    gtk_box_pack_start(GTK_BOX(vbox0), frame0, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame0), padding);
    vbox1 = vbox_in_container(frame0);

    /* do the module menu */
    pui->module = add_pref_menu(_("Spell Check Module"),
				 spell_check_modules_name, NUM_PSPELL_MODULES,
				 &pui->module_index, GTK_BOX(vbox1), padding,
                                GTK_SIGNAL_FUNC(spelling_optionmenu_cb));

    /* do the suggestion modes menu */
    pui->suggestion_mode = add_pref_menu(
	_("Suggestion Level"), spell_check_suggest_mode_label,
	NUM_SUGGEST_MODES, &pui->suggestion_mode_index,GTK_BOX(vbox1),padding,
        GTK_SIGNAL_FUNC(spelling_optionmenu_cb));

    /* do the ignore length */
    ignore_adj = gtk_adjustment_new(0.0, 0.0, 99.0, 1.0, 5.0, 0.0);
    ignore_spin = gtk_spin_button_new(GTK_ADJUSTMENT(ignore_adj), 1, 0);
    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox2),
		       gtk_label_new(_("Ignore words shorter than")),
		       FALSE, FALSE, padding);
    gtk_box_pack_start(GTK_BOX(hbox2), ignore_spin, FALSE, FALSE, padding);
    gtk_box_pack_start(GTK_BOX(vbox1), hbox2, FALSE, FALSE, 0);
    pui->ignore_length = ignore_spin;

    /* ignore signature check box */
    frame1 = gtk_frame_new(_("Misc Spelling Settings"));
    gtk_box_pack_start(GTK_BOX(vbox0), frame1, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame1), padding);

    vbox2 = vbox_in_container(frame1);

    sig_check_toggle = gtk_check_button_new_with_label(_("Check signature"));
    gtk_box_pack_start(GTK_BOX(vbox2), sig_check_toggle, FALSE, FALSE, 0);
    pui->spell_check_sig = sig_check_toggle;

    check_quoted_toggle = gtk_check_button_new_with_label(_("Check quoted"));
    gtk_box_pack_start(GTK_BOX(vbox2), check_quoted_toggle,
		       FALSE, FALSE, 0);
    pui->spell_check_quoted = check_quoted_toggle;

    return vbox0;
}


static GtkWidget *
create_misc_page(gpointer data)
{
    /*
     * done encoding, starting misc
     */
    GtkWidget *vbox9;
    GtkWidget *frame13;
    GtkWidget *vbox10;
    GtkWidget *label33;
    GtkWidget *hbox1;
    GtkObject *spinbutton4_adj;

    vbox9 = gtk_vbox_new(FALSE, 0);

    frame13 = gtk_frame_new(_("Misc"));
    gtk_box_pack_start(GTK_BOX(vbox9), frame13, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame13), 5);

    vbox10 = vbox_in_container(frame13);

    pui->debug = gtk_check_button_new_with_label(_("Debug"));
    gtk_box_pack_start(GTK_BOX(vbox10), pui->debug, FALSE, FALSE, 0);

    pui->empty_trash =
	gtk_check_button_new_with_label(_("Empty Trash on exit"));
    gtk_box_pack_start(GTK_BOX(vbox10), pui->empty_trash, FALSE, FALSE, 0);

    hbox1 = gtk_hbox_new(FALSE, 0);
    gtk_widget_show(hbox1);
    gtk_box_pack_start(GTK_BOX(vbox10), hbox1, FALSE, FALSE, 0);

    pui->close_mailbox_auto =
	gtk_check_button_new_with_label(_("Automatically close mailbox if unused more than"));
    gtk_widget_show(pui->close_mailbox_auto);
    gtk_box_pack_start(GTK_BOX(hbox1), pui->close_mailbox_auto, FALSE, FALSE, 0);

    spinbutton4_adj = gtk_adjustment_new(10, 1, 100, 1, 10, 10);
    pui->close_mailbox_minutes =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton4_adj), 1, 0);
    gtk_widget_show(pui->close_mailbox_minutes);
    gtk_widget_set_sensitive(pui->close_mailbox_minutes, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox1), pui->close_mailbox_minutes, FALSE, FALSE, 0);

    label33 = gtk_label_new(_("minutes"));
    gtk_widget_show(label33);
    gtk_box_pack_start(GTK_BOX(hbox1), label33, FALSE, TRUE, 0);

    pui->drag_default_is_move =
	gtk_check_button_new_with_label(_("Drag-and-drop moves messages by default"));
    gtk_widget_show(pui->drag_default_is_move);
    gtk_box_pack_start(GTK_BOX(vbox10), pui->drag_default_is_move, 
		       FALSE, FALSE, 0);

    {
        GtkWidget *frame = gtk_frame_new(_("Deleting Messages"));
        GtkWidget *vbox = vbox_in_container(frame);

        gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
        gtk_box_pack_start(GTK_BOX(vbox9), frame, FALSE, FALSE, 0);

        pui->delete_immediately =
            gtk_check_button_new_with_label(_("Delete immediately "
                                              "and irretrievably"));
        gtk_box_pack_start(GTK_BOX(vbox), pui->delete_immediately, FALSE,
                           FALSE, 0);

        pui->hide_deleted =
            gtk_check_button_new_with_label(_("Hide deleted messages"));
        gtk_box_pack_start(GTK_BOX(vbox), pui->hide_deleted, FALSE,
                           FALSE, 0);

        gtk_widget_show_all(frame);
    }

    return vbox9;
}

static GtkWidget *
create_startup_page(gpointer data)
{
    GtkWidget *vbox1;
    GtkWidget *frame;
    GtkWidget *vb1;
    GtkObject *scan_adj;
    GtkWidget *hbox;
    GtkWidget *label;
    const guint padding = 5;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame = gtk_frame_new(_("Options"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame, FALSE, FALSE, 0);

    vb1 = vbox_in_container(frame);

    pui->open_inbox_upon_startup =
	gtk_check_button_new_with_label(_("Open Inbox upon startup"));
    gtk_box_pack_start(GTK_BOX(vb1), pui->open_inbox_upon_startup,
		       FALSE, FALSE, 0);
    pui->check_mail_upon_startup =
	gtk_check_button_new_with_label(_("Check mail upon startup"));
    gtk_box_pack_start(GTK_BOX(vb1), pui->check_mail_upon_startup,
		       FALSE, FALSE, 0);
    pui->remember_open_mboxes =
	gtk_check_button_new_with_label(
	    _("Remember open mailboxes between sessions"));
    gtk_box_pack_start(GTK_BOX(vb1), pui->remember_open_mboxes,
		       FALSE, FALSE, 0);

    /* do the IMAP scan depth */
    frame = gtk_frame_new(_("IMAP Folder Scanning"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), padding);
    gtk_box_pack_start(GTK_BOX(vbox1), frame, FALSE, FALSE, 0);
    vb1 = vbox_in_container(frame);

    label = gtk_label_new(_("Choose depth 1 for fast startup; "
                            "this defers scanning some folders.\n"
                            "To see more of the tree at startup, "
                            "choose a greater depth."));
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
    gtk_box_pack_start(GTK_BOX(vb1), label, FALSE, FALSE, padding);

    hbox = gtk_hbox_new(FALSE, padding);
    gtk_box_pack_start(GTK_BOX(vb1), hbox, FALSE, FALSE, padding);
    gtk_box_pack_start(GTK_BOX(hbox),
                       gtk_label_new(_("Scan tree to depth")),
                       FALSE, FALSE, 0);
    scan_adj = gtk_adjustment_new(1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
    pui->imap_scan_depth =
        gtk_spin_button_new(GTK_ADJUSTMENT(scan_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), pui->imap_scan_depth,
                       FALSE, FALSE, 0);


    return vbox1;

}

static GtkWidget *
create_address_book_page(gpointer data)
{
    GtkWidget *table;
    GtkWidget *frame;
    GtkWidget *hbox;
    GtkWidget *scrolledwindow;
    GtkWidget *label;
    GtkWidget *vbox;

    table = gtk_table_new(2, 1, FALSE);

    frame = gtk_frame_new(_("Address Books"));
    gtk_table_attach(GTK_TABLE(table), frame, 0, 1, 0, 1,
		     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
/*     gtk_widget_set_usize(frame, -2, 115); */
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), hbox);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(hbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(scrolledwindow), 5);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
				   GTK_POLICY_NEVER, GTK_POLICY_NEVER);

    pui->address_books = gtk_clist_new(3);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), pui->address_books);
    gtk_clist_set_column_width(GTK_CLIST(pui->address_books), 0, 48);
    gtk_clist_set_column_width(GTK_CLIST(pui->address_books), 1, 200);
    gtk_clist_set_column_width(GTK_CLIST(pui->address_books), 2, 20);
    gtk_clist_column_titles_show(GTK_CLIST(pui->address_books));

    label = gtk_label_new(_("Type"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->address_books), 0, label);

    label = gtk_label_new(_("Address Book Name"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->address_books), 1, label);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

    label = gtk_label_new(_("Expand aliases"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->address_books), 2, label);

    vbox = vbox_in_container(hbox);
    add_button_to_box(_("Add"),
                      GTK_SIGNAL_FUNC(address_book_add_cb),         vbox);
    add_button_to_box(_("Modify"),
                      GTK_SIGNAL_FUNC(address_book_edit_cb),        vbox);
    add_button_to_box(_("Delete"),         
                      GTK_SIGNAL_FUNC(address_book_delete_cb),      vbox);
    add_button_to_box(_("Set as default"), 
                      GTK_SIGNAL_FUNC(set_default_address_book_cb), vbox);

    update_address_books();

    return table;
}


/*
 * callbacks
 */
static void
properties_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gnome_property_box_changed(GNOME_PROPERTY_BOX(pbox));
}


static void
font_changed(GtkWidget * widget, GtkWidget * pbox)
{
    const gchar *font;
    GtkWidget *peer;
    if (GNOME_IS_FONT_PICKER(widget)) {
	font = gnome_font_picker_get_font_name(GNOME_FONT_PICKER(widget));
	peer = gtk_object_get_user_data(GTK_OBJECT(widget));
	gtk_entry_set_text(GTK_ENTRY(peer), font);
    } else {
	font = gtk_entry_get_text(GTK_ENTRY(widget));
        /* called once on deleting the text, and again on inserting the
         * new text--we must just return the first time */
        if (font[0] == '\0')
            return;
	peer = gtk_object_get_user_data(GTK_OBJECT(widget));
	gnome_font_picker_set_font_name(GNOME_FONT_PICKER(peer), font);
	properties_modified_cb(widget, pbox);
    }
}

static void
server_edit_cb(GtkWidget * widget, gpointer data)
{
    GtkCList *clist = GTK_CLIST(pui->mail_servers);
    gint row;
    BalsaMailboxNode *mbnode;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);

    mbnode = gtk_clist_get_row_data(clist, row);
    g_return_if_fail(mbnode);
    balsa_mailbox_node_show_prop_dialog(mbnode);
}

static void
address_book_edit_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkCList *clist = GTK_CLIST(pui->address_books);
    gint row;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);

    address_book = LIBBALSA_ADDRESS_BOOK(gtk_clist_get_row_data(clist, row));

    g_assert(address_book != NULL);

    address_book = balsa_address_book_config_new(address_book);
    if (address_book) {
	config_address_book_save(address_book);
	update_address_books();
    }
}

static void
set_default_address_book_cb(GtkWidget * button, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkCList *clist = GTK_CLIST(pui->address_books);
    gint row;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);

    address_book = LIBBALSA_ADDRESS_BOOK(gtk_clist_get_row_data(clist, row));

    g_assert(address_book != NULL);
    balsa_app.default_address_book = address_book;
    update_address_books();
}

static void
address_book_add_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaAddressBook *address_book;
    address_book = balsa_address_book_config_new(NULL);

    if (address_book != NULL) {
	balsa_app.address_book_list =
	    g_list_append(balsa_app.address_book_list, address_book);
	config_address_book_save(address_book);
	update_address_books();
    }
}

static void
address_book_delete_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaAddressBook *address_book;
    GtkCList *clist = GTK_CLIST(pui->address_books);
    gint row;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);

    address_book = LIBBALSA_ADDRESS_BOOK(gtk_clist_get_row_data(clist, row));

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
pop3_add(GtkWidget * widget, gpointer data)
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
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
                       GTK_SIGNAL_FUNC(pop3_add),
                       NULL);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP mailbox..."));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(mailbox_conf_add_imap_cb),
                       NULL);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Remote IMAP folder..."));
    gtk_signal_connect(GTK_OBJECT(menuitem), "activate",
		       GTK_SIGNAL_FUNC(folder_conf_add_imap_cb),
                       NULL);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
    gtk_widget_show(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, 0);
}

static void
server_del_cb(GtkWidget * widget, gpointer data)
{
    GtkCList *clist = GTK_CLIST(pui->mail_servers);
    gint row;
    BalsaMailboxNode *mbnode;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);
    mbnode = gtk_clist_get_row_data(clist, row);
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
    gtk_widget_set_sensitive(GTK_WIDGET(pui->recognize_rfc2646_format_flowed),
			     newstate);
    properties_modified_cb(widget, pbox);
}

static void
wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->wraplength), newstate);
    gtk_widget_set_sensitive(GTK_WIDGET(pui->send_rfc2646_format_flowed),
			     newstate);
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
    *index = GPOINTER_TO_INT(gtk_object_get_data
			     (GTK_OBJECT(menuitem), "menu_index"));

}


static void
threading_optionmenu_cb(GtkItem* menuitem, gpointer data)
{
    /* update the index number */
    gint *index = (gint*) data;
    *index = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menuitem), "menu_index"));
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
	gtk_object_set_data(GTK_OBJECT(menuitem), "menu_index",
			    GINT_TO_POINTER(i));
	gtk_signal_connect(GTK_OBJECT(menuitem), "select",
                           GTK_SIGNAL_FUNC(callback),
			   (gpointer) index);
	gtk_signal_connect(GTK_OBJECT(menuitem), "select",
			   GTK_SIGNAL_FUNC(properties_modified_cb),
			   property_box);

	gtk_menu_append(GTK_MENU(gmenu), menuitem);
    }

    gtk_option_menu_set_menu(GTK_OPTION_MENU(omenu), gmenu);

    return omenu;
}


static void
add_show_menu(const char* label, gint level, GtkWidget* menu)
{
    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
    gtk_widget_show(menu_item);
    gtk_object_set_user_data(GTK_OBJECT(menu_item),  GINT_TO_POINTER(level));
    gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_menu_append(GTK_MENU(menu), menu_item);
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
create_mdn_reply_menu(void)
{
    GtkWidget *menu = gtk_menu_new();
    add_show_menu(_("Never"),  BALSA_MDN_REPLY_NEVER,  menu);
    add_show_menu(_("Ask me"), BALSA_MDN_REPLY_ASKME,  menu);
    add_show_menu(_("Always"), BALSA_MDN_REPLY_ALWAYS, menu);
    return menu;
}

void
mailbox_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	    pui->close_mailbox_auto));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->close_mailbox_minutes),
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

#if BALSA_MAJOR < 2
#else
static void
balsa_help_pbox_display(GnomePropertyBox * property_box, gint page_num)
{
    gchar *file_name = g_strdup_printf("preferences-%d.html", page_num);
    GError *err = NULL;

    gnome_help_display(file_name, NULL, &err);
    if (err) {
        g_print(_("Error displaying %s: %s\n"), file_name, err->message);
        g_error_free(err);
    }

    g_free(file_name);
}
#endif                          /* BALSA_MAJOR < 2 */
