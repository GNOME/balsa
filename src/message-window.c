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
#include "balsa-message.h"
#include "main-window.h"
#include "sendmsg-window.h"
#include "message-window.h"

#include "libbalsa.h"

/* callbacks */
static void destroy_message_window (GtkWidget * widget, gpointer data);
static void close_message_window(GtkWidget * widget, gpointer data);

static void replyto_message_cb(GtkWidget * widget, gpointer data);
static void replytoall_message_cb(GtkWidget * widget, gpointer data);
static void forward_message_cb(GtkWidget * widget, gpointer data);

static void next_part_cb(GtkWidget * widget, gpointer data);
static void previous_part_cb(GtkWidget * widget, gpointer data);
static void save_current_part_cb (GtkWidget *widget, gpointer data);

static void show_no_headers_cb(GtkWidget * widget, gpointer data);
static void show_selected_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_cb(GtkWidget * widget, gpointer data);
static void wrap_message_cb(GtkWidget *widget, gpointer data);

static void copy_cb(GtkWidget *widget, gpointer data);
static void select_all_cb(GtkWidget *widget, gpointer);

static void select_part_cb(BalsaMessage *bm, gpointer data);

/*
 * The list of messages which are being displayed.
 */
static GHashTable * displayed_messages = NULL;

static GnomeUIInfo shown_hdrs_menu[] =
{
   GNOMEUIINFO_RADIOITEM( N_ ("N_o headers"), NULL, 
			  show_no_headers_cb, NULL),
   GNOMEUIINFO_RADIOITEM( N_ ("_Selected headers"),NULL,
			  show_selected_cb, NULL),
   GNOMEUIINFO_RADIOITEM( N_ ("All _headers"), NULL, 
			  show_all_headers_cb, NULL),
   GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] =
{
  GNOMEUIINFO_MENU_CLOSE_ITEM(close_message_window, NULL),

  GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] =
{
  /* FIXME: Features to hook up... */
  /*  GNOMEUIINFO_MENU_UNDO_ITEM(NULL, NULL); */
  /*  GNOMEUIINFO_MENU_REDO_ITEM(NULL, NULL); */
  /*  GNOMEUIINFO_SEPARATOR, */
#define MENU_EDIT_COPY_POS 0
  GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define MENU_EDIT_SELECT_ALL_POS 1
  GNOMEUIINFO_MENU_SELECT_ALL_ITEM(select_all_cb, NULL),
  /*  GNOMEUINFO_SEPARATOR, */
  /*  GNOMEUIINFO_MENU_FIND_ITEM(NULL, NULL); */
  /*  GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(NULL, NULL); */
  /*  GNOMEUIINFO_MENU_REPLACE_ITEM(NULL, NULL); */
  GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] =
{
#define MENU_VIEW_WRAP_POS 0
  GNOMEUIINFO_TOGGLEITEM( N_ ("_Wrap"), NULL, wrap_message_cb, NULL),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_RADIOLIST(shown_hdrs_menu),
  GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] =
{
    /* R */
  {
    GNOME_APP_UI_ITEM, N_ ("_Reply"), N_("Reply to this message"),
    replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'R', 0, NULL
  },
    /* A */
  {
    GNOME_APP_UI_ITEM, N_ ("Reply to _all"),
    N_("Reply to all recipients of this message"),
    replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'A', 0, NULL
  },
    /* F */
  {
    GNOME_APP_UI_ITEM, N_ ("_Forward"), N_("Forward this message"),
    forward_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_FWD, 'F', 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_ ("Next Part"), N_ ("Next Part in Message"),
    next_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_FORWARD, '.', GDK_CONTROL_MASK, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_ ("Previous Part"), N_ ("Previous Part in Message"),
    previous_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_BACK, ',', GDK_CONTROL_MASK, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_ ("Save Current Part"), 
    N_ ("Save Current Part in Message"),
    save_current_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_SAVE, 's', GDK_CONTROL_MASK, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_MENU_FILE_TREE(file_menu),
  GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
  GNOMEUIINFO_MENU_VIEW_TREE(view_menu),
  GNOMEUIINFO_SUBTREE ("_Message", message_menu),  
  GNOMEUIINFO_END
};

typedef struct _MessageWindow MessageWindow;
struct _MessageWindow
  {
    GtkWidget *window;

    GtkWidget *bmessage;

    LibBalsaMessage * message;
  };

void
message_window_new (LibBalsaMessage * message)
{
  MessageWindow *mw;
  GtkWidget *scroll;

  if (!message)
    return;

  /*
   * Check to see if this message is already displayed
   */
  if (displayed_messages != NULL)
    {
      mw = (MessageWindow *) g_hash_table_lookup(displayed_messages,
						 message);
      if (mw != NULL)
	{
	  /*
	   * The message is already displayed in a window, so just use
	   * that one.
	   */
	  gdk_window_raise(GTK_WIDGET(mw->window)->window);
	  return;
	}
    }
  else
    {
      /*
       * We've never displayed a message before; initialize the hash
       * table.
       */
      displayed_messages = g_hash_table_new(g_direct_hash, g_direct_equal);
    }

  mw = g_malloc0 (sizeof (MessageWindow));

  g_hash_table_insert(displayed_messages, message, mw);

  mw->message = message;

  mw->window = gnome_app_new ("balsa", "Message");

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      GTK_SIGNAL_FUNC(destroy_message_window),
		      mw);

  gnome_app_create_menus_with_data (GNOME_APP (mw->window),
		  main_menu, mw);

  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  mw->bmessage = balsa_message_new ();

  gtk_signal_connect(GTK_OBJECT(mw->bmessage), "select-part",
		     GTK_SIGNAL_FUNC(select_part_cb), mw);

  gtk_container_add(GTK_CONTAINER(scroll), mw->bmessage);
  gtk_widget_show(scroll);

  gnome_app_set_contents (GNOME_APP (mw->window), scroll);

  if(balsa_app.shown_headers>= HEADERS_NONE && 
     balsa_app.shown_headers<= HEADERS_ALL)
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(shown_hdrs_menu[balsa_app.shown_headers].widget),
      TRUE);

  gtk_check_menu_item_set_active
    (GTK_CHECK_MENU_ITEM(view_menu[MENU_VIEW_WRAP_POS].widget), 
     balsa_app.browse_wrap);

  /* FIXME: set it to the size of the canvas, unless it is
   * bigger than the desktop, in which case it should be at about a
   * 2/3 proportional size based on the size of the desktop and the
   * height and width of the canvas.  [save and restore window size too]
   */

  gtk_window_set_default_size(GTK_WINDOW(mw->window), 400, 500);

  gtk_widget_show(mw->bmessage);
  gtk_widget_show(mw->window);

  balsa_message_set (BALSA_MESSAGE (mw->bmessage), message);
}

static void
destroy_message_window (GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *) data;

  g_hash_table_remove(displayed_messages, mw->message);
  
  gtk_widget_destroy (mw->window);
  gtk_widget_destroy (mw->bmessage);

  g_free (mw);
}

static void
replyto_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_REPLY);
}

static void
replytoall_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_REPLY_ALL);
}

static void
forward_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_FORWARD);
}

static void 
next_part_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  balsa_message_next_part(BALSA_MESSAGE(mw->bmessage));  
}

static void 
previous_part_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  balsa_message_previous_part(BALSA_MESSAGE(mw->bmessage));
}

static void 
save_current_part_cb(GtkWidget *widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  balsa_message_save_current_part(BALSA_MESSAGE(mw->bmessage));
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  gtk_widget_destroy(GTK_WIDGET(mw->window));
}


static void 
show_no_headers_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;
  
  balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage), 
				      HEADERS_NONE);
}

static void 
show_selected_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;
  
  balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage), 
				      HEADERS_SELECTED);
}

static void 
show_all_headers_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;
  
  balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage), 
				      HEADERS_ALL);
}

static void 
wrap_message_cb(GtkWidget *widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *)data;

  balsa_message_set_wrap(BALSA_MESSAGE(mw->bmessage), 
			 GTK_CHECK_MENU_ITEM(widget)->active);
}

static void 
select_part_cb(BalsaMessage *bm, gpointer data)
{
  gboolean enable;

  if ( balsa_message_can_select(bm) )
    enable = TRUE;
  else
    enable = FALSE;

  gtk_widget_set_sensitive ( edit_menu[MENU_EDIT_COPY_POS].widget, enable);
  gtk_widget_set_sensitive ( edit_menu[MENU_EDIT_SELECT_ALL_POS].widget, enable);
}


static void
copy_cb(GtkWidget *widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow*)(data);

 if ( mw->bmessage )
    balsa_message_copy_clipboard(BALSA_MESSAGE(mw->bmessage));
}

static void
select_all_cb(GtkWidget *widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow*)(data);
 
 if ( mw->bmessage )
    balsa_message_select_all(BALSA_MESSAGE(mw->bmessage));
}

