/* Balsa E-Mail Client
 * Copyright (C) 1997-99 Jay Painter and Stuart Parmenter
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

#include <string.h>
#include <gnome.h>
#include <gdk/gdkx.h>
#include <X11/Xutil.h>


#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "filter.h"
#include "index-child.h"
#include "mailbox.h"
#include "misc.h"
#include "main.h"
#include "main-window.h"
#include "message-window.h"
#include "pref-manager.h"
#include "sendmsg-window.h"
#include "mailbox-conf.h"
#include "mblist-window.h"
#define MAILBOX_DATA "mailbox_data"

#define APPBAR_KEY "balsa_appbar"

GnomeMDI *mdi = NULL;
static guint pbar_timeout;

static gint about_box_visible = FALSE;

/* main window widget components */
static gint progress_timeout (gpointer data);

static void app_created (GnomeMDI *, GnomeApp * app);

/* dialogs */
static void show_about_box (void);


/* callbacks */
static void check_new_messages_cb (GtkWidget *, gpointer data);

static void new_message_cb (GtkWidget * widget, gpointer data);
static void replyto_message_cb (GtkWidget * widget, gpointer data);
static void replytoall_message_cb (GtkWidget * widget, gpointer data);
static void forward_message_cb (GtkWidget * widget, gpointer data);

static void next_message_cb (GtkWidget * widget, gpointer data);
static void previous_message_cb (GtkWidget * widget, gpointer data);

static void delete_message_cb (GtkWidget * widget, gpointer data);
static void undelete_message_cb (GtkWidget * widget, gpointer data);

static void filter_dlg_cb (GtkWidget * widget, gpointer data);

static void mailbox_close_child (GtkWidget * widget, gpointer data);
static void mailbox_commit_changes (GtkWidget * widget, gpointer data);
static void about_box_destroy_cb (void);

static void destroy_mdi_cb (GnomeMDI * mdi, gpointer data);
static void destroy_window_cb (GtkObject * object);

static void set_icon (GnomeApp * app);

static GnomeUIInfo file_menu[] =
{
    /* Ctrl-M */
  {
 GNOME_APP_UI_ITEM, N_ ("_Get new mail"), N_("Fetch new incoming mail"),
 check_new_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
 GNOME_STOCK_MENU_MAIL_RCV, 'M', GDK_CONTROL_MASK, NULL
  },


  GNOMEUIINFO_SEPARATOR,

  GNOMEUIINFO_MENU_EXIT_ITEM(close_main_window, NULL), 

  GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] =
{
    /* M */
  {
    GNOME_APP_UI_ITEM, N_ ("_New"), N_("Compose a new message"),
    new_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL, 'M', 0, NULL
  },
    /* R */
  {
    GNOME_APP_UI_ITEM, N_ ("_Reply"), N_("Reply to the current message"),
    replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'R', 0, NULL
  },
    /* A */
  {
    GNOME_APP_UI_ITEM, N_ ("Reply to _all"),
    N_("Reply to all recipients of the current message"),
    replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'A', 0, NULL
  },
    /* F */
  {
    GNOME_APP_UI_ITEM, N_ ("_Forward"), N_("Forward the current message"),
    forward_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_FWD, 'F', 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
    /* D */
  {
    GNOME_APP_UI_ITEM, N_ ("_Delete"), N_("Delete the current message"),
    delete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_TRASH, 'D', 0, NULL
  },
    /* U */
  {
    GNOME_APP_UI_ITEM, N_ ("_Undelete"), N_("Undelete the message"),
    undelete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_UNDELETE, 'U', 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo mailbox_menu[] =
{
    /* C */
#if 0
  {
    GNOME_APP_UI_ITEM, N_ ("List"), NULL, mblist_window_cb, NULL,
    NULL, GNOME_APP_PIXMAP_NONE, GNOME_STOCK_MENU_PROP, 'C', 0, NULL
  },
#endif
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Add"), N_("Add a new mailbox"),
			  mblist_menu_add_cb, GNOME_STOCK_MENU_PROP),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Edit"), N_("Edit the selected mailbox"),
			  mblist_menu_edit_cb, GNOME_STOCK_MENU_PROP),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Delete"), N_("Delete the selected mailbox"),
			  mblist_menu_delete_cb, GNOME_STOCK_MENU_TRASH),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Close current"), N_("Close the currently opened mailbox"),
			  mailbox_close_child, GNOME_STOCK_MENU_CLOSE),

  GNOMEUIINFO_ITEM_STOCK (N_ ("Commit current"), N_("Commit the changes in the currently opened mailbox"),
			  mailbox_commit_changes, GNOME_STOCK_MENU_CLOSE),

  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_END
};
static GnomeUIInfo settings_menu[] =
{
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Filters..."), N_("Manage filters"),
			  filter_dlg_cb, GNOME_STOCK_MENU_PROP),

  GNOMEUIINFO_MENU_PREFERENCES_ITEM(open_preferences_manager, NULL),

  GNOMEUIINFO_END
};
static GnomeUIInfo help_menu[] =
{
  GNOMEUIINFO_HELP ("balsa"),

  GNOMEUIINFO_MENU_ABOUT_ITEM(show_about_box, NULL),

  GNOMEUIINFO_END
};
static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_MENU_FILE_TREE(file_menu),
  GNOMEUIINFO_SUBTREE (N_("_Message"), message_menu),
  GNOMEUIINFO_SUBTREE (N_("Mail_boxes"), mailbox_menu),
  GNOMEUIINFO_MENU_SETTINGS_TREE(settings_menu),
  GNOMEUIINFO_MENU_HELP_TREE(help_menu),
  GNOMEUIINFO_END
};

static GnomeUIInfo main_toolbar[] =
{
  GNOMEUIINFO_ITEM_STOCK (N_ ("Check"), N_ ("Check Email"), check_new_messages_cb, GNOME_STOCK_PIXMAP_MAIL_RCV),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Delete"), N_ ("Delete Message"), delete_message_cb, GNOME_STOCK_PIXMAP_TRASH),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Compose"), N_ ("Compose Message"), new_message_cb, GNOME_STOCK_PIXMAP_MAIL_NEW),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Reply"), N_ ("Reply"), replyto_message_cb, GNOME_STOCK_PIXMAP_MAIL_RPL),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Reply to all"), N_ ("Reply to all"), replytoall_message_cb, GNOME_STOCK_PIXMAP_MAIL_RPL),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Forward"), N_ ("Forward"), forward_message_cb, GNOME_STOCK_PIXMAP_MAIL_FWD),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Previous"), N_ ("Open Previous Message"), previous_message_cb, GNOME_STOCK_PIXMAP_BACK),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Next"), N_ ("Open Next Message"), next_message_cb, GNOME_STOCK_PIXMAP_FORWARD),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Print"), N_ ("Print current message"), NULL, GNOME_STOCK_PIXMAP_PRINT),
  GNOMEUIINFO_END
};

void
main_window_set_cursor (gint type)
{
  GnomeAppBar *appbar;
  GList *list;
  GtkWidget *widget;
  GdkCursor *cursor;
  GtkProgress *pbar;

  if (mdi->windows == NULL)
    return;


  for (list = mdi->windows; list; list = list->next)
    {
      widget = GTK_WIDGET (GNOME_APP (list->data));
      appbar = GNOME_APPBAR (gtk_object_get_data (GTK_OBJECT(widget),
						  APPBAR_KEY));
      pbar = gnome_appbar_get_progress(appbar);
      
      if (type == -1)
	{
	  gtk_widget_set_sensitive (GTK_WIDGET (pbar), FALSE);
          gtk_progress_set_activity_mode (GTK_PROGRESS (pbar), FALSE);
          gtk_timeout_remove (pbar_timeout);
          gtk_progress_set_value (GTK_PROGRESS (pbar), 0.0);
	  gdk_window_set_cursor (widget->window, NULL);
	}
      else
	{
	  gtk_widget_set_sensitive (GTK_WIDGET (pbar), TRUE);
          gtk_progress_set_activity_mode (GTK_PROGRESS (pbar), TRUE);
          pbar_timeout = gtk_timeout_add (50, progress_timeout, pbar);
	  cursor = gdk_cursor_new (type);
	  gdk_window_set_cursor (widget->window, cursor);
	  gdk_cursor_destroy (cursor);
	}
    }
}

static void
destroy_mdi_cb (GnomeMDI * mdi, gpointer data)
{
  balsa_exit ();
}

static void
destroy_window_cb (GtkObject *object)
{
  balsa_app.mw_width = GTK_WIDGET (object)->allocation.width;
  balsa_app.mw_height = GTK_WIDGET (object)->allocation.height;  
}

void
main_window_init (void)
{
  /* main window */
  mdi = GNOME_MDI (gnome_mdi_new ("balsa", "Balsa"));

  gtk_signal_connect (GTK_OBJECT (mdi),
		      "destroy",
		      (GtkSignalFunc) destroy_mdi_cb,
		      NULL);

  /* meubar and toolbar */
  gtk_signal_connect (GTK_OBJECT (mdi), "child_changed", GTK_SIGNAL_FUNC (index_child_changed), NULL);
  gtk_signal_connect (GTK_OBJECT (mdi), "app_created", GTK_SIGNAL_FUNC (app_created), NULL);
  gnome_mdi_set_child_list_path (mdi, _ ("Mailboxes/<Separator>"));

  gnome_mdi_set_menubar_template (mdi, main_menu);
  gnome_mdi_set_toolbar_template (mdi, main_toolbar);

  /* we are forcing notebook mode. */
  gnome_mdi_set_mode (mdi, GNOME_MDI_NOTEBOOK);
  gnome_mdi_open_toplevel (mdi);

  gnome_app_install_menu_hints(mdi->active_window,
		       gnome_mdi_get_menubar_info(mdi->active_window));
}

static gint
progress_timeout (gpointer data)
{
  GtkProgress *pbar;
  GtkAdjustment *adj;
  gfloat new_val;

  pbar = GTK_PROGRESS(data);
  adj = pbar->adjustment;

  new_val = adj->value + 1;
  if (new_val > adj->upper)
    new_val = adj->lower;

  gtk_progress_set_value (GTK_PROGRESS (data), new_val);

  return TRUE;
}


static void
app_created (GnomeMDI * mdi, GnomeApp * app)
{
  GnomeAppBar *appbar;

  /* we can only set icon after realization, as we have no windows before. */
  gtk_signal_connect (GTK_OBJECT (app), "realize",
		      GTK_SIGNAL_FUNC (set_icon), NULL);
  gtk_signal_connect (GTK_OBJECT (app), "destroy",
		      GTK_SIGNAL_FUNC (destroy_window_cb), NULL);

  appbar = GNOME_APPBAR(gnome_appbar_new(TRUE, TRUE, GNOME_PREFERENCES_USER));

  gnome_app_set_statusbar (app, GTK_WIDGET (appbar));

  gtk_object_set_data (GTK_OBJECT (app), APPBAR_KEY, appbar);

  gtk_window_set_policy (GTK_WINDOW (app), TRUE, TRUE, FALSE);
  gtk_widget_set_usize (GTK_WIDGET (app), balsa_app.mw_width, balsa_app.mw_height);

  mblist_open_window (mdi);

  refresh_main_window ();

}

/*
 * close the main window 
 */
void
close_main_window (void)
{
  if (gnome_mdi_remove_all (mdi, FALSE))
    gtk_object_destroy (GTK_OBJECT (mdi));
  mdi = NULL;
}


/*
 * refresh data in the main window
 */
void
refresh_main_window (void)
{
  GnomeDockItem *item;
  GtkWidget *toolbar;

  /*
   * set the toolbar style
   */
  item = gnome_app_get_dock_item_by_name (GNOME_APP (mdi->active_window),
					  GNOME_APP_TOOLBAR_NAME);
  toolbar = gnome_dock_item_get_child (item);

  gtk_toolbar_set_style (GTK_TOOLBAR (toolbar), balsa_app.toolbar_style);
}

/*
 * show the about box for Balsa
 */
static void
show_about_box (void)
{
  GtkWidget *about;
  const gchar *authors[] =
  {
    "Stuart Parmenter <pavlov@pavlov.net>",
    "Jay Painter <jpaint@gimp.org>",
    NULL
  };
  gchar *logo;


  /* only show one about box at a time */
  if (about_box_visible)
    return;
  else
    about_box_visible = TRUE;

  logo = gnome_unconditional_pixmap_file ("balsa/balsa_logo.png");
  about = gnome_about_new ("Balsa",
			   BALSA_VERSION,
			   "Copyright (C) 1997-99",
			   authors,
			   _ ("The Balsa email client is part of the GNOME desktop environment.  Information on Balsa can be found at http://www.balsa.net/\n\nIf you need to report bugs, please do so at: http://www.gnome.org/cgi-bin/bugs"),
			   logo);
  g_free (logo);

  gtk_signal_connect (GTK_OBJECT (about),
		      "destroy",
		      (GtkSignalFunc) about_box_destroy_cb,
		      NULL);

  gtk_widget_show (about);
}


/*
 * Callbacks
 */

static void
check_new_messages_cb (GtkWidget * widget, gpointer data)
{
  if (balsa_app.current_index_child != NULL)
    mailbox_check_new_messages (BALSA_INDEX (balsa_app.current_index_child->index)->mailbox);

  check_all_pop3_hosts (balsa_app.inbox, balsa_app.inbox_input);
  check_all_imap_hosts (balsa_app.inbox, balsa_app.inbox_input);
}

static void
new_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  sendmsg_window_new (widget, NULL, SEND_NORMAL);
}


static void
replyto_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (balsa_app.current_index_child->index);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      sendmsg_window_new (widget, message, SEND_REPLY);
      list = list->next;
    }
}

static void
replytoall_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (balsa_app.current_index_child->index);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      sendmsg_window_new (widget, message, SEND_REPLY_ALL);
      list = list->next;
    }
}


static void
forward_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (balsa_app.current_index_child->index);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      sendmsg_window_new (widget, message, SEND_FORWARD);
      list = list->next;
    }
}


static void
next_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
previous_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  balsa_index_select_previous (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
delete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (balsa_app.current_index_child->index);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      message_delete (message);
      list = list->next;
    }

  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
undelete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (balsa_app.current_index_child->index);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      message_undelete (message);
      list = list->next;
    }
  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}

static void
filter_dlg_cb (GtkWidget * widget, gpointer data)
{
  filter_edit_dialog (NULL);
}

static void
mblist_window_cb (GtkWidget * widget, gpointer data)
{
  mblist_open_window (mdi);
}

static void
mailbox_close_child (GtkWidget * widget, gpointer data)
{
  if (balsa_app.current_index_child)
    gnome_mdi_remove_child (mdi,
			    GNOME_MDI_CHILD (balsa_app.current_index_child),
			    TRUE);
}

static void
mailbox_commit_changes (GtkWidget * widget, gpointer data)
{
  Mailbox *current_mailbox;
  
  if (!balsa_app.current_index_child)
    return;
  current_mailbox =  balsa_app.current_index_child->mailbox;
  mailbox_commit_flagged_changes( current_mailbox );
}


static void
about_box_destroy_cb (void)
{
  about_box_visible = FALSE;
}

static void
set_icon (GnomeApp * app)
{
  GdkImlibImage *im = NULL;
  GdkWindow *ic_win, *w;
  GdkWindowAttr att;
  XIconSize *is;
  gint i, count, j;
  GdkPixmap *pmap, *mask;

  w = GTK_WIDGET (app)->window;

  if ((XGetIconSizes (GDK_DISPLAY (), GDK_ROOT_WINDOW (), &is, &count)) &&
      (count > 0))
    {
      i = 0;			/* use first icon size - not much point using the others */
      att.width = is[i].max_width;
      att.height = is[i].max_height;
      /*
       * raster had:
       * att.height = 3 * att.width / 4;
       * but this didn't work  (it scaled the icons incorrectly
       */

      /* make sure the icon is inside the min and max sizes */
      if (att.height < is[i].min_height)
	att.height = is[i].min_height;
      if (att.height > is[i].max_height)
	att.height = is[i].max_height;
      if (is[i].width_inc > 0)
	{
	  j = ((att.width - is[i].min_width) / is[i].width_inc);
	  att.width = is[i].min_width + (j * is[i].width_inc);
	}
      if (is[i].height_inc > 0)
	{
	  j = ((att.height - is[i].min_height) / is[i].height_inc);
	  att.height = is[i].min_height + (j * is[i].height_inc);
	}
      XFree (is);
    }
  else
    /* no icon size hints at all? ok - invent our own size */
    {
      att.width = 32;
      att.height = 24;
    }
  att.wclass = GDK_INPUT_OUTPUT;
  att.window_type = GDK_WINDOW_TOPLEVEL;
  att.x = 0;
  att.y = 0;
  att.visual = gdk_imlib_get_visual ();
  att.colormap = gdk_imlib_get_colormap ();
  ic_win = gdk_window_new (NULL, &att, GDK_WA_VISUAL | GDK_WA_COLORMAP);
  im = gdk_imlib_load_image (gnome_unconditional_pixmap_file ("balsa/balsa_icon.png"));
  gdk_window_set_icon (w, ic_win, NULL, NULL);
  gdk_imlib_render (im, att.width, att.height);
  pmap = gdk_imlib_move_image (im);
  mask = gdk_imlib_move_mask (im);
  gdk_window_set_back_pixmap (ic_win, pmap, FALSE);
  gdk_window_clear (ic_win);
  gdk_window_shape_combine_mask (ic_win, mask, 0, 0);
  gdk_imlib_free_pixmap (pmap);
  gdk_imlib_destroy_image (im);
}



