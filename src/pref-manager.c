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
#include "pref-manager.h"
#include "balsa-app.h"
#include "save-restore.h"
#include "main-window.h"

#define NUM_TOOLBAR_MODES 3
#define NUM_MDI_MODES 4

typedef struct _PropertyUI
  {
    GnomePropertyBox *pbox;
    GtkRadioButton *toolbar_type[NUM_TOOLBAR_MODES];
    GtkRadioButton *mdi_type[NUM_MDI_MODES];

    GtkWidget *real_name, *email, *replyto, *signature;

    GtkWidget *smtp_server, *mail_directory;

    GtkWidget *previewpane;
    GtkWidget *debug;		/* enable/disable debugging */
  }
PropertyUI;

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

guint mdi_type[NUM_MDI_MODES] =
{
  GNOME_MDI_DEFAULT_MODE,
  GNOME_MDI_NOTEBOOK,
  GNOME_MDI_TOPLEVEL,
  GNOME_MDI_MODAL
};

gchar *mdi_type_label[NUM_MDI_MODES] =
{
  "Default",
  "Notebook",
  "Toplevel",
  "Modal",
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

static void properties_modified_cb (GtkWidget *, GnomePropertyBox *);


void
open_preferences_manager (void)
{
  GtkWidget *label;
  gint i;

  /* only one preferences manager window */
  if (pui)
    {
      gdk_window_raise (GTK_WIDGET (GNOME_DIALOG (pui->pbox))->window);
      return;
    }

  pui = g_malloc (sizeof (PropertyUI));

  pui->pbox = GNOME_PROPERTY_BOX (gnome_property_box_new ());

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

  for (i = 0; i < NUM_MDI_MODES; i++)
    {
      gtk_signal_connect (GTK_OBJECT (pui->mdi_type[i]), "clicked",
			  properties_modified_cb, pui->pbox);
    }
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
  gtk_signal_connect (GTK_OBJECT (pui->smtp_server), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
  gtk_signal_connect (GTK_OBJECT (pui->mail_directory), "changed",
		      GTK_SIGNAL_FUNC (properties_modified_cb), pui->pbox);
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
  gchar *email, *c;
  gint i;

  /*
   * identity page
   */
  g_free (balsa_app.real_name);
  balsa_app.real_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->real_name)));

  /* parse username/hostname from the email entry */
  email = c = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->email)));

  while (*c != '\0' && *c != '@')
    c++;

  if (*c == '\0')
    {
      g_free (balsa_app.username);
      balsa_app.username = g_strdup (email);
    }
  else
    {
      *c = '\0';
      c++;

      g_free (balsa_app.username);
      balsa_app.username = g_strdup (email);

      g_free (balsa_app.hostname);
      balsa_app.hostname = g_strdup (c);
    }
  g_free (email);

  g_free (balsa_app.smtp_server);
  balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->smtp_server)));

  g_free (balsa_app.local_mail_directory);
  balsa_app.local_mail_directory = g_strdup (gtk_entry_get_text (GTK_ENTRY (pui->mail_directory)));


  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    if (GTK_TOGGLE_BUTTON (pui->toolbar_type[i])->active)
      {
	balsa_app.toolbar_style = toolbar_type[i];
	break;
      }

  for (i = 0; i < NUM_MDI_MODES; i++)
    if (GTK_TOGGLE_BUTTON (pui->mdi_type[i])->active)
      {
	balsa_app.mdi_style = mdi_type[i];
	break;
      }

  balsa_app.debug = GTK_TOGGLE_BUTTON (pui->debug)->active;
  balsa_app.previewpane = GTK_TOGGLE_BUTTON (pui->previewpane)->active;

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
  GString *str;

  for (i = 0; i < NUM_TOOLBAR_MODES; i++)
    if (balsa_app.toolbar_style == toolbar_type[i])
      {
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->toolbar_type[i]), TRUE);
	break;
      }

  for (i = 0; i < NUM_MDI_MODES; i++)
    if (balsa_app.mdi_style == mdi_type[i])
      {
	gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->mdi_type[i]), TRUE);
	break;
      }

  gtk_entry_set_text (GTK_ENTRY (pui->real_name), balsa_app.real_name);

  str = g_string_new (balsa_app.username);
  g_string_append_c (str, '@');
  g_string_append (str, balsa_app.hostname);
  gtk_entry_set_text (GTK_ENTRY (pui->email), str->str);
  g_string_free (str, TRUE);

  gtk_entry_set_text (GTK_ENTRY (pui->smtp_server), balsa_app.smtp_server);

  gtk_entry_set_text (GTK_ENTRY (pui->mail_directory), balsa_app.local_mail_directory);

  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->previewpane), balsa_app.previewpane);
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (pui->debug), balsa_app.debug);
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
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);

  table = gtk_table_new (4, 3, FALSE);
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

  pui->signature = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pui->signature, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  button = gtk_button_new_with_label ("Browse");
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  return vbox;
}

/*
 * mailboxes notebook page
 */
static GtkWidget *
create_mailservers_page ()
{
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *frame;
  GtkWidget *hbox;
  GtkWidget *bbox;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);

  frame = gtk_frame_new ("POP3 Servers");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  label = gtk_label_new ("clist goes here");
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);

  bbox = gtk_vbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 2);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), 25, 15);

  button = gtk_button_new_with_label ("Add");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  button = gtk_button_new_with_label ("Modify");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  button = gtk_button_new_with_label ("Delete");
  gtk_container_add (GTK_CONTAINER (bbox), button);

  frame = gtk_frame_new ("Local Mail");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);
  hbox = gtk_hbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  label = gtk_label_new (_ ("Local mail directory:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);
  pui->mail_directory = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), pui->mail_directory, TRUE, TRUE, 2);

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
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);

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

/* MDI */
  frame = gtk_frame_new (_ ("MDI"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (vbox1));

  group = NULL;
  for (i = 0; i < NUM_MDI_MODES; i++)
    {
      pui->mdi_type[i] = GTK_RADIO_BUTTON (gtk_radio_button_new_with_label (group,
							mdi_type_label[i]));
      gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (pui->mdi_type[i]), TRUE, TRUE,
			  2);
      group = gtk_radio_button_group (pui->mdi_type[i]);
    }

  return vbox;
}

static GtkWidget *
create_misc_page ()
{
  GtkWidget *vbox;
  GtkWidget *frame;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);

/* Misc */
  frame = gtk_frame_new ("Misc");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 5);

  pui->debug = gtk_check_button_new_with_label (_ ("Debug"));
  gtk_container_add (GTK_CONTAINER (frame), GTK_WIDGET (pui->debug));

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
