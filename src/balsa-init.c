/* Balsa E-Mail Client
 * Copyright (C) 1998 Stuart Parmenter
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
#include "balsa-init.h"
#include "balsa.xpm"
#include "save-restore.h"

typedef enum
  {
    IW_PAGE_WELCOME,
    IW_PAGE_GENERAL,
    IW_PAGE_MBOXS,
    IW_PAGE_FINISHED,
  }
InitWindowPageType;

typedef struct _InitWindow InitWindow;
struct _InitWindow
  {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *next;
    GtkWidget *prev;
  };

InitWindow *iw = NULL;

typedef struct _Prefs Prefs;
struct _Prefs
  {
    /* identity */
    GtkWidget *real_name;
    GtkWidget *email;
    GtkWidget *smtp_server;
  };
static Prefs *prefs = NULL;


void balsa_init_window_new ();
static gint delete_init_window (GtkWidget *);

static void next_cb (GtkWidget *);
static void prev_cb (GtkWidget *);
static void complete_cb (GtkWidget *);

static GtkWidget *create_welcome_page ();
static GtkWidget *create_general_page ();
static GtkWidget *create_mailboxes_page ();
static GtkWidget *create_finished_page ();


/*
 * Balsa Initializing Stuff
 */

void
initialize_balsa (int argc, char *argv[])
{
  balsa_init_window_new ();
  /*
     open_preferences_manager();
   */
}

void
balsa_init_window_new ()
{
  GtkWidget *vbox;
  GtkWidget *pixmap;
  GtkWidget *label;
  GtkWidget *bbox;

  iw = g_malloc0 (sizeof (InitWindow));
  prefs = g_malloc0 (sizeof (Prefs));

  iw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (iw->window), _ ("Welcome To Balsa!"));

  gtk_widget_realize (iw->window);

  gtk_signal_connect (GTK_OBJECT (iw->window),
		      "delete_event",
		      (GtkSignalFunc) delete_init_window,
		      NULL);

  vbox = gtk_vbox_new (FALSE, 0);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (iw->window)->vbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  pixmap = gnome_pixmap_new_from_xpm_d (balsa_logo_xpm);
  gtk_box_pack_start (GTK_BOX (vbox), pixmap, FALSE, FALSE, 0);
  gtk_widget_show (pixmap);

  iw->notebook = gtk_notebook_new ();
  gtk_container_border_width (GTK_CONTAINER (iw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (vbox), iw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (iw->notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (iw->notebook), FALSE);
  gtk_widget_show (iw->notebook);

  label = gtk_label_new ("welcome");
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_welcome_page (),
			    label);

  label = gtk_label_new ("general");
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_general_page (),
			    label);

  label = gtk_label_new ("mailboxs");
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_mailboxes_page (),
			    label);

  label = gtk_label_new ("finished");
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_finished_page (),
			    label);


  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (iw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  iw->prev = gtk_button_new_with_label (_ ("Previous..."));
  gtk_container_add (GTK_CONTAINER (bbox), iw->prev);
  gtk_widget_show (iw->prev);

  gtk_widget_set_sensitive (iw->prev, FALSE);

  gtk_signal_connect (GTK_OBJECT (iw->prev),
		      "clicked",
		      (GtkSignalFunc) prev_cb,
		      NULL);

  iw->next = gtk_button_new_with_label (_ ("Next..."));
  gtk_container_add (GTK_CONTAINER (bbox), iw->next);
  gtk_widget_show (iw->next);

  gtk_signal_connect (GTK_OBJECT (iw->next),
		      "clicked",
		      (GtkSignalFunc) next_cb,
		      NULL);

  gtk_widget_show (iw->window);
}

static GtkWidget *
create_welcome_page ()
{
  GtkWidget *vbox;
  GtkWidget *text;
  GtkWidget *label;

#if 0
  gchar *buf;

  buf = g_new (gchar, 2048);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  snprintf (buf, 2048, _ ("Welcome to Balsa!\n\n"),
	    _ ("You seem to be running Balsa for the first time.\n"),
	    _ ("The following steps will setup Balsa by asking a few simple questions."), "  ",
	    _ ("Once you have completed these steps, you can always change them at a later time through Balsa's preferences."),
	    "  ",
	    _ ("Please check the about box in Balsa's main window for more information on contacting the authors or reporting bugs."));
  gtk_text_freeze (GTK_TEXT (text));
  text = gtk_text_new (NULL, NULL);
  gtk_box_pack_start (GTK_BOX (vbox), text, FALSE, FALSE, 5);
  gtk_widget_show (text);
  gtk_text_set_editable (GTK_TEXT (text), TRUE);
  gtk_text_set_word_wrap (GTK_TEXT (text), TRUE);
  gtk_text_insert (GTK_TEXT (text), NULL, NULL, NULL, buf, 2048);
  g_free (buf);
  gtk_text_thaw (text);
#endif
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  label = gtk_label_new (_ ("Welcome to Balsa!  The following steps will help you get setup to use Balsa."));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 5);
  gtk_widget_show (label);

  return vbox;
}

static GtkWidget *
create_general_page ()
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;

  GString *str;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 10);
  gtk_widget_show (vbox);


  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  /* your name */
  label = gtk_label_new (_ ("Your name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);

  prefs->real_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->real_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->real_name);

  /* email address */
  label = gtk_label_new (_ ("E-Mail address:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  prefs->email = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->email, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->email);

  /* smtp server */
  label = gtk_label_new (_ ("SMTP server:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  prefs->smtp_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), prefs->smtp_server, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (prefs->smtp_server);

  str = g_string_new (balsa_app.username);
  g_string_append_c (str, '@');
  g_string_append (str, balsa_app.hostname);
  gtk_entry_set_text (GTK_ENTRY (prefs->email), str->str);
  g_string_free (str, TRUE);

  gtk_entry_set_text (GTK_ENTRY (prefs->real_name), balsa_app.real_name);
  gtk_entry_set_text (GTK_ENTRY (prefs->smtp_server), balsa_app.smtp_server);

  return vbox;
}

static GtkWidget *
create_mailboxes_page ()
{
  GtkWidget *vbox;

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_widget_show (vbox);

  return vbox;
}

static GtkWidget *
create_finished_page ()
{
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *label;

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_widget_show (vbox);

  label = gtk_label_new (_ ("Balsa is now ready to run.  Click finish to save your settings"));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 5);
  gtk_widget_show (label);

  button = gtk_button_new_with_label (_ ("Finish"));
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 5);
  gtk_widget_show (button);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) complete_cb,
		      NULL);


  return vbox;
}

static gint
delete_init_window (GtkWidget * widget)
{
  printf ("we are deleting the window, not saving, lets quit now\n");
  balsa_exit ();
  return FALSE;
}

static void
next_cb (GtkWidget * widget)
{
  gtk_notebook_next_page (GTK_NOTEBOOK (iw->notebook));
  if (gtk_notebook_current_page (GTK_NOTEBOOK (iw->notebook)) != IW_PAGE_WELCOME)
    gtk_widget_set_sensitive (iw->prev, TRUE);
  if (gtk_notebook_current_page (GTK_NOTEBOOK (iw->notebook)) == IW_PAGE_FINISHED)
    gtk_widget_set_sensitive (iw->next, FALSE);
}

static void
prev_cb (GtkWidget * widget)
{
  gtk_notebook_prev_page (GTK_NOTEBOOK (iw->notebook));
  gtk_widget_set_sensitive (iw->next, TRUE);
  if (gtk_notebook_current_page (GTK_NOTEBOOK (iw->notebook)) == IW_PAGE_WELCOME)
    gtk_widget_set_sensitive (iw->prev, FALSE);
}

static void
complete_cb (GtkWidget * widget)
{
  gchar *email, *c;

  g_free (balsa_app.real_name);
  balsa_app.real_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->real_name)));

  email = c = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->email)));

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
  balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->smtp_server)));

  save_global_settings ();

  gtk_widget_destroy (prefs->real_name);
  gtk_widget_destroy (prefs->email);
  gtk_widget_destroy (prefs->smtp_server);
  g_free (prefs);

  gtk_widget_destroy (iw->next);
  gtk_widget_destroy (iw->prev);
  gtk_widget_destroy (iw->notebook);
  gtk_widget_destroy (iw->window);
  g_free (iw);

  do_load_mailboxes ();
  open_main_window ();
}
