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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "filter.h"
#include "balsa-index-page.h"
#include "misc.h"
#include "main.h"
#include "message-window.h"
#include "pref-manager.h"
#include "sendmsg-window.h"
#include "mailbox-conf.h"
#include "mblist-window.h"
#include "main-window.h"
#include "print.h"
#include "address-book.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#ifdef BALSA_USE_EXPERIMENTAL_INIT
#include "libinit_balsa/init_balsa.h"
#endif

#define MAILBOX_DATA "mailbox_data"

#define APPBAR_KEY "balsa_appbar"

enum {
  SET_CURSOR,
  OPEN_MAILBOX,
  CLOSE_MAILBOX,
  LAST_SIGNAL
};

#ifdef BALSA_USE_THREADS
/* Define thread-related globals, including dialogs */
  GtkWidget *progress_dialog = NULL;
  GtkWidget *progress_dialog_source = NULL;
  GtkWidget *progress_dialog_message = NULL;

extern void load_messages (Mailbox * mailbox, gint emit);

void progress_dialog_destroy_cb ( GtkWidget *, gpointer data);
#endif

static void balsa_window_class_init(BalsaWindowClass *klass);
static void balsa_window_init(BalsaWindow *window);
static void balsa_window_real_set_cursor(BalsaWindow *window, GdkCursor *cursor);
static void balsa_window_real_open_mailbox(BalsaWindow *window, Mailbox *mailbox);
static void balsa_window_real_close_mailbox(BalsaWindow *window, Mailbox *mailbox);
static void balsa_window_destroy(GtkObject * object);
void check_messages_thread( Mailbox *mbox );

static GtkWidget *balsa_window_create_preview_pane(BalsaWindow *window);
GtkWidget *balsa_window_find_current_index(BalsaWindow *window);
void       balsa_window_open_mailbox( BalsaWindow *window, Mailbox *mailbox );
void       balsa_window_close_mailbox( BalsaWindow *window, Mailbox *mailbox );

/*FIXME unused
static guint pbar_timeout;
*/
static gint about_box_visible = FALSE;

/* FIXME unused main window widget components
static gint progress_timeout (gpointer data);
*/


/* dialogs */
static void show_about_box (void);

/* callbacks */
static void check_new_messages_cb (GtkWidget *, gpointer data);

static void new_message_cb (GtkWidget * widget, gpointer data);
static void replyto_message_cb (GtkWidget * widget, gpointer data);
static void replytoall_message_cb (GtkWidget * widget, gpointer data);
static void forward_message_cb (GtkWidget * widget, gpointer data);
static void continue_message_cb (GtkWidget * widget, gpointer data);

static void next_message_cb (GtkWidget * widget, gpointer data);
static void previous_message_cb (GtkWidget * widget, gpointer data);

static void delete_message_cb (GtkWidget * widget, gpointer data);
static void undelete_message_cb (GtkWidget * widget, gpointer data);

static void filter_dlg_cb (GtkWidget * widget, gpointer data);

static void mailbox_close_child (GtkWidget * widget, gpointer data);
static void mailbox_commit_changes (GtkWidget * widget, gpointer data);
static void about_box_destroy_cb (void);

static void set_icon (GnomeApp * app);

static void notebook_size_alloc_cb( GtkWidget *notebook, GtkAllocation *alloc );
static void mw_size_alloc_cb( GtkWidget *window, GtkAllocation *alloc );

static GnomeUIInfo file_menu[] =
{
    /* Ctrl-M */
  {
 GNOME_APP_UI_ITEM, N_ ("_Get new mail"), N_("Fetch new incoming mail"),
 check_new_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
 GNOME_STOCK_MENU_MAIL_RCV, 'M', GDK_CONTROL_MASK, NULL
  },
  GNOMEUIINFO_SEPARATOR,

  /* Ctrl-I */
  {
    GNOME_APP_UI_ITEM, N_ ("Pr_int"), N_("Print current mail"),
    file_print_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RCV, 'I', GDK_CONTROL_MASK, NULL
  },
  GNOMEUIINFO_SEPARATOR,

  #ifdef BALSA_USE_EXPERIMENTAL_INIT
  {
      GNOME_APP_UI_ITEM, "Test new init", "Test the new initialization druid",
      balsa_init_funky_new_init_is_much_cooler, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      GNOME_STOCK_MENU_MAIL_RCV, '\0', GDK_CONTROL_MASK, NULL
  },  
  GNOMEUIINFO_SEPARATOR,
  #endif

  // XXX 
  // GNOMEUIINFO_MENU_EXIT_ITEM(close_main_window, NULL), 
  GNOMEUIINFO_MENU_EXIT_ITEM(balsa_exit, NULL),

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
    /* C */
  {
    GNOME_APP_UI_ITEM, N_ ("_Continue"), N_("Continue editing current message"),
    continue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL, 'C', 0, NULL
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

  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_ ("Address Book"), N_("Opens the address book"),
    address_book_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_BOOK_RED, 'B', 0, NULL
  },

  GNOMEUIINFO_END
};

static GnomeUIInfo open_mailboxes[] =
{
  GNOMEUIINFO_END
};

static GnomeUIInfo mailbox_menu[] =
{
#if 0
  {
    GNOME_APP_UI_ITEM, N_ ("List"), NULL, mblist_window_cb, NULL,
    NULL, GNOME_APP_PIXMAP_NONE, GNOME_STOCK_MENU_PROP, 'C', 0, NULL
  },
#endif
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Open"), N_("Open the selected mailbox"),
			  mblist_menu_open_cb, GNOME_STOCK_MENU_OPEN),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Close"), N_("Close the selected mailbox"),
			  mblist_menu_close_cb, GNOME_STOCK_MENU_CLOSE),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Add"), N_("Add a new mailbox"),
			  mblist_menu_add_cb, GNOME_STOCK_PIXMAP_ADD),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Edit"), N_("Edit the selected mailbox"),
			  mblist_menu_edit_cb, GNOME_STOCK_MENU_PREF),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Delete"), N_("Delete the selected mailbox"),
			  mblist_menu_delete_cb, GNOME_STOCK_PIXMAP_REMOVE),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("C_lose current"), N_("Close the currently opened mailbox"),
			  mailbox_close_child, GNOME_STOCK_MENU_CLOSE),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Co_mmit current"), N_("Commit the changes in the currently opened mailbox"),
			  mailbox_commit_changes, GNOME_STOCK_MENU_REFRESH),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_SUBTREE (N_("O_pened"), open_mailboxes),
  GNOMEUIINFO_END
};
static GnomeUIInfo settings_menu[] =
{
#ifdef BALSA_SHOW_ALL
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Filters..."), N_("Manage filters"),
			  filter_dlg_cb, GNOME_STOCK_MENU_PROP),
#endif
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
  GNOMEUIINFO_ITEM_STOCK (N_ ("Check"), N_ ("Check Email"),
                          check_new_messages_cb,
                          GNOME_STOCK_PIXMAP_MAIL_RCV),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Delete"), N_ ("Delete Message"),
                          delete_message_cb,
                          GNOME_STOCK_PIXMAP_TRASH),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Compose"), N_ ("Compose Message"),
                          new_message_cb,
                          GNOME_STOCK_PIXMAP_MAIL_NEW),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Reply"), N_ ("Reply"),
                          replyto_message_cb,
                          GNOME_STOCK_PIXMAP_MAIL_RPL),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Reply to all"), N_ ("Reply to all"),
                          replytoall_message_cb,
                          GNOME_STOCK_PIXMAP_MAIL_RPL),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Forward"), N_ ("Forward"),
                          forward_message_cb,
                          GNOME_STOCK_PIXMAP_MAIL_FWD),
  GNOMEUIINFO_ITEM_STOCK (N_ ("Continue"), N_ ("Continue"),
                          continue_message_cb,
                          GNOME_STOCK_PIXMAP_MAIL),
  GNOMEUIINFO_SEPARATOR,
  {
    GNOME_APP_UI_ITEM, N_ ("Previous"), N_("Open Previous message"),
    previous_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_PIXMAP_BACK, 'P', 0, NULL
  },
  {
    GNOME_APP_UI_ITEM, N_ ("Next"), N_("Open Next message"),
    next_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_PIXMAP_FORWARD, 'N', 0, NULL
  },
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("Print"), N_ ("Print current message"), file_print_cb, GNOME_STOCK_PIXMAP_PRINT),

  GNOMEUIINFO_END
};

static GnomeAppClass *parent_class = NULL;
static guint window_signals[LAST_SIGNAL] = { 0 };

GtkType
balsa_window_get_type (void)
{
  static GtkType window_type = 0;

  if (!window_type)
    {
      static const GtkTypeInfo window_info =
      {
	"BalsaWindow",
	sizeof (BalsaWindow),
	sizeof (BalsaWindowClass),
	(GtkClassInitFunc) balsa_window_class_init,
	(GtkObjectInitFunc) balsa_window_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      window_type = gtk_type_unique (gnome_app_get_type (), &window_info);
    }

  return window_type;
}

static void
balsa_window_class_init (BalsaWindowClass *klass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass*) klass;
  widget_class = (GtkWidgetClass*) klass;

  parent_class = gtk_type_class (gnome_app_get_type ());


  window_signals[SET_CURSOR] =
    gtk_signal_new ("set_cursor",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (BalsaWindowClass, set_cursor),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1,
		    GTK_TYPE_POINTER);

  window_signals[OPEN_MAILBOX] =
    gtk_signal_new ("open_mailbox",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (BalsaWindowClass, open_mailbox),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1,
		    GTK_TYPE_POINTER);

  window_signals[CLOSE_MAILBOX] =
    gtk_signal_new ("close_mailbox",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (BalsaWindowClass, close_mailbox),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1,
		    GTK_TYPE_POINTER);

  gtk_object_class_add_signals (object_class, window_signals, LAST_SIGNAL);


  object_class->destroy = balsa_window_destroy;


  klass->set_cursor = balsa_window_real_set_cursor;
  klass->open_mailbox = balsa_window_real_open_mailbox;
  klass->close_mailbox = balsa_window_real_close_mailbox;

  //  widget_class->draw = gtk_window_draw;
}

static void
balsa_window_init (BalsaWindow *window)
{
  //  window->modal = FALSE;
  
  //  gtk_container_register_toplevel (GTK_CONTAINER (window));
}

GtkWidget*
balsa_window_new ()
{
  BalsaWindow *window;
  GnomeAppBar *appbar;
  GtkWidget *preview;
  GtkWidget *hpaned;
  GtkWidget *vpaned;


  window = gtk_type_new (BALSA_TYPE_WINDOW);
  gnome_app_construct(GNOME_APP(window), "balsa", "Balsa");

  gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);
  gnome_app_create_toolbar_with_data(GNOME_APP(window), main_toolbar, window);

  /* set the toolbar style */
  balsa_window_refresh(window);

  if (balsa_app.check_mail_upon_startup)
    check_new_messages_cb(NULL, NULL);

  /* we can only set icon after realization, as we have no windows before. */
  gtk_signal_connect (GTK_OBJECT (window), "realize",
		      GTK_SIGNAL_FUNC (set_icon), NULL);
  gtk_signal_connect( GTK_OBJECT( window ), "size_allocate", 
		      GTK_SIGNAL_FUNC( mw_size_alloc_cb ), NULL );

  appbar = GNOME_APPBAR(gnome_appbar_new(TRUE, TRUE, GNOME_PREFERENCES_USER));
  gnome_app_set_statusbar(GNOME_APP(window), GTK_WIDGET(appbar));
  gtk_object_set_data(GTK_OBJECT(window), APPBAR_KEY, appbar);

  gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), balsa_app.mw_width, balsa_app.mw_height);

  vpaned = gtk_vpaned_new();
  hpaned = gtk_hpaned_new();
  window->notebook = gtk_notebook_new();
  gtk_notebook_set_show_border(GTK_NOTEBOOK(window->notebook), FALSE);
  gtk_signal_connect( GTK_OBJECT(window->notebook), "size_allocate", 
		      GTK_SIGNAL_FUNC(notebook_size_alloc_cb), NULL );
  /* this call will set window->preview */
  preview = balsa_window_create_preview_pane(window);

  gnome_app_set_contents(GNOME_APP(window), hpaned);

  // XXX
  window->mblist = balsa_mailbox_list_window_new(window);
  gtk_paned_pack1(GTK_PANED(hpaned), window->mblist, TRUE, TRUE);
  gtk_paned_pack2(GTK_PANED(hpaned), vpaned, TRUE, TRUE);
  /*PKGW: do it this way, without the usizes.*/
  gtk_paned_set_position( GTK_PANED(hpaned), balsa_app.mblist_width );

  gtk_paned_pack1(GTK_PANED(vpaned), window->notebook, TRUE, TRUE);
  gtk_paned_pack2(GTK_PANED(vpaned), preview, TRUE, TRUE);
  /*PKGW: do it this way, without the usizes.*/
  gtk_paned_set_position( GTK_PANED(vpaned), balsa_app.notebook_height );

  gtk_widget_show(vpaned);
  gtk_widget_show(hpaned);
  gtk_widget_show(window->notebook);
  gtk_widget_show(window->mblist);
  gtk_widget_show(preview);

  return GTK_WIDGET (window);
}

void
balsa_window_set_cursor (BalsaWindow *window,
			 GdkCursor *cursor)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (BALSA_IS_WINDOW (window));

  gtk_signal_emit (GTK_OBJECT (window), window_signals[SET_CURSOR], cursor);
}

static void
balsa_window_real_set_cursor (BalsaWindow *window,
			      GdkCursor *cursor)
{
  // XXX fixme to work with NULL cursors
  //  gtk_widget_set_sensitive (GTK_WIDGET(window->progress_bar), FALSE);
  //  gtk_progress_set_activity_mode (GTK_WIDGET(window->progress_bar), FALSE);
  //  gtk_timeout_remove (pbar_timeout);
  //  gtk_progress_set_value (GTK_PROGRESS (pbar), 0.0);
  gdk_window_set_cursor (GTK_WIDGET(window)->window, cursor);
}




void balsa_window_open_mailbox(BalsaWindow *window, Mailbox *mailbox)
{
  g_return_if_fail(window != NULL);
  g_return_if_fail(BALSA_IS_WINDOW(window));

  gtk_signal_emit(GTK_OBJECT(window), window_signals[OPEN_MAILBOX], mailbox);
}

/*void balsa_window_open_mailbox(BalsaWindow *window, Mailbox *mailbox)*/
void balsa_window_close_mailbox(BalsaWindow *window, Mailbox *mailbox)
{
  g_return_if_fail(window != NULL);
  g_return_if_fail(BALSA_IS_WINDOW (window));

  gtk_signal_emit(GTK_OBJECT(window), window_signals[CLOSE_MAILBOX], mailbox);
}


static void balsa_window_real_open_mailbox(BalsaWindow *window, Mailbox *mailbox)
{
  GtkObject *page;
  GtkWidget *label;

/*  label = gtk_label_new("blah"); PKGW: dunno why this was here. */

  page = balsa_index_page_new(window);
  balsa_index_page_load_mailbox(BALSA_INDEX_PAGE(page), mailbox);

  label = gtk_label_new(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->mailbox->name);

  /* store for easy access */
  gtk_object_set_data(GTK_OBJECT(BALSA_INDEX_PAGE(page)->sw), "indexpage", page);
  gtk_notebook_append_page(GTK_NOTEBOOK(window->notebook), GTK_WIDGET(BALSA_INDEX_PAGE(page)->sw), label);

  /* change the page to the newly selected notebook item */
  gtk_notebook_set_page(GTK_NOTEBOOK(window->notebook),
			gtk_notebook_page_num(GTK_NOTEBOOK(window->notebook), GTK_WIDGET(BALSA_INDEX_PAGE(page)->sw)));
}

static void balsa_window_real_close_mailbox(BalsaWindow *window, Mailbox *mailbox)
{
/*  printf("FIXME: Can't close mailboxes.\n"); 
    Sadly, we don't get the IndexPage pointer given to us. Ah well. */
    GtkWidget *page;
    guint32 i;

    /*Eeeew.... we'd better hope that the mailbox is actually opened, or 
      you're asking for Bad Things to Happen (TM).*/
    i = 0;
    while( 1 ) {
	/* This is the scrolled window. */
	page = gtk_notebook_get_nth_page( GTK_NOTEBOOK( window->notebook ), i );
	if( page == NULL ) {
	    g_warning( "Can't find mailbox \"%s\" in notebook!", mailbox->name );
	    return;
	}
	page = gtk_object_get_data( GTK_OBJECT( page ), "indexpage" );
	if( (BALSA_INDEX_PAGE( page ))->mailbox == mailbox )
	    break;
	i++;
    }

    gtk_notebook_remove_page( GTK_NOTEBOOK( window->notebook ), i );
    (BALSA_INDEX_PAGE( page ))->sw = NULL; /* This was just toasted */
    gtk_object_destroy( GTK_OBJECT( page ) );
}



static GtkWidget *balsa_window_create_preview_pane(BalsaWindow *window)
{
  GtkWidget *message;
  GtkWidget *sw;
  GtkAdjustment *vadj, *hadj;

  /* balsa_message */
  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (sw),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);
  message = balsa_message_new();
  gtk_widget_set_usize(message, -1, 250);
  gtk_widget_show(message);
  gtk_container_add(GTK_CONTAINER(sw), message);
    
  vadj = gtk_layout_get_vadjustment(GTK_LAYOUT(message));
  hadj = gtk_layout_get_hadjustment(GTK_LAYOUT(message));

  gtk_scrolled_window_set_vadjustment(GTK_SCROLLED_WINDOW(sw), vadj);
  gtk_scrolled_window_set_hadjustment(GTK_SCROLLED_WINDOW(sw), hadj);
  vadj->step_increment = 10;
  hadj->step_increment = 10;

  /* set window->preview to the BalsaMessage and not the scrolling window */
  window->preview = message;

  return sw;
}


static void balsa_window_destroy (GtkObject     *object)
{
  BalsaWindow *window;
  /*
    gint x, y;
    gchar *geometry;
    XXX this is too late to get the right width and height
    geometry = gnome_geometry_string(GTK_WIDGET(object)->window);
    gnome_parse_geometry(geometry,
                       &x, &y,
                       &balsa_app.mw_width, 
		       &balsa_app.mw_height);
  g_free (geometry);
  */

  window = BALSA_WINDOW(object);

  /*
  balsa_app.mw_width = GTK_WIDGET(object)->allocation.width;
  balsa_app.mw_height = GTK_WIDGET(object)->allocation.height;
  */

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));

  balsa_exit();
}

/*FIXME unused
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
*/


/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow *window)
{
  GnomeDockItem *item;
  GtkWidget *toolbar;

  /*
   * set the toolbar style
   */
  item = gnome_app_get_dock_item_by_name(GNOME_APP(window),
					 GNOME_APP_TOOLBAR_NAME);
  toolbar = gnome_dock_item_get_child(item);

  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), balsa_app.toolbar_style);
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


  /* only show one about box at a time */
  if (about_box_visible)
    return;
  else
    about_box_visible = TRUE;

  about = gnome_about_new ("Balsa",
			   BALSA_VERSION,
			   _ ("Copyright (C) 1997-1999"),
			   authors,
			   _ ("The Balsa email client is part of the GNOME desktop environment.  Information on Balsa can be found at http://www.balsa.net/\n\nIf you need to report bugs, please do so at: http://bugs.gnome.org/"),
			   "balsa/balsa_logo.png");

  gtk_signal_connect (GTK_OBJECT (about),
		      "destroy",
		      (GtkSignalFunc) about_box_destroy_cb,
		      NULL);

  gtk_widget_show (about);
}


/*
 * Callbacks
 */

gint
check_new_messages_auto_cb (gpointer data)
{
  check_new_messages_cb( (GtkWidget *) NULL, data);

  /*  preserver timer */
  return TRUE;
}

static void
check_new_messages_cb (GtkWidget * widget, gpointer data)
{

  GtkWidget *index;
  Mailbox *mbox;

#ifdef BALSA_USE_THREADS
/*  Only Run once -- If already checking mail, return.  */
  pthread_mutex_lock( &mailbox_lock );
  if( checking_mail )
  {
    pthread_mutex_unlock( &mailbox_lock );
    fprintf( stderr, "Already Checking Mail!  \n");
    return;
  }
  checking_mail = 1;
  pthread_mutex_unlock( &mailbox_lock );

  if( balsa_app.pwindow_option == WHILERETR || 
      balsa_app.pwindow_option == UNTILCLOSED )
    {
      if( progress_dialog && GTK_IS_WIDGET( progress_dialog ) )
	gtk_widget_destroy( GTK_WIDGET(progress_dialog) );

      progress_dialog = gnome_dialog_new("Checking Mail...", "Hide", NULL);
      gtk_signal_connect (GTK_OBJECT (progress_dialog), "destroy",
			  GTK_SIGNAL_FUNC (progress_dialog_destroy_cb), NULL);

      gnome_dialog_set_close(GNOME_DIALOG(progress_dialog), TRUE);

      progress_dialog_source = gtk_label_new("Checking Mail....");
      gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(progress_dialog)->vbox), 
			 progress_dialog_source, 
			 FALSE, FALSE, 0);

      progress_dialog_message = gtk_label_new("");
      gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(progress_dialog)->vbox), 
			 progress_dialog_message, 
			 FALSE, FALSE, 0);

      gtk_widget_show_all( progress_dialog );
    }
#endif BALSA_USE_THREADS

  if(data)
    {
      index = balsa_window_find_current_index(BALSA_WINDOW(data));
      if (index)
	mbox = BALSA_INDEX(index)->mailbox;
      else
	mbox = balsa_app.inbox;
    }
  else
    mbox = balsa_app.inbox;

#ifdef BALSA_USE_THREADS
/* initiate threads */
  pthread_create( &get_mail_thread,
  		NULL,
  		(void *) &check_messages_thread,
		mbox );
#else
  check_messages_thread(mbox);
#endif

}

void
check_messages_thread( Mailbox *mbox )
{
#ifdef BALSA_USE_THREADS
/*  
 *  It is assumed that this will always be called as a pthread,
 *  and that the calling procedure will check for an existing lock
 *  and set checking_mail to true before calling.
 */
  MailThreadMessage *threadmessage;

  MSGMAILTHREAD( threadmessage, MSGMAILTHREAD_SOURCE, "POP3" );
  check_all_pop3_hosts (balsa_app.inbox, balsa_app.inbox_input); 

  MSGMAILTHREAD( threadmessage, MSGMAILTHREAD_SOURCE, "IMAP" );
  check_all_imap_hosts (balsa_app.inbox, balsa_app.inbox_input);

  MSGMAILTHREAD( threadmessage, MSGMAILTHREAD_SOURCE, "Local Mail" );
  mailbox_check_new_messages( mbox );

  MSGMAILTHREAD( threadmessage, MSGMAILTHREAD_FINISHED, "Finished" );

  pthread_mutex_lock( &mailbox_lock );
  checking_mail = 0;
  pthread_mutex_unlock( &mailbox_lock );

  pthread_exit( 0 );

#else
  check_all_pop3_hosts (balsa_app.inbox, balsa_app.inbox_input); 
  check_all_imap_hosts (balsa_app.inbox, balsa_app.inbox_input);
  mailbox_check_new_messages( mbox );
  load_messages (balsa_app.inbox, 1);
#endif


}


#ifdef BALSA_USE_THREADS
gboolean
mail_progress_notify_cb( )
{
    MailThreadMessage *threadmessage;
    MailThreadMessage **currentpos;
    char *msgbuffer;
    uint count;

    msgbuffer = malloc( 2049 );

    g_io_channel_read( mail_thread_msg_receive, msgbuffer, 
          2048, &count );

    if( count < sizeof( void *) )
      {
	free( msgbuffer );
	return TRUE;
      }

    currentpos = msgbuffer;

    while( count ) 
      {
	threadmessage = *currentpos;

	if( balsa_app.debug )
	  fprintf( stderr, "Message: %lu, %d, %s\n", 
		   (unsigned long) threadmessage, threadmessage->message_type,
		   threadmessage->message_string );
	switch( threadmessage->message_type )  
	  {
	  case MSGMAILTHREAD_SOURCE:
	    if( progress_dialog && GTK_IS_WIDGET( progress_dialog ) )
	      {
		gtk_label_set_text( GTK_LABEL(progress_dialog_source), 
				  threadmessage->message_string );
		gtk_label_set_text( GTK_LABEL(progress_dialog_message), "" );
	        gtk_widget_show_all( progress_dialog );
	      }
	    break;
	  case MSGMAILTHREAD_MSGINFO:
	    if( progress_dialog && GTK_IS_WIDGET( progress_dialog ) )
	      {
		gtk_label_set_text( GTK_LABEL(progress_dialog_message), 
				  threadmessage->message_string );
		gtk_widget_show_all( progress_dialog );
	      }
	    break;
	  case MSGMAILTHREAD_UPDATECONFIG:
	    config_mailbox_update( threadmessage->mailbox, 
		      MAILBOX_POP3(threadmessage->mailbox)->mailbox.name ); 
	    break;
	  case MSGMAILTHREAD_LOAD:
	    LOCK_MAILBOX (balsa_app.inbox);
	    load_messages (balsa_app.inbox, 1);
	    UNLOCK_MAILBOX (balsa_app.inbox);
	    break;
	  case MSGMAILTHREAD_FINISHED:
	    if( balsa_app.pwindow_option != UNTILCLOSED )
	      {
		if( progress_dialog && GTK_IS_WIDGET( progress_dialog ))
		  gtk_widget_destroy( progress_dialog );
		progress_dialog = NULL;
	      }
	    else if( progress_dialog && GTK_IS_WIDGET( progress_dialog ))
		gtk_label_set_text( GTK_LABEL(progress_dialog_source), 
				  "Finished Checking." );
	    break;
	  default:
	    fprintf ( stderr, " Unknown: %s \n", 
		      threadmessage->message_string );
	    
	  }
	free( threadmessage );
	currentpos++;
	count -= sizeof(void *);
      }
    free( msgbuffer );
	
    return TRUE;
}

void progress_dialog_destroy_cb( GtkWidget *widget, gpointer data )
{
  gtk_widget_destroy( widget );
  widget = NULL;
}

gboolean
send_progress_notify_cb( )
{
    SendThreadMessage *threadmessage;
    SendThreadMessage **currentpos;
    char *msgbuffer;
    uint count;

    msgbuffer = malloc( 2049 );

    g_io_channel_read( send_thread_msg_receive, msgbuffer, 
          2048, &count );

    if( count < sizeof( void *) )
      {
	free( msgbuffer );
	return TRUE;
      }

    currentpos = msgbuffer;

    while( count ) 
      {
	threadmessage = *currentpos;

	if( balsa_app.debug )
	  fprintf( stderr, "Send_Message: %lu, %d, %s\n", 
		   (unsigned long) threadmessage, threadmessage->message_type,
		   threadmessage->message_string );
	switch( threadmessage->message_type )  
	  {
	  case MSGSENDTHREADERROR:
	    fprintf(stderr, "Send Error %s\n", threadmessage->message_string);
	    break;
	  case MSGSENDTHREADLOAD:
	    LOCK_MAILBOX (threadmessage->mbox);
	    load_messages (threadmessage->mbox, 1);
	    UNLOCK_MAILBOX (threadmessage->mbox);
	    break;
	  case MSGSENDTHREADPOSTPONE:
	    fprintf(stderr, "Send Postpone %s\n", 
		    threadmessage->message_string);
	    break;
	  default:
	    fprintf ( stderr, " Unknown: %s \n", 
		      threadmessage->message_string );
	  }
	free( threadmessage );
	currentpos++;
	count -= sizeof(void *);
      }
    free( msgbuffer );
	
    return TRUE;
}
#endif USE_BALSA_THREADS

//static
GtkWidget *balsa_window_find_current_index(BalsaWindow *window)
{
  GtkWidget *page;

  g_return_val_if_fail (window != NULL, NULL);

  page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(window->notebook), 
				   gtk_notebook_get_current_page(GTK_NOTEBOOK(window->notebook)));

  if (!page)
    return NULL;

  /* get the real page.. not the scrolled window */
  page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");

  if (!page)
    return NULL;

  return GTK_WIDGET(BALSA_INDEX_PAGE(page)->index);
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
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_REPLY);
    list = list->next;
  }
}

static void
replytoall_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_REPLY_ALL);
    list = list->next;
  }
}

static void
forward_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_FORWARD);
    list = list->next;
  }
}


static void
continue_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_CONTINUE);
    list = list->next;
  }
}


static void
next_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  balsa_index_select_next(BALSA_INDEX(index));
}


static void
previous_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  balsa_index_select_previous(BALSA_INDEX(index));
}


static void
delete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    message_delete(message);
    list = list->next;
  }

  balsa_index_select_next(BALSA_INDEX(index));
}


static void
undelete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    message_undelete(message);
    list = list->next;
  }

  balsa_index_select_next(BALSA_INDEX(index));
}

static void
filter_dlg_cb (GtkWidget * widget, gpointer data)
{
  filter_edit_dialog (NULL);
}

/*FIXME unused (#if0'ed out in GNOMEUI defs)
static void
mblist_window_cb (GtkWidget * widget, gpointer data)
{
  //  mblist_open_window (mdi);
}
*/

static void
mailbox_close_child (GtkWidget * widget, gpointer data)
{
  GtkWidget *index;

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  balsa_window_close_mailbox(BALSA_WINDOW(data), BALSA_INDEX(index)->mailbox);
}

static void
mailbox_commit_changes (GtkWidget * widget, gpointer data)
{
  Mailbox *current_mailbox;
  GtkWidget *index;

  index = balsa_window_find_current_index(BALSA_WINDOW(data));

  g_return_if_fail(index != NULL);

  current_mailbox = BALSA_INDEX(index)->mailbox;
  mailbox_commit_flagged_changes(current_mailbox);
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
  att.event_mask = GDK_ALL_EVENTS_MASK;
  att.wclass = GDK_INPUT_OUTPUT;
  att.window_type = GDK_WINDOW_TOPLEVEL;
  att.x = 0;
  att.y = 0;
  att.visual = gdk_imlib_get_visual ();
  att.colormap = gdk_imlib_get_colormap ();
  ic_win = gdk_window_new (NULL, &att, GDK_WA_VISUAL | GDK_WA_COLORMAP);
  {
    char *filename = gnome_unconditional_pixmap_file ("balsa/balsa_icon.png");
    im = gdk_imlib_load_image (filename);
    g_free (filename);
  }
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

/* PKGW: remember when they change the position of the vpaned. */
static void notebook_size_alloc_cb( GtkWidget *notebook, GtkAllocation *alloc )
{
    balsa_app.notebook_height = alloc->height;
}

static void mw_size_alloc_cb( GtkWidget *window, GtkAllocation *alloc )
{
    balsa_app.mw_height = alloc->height;
    balsa_app.mw_width = alloc->width;
}
