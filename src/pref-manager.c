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

/* MAKE SURE YOU USE THE HELPER FUNCTIONS, like create_table(), etc. */

#include "config.h"

#include <gnome.h>
#include "balsa-app.h"
#include "pref-manager.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "save-restore.h"
#include "spell-check.h"
#include "address-book-config.h"

/* FIXME: Mutt dependency for ENC7BIT ENC8BIT ENCQUOTEDPRINTABLE consts*/
#include "../libmutt/mime.h"

#define NUM_TOOLBAR_MODES 3
#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3

typedef struct _PropertyUI {
    GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];
    GtkWidget *real_name, *email, *replyto, *domain, *signature;
    GtkWidget *sig_whenforward, *sig_whenreply, *sig_sending;
    GtkWidget *sig_separator;
    GtkWidget *sig_prepend;

    GtkWidget *address_books;

    GtkWidget *pop3servers, *smtp_server, *smtp_port, *mail_directory;
    GtkWidget *rb_local_mua, *rb_smtp_server;
    GtkRadioButton *encoding_type[NUM_ENCODING_MODES];
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;

    GtkWidget *close_mailbox_auto;
    GtkWidget *close_mailbox_minutes;

    GtkWidget *previewpane;
    GtkWidget *alternative_layout;
    GtkWidget *view_allheaders;
    GtkWidget *debug;		/* enable/disable debugging */
    GtkWidget *empty_trash;
    GtkRadioButton *pwindow_type[NUM_PWINDOW_MODES];
    GtkWidget *wordwrap;
    GtkWidget *wraplength;
    GtkWidget *bcc;
    GtkWidget *check_mail_upon_startup;
    GtkWidget *remember_open_mboxes;
    GtkWidget *mblist_show_mb_content_info;

    /* Information messages */
    GtkWidget *information_message_menu;
    GtkWidget *warning_message_menu;
    GtkWidget *error_message_menu;
    GtkWidget *debug_message_menu;
    GtkWidget *fatal_message_menu;

    /* arp */
    GtkWidget *quote_str;

    GtkWidget *message_font;	/* font used to display messages */
    GtkWidget *subject_font;	/* font used to display messages */
    GtkWidget *font_picker;
    GtkWidget *font_picker2;


    GtkWidget *date_format;

    GtkWidget *selected_headers;

#ifndef HAVE_GNOME_PRINT
    /* printing */
    GtkWidget *PrintCommand;
    GtkWidget *PrintBreakline;
    GtkWidget *PrintLinesize;
#endif
    /* colours */
    GtkWidget *unread_color;
    GtkWidget *quoted_color_start;
    GtkWidget *quoted_color_end;

    /* quote regex */
    GtkWidget *quote_pattern;

    /* spell checking */
    GtkWidget *module;
    gint module_index;
    GtkWidget *suggestion_mode;
    gint suggestion_mode_index;
    GtkWidget *ignore_length;
    GtkWidget *spell_check_sig;
    GtkWidget *spell_check_quoted;

} PropertyUI;


static PropertyUI *pui = NULL;
static GtkWidget *property_box;
static gboolean already_open;

static GtkWidget *create_identity_page(gpointer);
static GtkWidget *create_signature_page(gpointer);
static GtkWidget *create_mailserver_page(gpointer);
static GtkWidget *create_mailoptions_page(gpointer);
static GtkWidget *create_display_page(gpointer);
static GtkWidget *create_misc_page(gpointer);
static GtkWidget *create_startup_page(gpointer);
static GtkWidget *create_spelling_page(gpointer);
static GtkWidget *create_spelling_option_menu(const gchar * names[],
					      gint size, gint * index);
static GtkWidget *create_address_book_page(gpointer);

static GtkWidget *create_information_message_menu(void);

static GtkWidget *incoming_page(gpointer);
static GtkWidget *outgoing_page(gpointer);
static void destroy_pref_window_cb(GtkWidget * pbox,
				   PropertyUI * property_struct);
static void set_prefs(void);
static void apply_prefs(GnomePropertyBox * pbox, gint page_num);
void update_pop3_servers(void);
static void update_address_books(void);
static void smtp_changed(void);
static void properties_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void font_changed(GtkWidget * widget, GtkWidget * pbox);
static void pop3_edit_cb(GtkWidget * widget, gpointer data);
static void pop3_add_cb(GtkWidget * widget, gpointer data);
static void pop3_del_cb(GtkWidget * widget, gpointer data);
static void address_book_edit_cb(GtkWidget * widget, gpointer data);
static void address_book_add_cb(GtkWidget * widget, gpointer data);
static void address_book_delete_cb(GtkWidget * widget, gpointer data);
static void timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void mailbox_timer_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox);
static void spelling_optionmenu_cb(GtkItem * menuitem, gpointer data);
static void set_default_address_book_cb(GtkWidget * button, gpointer data);

#ifndef HAVE_GNOME_PRINT
static GtkWidget *create_printing_page(gpointer);
static void print_modified_cb(GtkWidget * widget, GtkWidget * pbox);
#endif

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


/* and now the important stuff: */
void
open_preferences_manager(GtkWidget * widget, gpointer data)
{
    static GnomeHelpMenuEntry help_entry = { NULL, "win-config" };
    GnomeApp *active_win = GNOME_APP(data);
    gint i;

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
				   create_identity_page(property_box),
				   gtk_label_new(_("Identity")));

    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_signature_page(property_box),
				   gtk_label_new(_("Signature")));

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

#ifndef HAVE_GNOME_PRINT
    gnome_property_box_append_page(GNOME_PROPERTY_BOX(property_box),
				   create_printing_page(property_box),
				   gtk_label_new(_("Printing")));
#endif
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
			   properties_modified_cb, property_box);
    }

    for (i = 0; i < NUM_PWINDOW_MODES; i++) {
	gtk_signal_connect(GTK_OBJECT(pui->pwindow_type[i]), "clicked",
			   properties_modified_cb, property_box);
    }

    gtk_signal_connect(GTK_OBJECT(pui->previewpane), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->alternative_layout), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->debug), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->mblist_show_mb_content_info),
		       "toggled", GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->real_name), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->email), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->replyto), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->domain), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->sig_sending), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->sig_whenforward), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->sig_whenreply), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->sig_separator), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->sig_prepend), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->rb_smtp_server), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->rb_smtp_server), "toggled",
		       GTK_SIGNAL_FUNC(smtp_changed), NULL);

    gtk_signal_connect(GTK_OBJECT(pui->spell_check_sig), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->spell_check_quoted), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->smtp_server), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->smtp_port), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    for (i = 0; i < NUM_ENCODING_MODES; i++) {
	gtk_signal_connect(GTK_OBJECT(pui->encoding_type[i]), "clicked",
			   properties_modified_cb, property_box);
    }
    gtk_signal_connect(GTK_OBJECT(pui->mail_directory), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->signature), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->check_mail_auto), "toggled",
		       GTK_SIGNAL_FUNC(timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->check_mail_minutes), "changed",
		       GTK_SIGNAL_FUNC(timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->close_mailbox_auto), "toggled",
		       GTK_SIGNAL_FUNC(mailbox_timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->close_mailbox_minutes), "changed",
		       GTK_SIGNAL_FUNC(mailbox_timer_modified_cb), property_box);

    gtk_signal_connect(GTK_OBJECT(pui->wordwrap), "toggled",
		       GTK_SIGNAL_FUNC(wrap_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->wraplength), "changed",
		       GTK_SIGNAL_FUNC(wrap_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->bcc), "changed",
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

    /* message font */
    gtk_signal_connect(GTK_OBJECT(pui->message_font), "changed",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->font_picker), "font_set",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->subject_font), "changed",
		       GTK_SIGNAL_FUNC(font_changed), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->font_picker2), "font_set",
		       GTK_SIGNAL_FUNC(font_changed), property_box);


#ifndef HAVE_GNOME_PRINT
    /* printing */
    gtk_signal_connect(GTK_OBJECT(pui->PrintCommand), "changed",
		       GTK_SIGNAL_FUNC(print_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->PrintBreakline), "toggled",
		       GTK_SIGNAL_FUNC(print_modified_cb), property_box);
    gtk_signal_connect(GTK_OBJECT(pui->PrintLinesize), "changed",
		       GTK_SIGNAL_FUNC(print_modified_cb), property_box);
#endif

    gtk_signal_connect(GTK_OBJECT(pui->check_mail_upon_startup), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->remember_open_mboxes), "toggled",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    gtk_signal_connect(GTK_OBJECT(pui->empty_trash), "toggled",
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
    gtk_signal_connect(GTK_OBJECT(pui->selected_headers), "changed",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Colour */
    gtk_signal_connect(GTK_OBJECT(pui->unread_color), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->quoted_color_start), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);
    gtk_signal_connect(GTK_OBJECT(pui->quoted_color_end), "released",
		       GTK_SIGNAL_FUNC(properties_modified_cb),
		       property_box);

    /* Gnome Property Box Signals */
    gtk_signal_connect(GTK_OBJECT(property_box), "destroy",
		       GTK_SIGNAL_FUNC(destroy_pref_window_cb), pui);

    gtk_signal_connect(GTK_OBJECT(property_box), "apply",
		       GTK_SIGNAL_FUNC(apply_prefs), pui);

    help_entry.name = gnome_app_id;
    gtk_signal_connect(GTK_OBJECT(property_box), "help",
		       GTK_SIGNAL_FUNC(gnome_help_pbox_display),
		       &help_entry);

    gtk_widget_show_all(GTK_WIDGET(property_box));

}				/* open_preferences_manager */


static void
smtp_changed(void)
{
    balsa_app.smtp = !balsa_app.smtp;
    gtk_widget_set_sensitive(pui->smtp_server, balsa_app.smtp);
    gtk_widget_set_sensitive(pui->smtp_port, balsa_app.smtp);
}

/*
 * update data from the preferences window
 */

static void
destroy_pref_window_cb(GtkWidget * pbox, PropertyUI * property_struct)
{
    g_free(property_struct);
    property_struct = NULL;
    already_open = FALSE;
}

static void
apply_prefs(GnomePropertyBox * pbox, gint page_num)
{
    gint i;
    GtkWidget *balsa_window;
    GtkWidget *entry_widget;
    GtkWidget *menu_item;

    if (page_num != -1)
	return;

    /*
     * identity page
     */
    gtk_object_destroy(GTK_OBJECT(balsa_app.address));
    balsa_app.address = libbalsa_address_new();

    balsa_app.address->full_name =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->real_name)));
    balsa_app.address->address_list =
	g_list_append(balsa_app.address->address_list,
		      g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->email))));

    g_free(balsa_app.replyto);
    balsa_app.replyto =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->replyto)));

    g_free(balsa_app.domain);
    balsa_app.domain =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->domain)));

    g_free(balsa_app.smtp_server);
    balsa_app.smtp_server =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->smtp_server)));

    balsa_app.smtp_port =
	atoi(gtk_entry_get_text(GTK_ENTRY(pui->smtp_port)));

    g_free(balsa_app.signature_path);
    balsa_app.signature_path =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->signature)));

    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->mail_directory)));

    balsa_app.sig_sending = GTK_TOGGLE_BUTTON(pui->sig_sending)->active;
    balsa_app.sig_whenforward =
	GTK_TOGGLE_BUTTON(pui->sig_whenforward)->active;
    balsa_app.sig_whenreply =
	GTK_TOGGLE_BUTTON(pui->sig_whenreply)->active;
    balsa_app.sig_separator =
	GTK_TOGGLE_BUTTON(pui->sig_separator)->active;
    balsa_app.sig_prepend =
	GTK_TOGGLE_BUTTON(pui->sig_prepend)->active;

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
    
//    if (balsa_app.alt_layout_is_active != balsa_app.alternative_layout)
	balsa_change_window_layout(balsa_app.main_window);
    
    balsa_app.smtp = GTK_TOGGLE_BUTTON(pui->rb_smtp_server)->active;
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

    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    else
	update_timer(FALSE, 0);

    balsa_app.wordwrap = GTK_TOGGLE_BUTTON(pui->wordwrap)->active;
    balsa_app.wraplength =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));

    g_free(balsa_app.bcc);
    balsa_app.bcc = g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->bcc)));

    balsa_app.close_mailbox_auto =
	GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->active;
    balsa_app.close_mailbox_timeout =
	gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON
					 (pui->close_mailbox_minutes));

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
    balsa_app.quote_regex =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(entry_widget)));

#ifndef HAVE_GNOME_PRINT
    /* printing */
    g_free(balsa_app.PrintCommand.PrintCommand);
    balsa_app.PrintCommand.PrintCommand =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(pui->PrintCommand)));

    balsa_app.PrintCommand.linesize =
	atoi(gtk_entry_get_text(GTK_ENTRY(pui->PrintLinesize)));
    balsa_app.PrintCommand.breakline =
	GTK_TOGGLE_BUTTON(pui->PrintBreakline)->active;
#endif

    balsa_app.check_mail_upon_startup =
	GTK_TOGGLE_BUTTON(pui->check_mail_upon_startup)->active;
    balsa_app.remember_open_mboxes =
	GTK_TOGGLE_BUTTON(pui->remember_open_mboxes)->active;
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

    /* unread mailbox color */
    gdk_colormap_free_colors(gdk_window_get_colormap
			     (GTK_WIDGET(pbox)->window),
			     &balsa_app.mblist_unread_color, 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->unread_color),
			       &(balsa_app.mblist_unread_color.red),
			       &(balsa_app.mblist_unread_color.green),
			       &(balsa_app.mblist_unread_color.blue), 0);

    /* quoted text color */
    gdk_colormap_free_colors(gdk_window_get_colormap
			     (GTK_WIDGET(pbox)->window),
			     &balsa_app.quoted_color[0], 1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->quoted_color_start),
			       &(balsa_app.quoted_color[0].red),
			       &(balsa_app.quoted_color[0].green),
			       &(balsa_app.quoted_color[0].blue), 0);
    gdk_colormap_free_colors(gdk_window_get_colormap
			     (GTK_WIDGET(pbox)->window),
			     &balsa_app.quoted_color[MAX_QUOTED_COLOR - 1],
			     1);
    gnome_color_picker_get_i16(GNOME_COLOR_PICKER(pui->quoted_color_end),
			       &(balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].red),
			       &(balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].green),
			       &(balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].blue),
			       0);

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
    balsa_mblist_repopulate(balsa_app.mblist);
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
    gint i;

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

    gtk_entry_set_text(GTK_ENTRY(pui->real_name),
		       balsa_app.address->full_name);

    if (balsa_app.address->address_list) {
	gtk_entry_set_text(GTK_ENTRY(pui->email),
			   balsa_app.address->address_list->data);
    } else
	gtk_entry_set_text(GTK_ENTRY(pui->email), "");

    if (balsa_app.replyto)
	gtk_entry_set_text(GTK_ENTRY(pui->replyto), balsa_app.replyto);
    if (balsa_app.domain)
	gtk_entry_set_text(GTK_ENTRY(pui->domain), balsa_app.domain);

    gtk_entry_set_text(GTK_ENTRY(pui->signature),
		       balsa_app.signature_path);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->sig_sending),
				 balsa_app.sig_sending);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->sig_whenforward),
				 balsa_app.sig_whenforward);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->sig_whenreply),
				 balsa_app.sig_whenreply);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->sig_separator),
				 balsa_app.sig_separator);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->sig_prepend),
				 balsa_app.sig_prepend);

    if (balsa_app.smtp_server)
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_server),
			   balsa_app.smtp_server);

    if (balsa_app.smtp_port){
	
	char tmp[10];

	sprintf(tmp, "%d", balsa_app.smtp_port);
	gtk_entry_set_text(GTK_ENTRY(pui->smtp_port),tmp);
    }

    gtk_entry_set_text(GTK_ENTRY(pui->mail_directory),
		       balsa_app.local_mail_directory);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->previewpane),
				 balsa_app.previewpane);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->alternative_layout),
				 balsa_app.alternative_layout);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->debug),
				 balsa_app.debug);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->rb_smtp_server),
				 balsa_app.smtp);
    for (i = 0; i < NUM_ENCODING_MODES; i++)
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

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->close_mailbox_auto),
				 balsa_app.close_mailbox_auto);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->close_mailbox_minutes),
			      (float) balsa_app.close_mailbox_timeout);

    gtk_widget_set_sensitive(pui->close_mailbox_minutes,
			     GTK_TOGGLE_BUTTON(pui->close_mailbox_auto)->
    		    	    active);

    gtk_widget_set_sensitive(pui->smtp_server,
			     GTK_TOGGLE_BUTTON(pui->rb_smtp_server)->
			     active);

    gtk_widget_set_sensitive(pui->smtp_port,
			     GTK_TOGGLE_BUTTON(pui->rb_smtp_server)->
			     active);

    gtk_widget_set_sensitive(pui->check_mail_minutes,
			     GTK_TOGGLE_BUTTON(pui->check_mail_auto)->
			     active);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->wordwrap),
				 balsa_app.wordwrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pui->wraplength),
			      (float) balsa_app.wraplength);

    gtk_widget_set_sensitive(pui->wraplength,
			     GTK_TOGGLE_BUTTON(pui->wordwrap)->active);

    gtk_entry_set_text(GTK_ENTRY(pui->bcc),
		       balsa_app.bcc ? balsa_app.bcc : "");

    /* arp */
    gtk_entry_set_text(GTK_ENTRY(pui->quote_str), balsa_app.quote_str);
    entry_widget = gnome_entry_gtk_entry(GNOME_ENTRY(pui->quote_pattern));
    gtk_entry_set_text(GTK_ENTRY(entry_widget), balsa_app.quote_regex);

    /* message font */
    gtk_entry_set_text(GTK_ENTRY(pui->message_font),
		       balsa_app.message_font);
    gtk_entry_set_position(GTK_ENTRY(pui->message_font), 0);
    gtk_entry_set_text(GTK_ENTRY(pui->subject_font),
		       balsa_app.subject_font);
    gtk_entry_set_position(GTK_ENTRY(pui->subject_font), 0);

#ifndef HAVE_GNOME_PRINT
    /*printing */
    {
	gchar tmp[10];
	gtk_entry_set_text(GTK_ENTRY(pui->PrintCommand),
			   balsa_app.PrintCommand.PrintCommand);
	sprintf(tmp, "%d", balsa_app.PrintCommand.linesize);
	gtk_entry_set_text(GTK_ENTRY(pui->PrintLinesize), tmp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				     (pui->PrintBreakline),
				     balsa_app.PrintCommand.breakline);
    }
#endif

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->check_mail_upon_startup),
				 balsa_app.check_mail_upon_startup);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON
				 (pui->remember_open_mboxes),
				 balsa_app.remember_open_mboxes);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->empty_trash),
				 balsa_app.empty_trash_on_exit);

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
    if (balsa_app.selected_headers)
	gtk_entry_set_text(GTK_ENTRY(pui->selected_headers),
			   balsa_app.selected_headers);

    /* Colour */
    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->unread_color),
			       balsa_app.mblist_unread_color.red,
			       balsa_app.mblist_unread_color.green,
			       balsa_app.mblist_unread_color.blue, 0);
    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->quoted_color_start),
			       balsa_app.quoted_color[0].red,
			       balsa_app.quoted_color[0].green,
			       balsa_app.quoted_color[0].blue, 0);
    gnome_color_picker_set_i16(GNOME_COLOR_PICKER(pui->quoted_color_end),
			       balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].red,
			       balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].green,
			       balsa_app.quoted_color[MAX_QUOTED_COLOR - 1].blue, 0);

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

void
update_pop3_servers(void)
{
    GtkCList *clist;
    GList *list = balsa_app.inbox_input;
    gchar *text[2];
    gint row;

    BalsaMailboxNode *mbnode;

    if (!pui)
	return;

    clist = GTK_CLIST(pui->pop3servers);

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
attach_check(const gchar* label,gint row, GtkTable *table)
{
    GtkWidget *res = gtk_check_button_new_with_label(label);
    gtk_table_attach(table, res, 1, 2, row, row+1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    return res;
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
    gnome_color_picker_set_title(GNOME_COLOR_PICKER(picker), (gchar*)label);
    gnome_color_picker_set_dither(GNOME_COLOR_PICKER(picker),TRUE);
    gtk_box_pack_start(GTK_BOX(box), picker,  FALSE, FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(picker), 5);

    label = gtk_label_new(title);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    return picker;
}
/* create_identity_page:
   as all the other create_* functions, creates a page, prefills it
   and sets up respective signals.    These three parts should be nicely
   marked in the code, unless there are strong reasons agains it.
   The argument is the object that accepts modification callbacks
   (usually the property box).  
*/

static GtkWidget *
create_identity_page(gpointer data)
{
    GtkWidget *frame1;
    GtkTable  *table;
    GtkWidget *vbox1;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame1 = gtk_frame_new(_("Identity"));
    gtk_box_pack_start(GTK_BOX(vbox1), frame1, FALSE, FALSE, 0);

    table = GTK_TABLE(create_table(4, 2, GTK_CONTAINER(frame1)));
    pui->real_name = attach_entry(_("Your name:"),       0,table);
    pui->email     = attach_entry(_("E-mail address:"),  1,table);
    pui->replyto   = attach_entry(_("Reply-to address:"),2,table);
    pui->domain    = attach_entry(_("Default domain:"),  3,table);

    return vbox1;
}

static GtkWidget *
create_signature_page(gpointer data)
{
    GtkWidget *vbox;
    GtkWidget *frame1;
    GtkTable *table1;
    GtkWidget *fileentry1;
    GtkWidget *label1;

    vbox = gtk_vbox_new(FALSE, 0);

    frame1 = gtk_frame_new(_("Signature"));
    gtk_box_pack_start(GTK_BOX(vbox), frame1, FALSE, FALSE, 0);

    table1 = GTK_TABLE(create_table(6, 2, GTK_CONTAINER(frame1)));

    label1 = gtk_label_new(_("Use signature file when:"));
    gtk_table_attach(GTK_TABLE(table1), label1, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    pui->sig_sending   = attach_check(_("sending mail"),     0, table1);
    pui->sig_whenreply = attach_check(_("replying to mail"), 1, table1);
    pui->sig_whenforward = attach_check(_("forwarding mail"),2, table1);

    label1 = gtk_label_new(_("Signature file:"));
    gtk_table_attach(GTK_TABLE(table1), label1, 0, 1, 3, 4,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify(GTK_LABEL(label1), GTK_JUSTIFY_LEFT);

    fileentry1 = gnome_file_entry_new("SIGNATURE-FILE",
				      _("Select your signature file"));
    gtk_table_attach(GTK_TABLE(table1), fileentry1, 1, 2, 3, 4,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gnome_file_entry_set_modal(GNOME_FILE_ENTRY(fileentry1), TRUE);

    pui->signature = gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fileentry1));

    pui->quote_str = attach_entry(_("Reply prefix:"), 4, table1);


    pui->sig_separator=attach_check(_("enable signature separator"),5,table1);
    pui->sig_prepend=attach_check(_("insert signature before quoted messages"),6,table1);

    return vbox;
}

static GtkWidget *
create_mailserver_page(gpointer data)
{
    GtkWidget *table3;
    GtkWidget *frame3;
    GtkWidget *hbox1;
    GtkWidget *scrolledwindow3;
    GtkWidget *label1;
    GtkWidget *label14;
    GtkWidget *label15;
    GtkWidget *vbox1;
    GtkWidget *frame4;
    GtkWidget *box2;
    GtkWidget *fileentry2;
    GtkWidget *frame5;
    GtkWidget *table4;
    GSList *table4_group = NULL;

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
				   GTK_POLICY_NEVER, GTK_POLICY_NEVER);

    pui->pop3servers = gtk_clist_new(2);
    gtk_container_add(GTK_CONTAINER(scrolledwindow3), pui->pop3servers);
    gtk_clist_set_column_width(GTK_CLIST(pui->pop3servers), 0, 40);
    gtk_clist_set_column_width(GTK_CLIST(pui->pop3servers), 1, 80);
    gtk_clist_column_titles_show(GTK_CLIST(pui->pop3servers));

    label14 = gtk_label_new(_("Type"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->pop3servers), 0, label14);

    label15 = gtk_label_new(_("Mailbox Name"));
    gtk_clist_set_column_widget(GTK_CLIST(pui->pop3servers), 1, label15);
    gtk_label_set_justify(GTK_LABEL(label15), GTK_JUSTIFY_LEFT);

    vbox1 = vbox_in_container(hbox1);
    add_button_to_box(_("Add"),    pop3_add_cb,  vbox1);
    add_button_to_box(_("Modify"), pop3_edit_cb, vbox1);
    add_button_to_box(_("Delete"), pop3_del_cb,  vbox1);

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
    gnome_file_entry_set_directory(GNOME_FILE_ENTRY(fileentry2), TRUE);
    gnome_file_entry_set_modal(GNOME_FILE_ENTRY(fileentry2), TRUE);

    pui->mail_directory =
	gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fileentry2));

    frame5 = gtk_frame_new(_("Outgoing mail"));
    gtk_table_attach(GTK_TABLE(table3), frame5, 0, 1, 2, 3,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame5), 5);

    table4 = gtk_table_new(2, 4, FALSE);
    gtk_container_add(GTK_CONTAINER(frame5), table4);
    gtk_container_set_border_width(GTK_CONTAINER(table4), 10);

    pui->smtp_server = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table4), pui->smtp_server, 1, 2, 0, 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->smtp_server, FALSE);

    pui->smtp_port = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table4), pui->smtp_port, 3, 4, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->smtp_port, FALSE);

    label1 = gtk_label_new(_("Port"));
    gtk_table_attach(GTK_TABLE(table4), label1, 2, 3, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 5, 0);

    pui->rb_smtp_server = 
	gtk_radio_button_new_with_label(table4_group,
					_("Remote SMTP Server"));
    table4_group =
	gtk_radio_button_group(GTK_RADIO_BUTTON(pui->rb_smtp_server));
    gtk_table_attach(GTK_TABLE(table4), pui->rb_smtp_server, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    pui->rb_local_mua =
	gtk_radio_button_new_with_label(table4_group,
					_("Local mail user agent"));
    table4_group =
	gtk_radio_button_group(GTK_RADIO_BUTTON(pui->rb_local_mua));
    gtk_table_attach(GTK_TABLE(table4), pui->rb_local_mua, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    /* this must be here otherwise rb_local_mua never gets active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pui->rb_local_mua),
				 TRUE);

    /* fill in data */
    update_pop3_servers();

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
    GtkWidget *frame15;
    GtkWidget *table7;
    GtkWidget *label33;
    GtkObject *spinbutton4_adj;
    GtkWidget *regex_frame;
    GtkWidget *regex_hbox;
    GtkWidget *regex_label;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame15 = gtk_frame_new(_("Checking"));
    gtk_container_set_border_width(GTK_CONTAINER(frame15), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame15, FALSE, FALSE, 0);

    table7 = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame15), table7);
    gtk_container_set_border_width(GTK_CONTAINER(table7), 5);

    label33 = gtk_label_new(_("Minutes"));
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

    /* Quoted text regular expression */
    regex_frame = gtk_frame_new(_("Quoted Text"));
    gtk_container_set_border_width(GTK_CONTAINER(regex_frame), 2);
    gtk_box_pack_start(GTK_BOX(vbox1), regex_frame, FALSE, FALSE, 0);

    regex_hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(regex_frame), regex_hbox);
    gtk_container_set_border_width(GTK_CONTAINER(regex_hbox), 5);

    regex_label = gtk_label_new(_("Quoted Text Regular Expression"));
    gtk_box_pack_start(GTK_BOX(regex_hbox), regex_label, FALSE, FALSE, 5);

    pui->quote_pattern = gnome_entry_new("quote-regex-history");
    gtk_box_pack_start(GTK_BOX(regex_hbox),pui->quote_pattern, FALSE,FALSE,0);

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

    spinbutton_adj = gtk_adjustment_new(1.0, 40.0, 200.0, 1.0, 5.0, 0.0);

    pui->wraplength =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
    gtk_table_attach(GTK_TABLE(table), pui->wraplength, 1, 2, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_widget_set_sensitive(pui->wraplength, FALSE);

    label = gtk_label_new(_("Characters"));
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 0, 1,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);

    frame2 = gtk_frame_new(_("Other options"));
    gtk_box_pack_start(GTK_BOX(vbox1), frame2, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame2), 5);

    table2 = GTK_TABLE(gtk_table_new(1, 2, FALSE));
    gtk_container_add(GTK_CONTAINER(frame2), GTK_WIDGET(table2));
    gtk_container_set_border_width(GTK_CONTAINER(table2), 5);
    pui->bcc = attach_entry(_("Default Bcc to:"), 0, table2);

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
     * finnished mail options, starting on display
     * PKGW: This naming scheme is, uh, unclear.
     */
    gint i;
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
    GtkWidget *information_frame, *information_table, *label;
    GtkWidget *option_menu;

    vbox2 = gtk_vbox_new(FALSE, 0);

    frame7 = gtk_frame_new(_("Main window"));
    gtk_box_pack_start(GTK_BOX(vbox2), frame7, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame7), 5);

    vbox7 = vbox_in_container(frame7);

    pui->previewpane = box_start_check(_("use preview pane"), vbox7);
    pui->mblist_show_mb_content_info =	box_start_check(
	_("Show mailbox statistics in left pane"), vbox7);
    pui->alternative_layout =	box_start_check(
	_("Use alternative main window layout"), vbox7);

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
    pui->date_format = attach_entry(_("Date encoding (for strftime):"),0,ftbl);
    pui->selected_headers = attach_entry(_("Selected headers:"), 1, ftbl);

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
    information_frame = gtk_frame_new(_("Information Messages"));
    gtk_box_pack_start(GTK_BOX(vbox2), information_frame, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(information_frame), 5);

    information_table = create_table(5, 2, GTK_CONTAINER(information_frame));
    /* gtk_table_set_row_spacings(GTK_TABLE(information_table), 1);
       gtk_table_set_col_spacings(GTK_TABLE(information_table), 5); */

    label = gtk_label_new(_("Informational Messages"));
    gtk_table_attach(GTK_TABLE(information_table), label, 0, 1, 0, 1,
		     GTK_FILL, 0, 0, 0);

    option_menu = gtk_option_menu_new();
    pui->information_message_menu = create_information_message_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu),
			     pui->information_message_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu),
				balsa_app.information_message);
    gtk_table_attach(GTK_TABLE(information_table), option_menu, 1, 2, 0, 1,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Warning Messages"));
    gtk_table_attach(GTK_TABLE(information_table), label, 0, 1, 1, 2,
		     GTK_FILL, 0, 0, 0);

    option_menu = gtk_option_menu_new();
    pui->warning_message_menu = create_information_message_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu),
			     pui->warning_message_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu),
				balsa_app.warning_message);
    gtk_table_attach(GTK_TABLE(information_table), option_menu, 1, 2, 1, 2,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Error Messages"));
    gtk_table_attach(GTK_TABLE(information_table), label, 0, 1, 2, 3,
		     GTK_FILL, 0, 0, 0);

    option_menu = gtk_option_menu_new();
    pui->error_message_menu = create_information_message_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu),
			     pui->error_message_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu),
				balsa_app.error_message);
    gtk_table_attach(GTK_TABLE(information_table), option_menu, 1, 2, 2, 3,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Fatal Error Messages"));
    gtk_table_attach(GTK_TABLE(information_table), label, 0, 1, 3, 4,
		     GTK_FILL, 0, 0, 0);

    option_menu = gtk_option_menu_new();
    pui->fatal_message_menu = create_information_message_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu),
			     pui->fatal_message_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu),
				balsa_app.fatal_message);
    gtk_table_attach(GTK_TABLE(information_table), option_menu, 1, 2, 3, 4,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Debug Messages"));
    gtk_table_attach(GTK_TABLE(information_table), label, 0, 1, 4, 5,
		     GTK_FILL, 0, 0, 0);

    option_menu = gtk_option_menu_new();
    pui->debug_message_menu = create_information_message_menu();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu),
			     pui->debug_message_menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu),
				balsa_app.debug_message);
    gtk_table_attach(GTK_TABLE(information_table), option_menu, 1, 2, 4, 5,
		     GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return vbox2;
}

#ifndef HAVE_GNOME_PRINT
static GtkWidget *
create_printing_page(gpointer data)
{
    /*
     * done display, starting printing
     */

    GtkWidget *vbox1;
    GtkWidget *frame10;
    GtkWidget *table5;
    GtkObject *spinbutton2_adj;
    GtkWidget *label25;
    GtkWidget *label24;
    GtkWidget *label19;
    GtkWidget *hbox5;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame10 = gtk_frame_new(_("Printing"));
    gtk_container_set_border_width(GTK_CONTAINER(frame10), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame10, FALSE, FALSE, 0);

    table5 = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame10), table5);
    gtk_container_set_border_width(GTK_CONTAINER(table5), 5);

    pui->PrintCommand = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table5), pui->PrintCommand, 1, 2, 0, 1,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    hbox5 = gtk_hbox_new(FALSE, 0);
    gtk_table_attach(GTK_TABLE(table5), hbox5, 1, 2, 1, 2,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);

    spinbutton2_adj = gtk_adjustment_new(78, 50, 200, 1, 10, 10);
    pui->PrintLinesize =
	gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton2_adj), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox5), pui->PrintLinesize, FALSE, FALSE,
		       0);
    gtk_widget_set_sensitive(pui->PrintLinesize, FALSE);

    label25 = gtk_label_new(_("characters"));
    gtk_box_pack_start(GTK_BOX(hbox5), label25, FALSE, TRUE, 0);

    label24 = gtk_label_new(_("Print command:"));
    gtk_table_attach(GTK_TABLE(table5), label24, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 0);

    pui->PrintBreakline =
	gtk_check_button_new_with_label(_("Break line at:"));
    gtk_table_attach(GTK_TABLE(table5), pui->PrintBreakline, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (0), 0, 5);
    gtk_container_set_border_width(GTK_CONTAINER(pui->PrintBreakline), 2);

    label19 = gtk_label_new(_("Printing"));

    return vbox1;

}
#endif

static GtkWidget*
add_spell_menu(const gchar* label, const gchar *names[], gint size, 
	       gint *index, GtkBox* parent, gint padding)
{
    GtkWidget *omenu;
    GtkWidget* table, *hbox, *lbw;

    omenu = create_spelling_option_menu(names, size, index);

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
    pui->module = add_spell_menu(_("Spell Check Module"),
				 spell_check_modules_name, NUM_PSPELL_MODULES,
				 &pui->module_index, GTK_BOX(vbox1), padding);

    /* do the suggestion modes menu */
    pui->suggestion_mode = add_spell_menu(
	_("Suggestion Level"), spell_check_suggest_mode_label,
	NUM_SUGGEST_MODES, &pui->suggestion_mode_index,GTK_BOX(vbox1),padding);

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
    GtkWidget *frame14;
    GtkWidget *frame914;
    GtkWidget *table6;
    GtkWidget *table96;
    GtkWidget *label27;
    GtkWidget *label927;
    GtkWidget *label;
    GtkWidget *label9;
    GtkWidget *label33;
    GtkWidget *color_frame;
    GtkWidget *vbox12;
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
	gtk_check_button_new_with_label(_("Empty trash on exit"));
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

    label33 = gtk_label_new(_("Minutes"));
    gtk_widget_show(label33);
    gtk_box_pack_start(GTK_BOX(hbox1), label33, FALSE, TRUE, 0);

    /* font */
    frame14 = gtk_frame_new(_("Font"));
    gtk_box_pack_start(GTK_BOX(vbox9), frame14, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame14), 5);

    table6 = gtk_table_new(10, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame14), table6);
    gtk_container_set_border_width(GTK_CONTAINER(table6), 5);

    pui->message_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table6), pui->message_font, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);

    /*a */
    pui->font_picker = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table6), pui->font_picker, 1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(pui->font_picker), 5);

    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker),
				    balsa_app.message_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER(pui->font_picker),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);

    label = gtk_label_new(_("Browse..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker),
				    GTK_WIDGET(label));
    gtk_object_set_user_data(GTK_OBJECT(pui->font_picker),
			     GTK_OBJECT(pui->message_font));
    gtk_object_set_user_data(GTK_OBJECT(pui->message_font),
			     GTK_OBJECT(pui->font_picker));

    label27 = gtk_label_new(_("Preview pane"));
    gtk_table_attach(GTK_TABLE(table6), label27, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_JUSTIFY_RIGHT), 0, 0);
    gtk_label_set_justify(GTK_LABEL(label27), GTK_JUSTIFY_RIGHT);


    /* Subject Font */
    frame914 = gtk_frame_new(_("Subject Header Font"));
    gtk_box_pack_start(GTK_BOX(vbox9), frame914, FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(frame914), 5);

    table96 = gtk_table_new(10, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame914), table96);
    gtk_container_set_border_width(GTK_CONTAINER(table96), 5);

    pui->subject_font = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table96), pui->subject_font, 0, 1, 1, 2,
		     (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		     (GtkAttachOptions) (GTK_FILL), 0, 0);

    /*b */
    pui->font_picker2 = gnome_font_picker_new();
    gtk_table_attach(GTK_TABLE(table96), pui->font_picker2, 1, 2, 1, 2,
		     (GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
    gtk_container_set_border_width(GTK_CONTAINER(pui->font_picker2), 5);

    gnome_font_picker_set_font_name(GNOME_FONT_PICKER(pui->font_picker2),
				    balsa_app.subject_font);
    gnome_font_picker_set_preview_text(GNOME_FONT_PICKER
				       (pui->font_picker2),
				       _("Select a font to use"));
    gnome_font_picker_set_mode(GNOME_FONT_PICKER(pui->font_picker2),
			       GNOME_FONT_PICKER_MODE_USER_WIDGET);

    label9 = gtk_label_new(_("Browse..."));
    gnome_font_picker_uw_set_widget(GNOME_FONT_PICKER(pui->font_picker2),
				    GTK_WIDGET(label9));
    gtk_object_set_user_data(GTK_OBJECT(pui->font_picker2),
			     GTK_OBJECT(pui->subject_font));
    gtk_object_set_user_data(GTK_OBJECT(pui->subject_font),
			     GTK_OBJECT(pui->font_picker2));

    label927 = gtk_label_new(_("Preview pane"));
    gtk_table_attach(GTK_TABLE(table96), label927, 0, 1, 0, 1,
		     (GtkAttachOptions) (GTK_FILL),
		     (GtkAttachOptions) (GTK_JUSTIFY_RIGHT), 0, 0);
    gtk_label_set_justify(GTK_LABEL(label927), GTK_JUSTIFY_RIGHT);

    /* mblist unread colour  */
    color_frame = gtk_frame_new(_("Colours"));
    gtk_container_set_border_width(GTK_CONTAINER(color_frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox9), color_frame, FALSE, FALSE, 0);

    vbox12 = vbox_in_container(color_frame);

    pui->unread_color = color_box(
	GTK_BOX(vbox12), _("Mailbox with unread messages colour"));    
    pui->quoted_color_start = color_box(
	GTK_BOX(vbox12), _("Quoted text primary colour"));
    pui->quoted_color_end = color_box(
	GTK_BOX(vbox12), _("Quoted text secondary color"));

    return vbox9;
}

static GtkWidget *
create_startup_page(gpointer data)
{
    GtkWidget *vbox1;
    GtkWidget *frame;
    GtkWidget *vb1;

    vbox1 = gtk_vbox_new(FALSE, 0);

    frame = gtk_frame_new(_("Options"));
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_box_pack_start(GTK_BOX(vbox1), frame, FALSE, FALSE, 0);

    vb1 = vbox_in_container(frame);

    pui->check_mail_upon_startup =
	gtk_check_button_new_with_label(_("Check mail upon startup"));
    gtk_box_pack_start(GTK_BOX(vb1), pui->check_mail_upon_startup,
		       FALSE, FALSE, 0);
    pui->remember_open_mboxes =
	gtk_check_button_new_with_label(
	    _("Remember open mailboxes between sessions"));
    gtk_box_pack_start(GTK_BOX(vb1), pui->remember_open_mboxes,
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
    gtk_widget_set_usize(frame, -2, 115);
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
    add_button_to_box(_("Add"),            address_book_add_cb,         vbox);
    add_button_to_box(_("Modify"),         address_book_edit_cb,        vbox);
    add_button_to_box(_("Delete"),         address_book_delete_cb,      vbox);
    add_button_to_box(_("Set as default"), set_default_address_book_cb, vbox);

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
    gchar *font;
    GtkWidget *peer;
    if (GNOME_IS_FONT_PICKER(widget)) {
	font = gnome_font_picker_get_font_name(GNOME_FONT_PICKER(widget));
	peer = gtk_object_get_user_data(GTK_OBJECT(widget));
	gtk_entry_set_text(GTK_ENTRY(peer), font);
    } else {
	font = gtk_entry_get_text(GTK_ENTRY(widget));
	peer = gtk_object_get_user_data(GTK_OBJECT(widget));
	gnome_font_picker_set_font_name(GNOME_FONT_PICKER(peer), font);
	properties_modified_cb(widget, pbox);
    }
}

static void
pop3_edit_cb(GtkWidget * widget, gpointer data)
{
    GtkCList *clist = GTK_CLIST(pui->pop3servers);
    gint row;
    BalsaMailboxNode *mbnode;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);

    mbnode = gtk_clist_get_row_data(clist, row);
    g_return_if_fail(mbnode);
    mailbox_conf_edit(mbnode);
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

    gtk_object_unref(GTK_OBJECT(address_book));

    update_address_books();
}


static void
pop3_add_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_POP3);
}

static void
pop3_del_cb(GtkWidget * widget, gpointer data)
{
    GtkCList *clist = GTK_CLIST(pui->pop3servers);
    gint row;
    BalsaMailboxNode *mbnode;

    if (!clist->selection)
	return;

    row = GPOINTER_TO_INT(clist->selection->data);
    mbnode = gtk_clist_get_row_data(clist, row);
    g_return_if_fail(mbnode);

    mailbox_conf_delete(mbnode);
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
wrap_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->wraplength), newstate);
    properties_modified_cb(widget, pbox);
}

#ifndef HAVE_GNOME_PRINT
static void
print_modified_cb(GtkWidget * widget, GtkWidget * pbox)
{
    gboolean newstate =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->PrintBreakline));

    gtk_widget_set_sensitive(GTK_WIDGET(pui->PrintLinesize), newstate);
    properties_modified_cb(widget, pbox);
}
#endif

static void
spelling_optionmenu_cb(GtkItem * menuitem, gpointer data)
{
    /* update the index number */
    gint *index = (gint *) data;
    *index = GPOINTER_TO_INT(gtk_object_get_data
			     (GTK_OBJECT(menuitem), "menu_index"));

}


static GtkWidget *
create_spelling_option_menu(const gchar * names[], gint size, gint * index)
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
			   GTK_SIGNAL_FUNC(spelling_optionmenu_cb),
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
add_show_menu(const char* label, BalsaInformationShow level, GtkWidget* menu)
{
    GtkWidget *menu_item = gtk_menu_item_new_with_label(label);
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
    add_show_menu(_("Show Nothing"),     BALSA_INFORMATION_SHOW_NONE,   menu);
    add_show_menu(_("Show Dialog"),      BALSA_INFORMATION_SHOW_DIALOG, menu);
    add_show_menu(_("Show In List"),     BALSA_INFORMATION_SHOW_LIST,   menu);
    add_show_menu(_("Print to console"), BALSA_INFORMATION_SHOW_STDERR, menu);
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


