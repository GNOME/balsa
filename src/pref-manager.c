/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
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

#include <gnome.h>
#include "balsa-app.h"
#include "pref-manager.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "save-restore.h"
#include "../libmutt/mime.h"
#include "../libmutt/mutt.h"

#define NUM_TOOLBAR_MODES 3
#define NUM_MDI_MODES 4
#define NUM_ENCODING_MODES 3
#define NUM_PWINDOW_MODES 3

typedef struct _PropertyUI {
	GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];
	GtkWidget *real_name, *email, *replyto, *signature;
	GtkWidget *sig_whenforward, *sig_whenreply, *sig_sending;
        GtkWidget *sig_separator;
	
	GtkWidget *pop3servers, *smtp_server, *mail_directory;
	GtkWidget *rb_local_mua, *rb_smtp_server;
	GtkWidget *check_mail_auto;
	GtkWidget *check_mail_minutes;
	
	GtkWidget *previewpane;
	GtkWidget *view_allheaders;
	GtkWidget *debug;		/* enable/disable debugging */
	GtkWidget *empty_trash;
	GtkRadioButton *pwindow_type[NUM_PWINDOW_MODES];
	GtkWidget *wordwrap;
	GtkWidget *wraplength;
	GtkWidget *bcc;	  
	GtkWidget *check_mail_upon_startup;
	GtkWidget *remember_open_mboxes;
#ifdef BALSA_SHOW_INFO
	GtkWidget *mblist_show_mb_content_info;
#endif
	/* arp */
	GtkWidget *quote_str;
	
	GtkWidget *message_font;    /* font used to display messages */ 
	GtkWidget *font_picker;
	
	/* charset */
	GtkRadioButton *encoding_type[NUM_ENCODING_MODES];
	GtkWidget *charset;

	GtkWidget *date_format;

	GtkWidget *selected_headers;

	/* printing */
	GtkWidget *PrintCommand;
	GtkWidget *PrintBreakline;
	GtkWidget *PrintLinesize;

        /* colours */
        GtkWidget *unread_color;

	/* address book */
	GtkWidget *ab_location;
	  
} PropertyUI;


static PropertyUI *pui = NULL;
static GtkWidget *property_box;
static gboolean already_open;

static GtkWidget *create_identity_page( void );
static GtkWidget *create_signature_page ( void );
static GtkWidget *create_mailserver_page ( void );
static GtkWidget *create_mailoptions_page ( void );
static GtkWidget *create_display_page ( void );
static GtkWidget *create_printing_page ( void );
static GtkWidget *create_encondig_page ( void );
static GtkWidget *create_misc_page ( void );
static GtkWidget *create_startup_page ( void );

static GtkWidget *incoming_page ( void );
static GtkWidget *outgoing_page ( void );
static void destroy_pref_window_cb (GtkWidget *pbox, PropertyUI *property_struct);
static void set_prefs (void);
static void apply_prefs (GnomePropertyBox* pbox, gint page_num);
void update_pop3_servers (void);
static void smtp_changed (void);
static void properties_modified_cb (GtkWidget * widget, GtkWidget * pbox);
static void font_changed (GtkWidget * widget, GtkWidget * pbox);
static void pop3_edit_cb (GtkWidget * widget, gpointer data);
static void pop3_add_cb (GtkWidget * widget, gpointer data);
static void pop3_del_cb (GtkWidget * widget, gpointer data);
static void timer_modified_cb( GtkWidget *widget, GtkWidget *pbox);
static void print_modified_cb( GtkWidget *widget, GtkWidget *pbox);
static void wrap_modified_cb( GtkWidget *widget, GtkWidget *pbox);

guint toolbar_type[NUM_TOOLBAR_MODES] =
{
  GTK_TOOLBAR_TEXT,
  GTK_TOOLBAR_ICONS,
  GTK_TOOLBAR_BOTH
};

gchar *toolbar_type_label[NUM_TOOLBAR_MODES] =
{
  N_("Text"),
  N_("Icons"),
  N_("Both"),
};

guint encoding_type[NUM_ENCODING_MODES] =
{
    ENC7BIT,
    ENC8BIT,
    ENCQUOTEDPRINTABLE
};

gchar *encoding_type_label[NUM_ENCODING_MODES] =
{
    N_("7bits"),
    N_("8bits"),
    N_("quoted")
};

guint pwindow_type[NUM_PWINDOW_MODES] =
{
  WHILERETR,
  UNTILCLOSED,
  NEVER
};

gchar *pwindow_type_label[NUM_PWINDOW_MODES] =
{
  N_("While Retrieving Messages"),
  N_("Until Closed"),
  N_("Never")
};


/* and now the important stuff: */
void
open_preferences_manager(GtkWidget *widget, gpointer data)
{
	static GnomeHelpMenuEntry help_entry = { NULL, "win-config" };
	gint i;
	GnomeApp *active_win = GNOME_APP(data);
	GtkWidget *page;

	/* only one preferences manager window */
	if (already_open) {
		gdk_window_raise (GTK_WIDGET (property_box)->window);
		return;
	}

	pui = g_malloc (sizeof (PropertyUI));

        property_box = gnome_property_box_new ();

        already_open = TRUE;
        
	gtk_window_set_title (GTK_WINDOW (property_box), _("Balsa Preferences"));
	gtk_window_set_policy (GTK_WINDOW (property_box), FALSE, FALSE, FALSE);

        gnome_dialog_set_parent(GNOME_DIALOG(property_box), GTK_WINDOW(active_win));
        gtk_object_set_data (GTK_OBJECT (property_box), "balsawindow", (gpointer) active_win);
        
        /* Create the pages */
	page = create_identity_page();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Identity")) );

	page = create_signature_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Signature")) );

	page = create_mailserver_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Mail Servers")) );

	page = create_mailoptions_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Mail Options")) );

	page = create_display_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Display")) );

	page = create_printing_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Printing")) );

	page = create_encondig_page();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Encoding")) );

	page = create_misc_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Misc")) );

	page = create_startup_page ();
        gnome_property_box_append_page (GNOME_PROPERTY_BOX (property_box), GTK_WIDGET (page), gtk_label_new (_ ("Startup")) );

	set_prefs ();

        for (i = 0; i < NUM_TOOLBAR_MODES; i++) {
		gtk_signal_connect (GTK_OBJECT (pui->toolbar_type[i]), "clicked",
				    properties_modified_cb, property_box);
	}

	for (i = 0; i < NUM_PWINDOW_MODES; i++) {
		gtk_signal_connect (GTK_OBJECT (pui->pwindow_type[i]), "clicked",
				    properties_modified_cb, property_box);
	}

	gtk_signal_connect (GTK_OBJECT (pui->previewpane), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->debug), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
#ifdef BALSA_SHOW_INFO
	gtk_signal_connect (GTK_OBJECT (pui->mblist_show_mb_content_info), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
#endif

	gtk_signal_connect (GTK_OBJECT (pui->real_name), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->email), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->replyto), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

	gtk_signal_connect (GTK_OBJECT (pui->sig_sending), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->sig_whenforward), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->sig_whenreply), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->sig_separator), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

	gtk_signal_connect (GTK_OBJECT (pui->rb_smtp_server), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->rb_smtp_server), "toggled",
			    GTK_SIGNAL_FUNC (smtp_changed), NULL);
	gtk_signal_connect (GTK_OBJECT (pui->smtp_server), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->mail_directory), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->signature), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->check_mail_auto), "toggled",
			    GTK_SIGNAL_FUNC (timer_modified_cb), property_box);

	gtk_signal_connect (GTK_OBJECT (pui->check_mail_minutes), "changed",
			    GTK_SIGNAL_FUNC (timer_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->wordwrap), "toggled",
			    GTK_SIGNAL_FUNC (wrap_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->wraplength), "changed",
			    GTK_SIGNAL_FUNC (wrap_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->bcc), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

	/* arp */
	gtk_signal_connect (GTK_OBJECT (pui->quote_str), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb),
			    property_box);
	
	/* message font */
	gtk_signal_connect (GTK_OBJECT (pui->message_font), "changed",
			    GTK_SIGNAL_FUNC (font_changed),
			    property_box);
	
	gtk_signal_connect (GTK_OBJECT (pui->font_picker), "font_set",
			    GTK_SIGNAL_FUNC (font_changed),
			    property_box);
 
	/* charset */
	gtk_signal_connect (GTK_OBJECT (pui->charset), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb),
			    property_box);

	for (i = 0; i < NUM_ENCODING_MODES; i++) {
		gtk_signal_connect (GTK_OBJECT (pui->encoding_type[i]), "clicked",
				    properties_modified_cb, property_box);
	}

	/* printing */
	gtk_signal_connect (GTK_OBJECT (pui->PrintCommand), "changed",
			    GTK_SIGNAL_FUNC (print_modified_cb),
			    property_box);
	gtk_signal_connect (GTK_OBJECT (pui->PrintBreakline), "toggled",
			    GTK_SIGNAL_FUNC (print_modified_cb),
			    property_box);
	gtk_signal_connect (GTK_OBJECT (pui->PrintLinesize), "changed",
			    GTK_SIGNAL_FUNC (print_modified_cb),
			    property_box);

	gtk_signal_connect (GTK_OBJECT (pui->check_mail_upon_startup), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->remember_open_mboxes), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

	gtk_signal_connect (GTK_OBJECT (pui->empty_trash), "toggled",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

	/* Date format */
	gtk_signal_connect (GTK_OBJECT (pui->date_format), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	gtk_signal_connect (GTK_OBJECT (pui->selected_headers), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
        gtk_signal_connect (GTK_OBJECT (pui->unread_color), "released",
                            GTK_SIGNAL_FUNC (properties_modified_cb), property_box);
	/* address book */
	gtk_signal_connect (GTK_OBJECT (pui->ab_location), "changed",
			    GTK_SIGNAL_FUNC (properties_modified_cb), property_box);

        /* Gnome Property Box Signals */
        gtk_signal_connect (GTK_OBJECT (property_box), "destroy",
                            GTK_SIGNAL_FUNC (destroy_pref_window_cb), pui);

        gtk_signal_connect (GTK_OBJECT (property_box), "apply", 
                            GTK_SIGNAL_FUNC (apply_prefs), pui);

        help_entry.name = gnome_app_id;        
        gtk_signal_connect (GTK_OBJECT (property_box), "help", 
                            GTK_SIGNAL_FUNC (gnome_help_pbox_display), 
                            &help_entry); 

	gtk_widget_show_all ( GTK_WIDGET(property_box));

} /* open_preferences_manager */


static void
smtp_changed (void)
{
	balsa_app.smtp=!balsa_app.smtp;
	gtk_widget_set_sensitive (pui->smtp_server, balsa_app.smtp);
} 

/*
 * update data from the preferences window
 */

static void
destroy_pref_window_cb (GtkWidget *pbox, PropertyUI *property_struct)
{
        g_free (property_struct);
	property_struct = NULL;
        already_open = FALSE;
}

static void
apply_prefs (GnomePropertyBox* pbox, gint page_num)
{
	gint i;
        GtkWidget *balsa_window;

        if (page_num != -1)
                return;

	/*
	 * identity page
	 */
	g_free (balsa_app.address->personal);
	balsa_app.address->personal = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->real_name)));

	g_free (balsa_app.address->mailbox);
	balsa_app.address->mailbox = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->email)));

	g_free (balsa_app.replyto);
	balsa_app.replyto = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->replyto)));

	g_free (balsa_app.smtp_server);
	balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->smtp_server)));
	
	g_free (balsa_app.signature_path);
	balsa_app.signature_path = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->signature)));

	g_free (balsa_app.local_mail_directory);
	balsa_app.local_mail_directory = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->mail_directory)));

	balsa_app.sig_sending = GTK_TOGGLE_BUTTON (pui->sig_sending)->active;
	balsa_app.sig_whenforward = GTK_TOGGLE_BUTTON (pui->sig_whenforward)->active;
	balsa_app.sig_whenreply = GTK_TOGGLE_BUTTON (pui->sig_whenreply)->active;
	balsa_app.sig_separator = GTK_TOGGLE_BUTTON (pui->sig_separator)->active;

	/* 
	 * display page 
	 */
	for (i = 0; i < NUM_TOOLBAR_MODES; i++)
		if (GTK_TOGGLE_BUTTON (pui->toolbar_type[i])->active) {
			balsa_app.toolbar_style = toolbar_type[i];
			balsa_window_refresh (NULL);
			break;
		}
	for (i=0; i < NUM_PWINDOW_MODES; i++)
		if(GTK_TOGGLE_BUTTON (pui->pwindow_type[i])->active) {
			balsa_app.pwindow_option = pwindow_type[i];
			break;
		}

	balsa_app.debug = GTK_TOGGLE_BUTTON (pui->debug)->active;
	balsa_app.previewpane = GTK_TOGGLE_BUTTON (pui->previewpane)->active;
	balsa_app.smtp = GTK_TOGGLE_BUTTON (pui->rb_smtp_server)->active;
#ifdef BALSA_SHOW_INFO
	if (balsa_app.mblist_show_mb_content_info != GTK_TOGGLE_BUTTON (pui->mblist_show_mb_content_info)->active) {
		balsa_app.mblist_show_mb_content_info = !balsa_app.mblist_show_mb_content_info;
		gtk_object_set ( GTK_OBJECT (balsa_app.mblist),"show_content_info", balsa_app.mblist_show_mb_content_info, NULL  );
	}
#endif
	balsa_app.check_mail_auto = GTK_TOGGLE_BUTTON(pui->check_mail_auto)->active;
	balsa_app.check_mail_timer = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->check_mail_minutes));

	if(balsa_app.check_mail_auto)
		update_timer( TRUE, balsa_app.check_mail_timer );
	else
		update_timer( FALSE, 0);

	balsa_app.wordwrap = GTK_TOGGLE_BUTTON(pui->wordwrap)->active;
	balsa_app.wraplength = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(pui->wraplength));

	g_free (balsa_app.bcc);
	balsa_app.bcc = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->bcc)));


	/* arp */
	g_free (balsa_app.quote_str);
	balsa_app.quote_str = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->quote_str)));

	g_free (balsa_app.message_font);
	balsa_app.message_font = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->message_font)));


	/* charset*/
	g_free (balsa_app.charset);
	balsa_app.charset = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->charset)));
	mutt_set_charset (balsa_app.charset);

	for (i = 0; i < NUM_ENCODING_MODES; i++)
		if (GTK_TOGGLE_BUTTON (pui->encoding_type[i])->active) {
			balsa_app.encoding_style = encoding_type[i];
			break;
		}

	/* printing */
	g_free (balsa_app.PrintCommand.PrintCommand);
	balsa_app.PrintCommand.PrintCommand = g_strdup( gtk_entry_get_text(GTK_ENTRY (pui->PrintCommand)));

	balsa_app.PrintCommand.linesize = atoi(gtk_entry_get_text(GTK_ENTRY( pui->PrintLinesize)));
	balsa_app.PrintCommand.breakline =  GTK_TOGGLE_BUTTON(pui->PrintBreakline)->active;


	balsa_app.check_mail_upon_startup = GTK_TOGGLE_BUTTON(pui->check_mail_upon_startup)->active;
	balsa_app.remember_open_mboxes = GTK_TOGGLE_BUTTON(pui->remember_open_mboxes)->active;
	balsa_app.empty_trash_on_exit = GTK_TOGGLE_BUTTON(pui->empty_trash)->active;

	/* date format */
	g_free (balsa_app.date_string);
	balsa_app.date_string = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->date_format)));

	/* selected headers */
	g_free (balsa_app.selected_headers);
	balsa_app.selected_headers = g_strdup (gtk_entry_get_text (
	   GTK_ENTRY (pui->selected_headers)) );
	g_strdown(balsa_app.selected_headers);

        /* unread mailbox color */
        gdk_colormap_free_colors (gdk_colormap_get_system (), &balsa_app.mblist_unread_color, 1);
        gnome_color_picker_get_i16 (GNOME_COLOR_PICKER(pui->unread_color), &(balsa_app.mblist_unread_color.red), &(balsa_app.mblist_unread_color.green), &(balsa_app.mblist_unread_color.blue), 0);

	/* address book */
	g_free (balsa_app.ab_location);
	balsa_app.ab_location = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->ab_location)));

	/* XXX */
	/*  refresh_main_window (); */
	
	/*
	 * close window and free memory
	 */
	config_global_save ();
        balsa_mblist_redraw (balsa_app.mblist);
        balsa_window = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (pbox), "balsawindow"));
        balsa_window_refresh (BALSA_WINDOW (balsa_window));
}


/*
 * refresh data in the preferences window
 */
void
set_prefs (void)
{
	gchar tmp[10];
	gint i;

	for (i = 0; i < NUM_TOOLBAR_MODES; i++)
		if (balsa_app.toolbar_style == toolbar_type[i]) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->toolbar_type[i]), TRUE);
			break;
		}

	for (i = 0; i < NUM_PWINDOW_MODES; i++)
		if( balsa_app.pwindow_option == pwindow_type[i]) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->pwindow_type[i]), TRUE);
			break;
		}
	
	gtk_entry_set_text (GTK_ENTRY (pui->real_name), balsa_app.address->personal);

	gtk_entry_set_text (GTK_ENTRY (pui->email), balsa_app.address->mailbox);
	gtk_entry_set_text (GTK_ENTRY (pui->replyto), balsa_app.replyto);

	gtk_entry_set_text (GTK_ENTRY (pui->signature), balsa_app.signature_path);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (pui->sig_sending), balsa_app.sig_sending);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (pui->sig_whenforward), balsa_app.sig_whenforward);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (pui->sig_whenreply), balsa_app.sig_whenreply);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (pui->sig_separator), balsa_app.sig_separator);

	if (balsa_app.smtp_server) 
		gtk_entry_set_text (GTK_ENTRY (pui->smtp_server), balsa_app.smtp_server);
   
  
	gtk_entry_set_text (GTK_ENTRY (pui->mail_directory), balsa_app.local_mail_directory);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->previewpane), balsa_app.previewpane);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->debug), balsa_app.debug);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->rb_smtp_server), balsa_app.smtp);
#ifdef BALSA_SHOW_INFO
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->mblist_show_mb_content_info), balsa_app.mblist_show_mb_content_info);
#endif

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->check_mail_auto),
				      balsa_app.check_mail_auto);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (pui->check_mail_minutes), 
				   (float) balsa_app.check_mail_timer );
	
	gtk_widget_set_sensitive (pui->smtp_server,
				  GTK_TOGGLE_BUTTON(pui->rb_smtp_server)->active);

	gtk_widget_set_sensitive (pui->check_mail_minutes,
				  GTK_TOGGLE_BUTTON(pui->check_mail_auto)->active);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->wordwrap),
				      balsa_app.wordwrap);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (pui->wraplength), 
				   (float) balsa_app.wraplength );
	
	gtk_widget_set_sensitive (pui->wraplength,
				  GTK_TOGGLE_BUTTON(pui->wordwrap)->active);
	
	gtk_entry_set_text (GTK_ENTRY (pui->bcc),
			    balsa_app.bcc ? balsa_app.bcc : "");

	/* arp */
	gtk_entry_set_text (GTK_ENTRY (pui->quote_str), balsa_app.quote_str);

	/* message font */
	gtk_entry_set_text (GTK_ENTRY (pui->message_font), balsa_app.message_font);
	gtk_entry_set_position (GTK_ENTRY (pui->message_font), 0);

	/* charset */
	gtk_entry_set_text (GTK_ENTRY (pui->charset), balsa_app.charset);
	for (i = 0; i < NUM_ENCODING_MODES; i++)
		if (balsa_app.encoding_style == encoding_type[i]) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->encoding_type[i]), TRUE);
			break;
		}

	/*printing */
	gtk_entry_set_text(GTK_ENTRY(pui->PrintCommand), balsa_app.PrintCommand.PrintCommand);
	sprintf(tmp, "%d", balsa_app.PrintCommand.linesize);
	gtk_entry_set_text(GTK_ENTRY(pui->PrintLinesize), tmp);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->PrintBreakline),
				      balsa_app.PrintCommand.breakline);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
	    pui->check_mail_upon_startup), balsa_app.check_mail_upon_startup);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
	    pui->remember_open_mboxes), balsa_app.remember_open_mboxes);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (
	    pui->empty_trash), balsa_app.empty_trash_on_exit);

	/* date format */
	if(balsa_app.date_string) 
	   gtk_entry_set_text (GTK_ENTRY (pui->date_format),
			       balsa_app.date_string);
	if(balsa_app.selected_headers) 
	   gtk_entry_set_text (GTK_ENTRY (pui->selected_headers),
			       balsa_app.selected_headers);

        gnome_color_picker_set_i16 (GNOME_COLOR_PICKER(pui->unread_color), balsa_app.mblist_unread_color.red, balsa_app.mblist_unread_color.green, balsa_app.mblist_unread_color.blue, 0);

	/* address book */
	gtk_entry_set_text (GTK_ENTRY (pui->ab_location), 
			    balsa_app.ab_location);

}

void
update_pop3_servers (void)
{
	GtkCList *clist;
	GList *list = balsa_app.inbox_input;
	gchar *text[2];
	gint row;

	LibBalsaMailbox *mailbox;

	if (!pui)
		return;

	clist = GTK_CLIST (pui->pop3servers);
	
	gtk_clist_clear (clist);
	
	gtk_clist_freeze (clist);
	while (list) {
		mailbox = list->data;
		if (mailbox) {
			switch (mailbox->type) {
			case MAILBOX_POP3:	
				text[0] = "POP3"; 
				break;
			case MAILBOX_IMAP:	
				text[0] = "IMAP"; 
				break;
			default:		
				text[0] = "????"; 
				break;
			}
			text[1] = mailbox->name;
			row = gtk_clist_append (clist, text);
			gtk_clist_set_row_data (clist, row, mailbox);
		}
		list = list->next;
	}
	gtk_clist_select_row(clist, 0, 0);
	gtk_clist_thaw (clist);
}


static GtkWidget *
create_identity_page( )
{
	GtkWidget *frame1;
	GtkWidget *table1;
	GtkWidget *label1;
	GtkWidget *vbox1;

	vbox1 = gtk_vbox_new ( FALSE, 0);
	gtk_widget_show( vbox1);

	frame1 = gtk_frame_new (_("Identity"));
	gtk_widget_show (frame1);
	gtk_box_pack_start (GTK_BOX (vbox1), frame1, FALSE, FALSE, 0);

	table1 = gtk_table_new (3, 2, FALSE);
	gtk_widget_show (table1);
	gtk_container_add (GTK_CONTAINER (frame1), table1);
	gtk_container_set_border_width (GTK_CONTAINER (table1), 10);
	gtk_table_set_row_spacings (GTK_TABLE (table1), 10);
	gtk_table_set_col_spacings (GTK_TABLE (table1), 10);
	gtk_container_set_border_width (GTK_CONTAINER (frame1), 5);	

	pui->real_name = gtk_entry_new ();
	gtk_widget_show (pui->real_name);
	gtk_table_attach (GTK_TABLE (table1), pui->real_name, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	pui->email = gtk_entry_new ();
	gtk_widget_show (pui->email);
	gtk_table_attach (GTK_TABLE (table1), pui->email, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	label1 = gtk_label_new (_("Your name:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_label_set_justify (GTK_LABEL (label1), GTK_JUSTIFY_RIGHT);

	label1 = gtk_label_new (_("E-mail address:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	label1 = gtk_label_new (_("Reply-to address:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 2, 3,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	pui->replyto = gtk_entry_new ();
	gtk_widget_show (pui->replyto);
	gtk_table_attach (GTK_TABLE (table1), pui->replyto, 1, 2, 2, 3,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);


	return vbox1;
}


	/*
	 * finnished Identity, starting on signature
	 */
static GtkWidget *
create_signature_page ( )
{
	GtkWidget *vbox;
	GtkWidget *frame1;
	GtkWidget *table1;
	GtkWidget *fileentry1;
	GtkWidget *vbox1;
	GtkWidget *label1;

	vbox = gtk_vbox_new ( FALSE, 0);
	gtk_widget_show(vbox);

	frame1 = gtk_frame_new (_("Signature"));
	gtk_widget_show (frame1);
	gtk_box_pack_start( GTK_BOX(vbox), frame1, FALSE, FALSE, 0 );

	table1 = gtk_table_new (6, 2, FALSE);
	gtk_widget_show (table1);
	gtk_container_add (GTK_CONTAINER (frame1), table1);
	gtk_container_set_border_width (GTK_CONTAINER (table1), 10);
	gtk_table_set_row_spacings (GTK_TABLE (table1), 5);
	gtk_table_set_col_spacings (GTK_TABLE (table1), 10);
	gtk_container_set_border_width (GTK_CONTAINER (frame1), 5);	

	fileentry1 = gnome_file_entry_new ("SIGNATURE-FILE", _("Select your signature file"));
	gtk_widget_show (fileentry1);
	gtk_table_attach (GTK_TABLE (table1), fileentry1, 1, 2, 3, 4,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gnome_file_entry_set_modal (GNOME_FILE_ENTRY (fileentry1), TRUE);
	
	pui->signature = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (fileentry1));
	gtk_widget_show (pui->signature);

	pui->sig_sending = gtk_check_button_new_with_label (_("sending mail"));
	gtk_widget_show ( pui->sig_sending);
	gtk_table_attach (GTK_TABLE (table1),  pui->sig_sending, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	pui->sig_whenreply = gtk_check_button_new_with_label (_("replying to mail"));
	gtk_widget_show ( pui->sig_whenreply);
	gtk_table_attach (GTK_TABLE (table1),  pui->sig_whenreply, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	pui->sig_whenforward  = gtk_check_button_new_with_label (_("forwarding mail"));
	gtk_widget_show ( pui->sig_whenforward );
	gtk_table_attach (GTK_TABLE (table1),  pui->sig_whenforward , 1, 2, 2, 3,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_table_attach (GTK_TABLE (table1), vbox1, 1, 2, 4, 5,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	pui->quote_str = gtk_entry_new_with_max_length (10);
	gtk_widget_show (pui->quote_str);
	gtk_box_pack_start (GTK_BOX (vbox1), pui->quote_str, FALSE, TRUE, 0);
	gtk_entry_set_text (GTK_ENTRY (pui->quote_str), _(">"));
	
	label1 = gtk_label_new (_("Signature file:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 3, 4,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_label_set_justify (GTK_LABEL (label1), GTK_JUSTIFY_LEFT);
	
	label1 = gtk_label_new (_("Reply prefix:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 4, 5,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 7);
	gtk_label_set_justify (GTK_LABEL (label1), GTK_JUSTIFY_RIGHT);
	
	label1 = gtk_label_new (_("Use signature file when:"));
	gtk_widget_show (label1);
	gtk_table_attach (GTK_TABLE (table1), label1, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	pui->sig_separator = gtk_check_button_new_with_label (
	   _("enable signature separator"));
	gtk_widget_show ( pui->sig_separator);
	gtk_table_attach (GTK_TABLE (table1),  pui->sig_separator, 1, 2, 5, 6,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	label1 = gtk_label_new (_("Signature"));

	return vbox;

}

static GtkWidget *
create_mailserver_page ( )
{
	/*
	 * finniched signature, starting on mailservers
	 */
	GtkWidget *table3;
	GtkWidget *frame3;
	GtkWidget *hbox1;
	GtkWidget *scrolledwindow3;
	GtkWidget *label14;
	GtkWidget *label15;
	GtkWidget *vbox1;
	GtkWidget *button;
	GtkWidget *frame4;
	GtkWidget *hbox2;
	GtkWidget *label16;
	GtkWidget *fileentry2;
	GtkWidget *frame5;
	GtkWidget *table4;
	GSList *table4_group = NULL;

	table3 = gtk_table_new (3, 1, FALSE);
	gtk_widget_show (table3);

	frame3 = gtk_frame_new (_("Remote Mailbox Servers"));
	gtk_widget_show (frame3);
	gtk_table_attach (GTK_TABLE (table3), frame3, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (frame3, -2, 115);
	gtk_container_set_border_width (GTK_CONTAINER (frame3), 5);
	
	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_container_add (GTK_CONTAINER (frame3), hbox1);
	
	scrolledwindow3 = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (scrolledwindow3);
	gtk_box_pack_start (GTK_BOX (hbox1), scrolledwindow3, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (scrolledwindow3), 5);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow3), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	
	pui->pop3servers = gtk_clist_new (2);
	gtk_widget_show (pui->pop3servers);
	gtk_container_add (GTK_CONTAINER (scrolledwindow3), pui->pop3servers);
	gtk_clist_set_column_width (GTK_CLIST (pui->pop3servers), 0, 40);
	gtk_clist_set_column_width (GTK_CLIST (pui->pop3servers), 1, 80);
	gtk_clist_column_titles_show (GTK_CLIST (pui->pop3servers));
	
	label14 = gtk_label_new (_("Type"));
	gtk_widget_show (label14);
	gtk_clist_set_column_widget (GTK_CLIST (pui->pop3servers), 0, label14);
	
	label15 = gtk_label_new (_("Mailbox name"));
	gtk_widget_show (label15);
	gtk_clist_set_column_widget (GTK_CLIST (pui->pop3servers), 1, label15);
	gtk_label_set_justify (GTK_LABEL (label15), GTK_JUSTIFY_LEFT);

	update_pop3_servers ();
	
	vbox1 = gtk_vbox_new (FALSE, 6);
	gtk_widget_show (vbox1);
	gtk_box_pack_start (GTK_BOX (hbox1), vbox1, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (vbox1), 5);
	
	button = gtk_button_new_with_label (_("Add"));
	gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_add_cb), NULL);

	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);

	button = gtk_button_new_with_label (_("Modify"));
	gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
				   GTK_SIGNAL_FUNC (pop3_edit_cb), NULL);
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);

	button = gtk_button_new_with_label (_("Delete"));
	gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
				   GTK_SIGNAL_FUNC (pop3_del_cb), NULL);
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);

	frame4 = gtk_frame_new (_("Local mail"));
	gtk_widget_show (frame4);
	gtk_table_attach (GTK_TABLE (table3), frame4, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame4), 5);

	hbox2 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox2);
	gtk_container_add (GTK_CONTAINER (frame4), hbox2);

	label16 = gtk_label_new ("");
	gtk_widget_show (label16);
	gtk_box_pack_start (GTK_BOX (hbox2), label16, FALSE, FALSE, 0);

	fileentry2 = gnome_file_entry_new ("MAIL-DIR", _("Select your local mail directory"));
	gtk_widget_show (fileentry2);
	gtk_box_pack_start (GTK_BOX (hbox2), fileentry2, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (fileentry2), 10);
	gnome_file_entry_set_directory (GNOME_FILE_ENTRY (fileentry2), TRUE);
	gnome_file_entry_set_modal (GNOME_FILE_ENTRY (fileentry2), TRUE);

	pui->mail_directory  = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (fileentry2));
	gtk_widget_show ( pui->mail_directory );
	
	frame5 = gtk_frame_new (_("Outgoing mail"));
	gtk_widget_show (frame5);
	gtk_table_attach (GTK_TABLE (table3), frame5, 0, 1, 2, 3,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame5), 5);
	
	table4 = gtk_table_new (2, 2, FALSE);
	gtk_widget_show (table4);
	gtk_container_add (GTK_CONTAINER (frame5), table4);
	gtk_container_set_border_width (GTK_CONTAINER (table4), 10);

	pui->smtp_server = gtk_entry_new ();
	gtk_widget_show ( pui->smtp_server);
	gtk_table_attach (GTK_TABLE (table4),  pui->smtp_server, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_widget_set_sensitive ( pui->smtp_server, FALSE);

	pui->rb_smtp_server  = gtk_radio_button_new_with_label (table4_group, _("Remote SMTP Server"));
	table4_group = gtk_radio_button_group (GTK_RADIO_BUTTON (pui->rb_smtp_server ));
	gtk_widget_show (pui->rb_smtp_server );
	gtk_table_attach (GTK_TABLE (table4), pui->rb_smtp_server , 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);

	pui->rb_local_mua  = gtk_radio_button_new_with_label (table4_group, _("Local mail user agent"));
	table4_group = gtk_radio_button_group (GTK_RADIO_BUTTON ( pui->rb_local_mua ));
	gtk_widget_show ( pui->rb_local_mua );
	gtk_table_attach (GTK_TABLE (table4),  pui->rb_local_mua , 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON ( pui->rb_local_mua ), TRUE);

	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->rb_local_mua)))
		gtk_widget_set_sensitive (pui->smtp_server, FALSE);

	return table3;
}



	/*
	 * finnish mail servers, starting mail options
	 */
static GtkWidget *
create_mailoptions_page ()
{
	GtkWidget *note;
	GtkWidget *page;
	GtkWidget *label;

	note = gtk_notebook_new();
	gtk_container_set_border_width (GTK_CONTAINER (note), 5);
	page = incoming_page();
	label =  gtk_label_new (_("Incoming"));
	gtk_notebook_append_page(GTK_NOTEBOOK(note), page, label);

	page = outgoing_page();
	label = gtk_label_new (_("Outgoing"));
	gtk_notebook_append_page(GTK_NOTEBOOK(note), page, label);

	return note;

}

static GtkWidget *
incoming_page()
{
	GtkWidget *vbox1;
	GtkWidget *frame15;
	GtkWidget *table7;
	GtkWidget *label33;
	GtkObject *spinbutton4_adj;

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show(vbox1);

	frame15 = gtk_frame_new (_("Checking"));
	gtk_widget_show (frame15);
	gtk_container_set_border_width( GTK_CONTAINER( frame15), 5);
	gtk_box_pack_start( GTK_BOX (vbox1), frame15, FALSE, FALSE, 0);

	table7 = gtk_table_new (2, 3, FALSE);
	gtk_widget_show (table7);
	gtk_container_add (GTK_CONTAINER (frame15), table7);
	gtk_container_set_border_width (GTK_CONTAINER (table7), 5);
	
	label33 = gtk_label_new (_("Minutes"));
	gtk_widget_show (label33);
	gtk_table_attach (GTK_TABLE (table7), label33, 2, 3, 0, 1,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);

	spinbutton4_adj = gtk_adjustment_new (10, 1, 100, 1, 10, 10);
	pui->check_mail_minutes  = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton4_adj), 1, 0);
	gtk_widget_show ( pui->check_mail_minutes );
	gtk_table_attach (GTK_TABLE (table7),  pui->check_mail_minutes , 1, 2, 0, 1,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_widget_set_sensitive ( pui->check_mail_minutes , FALSE);
	
	pui->check_mail_auto = gtk_check_button_new_with_label (_("Check mail automatically every:"));
	gtk_widget_show (  pui->check_mail_auto);
	gtk_table_attach (GTK_TABLE (table7),   pui->check_mail_auto, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	return vbox1;
}

static GtkWidget *
outgoing_page ( )
{
	GtkWidget *frame1;
	GtkWidget *frame2;
	GtkWidget *table;
	GtkWidget *table2;
	GtkObject *spinbutton_adj;
	GtkWidget *label;
	GtkWidget *vbox1;

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);

	frame1 = gtk_frame_new(_("Word Wrap"));
	gtk_widget_show(frame1);
	gtk_container_set_border_width( GTK_CONTAINER( frame1), 5);
	gtk_box_pack_start( GTK_BOX (vbox1), frame1, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame1), 5);

	table = gtk_table_new (2, 3, FALSE);
	gtk_widget_show (table);
	gtk_container_add (GTK_CONTAINER (frame1), table);
	gtk_container_set_border_width (GTK_CONTAINER (table), 5);
	
	pui->wordwrap = gtk_check_button_new_with_label(_("Wrap Outgoing Text at:"));
	gtk_widget_show (  pui->wordwrap);
	gtk_table_attach (GTK_TABLE (table),   pui->wordwrap, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_widget_show(pui->wordwrap);

	spinbutton_adj = gtk_adjustment_new( 1.0, 40.0, 200.0, 1.0, 5.0, 0.0);

	pui->wraplength = gtk_spin_button_new(GTK_ADJUSTMENT(spinbutton_adj), 1, 0);
	gtk_table_attach (GTK_TABLE (table), pui->wraplength, 1, 2, 0, 1,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_widget_set_sensitive (pui->wraplength, FALSE);	

	label = gtk_label_new (_("Characters"));
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, 2, 3, 0, 1,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);

	frame2 = gtk_frame_new (_("Other options") );
	gtk_widget_show(frame2);
	gtk_box_pack_start( GTK_BOX (vbox1), frame2, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame2), 5);

	table2 = gtk_table_new (2, 3, FALSE);
	gtk_widget_show (table2);
	gtk_container_add (GTK_CONTAINER (frame2), table2);
	gtk_container_set_border_width (GTK_CONTAINER (table2), 5);
	

	label = gtk_label_new(_("Default Bcc to:"));
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE (table2), label, 0, 1, 1, 2,
			 (GtkAttachOptions) (0),
			 (GtkAttachOptions) (0), 0, 0);

	pui->bcc = gtk_entry_new();
	gtk_widget_show(pui->bcc);
	gtk_table_attach(GTK_TABLE(table2), pui->bcc, 1, 3, 1, 3,
				   (GtkAttachOptions) (0),
				   (GtkAttachOptions) (0), 0, 0);
	return vbox1;

}

static GtkWidget *
create_display_page ( )
{	
	/*
	 * finnished mail options, starting on display
	 * PKGW: This naming scheme is, uh, unclear.
	 */
	gint      i;
	GtkWidget *frame7;
	GtkWidget *vbox7;
	GtkWidget *hbox4;
	GtkWidget *frame8;
	GtkWidget *vbox3;
	GtkWidget *frame9;
	GtkWidget *vbox4;
	GtkWidget *label18;
	GSList    *group;
	GtkWidget *vbox2;
	GtkWidget *format_frame, *format_table, *format_widget;
	
	vbox2 = gtk_vbox_new (FALSE, 0);
	
	frame7 = gtk_frame_new (_("Main window"));
	gtk_box_pack_start (GTK_BOX (vbox2), frame7, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame7), 5);

	vbox7 = gtk_vbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (frame7), vbox7);
	gtk_container_set_border_width (GTK_CONTAINER (vbox7), 5);	

	pui->previewpane = gtk_check_button_new_with_label (_("use preview pane"));
	gtk_widget_show (pui->previewpane);
	gtk_box_pack_start (GTK_BOX (vbox7), pui->previewpane, FALSE, TRUE, 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->previewpane), TRUE);
	
#ifdef BALSA_SHOW_INFO
	pui->mblist_show_mb_content_info = gtk_check_button_new_with_label (_("Show mailbox statistics in left pane"));
	gtk_box_pack_start (GTK_BOX (vbox7), pui->mblist_show_mb_content_info, FALSE, TRUE, 0);
#endif BALSA_SHOW_INFO

	hbox4 = gtk_hbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox4, FALSE, FALSE, 0);
	
	frame8 = gtk_frame_new (_("Toolbars"));
	gtk_box_pack_start (GTK_BOX (hbox4), frame8, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame8), 5);

	vbox3 = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame8), vbox3);
	gtk_container_set_border_width (GTK_CONTAINER (vbox3), 5);

	group = NULL;
	for (i = 0; i < NUM_TOOLBAR_MODES; i++) {
		pui->toolbar_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group,
											  _(toolbar_type_label[i])));
		gtk_box_pack_start (GTK_BOX (vbox3), GTK_WIDGET (pui->toolbar_type[i]), FALSE, TRUE,  0);
		group = gtk_radio_button_group (pui->toolbar_type[i]);
	}

	
	frame9 = gtk_frame_new (_("Display progress dialog"));
	gtk_box_pack_start (GTK_BOX (hbox4), frame9, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame9), 5);
	
	vbox4 = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (frame9), vbox4);
	gtk_container_set_border_width (GTK_CONTAINER (vbox4), 5);

	format_frame = gtk_frame_new( _("Display Formats") );
	gtk_box_pack_start( GTK_BOX(vbox2), format_frame, FALSE, FALSE, 0 );
	gtk_container_set_border_width( GTK_CONTAINER( format_frame ), 5 );

	format_table = gtk_table_new( 2, 2, FALSE );
	gtk_container_add( GTK_CONTAINER( format_frame ), format_table );
	gtk_container_set_border_width( GTK_CONTAINER( format_table ), 5 );	
	format_widget = gtk_label_new( _("Date encoding (for strftime):") );
	gtk_table_attach( GTK_TABLE( format_table ), format_widget, 0, 1, 0, 1,
			  (GtkAttachOptions) GTK_FILL, (GtkAttachOptions) 0,
			  0, 0 );
	gtk_label_set_justify( GTK_LABEL( format_widget ), GTK_JUSTIFY_RIGHT );

	format_widget = gtk_entry_new();
	gtk_table_attach( GTK_TABLE( format_table ), format_widget, 1, 2, 0, 1,
			  (GtkAttachOptions) GTK_FILL, (GtkAttachOptions) 0,
			  0, 0 );
	pui->date_format = format_widget;

	format_widget = gtk_label_new( _("Selected headers:") );
	gtk_table_attach( GTK_TABLE( format_table ), format_widget, 0, 1, 1, 2,
			  (GtkAttachOptions) GTK_FILL, (GtkAttachOptions) 0,
			  0, 0 );
	gtk_label_set_justify( GTK_LABEL( format_widget ), GTK_JUSTIFY_RIGHT );

	format_widget = gtk_entry_new();
	gtk_table_attach( GTK_TABLE( format_table ), format_widget, 1, 2, 1, 2,
			  (GtkAttachOptions) GTK_FILL, (GtkAttachOptions) 0,
			  0, 0 );
	pui->selected_headers = format_widget;

	group = NULL;
	for (i = 0; i < NUM_PWINDOW_MODES; i++)  {
		pui->pwindow_type[i] = GTK_RADIO_BUTTON (
		   gtk_radio_button_new_with_label (group, 
						    _(pwindow_type_label[i])));
		gtk_box_pack_start (GTK_BOX (vbox4), 
				    GTK_WIDGET (pui->pwindow_type[i]), 
				    FALSE, TRUE, 0);
		group = gtk_radio_button_group (pui->pwindow_type[i]);
	}

	label18 = gtk_label_new (_("Display"));

	gtk_widget_show_all (vbox2);
	return vbox2;
}

static GtkWidget *
create_printing_page ( )
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

	vbox1 = gtk_vbox_new ( FALSE, 0);
	gtk_widget_show(vbox1);

	frame10 = gtk_frame_new (_("Printing"));
	gtk_widget_show (frame10);
	gtk_container_set_border_width (GTK_CONTAINER (frame10), 5);
	gtk_box_pack_start( GTK_BOX(vbox1), frame10, FALSE, FALSE, 0);

	table5 = gtk_table_new (2, 3, FALSE);
	gtk_widget_show (table5);
	gtk_container_add (GTK_CONTAINER (frame10), table5);
	gtk_container_set_border_width (GTK_CONTAINER (table5), 5);
	
	pui->PrintCommand = gtk_entry_new ();
	gtk_widget_show (pui->PrintCommand);
	gtk_table_attach (GTK_TABLE (table5), pui->PrintCommand, 1, 2, 0, 1,
			  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	hbox5 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox5);
	gtk_table_attach (GTK_TABLE (table5), hbox5, 1, 2, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (GTK_FILL), 0, 0);
	
	spinbutton2_adj = gtk_adjustment_new (78, 50, 200, 1, 10, 10);
	pui->PrintLinesize = gtk_spin_button_new (GTK_ADJUSTMENT (spinbutton2_adj), 1, 0);
	gtk_widget_show (pui->PrintLinesize);
	gtk_box_pack_start (GTK_BOX (hbox5), pui->PrintLinesize, FALSE, FALSE, 0);
	gtk_widget_set_sensitive (pui->PrintLinesize, FALSE);
	
	label25 = gtk_label_new (_("characters"));
	gtk_widget_show (label25);
	gtk_box_pack_start (GTK_BOX (hbox5), label25, FALSE, TRUE, 0);
	
	label24 = gtk_label_new (_("Print command:"));
	gtk_widget_show (label24);
	gtk_table_attach (GTK_TABLE (table5), label24, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 0);
	
	pui->PrintBreakline = gtk_check_button_new_with_label (_("Break line at:"));
	gtk_widget_show (pui->PrintBreakline);
	gtk_table_attach (GTK_TABLE (table5), pui->PrintBreakline, 0, 1, 1, 2,
			  (GtkAttachOptions) (GTK_FILL),
			  (GtkAttachOptions) (0), 0, 5);
	gtk_container_set_border_width (GTK_CONTAINER (pui->PrintBreakline), 2);
	
	label19 = gtk_label_new (_("Printing"));
	gtk_widget_show (label19);

	return vbox1;

}

static GtkWidget *
create_encondig_page ( )
{

	/*
	 * done printing, starting encoding
	 */
	gint      i;
	GtkWidget *vbox5;
	GtkWidget *frame11;
	GtkWidget *hbox6;
	GtkWidget *frame12;
	GtkWidget *vbox6;
	GtkWidget *label20;
	GSList    *group = NULL;


	vbox5 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox5);
	
	frame11 = gtk_frame_new (_("Charset"));
	gtk_widget_show (frame11);
	gtk_box_pack_start (GTK_BOX (vbox5), frame11, FALSE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame11), 5);
	
	hbox6 = gtk_hbox_new (TRUE, 85);
	gtk_widget_show (hbox6);
	gtk_container_add (GTK_CONTAINER (frame11), hbox6);
	gtk_container_set_border_width (GTK_CONTAINER (hbox6), 10);
	
	pui->charset = gtk_entry_new ();
	gtk_widget_show (pui->charset);
	gtk_box_pack_start (GTK_BOX (hbox6), pui->charset, TRUE, TRUE, 0);

	frame12 = gtk_frame_new (_("Encoding"));
	gtk_widget_show (frame12);
	gtk_box_pack_start (GTK_BOX (vbox5), frame12, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame12), 5);
	
	vbox6 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox6);
	gtk_container_add (GTK_CONTAINER (frame12), vbox6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox6), 5);


	group = NULL;
	for (i = 0; i < NUM_ENCODING_MODES; i++)  {
		pui->encoding_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, _(encoding_type_label[i])));
		gtk_box_pack_start (GTK_BOX (vbox6), GTK_WIDGET (pui->encoding_type[i]), FALSE, TRUE, 0);
		group = gtk_radio_button_group (pui->encoding_type[i]);
	}

	label20 = gtk_label_new (_("Encoding"));
	gtk_widget_show (label20);

	return vbox5;
}

static GtkWidget*
create_misc_page ( )
{

	/*
	 * done encoding, starting misc
	 */
	GtkWidget *vbox9;
	GtkWidget *frame13;
	GtkWidget *vbox10;
	GtkWidget *frame14;
	GtkWidget *table6;
	GtkWidget *label27;
	GtkWidget *label;
        GtkWidget *color_frame;
        GtkWidget *unread_color_box;
        GtkWidget *unread_color_label;
        GtkWidget *ab_frame, *ab_box, *fileentry1;

	vbox9 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox9);

	frame13 = gtk_frame_new (_("Misc"));
	gtk_widget_show (frame13);
	gtk_box_pack_start (GTK_BOX (vbox9), frame13, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame13), 5);

	vbox10 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox10);
	gtk_container_add (GTK_CONTAINER (frame13), vbox10);
	gtk_container_set_border_width (GTK_CONTAINER (vbox10), 5);
	
	pui->debug = gtk_check_button_new_with_label (_("Debug"));
	gtk_widget_show (pui->debug);
	gtk_box_pack_start (GTK_BOX (vbox10), pui->debug, FALSE, FALSE, 0);
	
	pui->empty_trash = gtk_check_button_new_with_label ( _("Empty trash on exit"));
	gtk_widget_show(pui->empty_trash);
	gtk_box_pack_start (GTK_BOX (vbox10), pui->empty_trash, FALSE, FALSE, 0);
	
	/* address book */
        ab_box = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (vbox10), ab_box);
        gtk_widget_show (ab_box);
        label = gtk_label_new (_("Address book location"));
        gtk_widget_show (label);
        gtk_box_pack_start (GTK_BOX (ab_box), label, FALSE, FALSE, 5);

	fileentry1 = gnome_file_entry_new ("ADDRESS-BOOK-FILE", 
					   _("Select your address book file"));
	gtk_widget_show (fileentry1);
	gnome_file_entry_set_modal (GNOME_FILE_ENTRY (fileentry1), TRUE);
        gtk_box_pack_start (GTK_BOX (ab_box), fileentry1, 
                            TRUE, TRUE, 5);
	
	pui->ab_location = 
		gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (fileentry1));

	/* font */
	frame14 = gtk_frame_new (_("Font"));
	gtk_widget_show (frame14);
	gtk_box_pack_start (GTK_BOX (vbox9), frame14, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame14), 5);
	
	table6 = gtk_table_new (10, 3, FALSE);
	gtk_widget_show (table6);
	gtk_container_add (GTK_CONTAINER (frame14), table6);
	gtk_container_set_border_width (GTK_CONTAINER (table6), 5);
	
	pui->message_font = gtk_entry_new( );
	gtk_table_attach (GTK_TABLE (table6), pui->message_font, 0, 1, 1, 2,
			  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
			  (GtkAttachOptions) ( GTK_FILL ), 0, 0);
	
	gtk_widget_show (pui->message_font);


	pui->font_picker = gnome_font_picker_new ();
	gtk_widget_show ( pui->font_picker);
	gtk_table_attach (GTK_TABLE (table6), pui->font_picker, 1, 2, 1, 2,
			  (GtkAttachOptions) (0),
			  (GtkAttachOptions) (0), 0, 0);
	gtk_container_set_border_width (GTK_CONTAINER (pui->font_picker), 5);
	
	gnome_font_picker_set_font_name (GNOME_FONT_PICKER (pui->font_picker),
					 gtk_entry_get_text (GTK_ENTRY (pui->message_font)));
	gnome_font_picker_set_preview_text (GNOME_FONT_PICKER (pui->font_picker), 
					    _("Select a font to use"));
	gnome_font_picker_set_mode (GNOME_FONT_PICKER (pui->font_picker),
				    GNOME_FONT_PICKER_MODE_USER_WIDGET);
  
	label = gtk_label_new (_("Browse..."));
	gnome_font_picker_uw_set_widget (GNOME_FONT_PICKER (pui->font_picker), GTK_WIDGET (label));
	gtk_object_set_user_data (GTK_OBJECT(pui->font_picker), GTK_OBJECT(pui->message_font)); 
	gtk_object_set_user_data (GTK_OBJECT(pui->message_font), GTK_OBJECT(pui->font_picker)); 

	label27 = gtk_label_new (_("Preview pane"));
	gtk_widget_show (label27);
	gtk_table_attach (GTK_TABLE (table6), label27, 0, 1, 0, 1,
			  (GtkAttachOptions) (GTK_FILL ),
			  (GtkAttachOptions) (  GTK_JUSTIFY_RIGHT ), 0, 0);
	gtk_label_set_justify (GTK_LABEL (label27), GTK_JUSTIFY_RIGHT);

        /* mblist unread colour  */
        color_frame = gtk_frame_new (_("Colours"));
        gtk_widget_show (GTK_WIDGET (color_frame));
        gtk_container_set_border_width (GTK_CONTAINER (color_frame), 5);
	gtk_box_pack_start (GTK_BOX (vbox9), color_frame, FALSE, FALSE, 0);

        unread_color_box = gtk_hbox_new (FALSE, 0);
        gtk_widget_show (unread_color_box);
        gtk_container_add (GTK_CONTAINER (color_frame), unread_color_box);
        gtk_container_set_border_width (GTK_CONTAINER (unread_color_box), 0);

        pui->unread_color = gnome_color_picker_new ();
        gnome_color_picker_set_title (GNOME_COLOR_PICKER (pui->unread_color), _("Mailbox with unread messages colour"));
        gnome_color_picker_set_dither (GNOME_COLOR_PICKER (pui->unread_color) , TRUE);
        gtk_widget_show (pui->unread_color);
        gtk_box_pack_start (GTK_BOX (unread_color_box), pui->unread_color, 
                          FALSE, FALSE, 5);
        gtk_container_set_border_width (GTK_CONTAINER (pui->unread_color), 5);

        unread_color_label = gtk_label_new (_("Mailbox with unread messages colour"));
        gtk_widget_show (unread_color_label);
        gtk_box_pack_start (GTK_BOX (unread_color_box), unread_color_label, 
                            FALSE, FALSE, 5);
        gtk_label_set_justify (GTK_LABEL (unread_color_label), 
                               GTK_JUSTIFY_LEFT);

	/* address book */
        ab_frame = gtk_frame_new (_("Address Book"));
        gtk_widget_show (GTK_WIDGET (ab_frame));
        gtk_container_set_border_width (GTK_CONTAINER (ab_frame), 5);
	gtk_box_pack_start (GTK_BOX (vbox9), ab_frame, FALSE, FALSE, 0);


	return vbox9;
}

static GtkWidget *
create_startup_page ( )
{
	GtkWidget *vbox1;
	GtkWidget *frame;
	GtkWidget *vb1;

	vbox1 = gtk_vbox_new ( FALSE, 0);
	gtk_widget_show(vbox1);

	frame = gtk_frame_new (_("Options"));
	gtk_widget_show (frame);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);
	gtk_box_pack_start (GTK_BOX(vbox1), frame, FALSE, FALSE, 0);
	
	vb1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vb1);
	gtk_container_add (GTK_CONTAINER (frame), vb1);
	gtk_container_set_border_width (GTK_CONTAINER (vb1), 5);	

	pui->check_mail_upon_startup = gtk_check_button_new_with_label (
	    _("Check mail upon startup"));
	gtk_widget_show (pui->check_mail_upon_startup);
	gtk_box_pack_start (GTK_BOX (vb1), pui->check_mail_upon_startup, 
			    FALSE, FALSE, 0);
	pui->remember_open_mboxes = gtk_check_button_new_with_label (
	    _("Remember open mailboxes between sessions"));
	gtk_widget_show (pui->remember_open_mboxes);
	gtk_box_pack_start (GTK_BOX (vb1), pui->remember_open_mboxes, 
			    FALSE, FALSE, 0);
	
	return vbox1;

}

/*
 * callbacks
 */
static void
properties_modified_cb (GtkWidget * widget, GtkWidget * pbox)
{
        gnome_property_box_changed (GNOME_PROPERTY_BOX (pbox));
}


static void
font_changed (GtkWidget * widget, GtkWidget * pbox)
{
	gchar *font;
	GtkWidget *peer;
	if (GNOME_IS_FONT_PICKER (widget)){
		font = gnome_font_picker_get_font_name (GNOME_FONT_PICKER (widget));
		peer = gtk_object_get_user_data (GTK_OBJECT (widget));
		gtk_entry_set_text (GTK_ENTRY (peer), font);
	} 
	else {
		font = gtk_entry_get_text (GTK_ENTRY (widget));
		peer = gtk_object_get_user_data (GTK_OBJECT (widget));
		gnome_font_picker_set_font_name (GNOME_FONT_PICKER (peer), font);
		properties_modified_cb (widget, pbox);
	}
}

static void
pop3_edit_cb (GtkWidget * widget, gpointer data)
{
	GtkCList *clist = GTK_CLIST (pui->pop3servers);
	gint row;
  
	LibBalsaMailbox *mailbox = NULL;

	if (!clist->selection)
		return;

	row = GPOINTER_TO_INT (clist->selection->data);

	mailbox = gtk_clist_get_row_data (clist, row);
	if (!mailbox)
		return;

	mailbox_conf_new (mailbox, FALSE, MAILBOX_UNKNOWN);
}

static void
pop3_add_cb (GtkWidget * widget, gpointer data)
{
	mailbox_conf_new (NULL, FALSE, MAILBOX_POP3);
}

static void
pop3_del_cb (GtkWidget * widget, gpointer data)
{
	GtkCList *clist = GTK_CLIST (pui->pop3servers);
	gint row;

	LibBalsaMailbox *mailbox = NULL;

	if (!clist->selection)
		return;

	row = GPOINTER_TO_INT (clist->selection->data);

	mailbox = gtk_clist_get_row_data (clist, row);
	if (!mailbox)
		return;

	if (mailbox->type != MAILBOX_POP3)
		return;

	mailbox_conf_delete (mailbox);
}

void timer_modified_cb( GtkWidget *widget, GtkWidget *pbox)
{
	if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto)))
		gtk_widget_set_sensitive( GTK_WIDGET(pui->check_mail_minutes),
					  TRUE );
	else
		gtk_widget_set_sensitive( GTK_WIDGET(pui->check_mail_minutes),
					  FALSE );

	properties_modified_cb( widget, pbox );

}

static void 
wrap_modified_cb( GtkWidget *widget, GtkWidget *pbox)
{
	if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->wordwrap)))
		gtk_widget_set_sensitive( GTK_WIDGET(pui->wraplength),
					  TRUE );
	else
		gtk_widget_set_sensitive( GTK_WIDGET(pui->wraplength),
					  FALSE );

	properties_modified_cb( widget, pbox );
}

static void 
print_modified_cb( GtkWidget *widget, GtkWidget *pbox)
{
	if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->PrintBreakline)))
		gtk_widget_set_sensitive( GTK_WIDGET(pui->PrintLinesize),
					  TRUE );
	else
		gtk_widget_set_sensitive( GTK_WIDGET(pui->PrintLinesize),
					  FALSE );
	
	properties_modified_cb( widget, pbox );

}
