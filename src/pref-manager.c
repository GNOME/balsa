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
#include <gnome.h>
#include "pref-manager.h"
#include "balsa-app.h"



typedef struct _PreferencesManagerWindow PreferencesManagerWindow;
struct _PreferencesManagerWindow
{
  GtkWidget *window;
  
  /* identity */
  GtkWidget *real_name;

  GtkWidget *username;
  GtkWidget *hostname;

  GtkWidget *organization;
  
  /* local */
  GtkWidget *mail_directory;
  
  /* servers */
  GtkWidget *smtp_server;
  
};

static PreferencesManagerWindow *pmw = NULL;



/* notebook pages */
static GtkWidget * create_identity_page ();


/* callbacks */
static void ok_preferences_manager ();
static void cancel_preferences_manager ();




void
open_preferences_manager ()
{
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *notebook;


  /* only one preferences manager window */
  if (pmw)
    return;

  pmw = g_malloc (sizeof (PreferencesManagerWindow));



  pmw->window = gtk_dialog_new ();
  gtk_widget_set_usize (pmw->window,
			PREFERENCES_MANAGER_WIDTH,
			PREFERENCES_MANAGER_HEIGHT);
  gtk_window_set_title (GTK_WINDOW (pmw->window), "Preferences");
  gtk_window_set_wmclass (GTK_WINDOW (pmw->window), 
			  "preferences_manager",
			  "Balsa");
  gtk_window_position (GTK_WINDOW (pmw->window), GTK_WIN_POS_CENTER);

  gtk_container_border_width (GTK_CONTAINER (pmw->window), 0);

  gtk_signal_connect (GTK_OBJECT (pmw->window),
		      "destroy",
		      (GtkSignalFunc) cancel_preferences_manager,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (pmw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);


  /* get the vbox from the dialog window */
  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (pmw->window)->vbox), hbox, TRUE, TRUE, 5);
  gtk_widget_show (hbox);


  /* notbook */
  notebook = gtk_notebook_new ();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_box_pack_start (GTK_BOX (hbox), notebook, TRUE, TRUE, 5);
  gtk_widget_show (notebook);


  /* identity page */
  label = gtk_label_new ("Identity");
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    create_identity_page (),
			    label);


  /* ok/cancel buttons (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (pmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  button = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect_object (GTK_OBJECT (button),
			     "clicked",
			     (GtkSignalFunc) ok_preferences_manager,
			     GTK_OBJECT (pmw->window));

  gtk_widget_show (button);


  button = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect_object (GTK_OBJECT (button),
			     "clicked",
			     (GtkSignalFunc) gtk_widget_destroy,
			     GTK_OBJECT (pmw->window));

  gtk_widget_show (button);


  /* set data and show the whole thing */
  refresh_preferences_manager ();
  gtk_widget_show (pmw->window);
}


static void
ok_preferences_manager ()
{
}


static void
cancel_preferences_manager ()
{
  g_free (pmw);
  pmw = NULL;
}



/*
 * refresh data in the preferences window
 */
void
refresh_preferences_manager ()
{
  gtk_entry_set_text (GTK_ENTRY (pmw->real_name), balsa_app.real_name);

  /* we're gonna display this as  USERNAME @ HOSTNAME 
   * for the From: header */
  gtk_entry_set_text (GTK_ENTRY (pmw->username), balsa_app.username);
  gtk_entry_set_text (GTK_ENTRY (pmw->hostname), balsa_app.hostname);

  gtk_entry_set_text (GTK_ENTRY (pmw->organization), balsa_app.organization);

  gtk_entry_set_text (GTK_ENTRY (pmw->smtp_server), balsa_app.smtp_server);

  gtk_entry_set_text (GTK_ENTRY (pmw->mail_directory), balsa_app.local_mail_directory);
}



/*
 * identity notebook page
 */
static GtkWidget *
create_identity_page ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button;


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);
  gtk_widget_show (vbox);


  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);


  /* your name */
  label = gtk_label_new ("Your Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  pmw->real_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pmw->real_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (pmw->real_name);



  /* email address */
  label = gtk_label_new ("E-Mail Address:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  pmw->username = gtk_entry_new ();
  pmw->hostname = gtk_entry_new ();
  hbox=gtk_hbox_new(FALSE, 0);
  gtk_widget_show(hbox);
  gtk_box_pack_start (GTK_BOX (hbox), pmw->username, TRUE, TRUE, 0);
  gtk_widget_show(pmw->username);
  label = gtk_label_new ("@");
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gtk_widget_show(label);
  gtk_box_pack_start (GTK_BOX (hbox), pmw->hostname, TRUE, TRUE, 0);
  gtk_widget_show(pmw->hostname);
  
  gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);


  /* organization */
  label = gtk_label_new ("Organization:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  pmw->organization = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pmw->organization, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (pmw->organization);


  /* smtp server */
  label = gtk_label_new ("SMTP server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  pmw->smtp_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pmw->smtp_server, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (pmw->smtp_server);

  
  /* local mail dir */
  label = gtk_label_new ("Local mail directory:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  pmw->mail_directory = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), pmw->mail_directory, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (pmw->mail_directory);


  return vbox;
}
