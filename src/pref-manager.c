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

typedef struct _PropertyUI
  {
    GnomePropertyBox *pbox;
    GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];
    GtkWidget *real_name, *email, *replyto, *signature;
    GtkWidget *sig_whenforward, *sig_whenreply, *sig_sending;

    GtkWidget *pop3servers, *smtp_server, *mail_directory;
    GtkWidget *rb_local_mua, *rb_smtp_server;
    GtkWidget *check_mail_auto;
    GtkWidget *check_mail_minutes;

    GtkWidget *previewpane;
    GtkWidget *debug;		/* enable/disable debugging */
    GtkRadioButton *pwindow_type[NUM_PWINDOW_MODES];

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

      /* printing */
    GtkWidget *PrintCommand;
    GtkWidget *PrintBreakline;
    GtkWidget *PrintLinesize;

  }
PropertyUI;

/* main window, from main-window.c */
//extern GnomeMDI *mdi;

static PropertyUI *pui;

static void smtp_changed (void);

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


/* notebook pages */
static GtkWidget *create_identity_page (void);
static GtkWidget *create_mailservers_page (void);
static GtkWidget *create_display_page (void);
static GtkWidget *create_misc_page (void);
static GtkWidget *create_encoding_page (void);
static GtkWidget *create_printing_page (void);

/* save the settings */
static void apply_prefs (GnomePropertyBox * pbox, gint page, PropertyUI * pui);

/* cancel the changes and don't save */
static void cancel_prefs (void);

/* set defaults */
static void set_prefs (void);

void update_pop3_servers (void);

/* callbacks */
static void properties_modified_cb (GtkWidget *, GnomePropertyBox *);
static void font_changed (GtkWidget * widget, GnomePropertyBox * pbox);
void timer_modified_cb (GtkWidget * widget,  GnomePropertyBox * pbox);

static void pop3_add_cb (GtkWidget * widget, gpointer data);
static void pop3_edit_cb (GtkWidget * widget, gpointer data);
static void pop3_del_cb (GtkWidget * widget, gpointer data);


/* and now the important stuff: */

void
open_preferences_manager(GtkWidget *widget, gpointer data)
{
  static GnomeHelpMenuEntry help_entry = { NULL, "properties" };
  GtkWidget *label;
  gint i;
  GnomeApp *active_win = GNOME_APP(data);

  /* only one preferences manager window */
  if (pui)
    {
      gdk_window_raise (GTK_WIDGET (GNOME_DIALOG (pui->pbox))->window);
      return;
    }

  pui = g_malloc (sizeof (PropertyUI));

  pui->pbox = GNOME_PROPERTY_BOX (gnome_property_box_new ());
  gtk_window_set_title(GTK_WINDOW(pui->pbox), _("Balsa Preferences"));
  //  active_win = GNOME_APP(gnome_mdi_get_active_window(mdi));
  gnome_dialog_set_parent(GNOME_DIALOG(pui->pbox), GTK_WINDOW(active_win));

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "destroy",
		      GTK_SIGNAL_FUNC (cancel_prefs), pui);

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_false), NULL);

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "apply",
		      GTK_SIGNAL_FUNC (apply_prefs), pui);

  help_entry.name = gnome_app_id;
  gtk_signal_connect (GTK_OBJECT (pui->pbox), "help",
		      GTK_SIGNAL_FUNC (gnome_help_pbox_display),
		      &help_entry);


  /* identity page */
  label = gtk_label_new (_ ("Identity"));
  gtk_notebook_append_page (
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
			     create_identity_page (),
			     label);

  /* mailboxes page */
  label = gtk_label_new (_ ("Mail Servers"));
  gtk_notebook_append_page (
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
			     create_mailservers_page (),
			     label);

  /* display page */
  label = gtk_label_new (_ ("Display"));
  gtk_notebook_append_page (
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
			     create_display_page (),
			     label);

  /* Misc page */
  label = gtk_label_new (_ ("Misc"));
  gtk_notebook_append_page (
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
			     create_misc_page (),
			     label);

  /* Encoding page */
  label = gtk_label_new (_ ("Encoding"));
  gtk_notebook_append_page (
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
			     create_encoding_page (),
			     label);
  /* Printing page */
  label = gtk_label_new (_ ("Printing"));
  gtk_notebook_append_page(
		    GTK_NOTEBOOK (GNOME_PROPERTY_BOX (pui->pbox)->notebook),
		             create_printing_page (),
		             label);
  set_prefs ();
  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    {
      gtk_signal_connect (GTK_OBJECT (pui->toolbar_type[i]), "clicked",
			  properties_modified_cb, pui->pbox);
    }

  for (i = 0; i < NUM_PWINDOW_MODES; i++)
    {
      gtk_signal_connect (GTK_OBJECT (pui->pwindow_type[i]), "clicked",
			  properties_modified_cb, pui->pbox);
    }

  gtk_signal_connect (GTK_OBJECT (pui->previewpane), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->debug), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
#ifdef BALSA_SHOW_INFO
  gtk_signal_connect (GTK_OBJECT (pui->mblist_show_mb_content_info), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
#endif

  gtk_signal_connect (GTK_OBJECT (pui->real_name), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->email), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->replyto), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);

  gtk_signal_connect (GTK_OBJECT (pui->sig_sending), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->sig_whenforward), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->sig_whenreply), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);

  gtk_signal_connect (GTK_OBJECT (pui->rb_smtp_server), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->rb_smtp_server), "toggled",
		      GTK_SIGNAL_FUNC (smtp_changed), NULL);
  gtk_signal_connect (GTK_OBJECT (pui->smtp_server), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->mail_directory), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->signature), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->check_mail_auto), "toggled",
		      GTK_SIGNAL_FUNC (timer_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->check_mail_minutes), "changed",
		      GTK_SIGNAL_FUNC (timer_modified_cb), pui->pbox);

  /* arp */
  gtk_signal_connect (GTK_OBJECT (pui->quote_str), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb),
		      pui->pbox);

  /* message font */
  gtk_signal_connect (GTK_OBJECT (pui->message_font), "changed",
		      GTK_SIGNAL_FUNC (font_changed),
		      pui->pbox);

  gtk_signal_connect (GTK_OBJECT (pui->font_picker), "font_set",
  		      GTK_SIGNAL_FUNC (font_changed),
		      pui->pbox);
 
  /* charset */
  gtk_signal_connect (GTK_OBJECT (pui->charset), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb),
		      pui->pbox);

 for (i = 0; i < NUM_ENCODING_MODES; i++)
    {
      gtk_signal_connect (GTK_OBJECT (pui->encoding_type[i]), "clicked",
			  properties_modified_cb, pui->pbox);
    }

  /* printing */
  gtk_signal_connect (GTK_OBJECT (pui->PrintCommand), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb),
		      pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->PrintBreakline), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb),
		      pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->PrintLinesize), "changed",
  		      GTK_SIGNAL_FUNC (properties_modified_cb),
  		      pui->pbox);

  /* set data and show the whole thing */
  gtk_widget_show_all (GTK_WIDGET (pui->pbox));
}


static void
cancel_prefs (void)
{
  gtk_widget_destroy (GTK_WIDGET (pui->pbox));
  g_free (pui);
  pui = NULL;
}

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
apply_prefs (GnomePropertyBox * pbox, gint page, PropertyUI * pui)
{
  gint i;
  
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

  /* 
   * display page 
   */
  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    if (GTK_TOGGLE_BUTTON (pui->toolbar_type[i])->active)
      {
	balsa_app.toolbar_style = toolbar_type[i];
	break;
      }
  for (i=0; i < NUM_PWINDOW_MODES; i++)
    if(GTK_TOGGLE_BUTTON (pui->pwindow_type[i])->active)
      {
	balsa_app.pwindow_option = pwindow_type[i];
	break;
      }

  balsa_app.debug = GTK_TOGGLE_BUTTON (pui->debug)->active;
  balsa_app.previewpane = GTK_TOGGLE_BUTTON (pui->previewpane)->active;
  balsa_app.smtp = GTK_TOGGLE_BUTTON (pui->rb_smtp_server)->active;
#ifdef BALSA_SHOW_INFO
  if (balsa_app.mblist_show_mb_content_info != GTK_TOGGLE_BUTTON (pui->mblist_show_mb_content_info)->active)
   {
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

  /* arp */
  g_free (balsa_app.quote_str);
  balsa_app.quote_str =
    g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->quote_str)));

  g_free (balsa_app.message_font);
  balsa_app.message_font =
    g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->message_font)));


  /* charset*/
  g_free (balsa_app.charset);
  balsa_app.charset =
      g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->charset)));
  mutt_set_charset (balsa_app.charset);

  for (i = 0; i < NUM_ENCODING_MODES; i++)
    if (GTK_TOGGLE_BUTTON (pui->encoding_type[i])->active)
      {
	balsa_app.encoding_style = encoding_type[i];
	break;
      }

  /* printing */
  g_free (balsa_app.PrintCommand.PrintCommand);
  balsa_app.PrintCommand.PrintCommand =
      g_strdup( gtk_entry_get_text(GTK_ENTRY (pui->PrintCommand)));

  balsa_app.PrintCommand.linesize = atoi(gtk_entry_get_text(GTK_ENTRY( pui->PrintLinesize)));
  balsa_app.PrintCommand.breakline =  GTK_TOGGLE_BUTTON(pui->PrintBreakline)->active;

  // XXX
  //  refresh_main_window ();

  /*
   * close window and free memory
   */
  config_global_save ();
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
    if (balsa_app.toolbar_style == toolbar_type[i])
      {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->toolbar_type[i]), TRUE);
	break;
      }

  for (i = 0; i < NUM_PWINDOW_MODES; i++)
    if( balsa_app.pwindow_option == pwindow_type[i])
      {
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

  /* arp */
  gtk_entry_set_text (GTK_ENTRY (pui->quote_str), balsa_app.quote_str);

  /* message font */
  gtk_entry_set_text (GTK_ENTRY (pui->message_font), balsa_app.message_font);
  gtk_entry_set_position (GTK_ENTRY (pui->message_font), 0);

  /* charset */
  gtk_entry_set_text (GTK_ENTRY (pui->charset), balsa_app.charset);
  for (i = 0; i < NUM_ENCODING_MODES; i++)
    if (balsa_app.encoding_style == encoding_type[i])
      {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->encoding_type[i]), TRUE);
	break;
      }

  /*printing */
  gtk_entry_set_text(GTK_ENTRY(pui->PrintCommand), balsa_app.PrintCommand.PrintCommand);
  sprintf(tmp, "%d", balsa_app.PrintCommand.linesize);
  gtk_entry_set_text(GTK_ENTRY(pui->PrintLinesize), tmp);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->PrintBreakline), balsa_app.PrintCommand.breakline);

}

void
update_pop3_servers (void)
{
  GtkCList *clist;
  GList *list = balsa_app.inbox_input;
  gchar *text[2];
  gint row;

  Mailbox *mailbox;

  if (!pui)
    return;

  clist = GTK_CLIST (pui->pop3servers);

  gtk_clist_clear (clist);

  gtk_clist_freeze (clist);
  while (list)
    {
      mailbox = list->data;
      if (mailbox)
	{
	  switch (mailbox->type) {
	    case MAILBOX_POP3:	text[0] = "POP3"; break;
	    case MAILBOX_IMAP:	text[0] = "IMAP"; break;
	    default:		text[0] = "????"; break;
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


/*
 * identity notebook page
 */
static GtkWidget *
create_identity_page (void)
{

  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *signature;

  GtkWidget *frame1;
  GtkWidget *vbox1;
  GtkWidget *table1;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER (vbox), 10);

  frame = gtk_frame_new (_("Identity"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  table = gtk_table_new (4, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 5);
  gtk_container_add (GTK_CONTAINER (frame), table);

  /* your name */
  label = gtk_label_new (_ ("Your name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->real_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->real_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  /* email address */
  label = gtk_label_new (_ ("E-Mail address:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->email = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->email, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  /* reply-to address */
  label = gtk_label_new (_ ("Reply-to address:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->replyto = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->replyto, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);


  /* Signature stuff */
  frame1 = gtk_frame_new (_("Signature"));
  gtk_box_pack_start (GTK_BOX (vbox), frame1, FALSE, FALSE, 5);

  table1 = gtk_table_new (8, 6, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table1), 5);
  gtk_container_add (GTK_CONTAINER (frame1), table1);


  label = gtk_label_new (_ ("Signature file:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  signature = gnome_file_entry_new ("Signature", "Signature");
  gtk_table_attach (GTK_TABLE (table1), signature, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  pui->signature = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (signature));

  pui->sig_sending = gtk_check_button_new_with_label ( _("Include signature when sending mail"));
  gtk_table_attach (GTK_TABLE (table1), pui->sig_sending, 1, 2, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);

  pui->sig_whenforward = gtk_check_button_new_with_label ( _("Include signature when forwarding mail"));
  gtk_table_attach (GTK_TABLE (table1), pui->sig_whenforward, 2, 3, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);

  pui->sig_whenreply = gtk_check_button_new_with_label ( _("Include signature when replying to mail"));
  gtk_table_attach (GTK_TABLE (table1), pui->sig_whenreply, 1, 2, 3, 4,
		    GTK_FILL, GTK_FILL, 0, 0);

  return vbox;

}

/*
 * mailboxes notebook page
 */
static GtkWidget *
create_mailservers_page ()
{
  GtkWidget *sw;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *label_minutes;
  GtkWidget *frame;
  GtkWidget *hbox;
  GtkWidget *hbox2;
  GtkWidget *table1;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *mail_dir;
  GtkObject *adj;
  GSList *rbgroup;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  frame = gtk_frame_new (_("Remote Mailbox Servers"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  sw = gtk_scrolled_window_new (NULL, NULL);
  pui->pop3servers = gtk_clist_new (2);
  gtk_clist_set_column_title(GTK_CLIST(pui->pop3servers), 0, _("Type"));
  gtk_clist_set_column_title(GTK_CLIST(pui->pop3servers), 1, _("Mailbox name"));
  gtk_clist_column_titles_show(GTK_CLIST(pui->pop3servers));
  gtk_clist_set_column_width(GTK_CLIST(pui->pop3servers), 0, 45);
  gtk_clist_column_titles_passive(GTK_CLIST(pui->pop3servers));
  gtk_container_add (GTK_CONTAINER (sw), pui->pop3servers);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (hbox), sw, TRUE, TRUE, 2);

  update_pop3_servers ();

  bbox = gtk_vbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, TRUE, 2);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), 25, 15);

  button = gtk_button_new_with_label (_("Add"));
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_add_cb), NULL);

  button = gtk_button_new_with_label (_("Modify"));
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_edit_cb), NULL);

  button = gtk_button_new_with_label (_("Delete"));
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_del_cb), NULL);

  frame = gtk_frame_new (_("Local Mail"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  label = gtk_label_new (_ ("Local mail directory:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 2);
  mail_dir = gnome_file_entry_new ("LocalMailDir", "LocalMailDir");
  pui->mail_directory = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (mail_dir));
  gtk_box_pack_start (GTK_BOX (hbox), mail_dir, TRUE, TRUE, 2);

  /* smtp server */
  frame = gtk_frame_new (_("Outgoing Mail"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  table1 = gtk_table_new (2, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table1), 5);
  gtk_container_add (GTK_CONTAINER (frame), table1);
  gtk_widget_show (table1);

  pui->rb_smtp_server = gtk_radio_button_new_with_label (NULL,
  	_("Remote SMTP Server"));
  rbgroup = gtk_radio_button_group (GTK_RADIO_BUTTON (pui->rb_smtp_server));
  gtk_widget_show (pui->rb_smtp_server);
  gtk_table_attach (GTK_TABLE (table1), pui->rb_smtp_server, 0, 1, 0, 1,
                    (GtkAttachOptions) GTK_EXPAND | GTK_FILL,
		    (GtkAttachOptions) GTK_EXPAND | GTK_FILL, 0, 0);
  /* This line is to be deleted whenever smtp works */
  //gtk_widget_set_sensitive (pui->rb_smtp_server, FALSE); 
  
  pui->rb_local_mua = gtk_radio_button_new_with_label (rbgroup,
  	_("Local mail user agent"));
  gtk_widget_show (pui->rb_local_mua);
  gtk_table_attach (GTK_TABLE (table1), pui->rb_local_mua, 0, 1, 1, 2,
                    (GtkAttachOptions) GTK_EXPAND | GTK_FILL,
		    (GtkAttachOptions) GTK_EXPAND | GTK_FILL, 0, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pui->rb_local_mua), TRUE);

  pui->smtp_server = gtk_entry_new ();
  gtk_widget_show (pui->smtp_server);
  gtk_table_attach (GTK_TABLE (table1), pui->smtp_server, 1, 2, 0, 1,
                    (GtkAttachOptions) GTK_EXPAND | GTK_FILL,
		    (GtkAttachOptions) GTK_EXPAND | GTK_FILL, 0, 0);
  gtk_widget_set_sensitive (pui->smtp_server, balsa_app.smtp);

  adj = gtk_adjustment_new( 1.0, 1.0, 99.0, 1.0, 5.0, 0.0);
  pui->check_mail_auto = gtk_check_button_new_with_label( "Check mail automatically every:" );
  pui->check_mail_minutes = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
  gtk_box_pack_start( GTK_BOX(vbox), pui->check_mail_auto, FALSE, FALSE, 5);

  hbox2 = gtk_hbox_new( FALSE, 0 );
  label_minutes = gtk_label_new( "Minutes" );
  gtk_box_pack_start( GTK_BOX(hbox2), pui->check_mail_minutes, 
		      FALSE, FALSE, 5);
  gtk_box_pack_start( GTK_BOX(hbox2), label_minutes, FALSE, FALSE, 0);
  gtk_box_pack_start( GTK_BOX(vbox), hbox2, FALSE, FALSE, 5);

  return vbox;
}


/*
 * display notebook page
 */
static GtkWidget *
create_display_page ()
{
  GtkWidget *vbox, *vbox1;
  GtkWidget *frame;
  GSList *group;
  gint i;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  /* Toolbars */
  frame = gtk_frame_new (_ ("Toolbars"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  group = NULL;
  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    {
      pui->toolbar_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group,
						    _(toolbar_type_label[i])));
      gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->toolbar_type[i]), TRUE, TRUE,
			  2);
      group = gtk_radio_button_group (pui->toolbar_type[i]);
    }

/* Main window */
  frame = gtk_frame_new (_("Main window"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  pui->previewpane = gtk_check_button_new_with_label ( _("Use preview pane"));
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (pui->previewpane));

/* Progress Dialog */
  frame = gtk_frame_new (_ ("Display Progress Dialog"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  group = NULL;
  for (i = 0; i < NUM_PWINDOW_MODES; i++)
    {
      pui->pwindow_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group, _(pwindow_type_label[i])));
      gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->pwindow_type[i]), TRUE, TRUE, 2);
      group = gtk_radio_button_group (pui->pwindow_type[i]);
    }


#ifdef BALSA_SHOW_INFO
  /* mailbox list window */
  frame = gtk_frame_new ("Mailbox list window");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  pui->mblist_show_mb_content_info = gtk_check_button_new_with_label ( _("View mailbox content informations "));
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (pui->mblist_show_mb_content_info));
#endif
  return vbox;
}

static GtkWidget *
create_misc_page ()
{
  GtkWidget *vbox;
  GtkWidget *frame;

  /* arp */
  GtkWidget *vbox1;
  GtkWidget *table;
  GtkWidget *label;

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  /* Misc */
  frame = gtk_frame_new (_("Misc"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  /* arp */
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox1), 5);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  pui->debug = gtk_check_button_new_with_label (_("Debug"));
  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->debug), TRUE, TRUE, 2);

  /* arp --- table containing leadin label and string. */
  table = gtk_table_new (1, 2, FALSE);

  label = gtk_label_new (_("Reply prefix:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->quote_str = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->quote_str, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);

  /* font picker */
  table = gtk_table_new (1, 3, FALSE);

  label = gtk_label_new (_("Font:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->message_font = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->message_font, 1, 2, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 10);
  
  pui->font_picker = gnome_font_picker_new ();
  gnome_font_picker_set_font_name (GNOME_FONT_PICKER (pui->font_picker),
				   gtk_entry_get_text (GTK_ENTRY (pui->message_font)));
  gnome_font_picker_set_mode (GNOME_FONT_PICKER (pui->font_picker),
			      GNOME_FONT_PICKER_MODE_USER_WIDGET);
  
  label = gtk_label_new (_("Browse..."));
  gnome_font_picker_uw_set_widget (GNOME_FONT_PICKER (pui->font_picker), GTK_WIDGET (label));
  gtk_object_set_user_data (GTK_OBJECT(pui->font_picker), GTK_OBJECT(pui->message_font)); 
  gtk_object_set_user_data (GTK_OBJECT(pui->message_font), GTK_OBJECT(pui->font_picker)); 

  gtk_table_attach (GTK_TABLE (table), pui->font_picker,
		    2, 3, 0, 1, GTK_FILL, GTK_FILL, 5, 10);

  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);


  return vbox;
}

/*
 * encoding notepad
 */
static GtkWidget *
create_encoding_page ()
{
   
  GtkWidget *vbox;
  GtkWidget *frame;

  /* arp */
  GtkWidget *vbox1;
  GtkWidget *table;
  GtkWidget *label;
  GSList    *group;
  gint      i;

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  // Misc 
  frame = gtk_frame_new (_("Encoding"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  // arp 
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox1), 5);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  
  table = gtk_table_new (1, 2, FALSE);

  label = gtk_label_new (_("Charset:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

   pui->charset = gtk_entry_new ();
   gtk_table_attach (GTK_TABLE (table), pui->charset, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);

  group = NULL;
  for (i = 0; i < NUM_ENCODING_MODES; i++)
    {
      pui->encoding_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group,
						    _(encoding_type_label[i])));
      gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->encoding_type[i]), TRUE, TRUE,
			  2);
      group = gtk_radio_button_group (pui->encoding_type[i]);
    }


  return vbox;

}

/*
 * printing notepad
 */
static GtkWidget *
create_printing_page ()
{
   
  GtkWidget *vbox;
  GtkWidget *frame;

  /* arp */
  GtkWidget *vbox1;
  GtkWidget *table;
  GtkWidget *label;

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  frame = gtk_frame_new (_("Printing"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox1), 5);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  
  table = gtk_table_new (1, 2, FALSE);

  label = gtk_label_new (_("Print command:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

   pui->PrintCommand = gtk_entry_new ();
   gtk_table_attach (GTK_TABLE (table), pui->PrintCommand, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

   //gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);

  label = gtk_label_new (_ ("Linesize:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->PrintLinesize = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->PrintLinesize, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);


   gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);

   pui->PrintBreakline = gtk_check_button_new_with_label(_("Break line"));
   gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->PrintBreakline), TRUE, TRUE, 2);
 
   gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (table), TRUE, TRUE, 2);

  return vbox;

}

/*
 * callbacks
 */
static void
properties_modified_cb (GtkWidget * widget, GnomePropertyBox * pbox)
{
  gnome_property_box_changed (pbox);
}

static void
font_changed (GtkWidget * widget, GnomePropertyBox * pbox)
{
  gchar *font;
  GtkWidget *peer;
  if (GNOME_IS_FONT_PICKER (widget)){
    font = gnome_font_picker_get_font_name (GNOME_FONT_PICKER (widget));
    peer = gtk_object_get_user_data (GTK_OBJECT (widget));
    gtk_entry_set_text (GTK_ENTRY (peer), font);
  } else {
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

  Mailbox *mailbox = NULL;

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

  Mailbox *mailbox = NULL;

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

void timer_modified_cb( GtkWidget *widget, GnomePropertyBox *pbox)
{
  if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pui->check_mail_auto)))
      gtk_widget_set_sensitive( GTK_WIDGET(pui->check_mail_minutes),
				 TRUE );
  else
      gtk_widget_set_sensitive( GTK_WIDGET(pui->check_mail_minutes),
				 FALSE );

  properties_modified_cb( widget, pbox );

}

