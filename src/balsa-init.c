/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Stuart Parmenter
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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "mailbox.h"
#include "main.h"
#include "misc.h"
#include "balsa-app.h"
#include "balsa-init.h"
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
    GtkWidget *bbox;
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

    GtkWidget *inbox;
    GtkWidget *outbox;
    GtkWidget *trash;
  };
static Prefs *prefs = NULL;


void balsa_init_window_new (void);
static gint delete_init_window (GtkWidget *, gpointer);
static void text_realize_handler (GtkWidget *, gpointer);

static void next_cb (GtkWidget *, gpointer);
static void prev_cb (GtkWidget *, gpointer);
static void complete_cb (GtkWidget *, gpointer);

static GtkWidget *create_welcome_page (void);
static GtkWidget *create_general_page (void);
static GtkWidget *create_mailboxes_page (void);

static int make_parents (gchar * filename);
static void create_mailbox_if_not_present (gchar * filename);

/*
 * Balsa Initializing Stuff
 */

void
initialize_balsa (void)
{
  balsa_init_window_new ();
}

void
balsa_init_window_new (void)
{
  GtkWidget *vbox;
  GtkWidget *pixmap;
  GtkWidget *label;
  gchar *logo;

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

  logo = gnome_unconditional_pixmap_file ("balsa/balsa_logo.png");
  pixmap = gnome_pixmap_new_from_file (logo);
  g_free (logo);
  gtk_box_pack_start (GTK_BOX (vbox), pixmap, FALSE, FALSE, 0);
  gtk_widget_show (pixmap);

  iw->notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (iw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (vbox), iw->notebook, FALSE, FALSE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (iw->notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (iw->notebook), FALSE);
  gtk_widget_show (iw->notebook);

  label = gtk_label_new (_("welcome"));
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_welcome_page (),
			    label);

  label = gtk_label_new (_("general"));
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_general_page (),
			    label);

  label = gtk_label_new (_("mailboxs"));
  gtk_notebook_append_page (GTK_NOTEBOOK (iw->notebook),
			    create_mailboxes_page (),
			    label);

  iw->bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (iw->window)->action_area), iw->bbox, TRUE, TRUE, 0);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (iw->bbox), 5);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (iw->bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (iw->bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (iw->bbox);


  iw->prev = gnome_stock_button (GNOME_STOCK_BUTTON_PREV);
  gtk_container_add (GTK_CONTAINER (iw->bbox), iw->prev);
  gtk_widget_show (iw->prev);

  gtk_widget_set_sensitive (iw->prev, FALSE);

  gtk_signal_connect (GTK_OBJECT (iw->prev),
		      "clicked",
		      (GtkSignalFunc) prev_cb,
		      NULL);

  iw->next = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
  gtk_container_add (GTK_CONTAINER (iw->bbox), iw->next);
  gtk_widget_show (iw->next);

  gtk_signal_connect (GTK_OBJECT (iw->next),
		      "clicked",
		      (GtkSignalFunc) next_cb,
		      NULL);

  gtk_widget_show (iw->window);

  /* start up the gtk_main for the initialize window */
  gtk_main ();
}

static void
text_realize_handler (GtkWidget * text, gpointer data)
{
  GString *str;

  str = g_string_new (_ ("Welcome to Balsa!\n\n"));

  str = g_string_append (str, _ ("You seem to be running Balsa for the first time.\n"));
  str = g_string_append (str, _ ("The following steps will setup Balsa by asking a few simple questions.  "));
  str = g_string_append (str, _ ("Once you have completed these steps, you can always change them at a later time through Balsa's preferences.  "));
  str = g_string_append (str, _ ("Please check the about box in Balsa's main window for more information on contacting the authors or reporting bugs."));

  gtk_text_freeze (GTK_TEXT (text));
  gtk_text_set_editable (GTK_TEXT (text), FALSE);
  gtk_text_set_word_wrap (GTK_TEXT (text), TRUE);
  gtk_text_insert (GTK_TEXT (text), NULL, NULL, NULL, str->str, strlen (str->str));
  gtk_text_thaw (GTK_TEXT (text));

  g_string_free (str, TRUE);
}

static GtkWidget *
create_welcome_page (void)
{
  GtkWidget *vbox;
  GtkWidget *text;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  text = gtk_text_new (NULL, NULL);
  gtk_box_pack_start (GTK_BOX (vbox), text, FALSE, FALSE, 5);
  gtk_widget_show (text);

  gtk_signal_connect (GTK_OBJECT (text), "realize", GTK_SIGNAL_FUNC (text_realize_handler), NULL);

  return vbox;
}

static GtkWidget *
create_general_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;

  GString *str;
  char *name;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);
  gtk_widget_show (vbox);


  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
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

  str = g_string_new (g_get_user_name ());
  g_string_append_c (str, '@');

  g_string_append (str, g_get_host_name ());
  gtk_entry_set_text (GTK_ENTRY (prefs->email), str->str);
  g_string_free (str, TRUE);

  name = g_get_real_name ();
  if (name != NULL)
    {
      char *p;

      /* Don't include other fields of the GECOS */
      p = strchr (name, ',');
      if (p != NULL)
	*p = '\0';

      gtk_entry_set_text (GTK_ENTRY (prefs->real_name), name);
    }

  gtk_entry_set_text (GTK_ENTRY (prefs->smtp_server), "localhost");

  return vbox;
}

static GtkWidget *
create_mailboxes_page (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;
  GString *gs;
  gchar *spool;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (3, 2, FALSE);
  gtk_widget_show (table);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

  label = gtk_label_new (_ ("Inbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->inbox = gtk_entry_new ();
  spool = getenv ("MAIL");
  if (spool)
    gtk_entry_set_text (GTK_ENTRY (prefs->inbox), spool);
  gtk_table_attach (GTK_TABLE (table), prefs->inbox, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_new (g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/outbox");

  label = gtk_label_new (_ ("Outbox Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->outbox = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->outbox), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->outbox, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  gs = g_string_truncate (gs, 0);
  gs = g_string_append (gs, g_get_home_dir ());
  gs = g_string_append (gs, "/Mail/trash");
  label = gtk_label_new (_ ("Trash Path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL, 10, 10);
  prefs->trash = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (prefs->trash), gs->str);
  gtk_table_attach (GTK_TABLE (table), prefs->trash, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  g_string_free (gs, TRUE);

  label = gtk_label_new (_ ("If you wish to use IMAP for these\nplease change them inside Balsa\n"));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);

  return vbox;
}

static gint
delete_init_window (GtkWidget * widget, gpointer data)
{
  printf ("we are deleting the window, not saving, lets quit now\n");
  balsa_exit ();
  return FALSE;
}

/* arp
 * We need to check whether the parent directories also exist. If not, they
 * need to be made too. We first try to create the mailbox if it doesn't
 * exist. If that fails, we check how much of the path is missing and
 * create the missing bits before trying to make the mailbox again.
 *
 * Return 0 if all missing parents were created, else !0.
 */
static int
make_parents (gchar * filename)
{
  struct stat st;
  gchar *dir = NULL;
  gchar *pathname = NULL;
  gint i = 1;			/* skip the initial / cause we don't care about it */
  gint len;

  pathname = g_strdup (filename);

  len = strlen (pathname);

  while (i < len)
    {
      if (pathname[i] == '/')
	{
	  dir = g_strndup (pathname, i);
	  if (stat (dir, &st) != 0)
	    {
	      int ret;
	      ret = mkdir (dir, S_IRUSR | S_IWUSR | S_IXUSR);
	      if (ret == -1)
		{
		  g_error ("Error creating directory %s: %s",
			   dir, strerror (errno));
		  return FALSE;
		}
	    }
	}
      i++;
    }

  if (stat (pathname, &st) != 0)
    {
      int ret;
      ret = mkdir (pathname, S_IRUSR | S_IWUSR | S_IXUSR);
      if (ret == -1)
	{
	  g_error ("Error creating directory %s: %s",
		   pathname, strerror (errno));
	  return FALSE;
	}
    }

  g_free (dir);
  g_free (pathname);

  return TRUE;
}


/* Check to see if the specified file exists; if it doesn't, try to
   create it */
static void
create_mailbox_if_not_present (gchar * filename)
{
  gchar *dir;
  dir = g_dirname (filename);

/* Make the as much of the path as required. */
  if (make_parents (dir))
    creat (filename, S_IRUSR | S_IWUSR);
  g_free (dir);
}				/* create_mailbox_if_not_present */

static void
check_mailboxes_for_finish (GtkWidget * widget, gpointer data)
{
  GtkWidget *ask;
  GString *str;
  gchar *mbox;
  gint clicked_button;

  str = g_string_new (NULL);

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->inbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, "Mailbox \"%s\" is not valid.\n\nWould you like to create it?", mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->outbox));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, "Mailbox \"%s\" is not valid.\n\nWould you like to create it?", mbox);
      goto BADMAILBOX;
    }

  mbox = gtk_entry_get_text (GTK_ENTRY (prefs->trash));
  if (mailbox_valid (mbox) == MAILBOX_UNKNOWN)
    {
      g_string_sprintf (str, "Mailbox \"%s\" is not valid.\n\nWould you like to create it?", mbox);
      goto BADMAILBOX;
    }
  else
    {
      g_string_free (str, TRUE);
      complete_cb (widget, data);
      return;
    }


BADMAILBOX:
  ask = gnome_message_box_new (str->str,
			       GNOME_MESSAGE_BOX_QUESTION,
			       GNOME_STOCK_BUTTON_YES,
			       GNOME_STOCK_BUTTON_NO,
			       NULL);
  clicked_button = gnome_dialog_run (GNOME_DIALOG (ask));
  g_string_free (str, TRUE);
  if (clicked_button == 0)
    {
      create_mailbox_if_not_present (mbox);
      check_mailboxes_for_finish (widget, data);
      return;
    }
  else
    {
      ask = gnome_message_box_new ("Unable to procede without a valid mailbox.  Please try again.",
				   GNOME_MESSAGE_BOX_ERROR,
				   GNOME_STOCK_BUTTON_OK,
				   NULL);
    }
}

static void
next_cb (GtkWidget * widget, gpointer data)
{
  switch (gtk_notebook_get_current_page (GTK_NOTEBOOK (iw->notebook)) + 1)
    {
    case IW_PAGE_FINISHED:
      check_mailboxes_for_finish (widget, data);
      break;

    case IW_PAGE_MBOXS:
      {
	GtkWidget *pixmap;
	gtk_widget_destroy (iw->next);
	pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_SAVE);
	iw->next = gnome_pixmap_button (pixmap, _ ("Finish"));
	gtk_container_add (GTK_CONTAINER (iw->bbox), iw->next);
	gtk_signal_connect (GTK_OBJECT (iw->next), "clicked",
			    (GtkSignalFunc) next_cb, NULL);
	gtk_widget_show (iw->next);

	gtk_notebook_next_page (GTK_NOTEBOOK (iw->notebook));
      }
      break;

    default:
      gtk_widget_set_sensitive (iw->prev, TRUE);
      gtk_notebook_next_page (GTK_NOTEBOOK (iw->notebook));
      break;
    }
  return;
}

static void
prev_cb (GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy (iw->next);
  iw->next = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
  gtk_container_add (GTK_CONTAINER (iw->bbox), iw->next);
  gtk_signal_connect (GTK_OBJECT (iw->next), "clicked",
		      (GtkSignalFunc) next_cb, NULL);
  gtk_widget_show (iw->next);

  gtk_notebook_prev_page (GTK_NOTEBOOK (iw->notebook));
  gtk_widget_set_sensitive (iw->next, TRUE);
  if (gtk_notebook_get_current_page (GTK_NOTEBOOK (iw->notebook)) == IW_PAGE_WELCOME)
    gtk_widget_set_sensitive (iw->prev, FALSE);
}

static void
complete_cb (GtkWidget * widget, gpointer data)
{
  GString *gs;
  Mailbox *mailbox;
  MailboxType type;

  g_free (balsa_app.address->personal);
  balsa_app.address->personal = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->real_name)));

  g_free (balsa_app.address->mailbox);
  balsa_app.address->mailbox = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->email)));

  g_free (balsa_app.smtp_server);
  balsa_app.smtp_server = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->smtp_server)));

  gs = g_string_new (g_get_home_dir ());
  gs = g_string_append (gs, "/Mail");
  balsa_app.local_mail_directory = g_strdup (gs->str);
  g_string_free (gs, TRUE);

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));
  mailbox = mailbox_new (type);
  mailbox->name = g_strdup (_("Inbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));
  config_mailbox_add (mailbox, "Inbox");
  add_mailboxes_for_checking (mailbox);
  mailbox_free (mailbox);

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->inbox)));
  mailbox = mailbox_new (type);
  mailbox->name = g_strdup (_("Outbox"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->outbox)));
  config_mailbox_add (mailbox, "Outbox");
  add_mailboxes_for_checking (mailbox);
  mailbox_free (mailbox);

  type = mailbox_valid (gtk_entry_get_text (GTK_ENTRY (prefs->trash)));
  mailbox = mailbox_new (type);
  mailbox->name = g_strdup (_("Trash"));
  MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (prefs->trash)));
  config_mailbox_add (mailbox, "Trash");
  add_mailboxes_for_checking (mailbox);
  mailbox_free (mailbox);

  config_global_save ();

  gtk_widget_destroy (prefs->real_name);
  gtk_widget_destroy (prefs->email);
  gtk_widget_destroy (prefs->smtp_server);

  gtk_widget_destroy (prefs->inbox);
  gtk_widget_destroy (prefs->outbox);
  gtk_widget_destroy (prefs->trash);

  g_free (prefs);

  gtk_widget_destroy (iw->next);
  gtk_widget_destroy (iw->prev);
  gtk_widget_destroy (iw->notebook);
  gtk_widget_destroy (iw->window);
  g_free (iw);

  /* end the initialize balsa main loop */
  gtk_main_quit ();
}
