/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#include <ctype.h>

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "mailbox.h"
#include "misc.h"
#include "mime.h"
#include "mailbox.h"
#include "send.h"
#include "sendmsg-window.h"

static void send_message_cb (GtkWidget *, BalsaSendmsg *);
static void attach_clicked (GtkWidget *, gpointer);
static void close_window (GtkWidget *, gpointer);

static void balsa_sendmsg_destroy (BalsaSendmsg * bsm);

/* Standard DnD types */
enum
  {
    TARGET_URI_LIST,
  };

static GtkTargetEntry drop_types[] =
{
  {"text/uri-list", 0, TARGET_URI_LIST}
};

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))


static GnomeUIInfo main_toolbar[] =
{
  GNOMEUIINFO_ITEM_STOCK (N_ ("Send"), N_ ("Send this mail"), send_message_cb, GNOME_STOCK_PIXMAP_MAIL_SND),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Attach"), N_ ("Add attachments to this message"), attach_clicked, GNOME_STOCK_PIXMAP_ATTACH),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Spelling"), N_ ("Check Spelling"), NULL, GNOME_STOCK_PIXMAP_SPELLCHECK),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Print"), N_ ("Print"), NULL, GNOME_STOCK_PIXMAP_PRINT),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Cancel"), N_ ("Cancel"), close_window, GNOME_STOCK_PIXMAP_CLOSE),
  GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] =
{
  {
    GNOME_APP_UI_ITEM, N_ ("_Send"), NULL, send_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL_SND, 'Y', 0, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_ ("_Attach file..."), NULL, attach_clicked, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_ATTACH, 'H', 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_ ("E_xit"), NULL, close_window, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT, 'Q', 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_SUBTREE ("_File", file_menu),
  GNOMEUIINFO_END
};

static void
close_window (GtkWidget * widget, gpointer data)
{
  BalsaSendmsg *bsm;
  bsm = data;
  balsa_sendmsg_destroy (bsm);
}


static void
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

static void
remove_attachment (GtkWidget * widget, GnomeIconList * ilist)
{
  gint num = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (ilist), "selectednumbertoremove"));
  gnome_icon_list_remove (ilist, num);
  gtk_object_remove_data (GTK_OBJECT (ilist), "selectednumbertoremove");
}

static GtkWidget *
create_popup_menu (GnomeIconList * ilist, gint num)
{
  GtkWidget *menu, *menuitem;
  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Remove"));
  gtk_object_set_data (GTK_OBJECT (ilist), "selectednumbertoremove", GINT_TO_POINTER (num));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (remove_attachment), ilist);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}

static void
select_attachment (GnomeIconList * ilist, gint num, GdkEventButton * event)
{

  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    gtk_menu_popup (GTK_MENU (create_popup_menu (ilist, num)),
		    NULL, NULL, NULL, NULL,
		    event->button, event->time);
}

static void
add_attachment (GnomeIconList * iconlist, char *filename)
{
  gint pos;

  pos = gnome_icon_list_append (
				 iconlist,
		   gnome_unconditional_pixmap_file ("balsa/attachment.png"),
				 g_basename (filename));
  gnome_icon_list_set_icon_data (iconlist, pos, filename);
}

static void
attach_dialog_ok (GtkWidget * widget, gpointer data)
{
  GtkFileSelection *fs;
  GnomeIconList *iconlist;
  gchar *filename;

  fs = GTK_FILE_SELECTION (data);
  iconlist = GNOME_ICON_LIST (gtk_object_get_user_data (GTK_OBJECT (fs)));

  filename = g_strdup (gtk_file_selection_get_filename (fs));

  add_attachment (iconlist, filename);

  /* FIXME */
  /* g_free(filename); */

  gtk_widget_destroy (GTK_WIDGET (fs));
}

static void
attach_dialog_cancel (GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
attach_clicked (GtkWidget * widget, gpointer data)
{
  GtkWidget *fsw;
  GnomeIconList *iconlist;
  GtkFileSelection *fs;
  BalsaSendmsg *bsm;

  bsm = data;

  iconlist = GNOME_ICON_LIST (bsm->attachments);

  fsw = gtk_file_selection_new (_ ("Attach file"));
  gtk_object_set_user_data (GTK_OBJECT (fsw), iconlist);

  fs = GTK_FILE_SELECTION (fsw);

  gtk_signal_connect (GTK_OBJECT (fs->ok_button), "clicked",
		      (GtkSignalFunc) attach_dialog_ok,
		      fs);
  gtk_signal_connect (GTK_OBJECT (fs->cancel_button), "clicked",
		      (GtkSignalFunc) attach_dialog_cancel,
		      fs);

  gtk_widget_show (fsw);
}

static void
attachments_add (GtkWidget * widget,
		 GdkDragContext * context,
		 gint x,
		 gint y,
		 GtkSelectionData * selection_data,
		 guint info,
		 guint32 time,
		 GnomeIconList * iconlist)
{
  GList *names, *l;

  names = gnome_uri_list_extract_uris (selection_data->data);
  for (l = names; l; l = l->next)
    {
      char *name = l->data;

      add_attachment (GNOME_ICON_LIST (widget), name);
    }
  gnome_uri_list_free_strings (names);
}

static GtkWidget *
create_info_pane (BalsaSendmsg * msg, SendType type)
{
  GtkWidget *sw;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *frame;
  GtkStyle *style;

  table = gtk_table_new (6, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);

  /* To: */
  label = gtk_label_new ("To:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);

  msg->to = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->to, 1, 2, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
  gtk_container_add (GTK_CONTAINER (button),
	       gnome_stock_pixmap_widget (NULL, GNOME_STOCK_MENU_BOOK_RED));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 0, 1,
		    0, 0, 0, 0);


  /* From: */
  label = gtk_label_new ("From:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);

  msg->from = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->from, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  GTK_WIDGET_UNSET_FLAGS (msg->from, GTK_CAN_FOCUS);
  gtk_entry_set_editable (GTK_ENTRY (msg->from), FALSE);

  button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
  gtk_container_add (GTK_CONTAINER (button),
	      gnome_stock_pixmap_widget (NULL, GNOME_STOCK_MENU_BOOK_BLUE));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 1, 2,
		    0, 0, 0, 0);

  /* Subject: */
  label = gtk_label_new ("Subject:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL, 0, 0);

  msg->subject = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->subject, 1, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  /* cc: */
  label = gtk_label_new ("cc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL, 0, 0);

  msg->cc = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->cc, 1, 2, 3, 4,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
  gtk_container_add (GTK_CONTAINER (button),
	    gnome_stock_pixmap_widget (NULL, GNOME_STOCK_MENU_BOOK_YELLOW));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 3, 4,
		    0, 0, 0, 0);

  /* bcc: */
  label = gtk_label_new ("bcc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);

  msg->bcc = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), msg->bcc, 1, 2, 4, 5,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
  gtk_container_add (GTK_CONTAINER (button),
	     gnome_stock_pixmap_widget (NULL, GNOME_STOCK_MENU_BOOK_GREEN));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 4, 5,
		    0, 0, 0, 0);

  /* Attachment list */
  label = gtk_label_new ("Attachments:");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 5, 6,
		    GTK_FILL, GTK_FILL, 0, 0);

  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());
  /* create icon list */
  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  msg->attachments = gnome_icon_list_new (100, NULL, FALSE);
  gtk_signal_connect (GTK_OBJECT (msg->attachments), "drag_data_received",
		      GTK_SIGNAL_FUNC (attachments_add), NULL);
  gtk_drag_dest_set (GTK_WIDGET (msg->attachments), GTK_DEST_DEFAULT_ALL,
		     drop_types, ELEMENTS (drop_types),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

  gtk_widget_pop_visual ();
  gtk_widget_pop_colormap ();

  /* set bg of icon list to white */
  style = gtk_widget_get_style (msg->attachments);
  gdk_color_white (gdk_imlib_get_colormap (), &style->bg[GTK_STATE_NORMAL]);
  gtk_widget_set_style (msg->attachments, style);

  gtk_widget_set_usize (msg->attachments, -1, 50);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (sw), msg->attachments);
  gtk_container_add (GTK_CONTAINER (frame), sw);

  gtk_table_attach (GTK_TABLE (table), frame, 1, 3, 5, 6,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

  gtk_signal_connect (GTK_OBJECT (msg->attachments), "select_icon",
		      GTK_SIGNAL_FUNC (select_attachment),
		      NULL);

  gnome_icon_list_set_selection_mode (GNOME_ICON_LIST (msg->attachments), GTK_SELECTION_MULTIPLE);
  GTK_WIDGET_SET_FLAGS (GNOME_ICON_LIST (msg->attachments), GTK_CAN_FOCUS);

  return table;
}

static GtkWidget *
create_text_area (BalsaSendmsg * msg)
{
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;
  GdkFont *font;
  GtkStyle *style;

  style = gtk_style_new ();
  font = gdk_font_load ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
  style->font = font;

  table = gtk_table_new (2, 2, FALSE);

  msg->text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (msg->text), TRUE);
  gtk_text_set_word_wrap (GTK_TEXT (msg->text), TRUE);
  gtk_widget_set_style (msg->text, style);
  gtk_widget_set_usize (msg->text, (72 * 7) + (2 * msg->text->style->klass->xthickness) + 8, -1);
  gtk_widget_show (msg->text);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->text, 0, 1, 0, 1);
  hscrollbar = gtk_hscrollbar_new (GTK_TEXT (msg->text)->hadj);
  GTK_WIDGET_UNSET_FLAGS (hscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), hscrollbar, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

  vscrollbar = gtk_vscrollbar_new (GTK_TEXT (msg->text)->vadj);
  GTK_WIDGET_UNSET_FLAGS (vscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
		    GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  return table;
}

void
sendmsg_window_new (GtkWidget * widget, Message * message, SendType type)
{
  GtkWidget *window;
  GtkWidget *vbox;

  BalsaSendmsg *msg = NULL;


  msg = g_malloc (sizeof (BalsaSendmsg));
  switch (type)
    {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
      window = gnome_app_new ("balsa", _ ("Reply to "));
      msg->orig_message = message;
      break;

    case SEND_FORWARD:
      window = gnome_app_new ("balsa", _ ("Forward message"));
      msg->orig_message = message;
      break;

    default:
      window = gnome_app_new ("balsa", _ ("New message"));
      msg->orig_message = NULL;
      break;

    }

  msg->window = window;
  msg->type = type;

  gtk_signal_connect (GTK_OBJECT (msg->window), "delete_event",
		      GTK_SIGNAL_FUNC (gtk_false), NULL);

  vbox = gtk_vbox_new (FALSE, 1);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);
  gtk_widget_show (vbox);

/* create the top portion with the to, from, etc in it */
  gtk_box_pack_start (GTK_BOX (vbox),
		      create_info_pane (msg, type),
		      TRUE, TRUE, 0);

  /* fill in that info: */

  /* To: */
  if (type == SEND_REPLY || type == SEND_REPLY_ALL)
    {
      Address *addr = NULL;
      gchar *tmp;

      if (message->reply_to)
	addr = message->reply_to;
      else
	addr = message->from;

      tmp = address_to_gchar (addr);
      gtk_entry_set_text (GTK_ENTRY (msg->to), tmp);
      g_free (tmp);
    }

  /* From: */
  {
    gchar *from;
    from = g_strdup_printf ("%s <%s>", balsa_app.address->personal, balsa_app.address->mailbox);
    gtk_entry_set_text (GTK_ENTRY (msg->from), from);
    g_free (from);
  }

  /* Subject: */
  switch (type)
    {
    case SEND_REPLY:
    case SEND_REPLY_ALL:
      {
	gchar *tmp;

	if (!message->subject)
	  {
	    gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	    break;
	  }

	tmp = g_strdup (message->subject);

	if (strlen (tmp) < 2)
	  gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	else if (!(toupper (tmp[0]) == 'R' &&
		   toupper (tmp[1]) == 'E') &&
		 tmp[2] == ':')
	  gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Re: ");
	g_free (tmp);
      }
      break;

    case SEND_FORWARD:
      {
	gchar *tmp;

	if (!message->subject)
	  {
	    gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Fw: ");
	    break;
	  }

	tmp = g_strdup (message->subject);

	if (strlen (tmp) < 2)
	  gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Fw: ");
	else if (!(toupper (tmp[0]) == 'F' && toupper (tmp[1]) == 'W') && tmp[2] == ':')
	  gtk_entry_prepend_text (GTK_ENTRY (msg->subject), "Fw: ");

	g_free (tmp);
      }
      break;
    default:
      break;
    }


  if (type == SEND_REPLY_ALL)
    {
      gchar *tmp;

      tmp = make_string_from_list (message->to_list);
      gtk_entry_set_text (GTK_ENTRY (msg->cc), tmp);
      g_free (tmp);

      if (message->cc_list)
	{
	  gtk_entry_append_text (GTK_ENTRY (msg->cc), ", ");

	  tmp = make_string_from_list (message->cc_list);
	  gtk_entry_append_text (GTK_ENTRY (msg->cc), tmp);
	  g_free (tmp);
	}
    }

  gtk_box_pack_end (GTK_BOX (vbox),
		    create_text_area (msg),
		    TRUE, TRUE, 0);

  gnome_app_set_contents (GNOME_APP (window), vbox);

  gnome_app_create_menus_with_data (GNOME_APP (window), main_menu, msg);
  gnome_app_create_toolbar_with_data (GNOME_APP (window), main_toolbar, msg);


  gtk_text_freeze (GTK_TEXT (msg->text));
  if (type != SEND_NORMAL)
    {
      Body *body = NULL;
      GString *str = g_string_new ("\n\n");
      GString *rbdy;
      gchar *tmp;

      message_body_ref (message);

      if (message->body_list)
	{
	  body = (Body *) message->body_list->data;

	  if (message->date)
	    {
	      tmp = g_strdup_printf ("On %s ", message->date);
	      str = g_string_append (str, tmp);
	      g_free (tmp);
	    }

	  if (message->from)
	    {
	      if (message->from->personal)
		str = g_string_append (str, message->from->personal);
	      else
		str = g_string_append (str, "you");
	    }
	  else
	    str = g_string_append (str, "you");

	  str = g_string_append (str, " wrote:\n");


	  gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, str->str, strlen (str->str));


	  g_string_free (str, TRUE);

	  rbdy = content2reply (message, balsa_app.quote_str);	/* arp */
	  gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, rbdy->str, strlen (rbdy->str));
	  g_string_free (rbdy, TRUE);
	  gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, "\n\n", 2);
	}
      message_body_unref (message);
    }

  if (balsa_app.signature)
    gtk_text_insert (GTK_TEXT (msg->text), NULL, NULL, NULL, balsa_app.signature, strlen (balsa_app.signature));

  gtk_text_set_point (GTK_TEXT (msg->text), 0);
  gtk_text_thaw (GTK_TEXT (msg->text));
  /* set the toolbar so we are consistant with the rest of balsa */
  gtk_toolbar_set_style (GTK_TOOLBAR (GNOME_APP (window)->toolbar), balsa_app.toolbar_style);

  /* display the window */
  gtk_widget_show_all (window);
}

static void
send_message_cb (GtkWidget * widget, BalsaSendmsg * bsmsg)
{
  Message *message;
  Body *body;
  gchar *tmp;

  tmp = gtk_entry_get_text (GTK_ENTRY (bsmsg->to));
  {
    size_t len;
    len = strlen (tmp);

    if (len < 1)		/* empty */
      return;

    if (tmp[len - 1] == '@')	/* this shouldn't happen */
      return;

    if (len < 4)
      {
	if (strchr (tmp, '@'))	/* you won't have an @ in an
				   address less than 4 characters */
	  return;

	/* assume they are mailing it to someone in their local domain */
      }
  }

  message = message_new ();

  /* we should just copy balsa_app.address */
  message->from = address_new ();
  message->from->personal = g_strdup (balsa_app.address->personal);
  message->from->mailbox = g_strdup (balsa_app.address->mailbox);
  message->subject = g_strdup (gtk_entry_get_text (GTK_ENTRY (bsmsg->subject)));

  message->to_list = make_list_from_string (gtk_entry_get_text (GTK_ENTRY (bsmsg->to)));
  message->cc_list = make_list_from_string (gtk_entry_get_text (GTK_ENTRY (bsmsg->cc)));

  message->reply_to = address_new ();

  message->reply_to->personal = g_strdup (balsa_app.address->personal);
  message->reply_to->mailbox = g_strdup (balsa_app.replyto);


  body = body_new ();

  body->buffer = gtk_editable_get_chars (GTK_EDITABLE (bsmsg->text), 0,
			      gtk_text_get_length (GTK_TEXT (bsmsg->text)));

  message->body_list = g_list_append (message->body_list, body);

  {				/* handle attachments */
    gint i;
    Body *abody;
    for (i = 0; i < GNOME_ICON_LIST (bsmsg->attachments)->icons; i++)
      {
	abody = body_new ();
	abody->filename = g_strdup ((gchar *) gnome_icon_list_get_icon_data (GNOME_ICON_LIST (bsmsg->attachments), i));
	message->body_list = g_list_append (message->body_list, abody);
      }
  }


  if (balsa_send_message (message))
    if (bsmsg->type == SEND_REPLY || bsmsg->type == SEND_REPLY_ALL)
      {
	if (bsmsg->orig_message)
	  message_reply (bsmsg->orig_message);
      }

  g_list_free (message->body_list);
  message_free (message);
  balsa_sendmsg_destroy (bsmsg);
}
