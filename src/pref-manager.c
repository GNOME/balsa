/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#define NUM_TOOLBAR_MODES 3
#define NUM_MDI_MODES 4

typedef struct _PropertyUI
  {
    GnomePropertyBox *pbox;
    GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];
    GtkWidget *real_name, *email, *replyto, *signature;

    GtkWidget *pop3servers, *smtp_server, *mail_directory;

    GtkWidget *previewpane;
    GtkWidget *debug;		/* enable/disable debugging */

    /* arp */
    GtkWidget *quote_str;
  }
PropertyUI;

/* main window, from main-window.c */
extern GnomeMDI *mdi;

static PropertyUI *pui;

guint toolbar_type[NUM_TOOLBAR_MODES] =
{
  GTK_TOOLBAR_TEXT,
  GTK_TOOLBAR_ICONS,
  GTK_TOOLBAR_BOTH
};

gchar *toolbar_type_label[NUM_TOOLBAR_MODES] =
{
  "Text",
  "Icons",
  "Both",
};

/* notebook pages */
static GtkWidget *create_identity_page (void);
static GtkWidget *create_mailservers_page (void);
static GtkWidget *create_display_page (void);
static GtkWidget *create_misc_page (void);


/* save the settings */
static void apply_prefs (GnomePropertyBox * pbox, gint page, PropertyUI * pui);

/* cancel the changes and don't save */
static void cancel_prefs (void);

/* set defaults */
static void set_prefs (void);

void update_pop3_servers (void);

/* callbacks */
static void properties_modified_cb (GtkWidget *, GnomePropertyBox *);

static void pop3_add_cb (GtkWidget * widget, gpointer data);
static void pop3_edit_cb (GtkWidget * widget, gpointer data);
static void pop3_del_cb (GtkWidget * widget, gpointer data);


/* and now the important stuff: */

void
open_preferences_manager (void)
{
  GtkWidget *label;
  gint i;
  GnomeApp *active_win;

  /* only one preferences manager window */
  if (pui)
    {
      gdk_window_raise (GTK_WIDGET (GNOME_DIALOG (pui->pbox))->window);
      return;
    }

  pui = g_malloc (sizeof (PropertyUI));

  pui->pbox = GNOME_PROPERTY_BOX (gnome_property_box_new ());

  active_win = GNOME_APP(gnome_mdi_get_active_window(mdi));
  gnome_dialog_set_parent(GNOME_DIALOG(pui->pbox), GTK_WINDOW(active_win));

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "destroy",
		      GTK_SIGNAL_FUNC (cancel_prefs), pui);

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_false), NULL);

  gtk_signal_connect (GTK_OBJECT (pui->pbox), "apply",
		      GTK_SIGNAL_FUNC (apply_prefs), pui);


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

  set_prefs ();
  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    {
      gtk_signal_connect (GTK_OBJECT (pui->toolbar_type[i]), "clicked",
			  properties_modified_cb, pui->pbox);
    }

  gtk_signal_connect (GTK_OBJECT (pui->previewpane), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->debug), "toggled",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);

  gtk_signal_connect (GTK_OBJECT (pui->real_name), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->email), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->replyto), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);

  gtk_signal_connect (GTK_OBJECT (pui->smtp_server), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->mail_directory), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->signature), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);


  /* arp */
  gtk_signal_connect (GTK_OBJECT (pui->quote_str), "changed",
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

  g_free (balsa_app.smtp_server);
  balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->smtp_server)));

  g_free (balsa_app.signature_path);
  balsa_app.signature_path = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->signature)));

  g_free (balsa_app.local_mail_directory);
  balsa_app.local_mail_directory = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->mail_directory)));

  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    if (GTK_TOGGLE_BUTTON (pui->toolbar_type[i])->active)
      {
	balsa_app.toolbar_style = toolbar_type[i];
	break;
      }
  balsa_app.debug = GTK_TOGGLE_BUTTON (pui->debug)->active;
  balsa_app.previewpane = GTK_TOGGLE_BUTTON (pui->previewpane)->active;

  /* arp */
  g_free (balsa_app.quote_str);
  balsa_app.quote_str =
    g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->quote_str)));


  refresh_main_window ();

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
  gint i;

  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    if (balsa_app.toolbar_style == toolbar_type[i])
      {
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->toolbar_type[i]), TRUE);
	break;
      }

  gtk_entry_set_text (GTK_ENTRY (pui->real_name), balsa_app.address->personal);

  gtk_entry_set_text (GTK_ENTRY (pui->email), balsa_app.address->mailbox);
  gtk_entry_set_text (GTK_ENTRY (pui->replyto), balsa_app.replyto);

  gtk_entry_set_text (GTK_ENTRY (pui->signature), balsa_app.signature_path);
  if (balsa_app.smtp_server)
    gtk_entry_set_text (GTK_ENTRY (pui->smtp_server), balsa_app.smtp_server);
  gtk_entry_set_text (GTK_ENTRY (pui->mail_directory), balsa_app.local_mail_directory);

  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->previewpane), balsa_app.previewpane);
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->debug), balsa_app.debug);

  /* arp */
  gtk_entry_set_text (GTK_ENTRY (pui->quote_str), balsa_app.quote_str);
}

void
update_pop3_servers (void)
{
  GtkCList *clist;
  GList *list = balsa_app.inbox_input;
  gchar *text[1];
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
	  text[0] = mailbox->name;
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
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *signature;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  table = gtk_table_new (4, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 5);

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

  /* signature */
  label = gtk_label_new (_ ("Signature:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL, 10, 10);

  signature = gnome_file_entry_new ("Signature", "Signature");
  gtk_table_attach (GTK_TABLE (table), signature, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  pui->signature = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (signature));

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
  GtkWidget *frame;
  GtkWidget *hbox;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *mail_dir;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  frame = gtk_frame_new ("POP3 Servers");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  sw = gtk_scrolled_window_new (NULL, NULL);
  pui->pop3servers = gtk_clist_new (1);
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

  button = gtk_button_new_with_label ("Add");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_add_cb), NULL);

  button = gtk_button_new_with_label ("Modify");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_edit_cb), NULL);

  button = gtk_button_new_with_label ("Delete");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (pop3_del_cb), NULL);

  frame = gtk_frame_new ("Local Mail");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  label = gtk_label_new (_ ("Local mail directory:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 2);
  mail_dir = gnome_file_entry_new ("LocalMailDir", "LocalMailDir");
  pui->mail_directory = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (mail_dir));
  gtk_box_pack_start (GTK_BOX (hbox), mail_dir, TRUE, TRUE, 2);

  /* smtp server */
  frame = gtk_frame_new ("Sending Mail");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  label = gtk_label_new (_ ("SMTP Server:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);
  pui->smtp_server = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), pui->smtp_server, TRUE, TRUE, 2);

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
						    toolbar_type_label[i]));
      gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->toolbar_type[i]), TRUE, TRUE,
			  2);
      group = gtk_radio_button_group (pui->toolbar_type[i]);
    }

/* Main window */
  frame = gtk_frame_new ("Main window");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  pui->previewpane = gtk_check_button_new_with_label (_ ("Use preview pane"));
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (pui->previewpane));

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


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  /* Misc */
  frame = gtk_frame_new ("Misc");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  /* arp */
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  pui->debug = gtk_check_button_new_with_label (_ ("Debug"));
  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->debug), TRUE, TRUE, 2);


  /* arp --- table containing leadin label and string. */
  table = gtk_table_new (1, 2, FALSE);

  label = gtk_label_new (_ ("Reply prefix:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  pui->quote_str = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->quote_str, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

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
