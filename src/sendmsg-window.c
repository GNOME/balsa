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

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include "c-client.h"
#include "sendmsg-window.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "index.h"
#include "mailbox.h"
#include "pixmaps/p8.xpm"
#include "pixmaps/p13.xpm"
#include "pixmaps/p14.xpm"
#include "pixmaps/p15.xpm"
#include "pixmaps/p16.xpm"

gint delete_event (GtkWidget *, gpointer);

extern GtkWidget *new_icon (gchar **, GtkWidget *);

static void send_smtp_message (GtkWidget *, BalsaSendmsg *);
static void close_window (GtkWidget *, gpointer);
static void balsa_sendmsg_free (BalsaSendmsg *);
static GtkWidget *create_menu (GtkWidget *);

static GtkWidget *menu_items[9];
GtkTooltips *tooltips;



static void
close_window (GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy (GTK_WIDGET (data));
}

static GtkWidget *
create_toolbar (GtkWidget * window, BalsaSendmsg * bsmw)
{
  GtkWidget *toolbar;
  GtkWidget *toolbarbutton;

  tooltips = gtk_tooltips_new ();

  toolbar = gtk_toolbar_new (0, 0);
  gtk_widget_realize (window);


  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					   "Send", "Send", NULL,
	     new_icon (p8_xpm, window), GTK_SIGNAL_FUNC (send_smtp_message),
					   bsmw);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					 "Spell Check", "Spell Check", NULL,
					   new_icon (p13_xpm, window), NULL,
					   "Spell Check");

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
				       "Address Book", "Address Book", NULL,
					   new_icon (p14_xpm, window), NULL,
					   "Address Book");

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					   "Print", "Print", NULL,
					   new_icon (p15_xpm, window), NULL,
					   "Print");

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
		   "Context Sensitive Help", "Context Sensitive Help", NULL,
					   new_icon (p16_xpm, window), NULL,
					   "Context Sensitive Help");

  gtk_widget_show (toolbar);
  return toolbar;
}

void
balsa_sendmsg_destroy (BalsaSendmsg * bsm)
{
  gtk_widget_destroy (bsm->to);
  gtk_widget_destroy (bsm->from);
  gtk_widget_destroy (bsm->subject);
  gtk_widget_destroy (bsm->cc);
  gtk_widget_destroy (bsm->bcc);
/*  g_free (bsm); */
}

static GtkWidget *
create_menu (GtkWidget * window)
{
  GtkWidget *menubar, *w, *menu;
  GtkAcceleratorTable *accel;
  int i = 0;

  accel = gtk_accelerator_table_new ();
  menubar = gtk_menu_bar_new ();
  gtk_widget_show (menubar);

  menu = gtk_menu_new ();

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Send"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Attach File"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Close"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     GTK_SIGNAL_FUNC (close_window),
			     GTK_OBJECT (window));
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Message"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_CUT, _ ("Cut"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'X', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_COPY, _ ("Copy"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'C', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PASTE, _ ("Paste"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'V', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Edit"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("Contents"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Help"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu_items[i] = NULL;

  g_print ("%d menu items\n", i);

  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);
  return menubar;
}


void
sendmsg_window_new (GtkWidget * widget, gpointer data)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;
  BalsaSendmsg *msg = NULL;

  msg = g_malloc (sizeof (BalsaSendmsg));

  window = gnome_app_new ("balsa_sendmsg_window", "New message");
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC (delete_event), NULL);
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ("To:");
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);
  gtk_widget_show (label);
  msg->to = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), msg->to, 1, 2, 0, 1);
  gtk_widget_show (msg->to);

  label = gtk_label_new ("From:");
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);
  gtk_widget_show (label);
  msg->from = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), msg->from, 1, 2, 1, 2);
  gtk_widget_show (msg->from);

  label = gtk_label_new ("Subject:");
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2, 3);
  gtk_widget_show (label);
  msg->subject = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), msg->subject, 1, 2, 2, 3);
  gtk_widget_show (msg->subject);

  label = gtk_label_new ("cc:");
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 3, 4);
  gtk_widget_show (label);
  msg->cc = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), msg->cc, 1, 2, 3, 4);
  gtk_widget_show (msg->cc);

  label = gtk_label_new ("bcc:");
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 4, 5);
  gtk_widget_show (label);
  msg->bcc = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), msg->bcc, 1, 2, 4, 5);
  gtk_widget_show (msg->bcc);



  table = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  msg->text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (msg->text), TRUE);
  gtk_widget_show (msg->text);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->text, 0, 1, 0, 1);
  hscrollbar = gtk_hscrollbar_new (GTK_TEXT (msg->text)->hadj);
  gtk_table_attach (GTK_TABLE (table), hscrollbar, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (hscrollbar);

  vscrollbar = gtk_vscrollbar_new (GTK_TEXT (msg->text)->vadj);
  gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
		    GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
  gtk_widget_show (vscrollbar);



  gnome_app_set_contents (GNOME_APP (window), vbox);

  gnome_app_set_menus (GNOME_APP (window),
		       GTK_MENU_BAR (create_menu (window)));

  gnome_app_set_toolbar (GNOME_APP (window),
			 GTK_TOOLBAR (create_toolbar (window, msg)));

  gtk_widget_show (window);
}




/*
 * C-client stuff below! LOOK OUT! :)
 */



static char *hostlist[] =
{				/* SMTP server host list */
  "localhost",
  NIL
};

static char *newslist[] =
{				/* Netnews server host list */
  "news",
  NIL
};

char *curhst = NIL;		/* currently connected host */
char *curusr = NIL;		/* current login user */
char personalname[MAILTMPLEN];	/* user's personal name */

static GString *
gtk_text_to_email (char *buff)
{
  int i = 0, len = strlen (buff);
  GString *gs = g_string_new (NULL);

  for (i = 0; i < len; i++)
    {
      switch (buff[i])
	{
	case '\n':
	  gs = g_string_append (gs, "\015\012");
	  break;
	default:
	  gs = g_string_append_c (gs, buff[i]);
	  break;
	}
    }
  return gs;
}

static void
send_smtp_message (GtkWidget * widget, BalsaSendmsg * bsmsg)
{
  long debug = 0;
  char line[MAILTMPLEN];

  SENDSTREAM *stream = NIL;
  ENVELOPE *msg = mail_newenvelope ();
  BODY *body = mail_newbody ();

  GString *text;
  gchar *textbuf;

  curusr = g_strdup (myusername ());
  curhst = g_strdup (mylocalhost ());

  msg->from = mail_newaddr ();
  msg->from->personal = g_strdup (personalname);
  msg->from->mailbox = g_strdup (curusr);
  msg->from->host = g_strdup (curhst);
  msg->return_path = mail_newaddr ();
  msg->return_path->mailbox = g_strdup (curusr);
  msg->return_path->host = g_strdup (curhst);

  rfc822_parse_adrlist (&msg->to, gtk_entry_get_text (GTK_ENTRY (bsmsg->to)), curhst);
  if (msg->to)
    {
      rfc822_parse_adrlist (&msg->cc, gtk_entry_get_text (GTK_ENTRY (bsmsg->cc)), curhst);
    }
  msg->subject = g_strdup (gtk_entry_get_text (GTK_ENTRY (bsmsg->subject)));
  body->type = TYPETEXT;

  textbuf = gtk_editable_get_chars (GTK_EDITABLE (bsmsg->text), 0, gtk_text_get_length (GTK_TEXT (bsmsg->text)));
  text = gtk_text_to_email (textbuf);
  text = g_string_append (text, "\015\012");

  body->contents.text.data = (unsigned char *) text->str;
  body->contents.text.size = strlen (text->str);
  rfc822_date (line);
  msg->date = (char *) fs_get (1 + strlen (line));
  strcpy (msg->date, line);
  if (msg->to)
    {
      fprintf (stderr, "Sending...\n");
      if (stream = smtp_open (hostlist, debug))
	{
	  if (smtp_mail (stream, "MAIL", msg, body))
	    fprintf (stderr, "[Ok]\n");
	  else
	    fprintf (stderr, "[Failed - %s]\n", stream->reply);
	}
    }
  if (stream)
    smtp_close (stream);
  else
    fprintf (stderr, "[Can't open connection to any server]\n");
  mail_free_envelope (&msg);
  mail_free_body (&body);
  g_string_free (text, 1);
/*  balsa_sendmsg_destroy (bsmsg); */
}
