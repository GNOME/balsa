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

#define MAILBOX_DATA "mailbox_data"

static GnomeMDI *mdi = NULL;
static GtkWidget *pbar;
static guint pbar_timeout;

static gint about_box_visible = FALSE;

/* main window widget components */
static gint progress_timeout (gpointer data);

static void app_created (GnomeMDI *, GnomeApp * app);
static void mblist_open_window (GnomeMDI * mdi);

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

static void mblist_window_cb (GtkWidget * widget, gpointer data);
static void mailbox_close_child (GtkWidget * widget, gpointer data);

static void about_box_destroy_cb (void);

static void destroy_window_cb (GnomeMDI * mdi, gpointer data);

static void set_icon (GnomeApp * app);

static GnomeUIInfo file_menu[] =
{
    /* Ctrl-M */
  {
 GNOME_APP_UI_ITEM, N_ ("_Get new mail"), NULL, check_new_messages_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL_RCV, 'M', GDK_CONTROL_MASK, NULL
  },
  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_ ("E_xit"), NULL, close_main_window, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_EXIT, 'Q', 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] =
{
    /* M */
  {
    GNOME_APP_UI_ITEM, N_ ("_New"), NULL, new_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL, 'M', 0, NULL
  },
    /* R */
  {
    GNOME_APP_UI_ITEM, N_ ("_Reply"), NULL, replyto_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL_RPL, 'R', 0, NULL
  },
    /* A */
  {
 GNOME_APP_UI_ITEM, N_ ("Reply to _all"), NULL, replytoall_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL_RPL, 'A', 0, NULL
  },
    /* F */
  {
    GNOME_APP_UI_ITEM, N_ ("_Forward"), NULL, forward_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_MAIL_FWD, 'F', 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
    /* D */
  {
    GNOME_APP_UI_ITEM, N_ ("_Delete"), NULL, delete_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_TRASH, 'D', 0, NULL
  },
    /* U */
  {
    GNOME_APP_UI_ITEM, N_ ("_Undelete"), NULL, undelete_message_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_UNDELETE, 'U', 0, NULL
  },
  GNOMEUIINFO_END
};
static GnomeUIInfo mailbox_menu[] =
{
    /* C */
#if 0
  {
    GNOME_APP_UI_ITEM, N_ ("List"), NULL, mblist_window_cb, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_PROP, 'C', 0, NULL
  },
#endif
  GNOMEUIINFO_ITEM_STOCK ("Close", NULL, mailbox_close_child, GNOME_STOCK_MENU_CLOSE),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_END
};
static GnomeUIInfo settings_menu[] =
{
  GNOMEUIINFO_ITEM_STOCK ("_Filters...", NULL, filter_dlg_cb, GNOME_STOCK_MENU_PROP),
  GNOMEUIINFO_ITEM_STOCK ("_Preferences...", NULL, open_preferences_manager, GNOME_STOCK_MENU_PROP),
  GNOMEUIINFO_END
};
static GnomeUIInfo help_menu[] =
{
  GNOMEUIINFO_ITEM_STOCK ("_About...", NULL, show_about_box, GNOME_STOCK_MENU_ABOUT),
  GNOMEUIINFO_HELP ("balsa"),
  GNOMEUIINFO_END
};
static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_SUBTREE ("_File", file_menu),
  GNOMEUIINFO_SUBTREE ("_Message", message_menu),
  GNOMEUIINFO_SUBTREE ("Mail_boxes", mailbox_menu),
  GNOMEUIINFO_SUBTREE ("_Settings", settings_menu),
  GNOMEUIINFO_SUBTREE ("_Help", help_menu),
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
  GList *list;
  GtkWidget *widget;
  GdkCursor *cursor;

  if (mdi->windows == NULL)
    return;


  for (list = mdi->windows; list; list = list->next)
    {
      widget = GTK_WIDGET (GNOME_APP (list->data));
      if (type == -1)
	{
	  gtk_widget_set_sensitive (pbar, FALSE);
	  gtk_progress_set_activity_mode (GTK_PROGRESS (pbar), FALSE);
	  gtk_timeout_remove (pbar_timeout);
	  gtk_progress_set_value (GTK_PROGRESS (pbar), 0.0);
	  gdk_window_set_cursor (widget->window, NULL);
	}
      else
	{
	  gtk_widget_set_sensitive (pbar, TRUE);
	  gtk_progress_set_activity_mode (GTK_PROGRESS (pbar), TRUE);
	  pbar_timeout = gtk_timeout_add (50, progress_timeout, pbar);
	  cursor = gdk_cursor_new (type);
	  gdk_window_set_cursor (widget->window, cursor);
	  gdk_cursor_destroy (cursor);
	}
    }
}

static void
destroy_window_cb (GnomeMDI * mdi, gpointer data)
{
  balsa_app.mw_width = GTK_WIDGET (mdi->active_window)->allocation.width;
  balsa_app.mw_height = GTK_WIDGET (mdi->active_window)->allocation.height;
  balsa_exit ();
}

void
main_window_init (void)
{
  /* main window */
  mdi = GNOME_MDI (gnome_mdi_new ("balsa", "Balsa"));

  gtk_signal_connect (GTK_OBJECT (mdi),
		      "destroy",
		      (GtkSignalFunc) destroy_window_cb,
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
}

static gint
progress_timeout (gpointer data)
{
  gfloat new_val;
  GtkAdjustment *adj;

  adj = GTK_PROGRESS (data)->adjustment;

  new_val = adj->value + 1;
  if (new_val > adj->upper)
    new_val = adj->lower;

  gtk_progress_set_value (GTK_PROGRESS (data), new_val);

  return TRUE;
}


static void
app_created (GnomeMDI * mdi, GnomeApp * app)
{
  GtkWidget *statusbar;

  /* we can only set icon after realization, as we have no windows before. */
  gtk_signal_connect (GTK_OBJECT (app), "realize",
		      GTK_SIGNAL_FUNC (set_icon), NULL);

  statusbar = gtk_statusbar_new ();
  pbar = gtk_progress_bar_new ();
  gtk_progress_bar_set_activity_step (GTK_PROGRESS_BAR (pbar), 5);
  gtk_progress_bar_set_activity_blocks (GTK_PROGRESS_BAR (pbar), 5);

  gtk_progress_set_activity_mode (GTK_PROGRESS (pbar), FALSE);
  gtk_progress_set_value (GTK_PROGRESS (pbar), 0.0);

  gtk_widget_set_usize (pbar, 100, -1);
  gtk_box_pack_start (GTK_BOX (statusbar), pbar, FALSE, FALSE, 5);
  gtk_widget_show (pbar);

  gnome_app_set_statusbar (app, statusbar);

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
  /*
   * set the toolbar style
   */
  gtk_toolbar_set_style (GTK_TOOLBAR (GNOME_APP (mdi->active_window)->toolbar), balsa_app.toolbar_style);
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
			   "Copyright (C) 1997-98",
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
  if (!balsa_app.current_index_child)
    return;

  check_all_pop3_hosts (balsa_app.inbox, balsa_app.inbox_input);

  mailbox_check_new_messages (BALSA_INDEX (balsa_app.current_index_child->index)->mailbox);
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








/* mailbox list code */


typedef struct _MBListWindow MBListWindow;
struct _MBListWindow
  {
    GnomeMDI *mdi;
    GtkWidget *sw;
    GtkCTree *ctree;
    GtkCTreeNode *parent;
  };

enum
  {
    TARGET_MESSAGE,
  };

static GtkTargetEntry drag_types[] =
{
  {"x-application-gnome/balsa", 0, TARGET_MESSAGE}
};
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))


static MBListWindow *mblw = NULL;

/* callbacks */
void mblist_close_mailbox (Mailbox * mailbox);
static void mailbox_select_cb (BalsaMBList *, Mailbox *, GtkCTreeNode *, GdkEventButton *);
static GtkWidget *mblist_create_menu (GtkCTree * ctree, Mailbox * mailbox);
static void mblist_drag_data_received (GtkWidget * widget, GdkDragContext * context, gint x, gint y, GtkSelectionData * selection_data, guint info, guint32 time, gpointer data);


static void mblist_open_cb (GtkWidget *, gpointer);
static void mblist_close_cb (GtkWidget *, gpointer);

static void
mblist_open_window (GnomeMDI * mdi)
{
  GtkWidget *dock_item;
  GtkWidget *bbox;
  GtkWidget *button;
  gint height;

  GnomeApp *app = GNOME_APP (mdi->active_window);

  mblw = g_malloc0 (sizeof (MBListWindow));

  mblw->mdi = mdi;

  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());

  dock_item = gnome_dock_item_new (GNOME_DOCK_ITEM_BEH_NEVER_HORIZONTAL);
  gnome_dock_item_set_shadow_type (GNOME_DOCK_ITEM (dock_item), GTK_SHADOW_NONE);


  mblw->sw = gtk_scrolled_window_new (NULL, NULL);
  mblw->ctree = GTK_CTREE (balsa_mblist_new ());
  balsa_app.mblist = BALSA_MBLIST (mblw->ctree);
  gtk_container_add (GTK_CONTAINER (mblw->sw), GTK_WIDGET (mblw->ctree));
  gtk_container_add (GTK_CONTAINER (dock_item), GTK_WIDGET (mblw->sw));

  gnome_dock_add_item (GNOME_DOCK (app->dock), dock_item, GNOME_DOCK_POS_LEFT, 0, 0, 0, TRUE);
  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_widget_set_usize (GTK_WIDGET (mblw->ctree), balsa_app.mblist_width, -1);
/*
   gtk_ctree_show_stub (mblw->ctree, FALSE);
 */
  gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (mblw->sw),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  gtk_clist_set_row_height (GTK_CLIST (mblw->ctree), 16);

  gtk_widget_show (GTK_WIDGET (mblw->sw));
  gtk_widget_show (GTK_WIDGET (mblw->ctree));
  gtk_widget_show (dock_item);

  balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));

  height = GTK_CLIST (mblw->ctree)->rows * GTK_CLIST (mblw->ctree)->row_height;

  gtk_drag_dest_set (GTK_WIDGET (mblw->ctree), GTK_DEST_DEFAULT_ALL,
		     drag_types, ELEMENTS (drag_types),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);


  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "select_mailbox",
		      GTK_SIGNAL_FUNC (mailbox_select_cb), NULL);



  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "drag_data_received",
		      GTK_SIGNAL_FUNC (mblist_drag_data_received), NULL);
#if 0
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mblw->window)->action_area),
		      bbox, TRUE, TRUE, 0);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH / 2,
				 BALSA_BUTTON_HEIGHT / 2);
  gtk_widget_show (bbox);

  button = gtk_button_new_with_label ("Open box");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     GTK_SIGNAL_FUNC (mblist_open_cb), NULL);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Close box");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (mblist_close_cb), NULL);
  gtk_widget_show (button);
#endif
}

static void
mblist_open_cb (GtkWidget * widget, gpointer data)
{
  IndexChild *index_child;
  GtkCTreeNode *ctnode;
  Mailbox *mailbox;

  if (!GTK_CLIST (mblw->ctree)->selection)
    return;

  ctnode = GTK_CLIST (mblw->ctree)->selection->data;
  mailbox = gtk_ctree_node_get_row_data (mblw->ctree, ctnode);
  if (!mailbox)
    return;

  index_child = index_child_new (mblw->mdi, mailbox);
  if (index_child)
    {
      gnome_mdi_add_child (mblw->mdi, GNOME_MDI_CHILD (index_child));
      gnome_mdi_add_view (mblw->mdi, GNOME_MDI_CHILD (index_child));
    }
  main_window_set_cursor (-1);
}

static void
mblist_close_cb (GtkWidget * widget, gpointer data)
{
  GtkCTreeNode *ctnode;
  Mailbox *mailbox;

  if (!GTK_CLIST (mblw->ctree)->selection)
    return;

  ctnode = GTK_CLIST (mblw->ctree)->selection->data;
  mailbox = gtk_ctree_node_get_row_data (mblw->ctree, ctnode);

  mblist_close_mailbox (mailbox);
}

void
mblist_close_mailbox (Mailbox * mailbox)
{
  GnomeMDIChild *child;

  if (!mblw)
    return;

  if (mailbox)
    {
      child = gnome_mdi_find_child (mblw->mdi, mailbox->name);
      if (child)
	{
	  mailbox_watcher_remove (mailbox, BALSA_INDEX (INDEX_CHILD (child)->index)->watcher_id);
	  gnome_mdi_remove_child (mblw->mdi, child, TRUE);
	}
    }
}

static void
mblist_drag_data_received (GtkWidget * widget,
			   GdkDragContext * context,
			   gint x,
			   gint y,
			   GtkSelectionData * selection_data,
			   guint info,
			   guint32 time,
			   gpointer data)
{
  GtkCList *clist = GTK_CLIST (widget);
  gint row;

  if (!gtk_clist_get_selection_info (clist, x, y, &row, NULL))
    return;

  /* FIXME what now... ? */
}

static void
mailbox_select_cb (BalsaMBList * bmbl, Mailbox * mailbox, GtkCTreeNode * row, GdkEventButton * event)
{
  IndexChild *index_child;

  if (!mblw)
    return;

  if (event && event->button == 1 && event->type == GDK_2BUTTON_PRESS)
    {

      index_child = index_child_new (mblw->mdi, mailbox);
      if (index_child)
	{
	  gnome_mdi_add_child (mblw->mdi, GNOME_MDI_CHILD (index_child));
	  gnome_mdi_add_view (mblw->mdi, GNOME_MDI_CHILD (index_child));
	}

      main_window_set_cursor (-1);

      if (!strcmp (mailbox->name, "Inbox") ||
	  !strcmp (mailbox->name, "Outbox") ||
	  !strcmp (mailbox->name, "Trash"))
	return;

      gtk_ctree_set_node_info (GTK_CTREE (bmbl),
			       row,
			       mailbox->name, 5,
			       NULL, NULL,
			       balsa_icon_get_pixmap (BALSA_ICON_TRAY_EMPTY),
			       balsa_icon_get_bitmap (BALSA_ICON_TRAY_EMPTY),
			       FALSE, TRUE);

      gtk_ctree_node_set_row_style (GTK_CTREE (bmbl), row, NULL);
    }

  if (event && event->button == 3)
    {
      gtk_menu_popup (GTK_MENU (mblist_create_menu (GTK_CTREE (bmbl), mailbox)), NULL, NULL, NULL, NULL, event->button, event->time);
    }
}

static void
mb_conf_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox, FALSE, MAILBOX_UNKNOWN);
}

static void
mb_add_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox, TRUE, MAILBOX_UNKNOWN);
}

static void
mb_del_cb (GtkWidget * wifget, Mailbox * mailbox)
{
  if (mailbox->type == MAILBOX_UNKNOWN)
    return;
  mailbox_conf_delete (mailbox);
}


static GtkWidget *
mblist_create_menu (GtkCTree * ctree, Mailbox * mailbox)
{
  GtkWidget *menu, *menuitem;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Add Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_add_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_ ("Edit Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_conf_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_ ("Delete Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_del_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}
