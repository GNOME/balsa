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

#include <stdio.h>
#include <string.h>
#include <gnome.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "mailbox.h"
#include "sendmsg-window.h"
#include "mail.h"
#include "smtp.h"


gint delete_event (GtkWidget *, gpointer);

extern GtkWidget *new_icon (gchar **, GtkWidget *);

static void send_smtp_message (GtkWidget *, BalsaSendmsg *);
static void close_window (GtkWidget *, gpointer);
static void balsa_sendmsg_free (BalsaSendmsg *);
static GtkWidget *create_menu (BalsaSendmsg *);

static gchar *gt_replys (gchar *);

static GtkWidget *menu_items[6];
GtkTooltips *tooltips;



static void
close_window (GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy (GTK_WIDGET (data));
}


static GtkWidget *
create_toolbar (BalsaSendmsg * bsmw)
{
  GtkWidget *window = bsmw->window;
  GtkWidget *toolbar;
  GtkWidget *toolbarbutton;

  tooltips = gtk_tooltips_new ();

  toolbar = gtk_toolbar_new (0, 0);
  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), balsa_app.toolbar_style);

  gtk_widget_realize (window);

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					   "Send", "Send", NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_SND),
					GTK_SIGNAL_FUNC (send_smtp_message),
					   bsmw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

#if 0
  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					 "Spell Check", "Spell Check", NULL,
					   new_icon (p13_xpm, window), NULL,
					   "Spell Check");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
				       "Address Book", "Address Book", NULL,
					   new_icon (p14_xpm, window), NULL,
					   "Address Book");

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton = gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
					   "Print", "Print", NULL,
	       gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_PRINT),
					   NULL, "Print");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);
#endif


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
  gtk_widget_destroy (bsm->window);
  g_free (bsm);
  bsm = NULL;
}


static GtkWidget *
create_menu (BalsaSendmsg * bmsg)
{
  GtkWidget *window = bmsg->window;
  GtkWidget *menubar, *w, *menu;
  GtkAcceleratorTable *accel;
  int i = 0;

  accel = gtk_accelerator_table_new ();
  menubar = gtk_menu_bar_new ();
  gtk_widget_show (menubar);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_SND, _ ("Send"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      GTK_SIGNAL_FUNC (send_smtp_message),
		      bmsg);


#if 0
  w = gnome_stock_menu_item (GNOME_STOCK_MENU_OPEN, _ ("Attach File"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;
#endif


  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_QUIT, _ ("Close"));
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
  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);
  return menubar;
}


void
sendmsg_window_new (GtkWidget * widget, BalsaIndex * bindex, gint type)
{
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;
  GtkCList *clist;

  BalsaSendmsg *msg = NULL;
  gchar *from;
  gint row;
  gchar *tmp;
  gchar *c;

  Message *message;

  msg = g_malloc (sizeof (BalsaSendmsg));
  switch (type)
    {
    case 0:
      msg->window = gnome_app_new ("balsa", "New message");
      break;
    case 1:
      clist = GTK_CLIST (GTK_BIN (bindex)->child);

      if (!clist->selection)
	return;

      row = (gint) clist->selection->data;

      message = (Message *) gtk_clist_get_row_data (clist, row);

      msg->window = gnome_app_new ("balsa", "Reply to ");
      break;
    case 2:
      clist = GTK_CLIST (GTK_BIN (bindex)->child);

      if (!clist->selection)
	return;

      row = (gint) clist->selection->data + 1;
      msg->window = gnome_app_new ("balsa", "Forward message");
      break;
    }
  gtk_signal_connect (GTK_OBJECT (msg->window), "destroy",
		      GTK_SIGNAL_FUNC (delete_event), NULL);
  gtk_signal_connect (GTK_OBJECT (msg->window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  vbox = gtk_vbox_new (FALSE, 1);
  gtk_container_border_width (GTK_CONTAINER (vbox), 2);
  gtk_widget_show (vbox);

  table = gtk_table_new (5, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);


  label = gtk_label_new ("To:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
  msg->to = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->to, 1, 2, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  if (type == 1)
    {
      if (message->reply_to->personal)
	gtk_entry_set_text (GTK_ENTRY (msg->to), message->reply_to->personal);
      else
	{
	  tmp = g_malloc (strlen (message->from->user) + 1 + strlen (message->from->host) + 1);
	  sprintf (tmp, "%s@%s", message->from->user, message->from->host);
	  gtk_entry_set_text (GTK_ENTRY (msg->to), tmp);
	  g_free (tmp);
	}
    }


  gtk_widget_show (msg->to);


  label = gtk_label_new ("From:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
  msg->from = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->from, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  GTK_WIDGET_UNSET_FLAGS (msg->from, GTK_CAN_FOCUS);
  gtk_entry_set_editable (GTK_ENTRY (msg->from), FALSE);

  from = g_malloc (strlen (balsa_app.real_name) + 2 + strlen (balsa_app.username) + 1 + strlen (balsa_app.hostname) + 2);
  sprintf (from, "%s <%s@%s>\0",
	   balsa_app.real_name,
	   balsa_app.username,
	   balsa_app.hostname);

  gtk_entry_set_text (GTK_ENTRY (msg->from), from);
  gtk_widget_show (msg->from);
  g_free (from);

  label = gtk_label_new ("Subject:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
  msg->subject = gtk_entry_new ();

  if (type != 0)
    {
      tmp = g_strdup (message->subject);
      gtk_entry_set_text (GTK_ENTRY (msg->subject), tmp);
      if (type == 1)
	{
	  if (strlen (tmp) < 2)
	    gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	  else
	    {
	      if (!((tmp[0] == 'R' || tmp[0] == 'r') &&
		    (tmp[1] == 'E' || tmp[1] == 'e') &&
		    (tmp[2] == ':')))
		gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	    }
	}
      else if (type == 2)
	{
	  if (strlen (tmp) < 2)
	    gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	  else
	    {
	      if (!((tmp[0] == 'F' || tmp[0] == 'f') &&
		    (tmp[1] == 'W' || tmp[1] == 'w') &&
		    (tmp[2] == ':')))
		gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Fw: ");
	    }
	}
      g_free (tmp);
    }

  gtk_table_attach (GTK_TABLE (table), msg->subject, 1, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (msg->subject);

  label = gtk_label_new ("cc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
  msg->cc = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->cc, 1, 2, 3, 4,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (msg->cc);

  label = gtk_label_new ("bcc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
  msg->bcc = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->bcc, 1, 2, 4, 5,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (msg->bcc);



  table = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_end (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  msg->text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (msg->text), TRUE);
  gtk_text_set_word_wrap (GTK_TEXT (msg->text), TRUE);
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

  gnome_app_set_contents (GNOME_APP (msg->window), vbox);

  gnome_app_set_menus (GNOME_APP (msg->window),
		       GTK_MENU_BAR (create_menu (msg)));

  gnome_app_set_toolbar (GNOME_APP (msg->window),
			 GTK_TOOLBAR (create_toolbar (msg)));

  gtk_widget_show (msg->window);

  gtk_text_freeze (GTK_TEXT (msg->text));
  if (type != 0)
    {
      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, "\n\n", 2);

      c = message->from->personal;

      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, c, strlen (c));

      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, " on ", 4);

      c = message->date;
      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, c, strlen (c));
      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, " wrote:\n", 8);
#if 0
      c = mail_fetchtext (bindex->stream, row);
      c = gt_replys (c);

      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, c, strlen (c));

      gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, "\n\n", 2);
#endif
    }
  if (balsa_app.signature)
    gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, balsa_app.signature, strlen (balsa_app.signature));
  gtk_text_set_point (GTK_TEXT (msg->text), 0);
  gtk_text_thaw (GTK_TEXT (msg->text));
}




/*
 * C-client stuff below! LOOK OUT! :)
 */

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
  long debug = 1;
  char line[MAILTMPLEN];

  SENDSTREAM *stream = NIL;
  ENVELOPE *msg = mail_newenvelope ();
  BODY *body = mail_newbody ();

  GString *text;
  gchar *textbuf;

  char *hostlist[] =
  {				/* SMTP server host list */
    NULL,
    "localhost",
    NIL
  };
  hostlist[0] = balsa_app.smtp_server;

  msg->from = mail_newaddr ();
  msg->from->personal = g_strdup (balsa_app.real_name);
  msg->from->mailbox = g_strdup (balsa_app.username);
  msg->from->host = g_strdup (balsa_app.hostname);
  msg->return_path = mail_newaddr ();
  msg->return_path->mailbox = g_strdup (balsa_app.username);
  msg->return_path->host = g_strdup (balsa_app.hostname);

  rfc822_parse_adrlist (&msg->to,
			gtk_entry_get_text (GTK_ENTRY (bsmsg->to)),
			balsa_app.hostname);
  if (strlen (gtk_entry_get_text (GTK_ENTRY (bsmsg->cc))) > 4)
    {
      rfc822_parse_adrlist (&msg->cc,
			    gtk_entry_get_text (GTK_ENTRY (bsmsg->cc)),
			    balsa_app.hostname);
    }
  msg->subject = g_strdup (gtk_entry_get_text (GTK_ENTRY (bsmsg->subject)));
  body->type = TYPETEXT;

  textbuf = gtk_editable_get_chars (GTK_EDITABLE (bsmsg->text),
				    0,
			      gtk_text_get_length (GTK_TEXT (bsmsg->text)));
  text = gtk_text_to_email (textbuf);
  text = g_string_append (text, "\015\012");

  body->contents.text.data = g_strdup (text->str);
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
  balsa_sendmsg_destroy (bsmsg);
}


static gchar *
gt_replys (char *buff)
{
  int i = 0, len = strlen (buff);
  GString *gs = g_string_new (NULL);

  for (i = 0; i < len; i++)
    {
      if (buff[i] == '\r' && buff[i + 1] == '\n')
	{
	  gs = g_string_append (gs, "\n> ");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\r')
	{
	  gs = g_string_append (gs, "\n> ");
	  i++;
	}
      else if (buff[i] == '\n')
	{
	  gs = g_string_append (gs, "\n> ");
	}
      else if (buff[i] == '\r')
	{
	  gs = g_string_append (gs, "\n> ");
	}
      else
	{
	  gs = g_string_append_c (gs, buff[i]);
	}
    }

  gs = g_string_prepend (gs, "> ");
  return gs->str;
}
